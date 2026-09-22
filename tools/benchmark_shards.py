#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Plan complete-fixture benchmark shards and validate their combined observations."""
import argparse
import copy
import json
import platform
from pathlib import Path

import benchmark_matrix


def fixture_key(task):
    return task['files'], task['cpus'], task['dataset']


def make_plan(file_counts, cpu_counts, count, reference=None, task_names=None):
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
    if len(fixtures) != len(set(fixtures)) or set(fixtures) != expected:
        raise ValueError('incomplete or duplicate fixture plan')
    return {fixture_key(fixture) for fixture in plan['shards'][index]['fixtures']}


def merge_reports(records):
    if not records:
        raise ValueError('no shard reports')
    reports = [record['tool_comparisons'] for record in records]
    plan = reports[0]['contract']['shard']['plan']
    if len(records) != len(plan['shards']):
        raise ValueError('missing shard reports')
    result = copy.deepcopy(next((record for record in records if 'base' in record), records[0]))
    combined = result['tool_comparisons']
    combined['tasks'] = []
    contract = copy.deepcopy(combined['contract'])
    contract.pop('shard')
    affinity = contract.pop('affinity_by_cpu_count')
    indices, keys = set(), set()
    task_names = set(plan['task_names'])
    provenance = []
    for record, report in zip(records, reports, strict=True):
        if 'baseline' in report:
            raise ValueError('attach historical baselines after merging shards')
        current = copy.deepcopy(report['contract'])
        shard = current.pop('shard')
        allocation = current.pop('affinity_by_cpu_count')
        index = shard['index']
        if shard['plan'] != plan or index in indices:
            raise ValueError('inconsistent plan or duplicate shard')
        if record['head'] != result['head'] or current != contract or report['tools'] != combined['tools']:
            raise ValueError('incompatible shard source, contract or tools')
        for cpu, value in allocation.items():
            if cpu in affinity and affinity[cpu] != value:
                raise ValueError('incompatible shard CPU affinity')
            affinity[cpu] = value
        indices.add(index)
        expected = assigned_fixtures(plan, index)
        actual = {}
        for task in report['tasks']:
            fixture = fixture_key(task)
            key = (*fixture, task['name'])
            if fixture not in expected or key in keys:
                raise ValueError('unexpected or duplicate benchmark case')
            keys.add(key)
            actual.setdefault(fixture, set()).add(task['name'])
            copied = copy.deepcopy(task)
            copied['shard_index'] = index
            combined['tasks'].append(copied)
        if set(actual) != expected:
            raise ValueError('missing fixture results')
        for names in actual.values():
            if names != task_names:
                raise ValueError('incomplete task set')
        provenance.append({'index': index, 'contract': report['contract'], 'tools': report['tools']})
    if indices != set(range(len(plan['shards']))):
        raise ValueError('missing shard index')
    combined['contract'] = dict(contract, affinity_by_cpu_count=affinity)
    combined['tasks'].sort(key=lambda task: (task['cpus'], task['files'], task['dataset'], task['name']))
    combined['relative_results'] = benchmark_matrix.relative_results(combined)
    threshold = combined.get('alarm', {}).get('threshold_percent', 15)
    combined['alarm'] = {'threshold_percent': threshold, 'blocking': False,
                         'cells': benchmark_matrix.regressions(combined, threshold, minimum_files=1)}
    result['measurement_shards'] = provenance
    return result


def latest_reference(root, machine):
    candidates = []
    for path in root.glob('runs/*/*/**/report.json'):
        record = json.loads(path.read_text())
        report = record.get('tool_comparisons')
        source = record.get('source', {})
        if (report and report['contract'].get('machine') == machine
                and source.get('event') == 'push' and source.get('head_branch') == 'main'):
            candidates.append((source['created_at'], source['id'], report))
    return max(candidates, key=lambda item: item[:2])[2] if candidates else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--plan', action='store_true')
    mode.add_argument('--merge', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--files', type=int, action='append')
    parser.add_argument('--cpus', type=int, action='append')
    parser.add_argument('--count', type=int, default=3)
    reference_group = parser.add_mutually_exclusive_group()
    reference_group.add_argument('--reference', type=Path)
    reference_group.add_argument('--reference-root', type=Path)
    args = parser.parse_args()
    if args.plan:
        reference = json.loads(args.reference.read_text())['tool_comparisons'] if args.reference else None
        if args.reference_root:
            reference = latest_reference(args.reference_root, platform.machine())
        import benchmark_compare
        tools = {'xff': {'path': 'xff'}, 'find': {}, 'rg': {}, 'fzf': {}}
        names = [name for name, _, _ in benchmark_compare.scenarios(Path('.'), [], tools)]
        result = make_plan(args.files or [], args.cpus or [1, 4], args.count, reference, names)
    else:
        result = merge_reports([json.loads(path.read_text()) for path in args.merge])
    args.output.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
