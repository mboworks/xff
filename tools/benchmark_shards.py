#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Plan fixture or sample shards and validate their combined observations."""
import argparse
import copy
import json
import platform
import re
from pathlib import Path

import benchmark_matrix
import benchmark_records
import process_resources


def fixture_key(task):
    return task['files'], task['cpus'], task['dataset']


def sample_policy(plan):
    """Return per-shard repetitions, total repetitions and retained count, or None."""
    partition = plan.get('partition', 'balanced')
    if partition == 'balanced':
        return None
    if partition != 'samples':
        raise ValueError('unknown shard partition')
    repetitions, keep, count = plan['repetitions'], plan['retained'], len(plan['shards'])
    if count < 1 or repetitions < count or repetitions % count or not 1 <= keep <= repetitions:
        raise ValueError('sample shards require equal positive rounds and a valid retained count')
    return repetitions // count, repetitions, keep


def make_plan(file_counts, cpu_counts, count, reference=None, task_names=None, *, partition='balanced', repetitions=9,
              keep=7, adaptive=False):
    if adaptive and partition != 'samples':
        raise ValueError('adaptive plans require the samples partition')
    if count < 1 or not file_counts or not cpu_counts or min(*file_counts, *cpu_counts) < 1:
        raise ValueError('positive file counts, CPU counts and shard count required')
    if len(set(file_counts)) != len(file_counts) or len(set(cpu_counts)) != len(cpu_counts):
        raise ValueError('duplicate file or CPU count')
    task_names = sorted(set(task_names or (task['name'] for task in (reference or {}).get('tasks', []))))
    if not task_names:
        raise ValueError('expected task names required')
    costs = {}
    for task in (reference or {}).get('tasks', []):
        costs[fixture_key(task)] = costs.get(fixture_key(task), 0) + sum(
            benchmark_matrix.elapsed_mean(reference, entry) for entry in task['participants'].values())
    fixtures = []
    for files in file_counts:
        for cpus in cpu_counts:
            for dataset in ('broad', 'deep'):
                candidates = [(abs(size - files), size, cost) for (size, cpu, shape), cost in costs.items()
                              if cpu == cpus and shape == dataset]
                weight = float(files)
                if candidates:
                    _, size, cost = min(candidates)
                    weight = max(cost * files / size, 0.000001)
                fixtures.append({'files': files, 'cpus': cpus, 'dataset': dataset, 'estimated_cost': weight})
    if partition == 'samples':
        shards = [{'estimated_cost': sum(fixture['estimated_cost'] for fixture in fixtures),
                   'fixtures': copy.deepcopy(fixtures)} for _ in range(count)]
        plan = {'schema': 1, 'partition': partition, 'file_counts': list(file_counts), 'cpu_counts': list(cpu_counts),
                'task_names': task_names, 'shards': shards, 'repetitions': repetitions, 'retained': keep}
        if adaptive:
            if count != 5 or repetitions != 15 or keep != 15:
                raise ValueError('adaptive sampling requires five candidates with three rounds each')
            plan['adaptive'] = {'policy': 'majority-cpu-count', 'quorum': 3, 'minimum_candidates': 3}
        sample_policy(plan)
        return plan
    if partition != 'balanced':
        raise ValueError('unknown shard partition')
    if count > len(fixtures):
        raise ValueError('more shards than fixtures')
    shards = [{'estimated_cost': 0, 'fixtures': []} for _ in range(count)]
    for fixture in sorted(fixtures, key=lambda value: (-value['estimated_cost'], fixture_key(value))):
        index = min(range(count), key=lambda index: (shards[index]['estimated_cost'], index))
        shards[index]['fixtures'].append(fixture)
        shards[index]['estimated_cost'] += fixture['estimated_cost']
    return {'schema': 1, 'file_counts': list(file_counts), 'cpu_counts': list(cpu_counts), 'task_names': task_names, 'shards': shards}


def assigned_fixtures(plan, index):
    if plan.get('schema') != 1 or not 0 <= index < len(plan['shards']):
        raise ValueError('invalid shard plan or index')
    fixtures = [fixture_key(fixture) for shard in plan['shards'] for fixture in shard['fixtures']]
    expected = {(files, cpus, shape) for files in plan['file_counts'] for cpus in plan['cpu_counts']
                for shape in ('broad', 'deep')}
    if sample_policy(plan):
        for shard in plan['shards']:
            keys = [fixture_key(fixture) for fixture in shard['fixtures']]
            if len(keys) != len(expected) or set(keys) != expected:
                raise ValueError('sample shard requires every fixture exactly once')
    elif len(fixtures) != len(set(fixtures)) or set(fixtures) != expected:
        raise ValueError('incomplete or duplicate fixture plan')
    return {fixture_key(fixture) for fixture in plan['shards'][index]['fixtures']}


def identity_difference(expected, actual, path=''):
    """Describe the first differing identity field, including absent fields."""
    if isinstance(expected, dict) and isinstance(actual, dict):
        for key in sorted(expected.keys() | actual.keys()):
            field = f'{path}.{key}' if path else key
            if key not in expected:
                return f'{field}: unexpected value {actual[key]!r}'
            if key not in actual:
                return f'{field}: missing (expected {expected[key]!r})'
            difference = identity_difference(expected[key], actual[key], field)
            if difference:
                return difference
    elif expected != actual:
        return f'{path}: expected {expected!r}, got {actual!r}'
    return ''


def append_samples(target, incoming, index, report):
    """Pool raw samples only after validating task and invocation identities."""
    def metadata(task):
        return {key: value for key, value in task.items() if key != 'participants'}
    def invocation(value):
        result = {key: item for key, item in value.items() if key not in ('samples', 'sources', 'cwd', 'stdin')}
        # Generated fixture/input names are stable; temporary parent directories differ per host.
        result.update({key: Path(value[key]).name for key in ('cwd', 'stdin') if value.get(key)})
        return result
    difference = identity_difference(metadata(target), metadata(incoming))
    if difference or target['participants'].keys() != incoming['participants'].keys():
        raise ValueError(f'incompatible sample task identity or participants in shard {index}: {difference}')
    for label, entry in incoming['participants'].items():
        destination = target['participants'][label]
        difference = identity_difference(invocation(destination), invocation(entry))
        if difference:
            raise ValueError(f'incompatible sample command in shard {index}, {label}: {difference}')
        benchmark_matrix.selected_samples(report, entry)  # Validate count and finite positive timings before pooling.
        destination['sources'].append({'shard_index': index, 'cwd': entry.get('cwd'), 'stdin': entry.get('stdin')})
        destination['samples'].extend(dict(sample, shard_index=index, round=number)
                                      for number, sample in enumerate(entry['samples'], 1))


def merge_reports(records, baseline_root=None, baseline_revisions=None, *, allow_sample_subset=False):
    if not records:
        raise ValueError('no shard reports')
    records = sorted(records, key=lambda record: record['tool_comparisons']['contract']['shard']['index'])
    reports = [record['tool_comparisons'] for record in records]
    plan = reports[0]['contract']['shard']['plan']
    if allow_sample_subset:
        if not plan.get('adaptive') or len(records) != plan['adaptive']['quorum']:
            raise ValueError('adaptive merge requires exactly the agreed sample quorum')
    elif len(records) != len(plan['shards']):
        raise ValueError('missing shard reports')
    sampling = sample_policy(plan)
    result = copy.deepcopy(next((record for record in records if 'base' in record), records[0]))
    combined = result['tool_comparisons']
    combined['tasks'] = []
    contract = copy.deepcopy(combined['contract'])
    contract.pop('shard')
    affinity = contract.pop('affinity_by_cpu_count')
    indices, keys = set(), set()
    pooled = {}
    task_names = set(plan['task_names'])
    provenance = []
    for record, report in zip(records, reports, strict=True):
        process_resources.validate_accounting_contract(report.get('resource_launcher'),
                                                       report['contract'].get('resource_accounting'))
        if 'baseline' in report:
            raise ValueError('attach historical baselines after merging shards')
        current = copy.deepcopy(report['contract'])
        shard = current.pop('shard')
        allocation = current.pop('affinity_by_cpu_count')
        index = shard['index']
        if shard['plan'] != plan or index in indices:
            raise ValueError('inconsistent plan or duplicate shard')
        difference = identity_difference(
            {'head': result['head'], 'contract': contract, 'tools': combined['tools']},
            {'head': record['head'], 'contract': current, 'tools': report['tools']})
        if difference:
            raise ValueError(f'incompatible shard {index}: {difference}')
        if sampling and (current.get('repetitions'), current.get('retained')) != (sampling[0], sampling[0]):
            raise ValueError('sample shard must retain every planned round')
        if sampling and not report.get('host_id'):
            raise ValueError('sample shard missing host identity')
        for cpu, value in allocation.items():
            if cpu in affinity and affinity[cpu] != value:
                raise ValueError('incompatible shard CPU affinity')
            affinity[cpu] = value
        indices.add(index)
        expected = assigned_fixtures(plan, index)
        actual = {}
        shard_keys = set()
        for task in report['tasks']:
            fixture = fixture_key(task)
            key = (*fixture, task['name'])
            if fixture not in expected or key in shard_keys or (not sampling and key in keys):
                raise ValueError('unexpected or duplicate benchmark case')
            shard_keys.add(key)
            keys.add(key)
            actual.setdefault(fixture, set()).add(task['name'])
            if sampling:
                if key not in pooled:
                    pooled[key] = copy.deepcopy(task)
                    for entry in pooled[key]['participants'].values():
                        entry['samples'], entry['sources'] = [], []
                append_samples(pooled[key], task, index, report)
            else:
                copied = copy.deepcopy(task)
                copied['shard_index'] = index
                combined['tasks'].append(copied)
        if set(actual) != expected:
            raise ValueError('missing fixture results')
        for names in actual.values():
            if names != task_names:
                raise ValueError('incomplete task set')
        provenance.append({'index': index, 'contract': report['contract'], 'tools': report['tools'],
                           'host_id': report.get('host_id')})
        if 'resource_launcher' in report:
            provenance[-1]['resource_launcher'] = dict(report['resource_launcher'])
    if (not allow_sample_subset and indices != set(range(len(plan['shards'])))):
        raise ValueError('missing shard index')
    combined['contract'] = dict(contract, affinity_by_cpu_count=affinity)
    combined.pop('host_id', None)
    if sampling:
        combined['tasks'] = list(pooled.values())
        pooled_rounds = sampling[0] * len(records)
        retained = min(7, pooled_rounds) if allow_sample_subset else sampling[2]
        combined['contract'].update(repetitions=pooled_rounds, retained=retained)
        combined['sample_shards'] = {'count': len(records), 'repetitions': sampling[0]}
        for task in combined['tasks']:
            for entry in task['participants'].values():
                entry['samples'].sort(key=lambda sample: (sample['shard_index'], sample['round']))
                entry['sources'].sort(key=lambda source: source['shard_index'])
                benchmark_matrix.selected_samples(combined, entry)
    combined['tasks'].sort(key=lambda task: (task['cpus'], task['files'], task['dataset'], task['name']))
    if baseline_root is not None:
        benchmark_matrix.attach_baseline(combined, baseline_root, baseline_revisions)
    combined['relative_results'] = benchmark_matrix.relative_results(combined)
    threshold = combined.get('alarm', {}).get('threshold_percent', 15)
    combined['alarm'] = {'threshold_percent': threshold, 'blocking': False,
                         'cells': benchmark_matrix.regressions(combined, threshold, minimum_files=1)}
    result['measurement_shards'] = sorted(provenance, key=lambda shard: shard['index'])
    return result


def adaptive_candidate_records(directory):
    """Load candidate shard artifacts grouped by platform, ignoring absent artifacts."""
    result = {'linux': {}, 'macos': {}}
    for path in sorted(directory.rglob('benchmark-shard.json')):
        name = path.parent.name
        match = re.fullmatch(r'benchmark-shard-(linux|macos)-(\d+)', name)
        if not match:
            continue
        platform_name, index = match.group(1), int(match.group(2))
        if index in result[platform_name]:
            raise ValueError(f'duplicate candidate artifact for {platform_name} shard {index}')
        result[platform_name][index] = json.loads(path.read_text())
    return result


def _adaptive_identity(record):
    report = record['tool_comparisons']
    contract = copy.deepcopy(report['contract'])
    contract.pop('shard', None)
    contract.pop('cpu_count', None)
    return json.dumps({'head': record['head'], 'contract': contract, 'tools': report['tools']},
                      sort_keys=True, separators=(',', ':'))


def validate_adaptive_candidate(record, index):
    """Check the candidate plan and full measured matrix before counting its CPU vote."""
    report = record['tool_comparisons']
    contract = report['contract']
    shard = contract.get('shard', {})
    plan = shard.get('plan', {})
    if not plan.get('adaptive') or shard.get('index') != index or len(plan.get('shards', [])) != 5:
        raise ValueError(f'invalid adaptive shard identity: {index}')
    rounds, total, retained = sample_policy(plan)
    if (rounds, total, retained) != (3, 15, 15):
        raise ValueError('adaptive plan must contain five complete three-round candidates')
    if contract.get('repetitions') != rounds or contract.get('retained') != rounds:
        raise ValueError(f'adaptive shard {index} did not retain all three rounds')
    if not report.get('host_id'):
        raise ValueError(f'adaptive shard {index} is missing host identity')
    expected = assigned_fixtures(plan, index)
    task_names = set(plan['task_names'])
    actual = set()
    for task in report['tasks']:
        fixture = fixture_key(task)
        if fixture not in expected or task['name'] not in task_names:
            raise ValueError(f'adaptive shard {index} contains an unexpected benchmark case')
        key = (*fixture, task['name'])
        if key in actual:
            raise ValueError(f'adaptive shard {index} contains a duplicate benchmark case')
        actual.add(key)
        for entry in task['participants'].values():
            benchmark_matrix.selected_samples(report, entry)
    if actual != {(*fixture, name) for fixture in expected for name in task_names}:
        raise ValueError(f'adaptive shard {index} is missing benchmark cases')
    cpu_count = contract.get('cpu_count')
    if not isinstance(cpu_count, int) or cpu_count < 1:
        raise ValueError(f'adaptive shard {index} has an invalid host CPU count')
    return cpu_count


def adaptive_decisions(candidates, stage):
    """Return whether another candidate is needed for each platform."""
    if stage not in ('initial', 'fourth'):
        raise ValueError('unknown adaptive decision stage')
    decisions = {}
    for platform_name in ('linux', 'macos'):
        reports = candidates.get(platform_name, {})
        validated = {}
        identity = None
        plan = None
        for index, record in sorted(reports.items()):
            cpu_count = validate_adaptive_candidate(record, index)
            current_plan = record['tool_comparisons']['contract']['shard']['plan']
            if plan is not None and current_plan != plan:
                raise ValueError(f'incompatible adaptive plans for {platform_name}')
            if identity is not None and _adaptive_identity(record) != identity:
                raise ValueError(f'incompatible non-CPU benchmark contracts for {platform_name}')
            plan = current_plan
            identity = _adaptive_identity(record)
            validated[index] = cpu_count
        largest_group = max((list(validated.values()).count(value) for value in set(validated.values())), default=0)
        decisions[platform_name] = largest_group < 3
        if stage == 'fourth' and len(validated) < 4 and largest_group < 3:
            decisions[platform_name] = True
    return decisions


def select_adaptive_reports(candidates, platform_name):
    """Select exactly three agreeing reports; fail if no CPU-count quorum exists."""
    reports = candidates.get(platform_name, {})
    groups = {}
    identity = None
    plan = None
    for index, record in sorted(reports.items()):
        cpu_count = validate_adaptive_candidate(record, index)
        current_plan = record['tool_comparisons']['contract']['shard']['plan']
        if plan is not None and current_plan != plan:
            raise ValueError(f'incompatible adaptive plans for {platform_name}')
        if identity is not None and _adaptive_identity(record) != identity:
            raise ValueError(f'incompatible non-CPU benchmark contracts for {platform_name}')
        plan = current_plan
        identity = _adaptive_identity(record)
        groups.setdefault(cpu_count, []).append((index, record))
    eligible = [values for values in groups.values() if len(values) >= 3]
    if not eligible:
        counts = {cpu: len(values) for cpu, values in groups.items()}
        raise ValueError(f'{platform_name} has no three-shard CPU-count agreement: {counts}')
    winner = max(eligible, key=lambda values: (len(values), -values[0][0]))
    return [record for _, record in winner[:3]]


def latest_reference(root, machine):
    candidates = []
    for path in benchmark_records.run_paths(root):
        record = benchmark_records.read(path)
        report = record.get('tool_comparisons')
        source = record.get('source', {})
        if (report and report['contract'].get('machine') == machine
                and source.get('event') == 'push' and source.get('head_branch') == 'main'):
            candidates.append((source['created_at'], source['id'], report))
    return max(candidates, key=lambda item: item[:2])[2] if candidates else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--plan', action='store_true')
    mode.add_argument('--merge', type=Path, nargs='+')
    mode.add_argument('--merge-directory', type=Path,
                      help='Discover benchmark-shard.json in flat or per-artifact download directories')
    mode.add_argument('--decide-adaptive', type=Path,
                      help='Inspect adaptive candidates and emit the next-shard workflow outputs')
    mode.add_argument('--merge-adaptive-directory', type=Path,
                      help='Select agreeing candidates and merge both platforms')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--output-directory', type=Path)
    parser.add_argument('--seed-directory', type=Path,
                        help='Download directory containing per-platform main-run metadata seed reports')
    parser.add_argument('--decision-stage', choices=('initial', 'fourth'))
    parser.add_argument('--files', type=int, action='append')
    parser.add_argument('--cpus', type=int, action='append')
    parser.add_argument('--count', type=int, default=3)
    parser.add_argument('--partition', choices=('balanced', 'samples'), default='balanced',
                        help='Balance complete fixtures, or repeat the full grid with equal rounds on every shard')
    parser.add_argument('--repetitions', type=int, default=9, help='Total measured rounds across sample shards')
    parser.add_argument('--keep', type=int, default=7, help='Fastest pooled rounds retained per participant')
    parser.add_argument('--adaptive', action='store_true', help='Plan five candidates with a three-shard quorum')
    parser.add_argument('--baseline-root', type=Path, help='Attach a compatible main baseline after merging')
    parser.add_argument('--baseline-revisions', type=Path,
                        help='Eligible baseline commit IDs, one per line, newest first (git rev-list --first-parent)')
    reference_group = parser.add_mutually_exclusive_group()
    reference_group.add_argument('--reference', type=Path)
    reference_group.add_argument('--reference-root', type=Path)
    args = parser.parse_args()
    if sum(value is not None and value is not False for value in
           (args.plan, args.merge, args.merge_directory, args.decide_adaptive, args.merge_adaptive_directory)) != 1:
        parser.error('select exactly one operation')
    if args.plan and not args.output:
        parser.error('--plan requires --output')
    if args.merge and not args.output:
        parser.error('--merge requires --output')
    if args.merge_directory and not args.output:
        parser.error('--merge-directory requires --output')
    if args.decide_adaptive and not args.decision_stage:
        parser.error('--decide-adaptive requires --decision-stage')
    if args.merge_adaptive_directory and not args.output_directory:
        parser.error('--merge-adaptive-directory requires --output-directory')
    if args.baseline_revisions and (args.plan or args.decide_adaptive or not args.baseline_root):
        parser.error('--baseline-revisions requires merge mode and --baseline-root')
    if args.plan:
        reference = json.loads(args.reference.read_text())['tool_comparisons'] if args.reference else None
        if args.reference_root:
            reference = latest_reference(args.reference_root, platform.machine())
        import benchmark_compare
        tools = {'xff': {'path': 'xff'}, 'find': {}, 'rg': {}, 'fzf': {}}
        names = [name for name, _, _ in benchmark_compare.scenarios(Path('.'), [], tools)]
        result = make_plan(args.files or [], args.cpus or [1, 4], args.count, reference, names,
                           partition=args.partition, repetitions=args.repetitions, keep=args.keep,
                           adaptive=args.adaptive)
    elif args.decide_adaptive:
        decisions = adaptive_decisions(adaptive_candidate_records(args.decide_adaptive), args.decision_stage)
        suffix = 'fourth' if args.decision_stage == 'initial' else 'fifth'
        for platform_name, needed in decisions.items():
            print(f'{platform_name}_{suffix}={str(needed).lower()}')
        return
    elif args.merge_adaptive_directory:
        candidates = adaptive_candidate_records(args.merge_adaptive_directory)
        revisions = args.baseline_revisions.read_text().splitlines() if args.baseline_revisions else None
        args.output_directory.mkdir(parents=True, exist_ok=True)
        for platform_name in ('linux', 'macos'):
            selected = select_adaptive_reports(candidates, platform_name)
            result = merge_reports(selected, args.baseline_root, revisions, allow_sample_subset=True)
            if args.seed_directory:
                seeds = sorted(args.seed_directory.rglob(f'benchmark-report-{platform_name}-*.json'))
                if len(seeds) != 1:
                    raise ValueError(f'expected exactly one {platform_name} metadata seed, found {len(seeds)}')
                seed = json.loads(seeds[0].read_text())
                if seed.get('head') != result.get('head'):
                    raise ValueError(f'{platform_name} metadata seed has a different source commit')
                seed['tool_comparisons'] = result['tool_comparisons']
                seed['measurement_shards'] = result['measurement_shards']
                result = seed
            (args.output_directory / f'benchmark-merged-{platform_name}.json').write_text(
                json.dumps(result, indent=2) + '\n')
        return
    else:
        paths = (sorted(args.merge_directory.rglob('benchmark-shard.json'))
                 if args.merge_directory is not None else args.merge)
        revisions = args.baseline_revisions.read_text().splitlines() if args.baseline_revisions else None
        result = merge_reports([json.loads(path.read_text()) for path in paths], args.baseline_root, revisions)
    args.output.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
