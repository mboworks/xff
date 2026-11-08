# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Validate and retain extended observations separately from core benchmark history."""

from dataclasses import asdict
from datetime import datetime
from functools import lru_cache
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import tempfile

import benchmark_extended as suite
import benchmark_extended_report as report
import benchmark_provenance


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def read(path):
    return json.loads(path.read_text() if path.exists() else gzip.decompress(path.with_suffix('.json.gz').read_bytes()))


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def observation_identity(record):
    # Retention attestations and trusted publication metadata can be added later;
    # the observation identity binds the original contract, builds and raw rows.
    return identity({key: value for key, value in record.items()
                     if key not in ('identity', 'view_verification', 'publication_source')})


def build_proof(directory, revision):
    manifest = read(directory / 'build.json')
    require(manifest['source']['revision'] == revision['sha'] and manifest['source']['clean'] is True,
            'build source identity changed')
    require(digest(directory / 'build.json') == revision['build_sha256']
            and digest(directory / 'link-actions.json') == revision['link_actions_sha256'], 'build evidence changed')
    require(digest(directory / 'xff_full') == manifest['sha256'] == revision['binary_sha256'], 'frozen binary changed')
    links = [action['arguments'] for action in read(directory / 'link-actions.json')['actions']
             if manifest['artifact'] in action.get('arguments', [])]
    require(len(links) == 1 and '-flto=thin' in links[0], 'production ThinLTO link evidence missing')
    require(all(flag in manifest['command'] for flag in ('//xff/cli:xff_full', '--config=clang_release', '--config=xff_full')),
            'full production configuration missing')
    return {'manifest': manifest, 'link_arguments': links[0],
            'manifest_sha256': revision['build_sha256'], 'link_actions_sha256': revision['link_actions_sha256']}


@lru_cache(maxsize=256)
def oracle(name, shape, files, fixture):
    wanted = suite.expected(suite.Case(name, shape, files, 1), Path(fixture))
    return len(wanted), hashlib.sha256(b'\0'.join(sorted(wanted))).hexdigest(), sum(map(len, wanted)) + len(wanted)


@lru_cache(maxsize=64)
def fixture_identity(shape, files, archive):
    return suite.fixture_identity(shape, files, archive)


def validate_rows(rows, contract, revision):
    expected = {suite.Case(**value).key: value for value in contract['cases']}
    require(len(rows) == len(expected), 'incomplete extended revision')
    seen = set()
    for row in rows:
        case = suite.Case(**row['case'])
        require(case.key in expected and case.key not in seen and row['case'] == expected[case.key],
                'unexpected or repeated extended cell')
        seen.add(case.key)
        require(row['revision'] == revision and row['anchor'] == contract['anchor'], 'cell provenance changed')
        count, expected_hash, expected_bytes = oracle(case.name, case.shape, case.files, contract['fixture'])
        require(row['expected_records'] == count and row['expected_sha256'] == expected_hash, 'cell oracle changed')
        commands, cwd, _ = suite.commands(case, Path(contract['fixture']), revision['binary'],
                                         {key: value['path'] for key, value in contract['tools'].items()})
        anchor, _, _ = suite.commands(case, Path(contract['fixture']), contract['anchor']['binary'],
                                      {key: value['path'] for key, value in contract['tools'].items()})
        commands['anchor'] = anchor['xff']
        require(row['commands'] == commands and row['cwd'] == str(cwd)
                and row['affinity'] == contract['affinity'][:case.cpus], 'cell commands or allocation changed')
        require(row['environment'] == {'PATH': '/usr/bin:/bin', 'HOME': str(Path(contract['fixture']) / 'home'),
                                       'LC_ALL': 'C', 'TZ': 'UTC'}, 'cell environment changed')
        samples = row['samples']
        keys = {(label, phase, round_number) for label in commands
                for phase, rounds in [('warmup', [0]), ('measured', range(1, contract['repetitions'] + 1))]
                for round_number in rounds}
        require(len(samples) == len(keys) and {(s['label'], s['phase'], s['round']) for s in samples} == keys,
                'incomplete or repeated extended samples')
        for sample in samples:
            require(sample['exit_codes'] == [0] and sample['stderr_bytes'] == 0 and not sample.get('stderr')
                    and sample['validated_records'] == count and sample['stdout_bytes'] == expected_bytes,
                    'unsuccessful or unvalidated extended sample')
            require(re.fullmatch('[0-9a-f]{64}', sample['stdout_sha256']) is not None, 'missing output hash')
            for field in ('elapsed_seconds', 'user_cpu_seconds', 'system_cpu_seconds', 'peak_child_rss_bytes'):
                value = sample[field]
                require(type(value) in (int, float) and math.isfinite(value) and value >= 0, 'invalid resource sample')
            require(sample['elapsed_seconds'] > 0, 'invalid elapsed sample')
        # Also validate the existing chart's estimator and complete participant populations.
        adapted = report.as_report([row], contract, 'traditional')
        for task in adapted['tasks']:
            for participant in task['participants'].values():
                report.matrix.selected_samples(adapted, participant)


def validate(record, source=None):
    require(record.get('schema') == 1 and record.get('kind') == 'extended-production', 'unsupported extended record')
    require(record['identity'] == observation_identity(record),
            'extended record identity changed')
    contract = record['contract']
    require(contract['recipe'] == suite.RECIPE and contract['cases'] == [asdict(case) for case in suite.cases()],
            'extended workload contract changed')
    require(contract['warmup_rounds'] == 1 and contract['estimator'] == 'mean-fastest'
            and (contract['repetitions'], contract['keep']) == (9, 7), 'invalid extended sampling')
    require(record['comparison'] in ('parent', 'fixed-anchor'), 'unknown extended comparison')
    require(record['origin'] in ('local', 'ci') and re.fullmatch('[A-Za-z0-9_.-]+', record['series']), 'invalid extended series')
    require(record['platform'] in ('linux', 'macos'), 'unknown extended platform')
    require(record['machine'] in contract['platform'].lower(), 'extended architecture does not match platform')
    require(contract['filesystem'] == 'tmpfs' if record['platform'] == 'linux' else
            isinstance(contract['filesystem'], dict) and contract['filesystem'].get('storage') == 'memory',
            'unverified memory fixture')
    affinity = contract['affinity']
    require((len(affinity) >= 3 and len(affinity) == len(set(affinity))
             and all(type(cpu) is int and cpu >= 0 for cpu in affinity)) if record['platform'] == 'linux'
            else affinity == [], 'invalid extended CPU allocation')
    revision = record['revision']
    require(revision in contract['revisions'], 'revision not in extended contract')
    for entry in (revision, contract['anchor']):
        require(re.fullmatch('[0-9a-f]{40}', entry['sha']) is not None, 'invalid revision')
        require(datetime.fromisoformat(entry['date']).tzinfo is not None, 'revision date needs a timezone')
        proof = record['builds'][entry['sha']]
        manifest = proof['manifest']
        require(manifest['source']['revision'] == entry['sha'] and manifest['source']['clean'] is True
                and manifest['sha256'] == entry['binary_sha256']
                and proof['manifest_sha256'] == entry['build_sha256']
                and proof['link_actions_sha256'] == entry['link_actions_sha256'], 'binary/build identity mismatch')
        require('-flto=thin' in proof['link_arguments'] and manifest['artifact'] in proof['link_arguments']
                and all(flag in manifest['command'] for flag in ('//xff/cli:xff_full', '--config=clang_release', '--config=xff_full')),
                'production link/configuration evidence missing')
    if record['origin'] == 'ci':
        collection = record['collection']
        require(all(type(collection[key]) is int and collection[key] > 0 for key in ('run_id', 'run_attempt')),
                'invalid extended workflow identity')
        require(record['comparison'] == 'parent' and collection['head_sha'] == revision['sha']
                and collection['base_sha'] == contract['anchor']['sha'], 'CI revision/parent changed')
        if source is not None:
            require(source['name'] == 'Benchmarks' and source['path'] == '.github/workflows/benchmarks.yml'
                    and source['event'] == 'push' and source['head_branch'] == 'main' and source['status'] == 'completed',
                    'untrusted extended workflow')
            require(all(str(collection[key]) == str(source[field]) for key, field in
                        [('run_id', 'id'), ('run_attempt', 'run_attempt'), ('head_sha', 'head_sha')]),
                    'extended workflow provenance mismatch')
            require(collection['repository'] == source['repository']['full_name'], 'extended repository mismatch')
            require(source['base_sha'] == contract['anchor']['sha'], 'extended anchor is not the trusted first parent')
    validate_rows(record['rows'], contract, revision)
    if record.get('controls'):
        validate_rows(record['controls'], contract, contract['anchor'])
    for view in ('traditional', 'anchor'):
        checked = benchmark_provenance.describe(view_report(record, view), record['view_verification'][view])
        require(checked['state'] == 'Verified against Retention', checked['reason'])


def view_report(record, view):
    adapted = report.as_report(record['rows'], record['contract'], view)
    anchor = record['contract']['anchor']
    tools = {key: dict(value, status='available') for key, value in record['contract']['tools'].items()}
    tools['xff'] = {'path': record['revision']['binary'], 'sha256': record['revision']['binary_sha256'], 'status': 'available'}
    if view == 'anchor':
        old = next(key for key in adapted['tasks'][0]['participants'] if key != 'xff')
        name = 'xff-parent' if record['comparison'] == 'parent' else old
        tools[name] = {'path': anchor['binary'], 'sha256': anchor['binary_sha256'], 'status': 'available'}
        if name != old:
            for task in adapted['tasks']:
                task['participants'][name] = task['participants'].pop(old)
    adapted['tools'] = tools
    for task in adapted['tasks']:
        task['fixture_identity'] = fixture_identity(task['dataset'], task['files'], task['name'] == 'archive-tar')
    adapted['contract'].update(comparison=record['comparison'] if view == 'anchor' else 'traditional',
                               production_target='//xff/cli:xff_full',
                               machine=record['machine'], warmup_rounds=1,
                               storage={'filesystem': 'tmpfs' if record['platform'] == 'linux' else 'case-sensitive HFS+',
                                        'memory_required': True},
                               resource_accounting={key: record['contract']['launcher'][key]
                                                    for key in ('wall_method', 'memory_method')})
    # Empty means unpinned workers, never an enforceable zero-length mask.
    if not record['contract']['affinity']:
        adapted['contract']['affinity_by_cpu_count'] = {str(n): None for n in adapted['contract']['cpu_counts']}
    source = record.get('publication_source', {'head_sha': record['revision']['sha']})
    source = dict(source, reference_time=record['revision']['date'])
    if record['origin'] == 'local':
        source = dict(source, created_at=record['completed_at'], reference_time=record['revision']['date'],
                      event='local', head_branch='local')
    result = {'schema': 1, 'kind': 'extended-production-view', 'head': record['revision']['sha'],
              'base': anchor['sha'], 'revision': record['revision'], 'platform': record['platform'],
              'series': record['series'], 'completed_at': record['completed_at'],
              'source': source, 'build': record['builds'][record['revision']['sha']]['manifest'],
              'tool_comparisons': adapted}
    if record['origin'] == 'local':
        result.update(kind='backfill', purpose='local-addition')
    if view in record.get('view_verification', {}):
        result['binary_verification'] = record['view_verification'][view]
    return result


def make_record(contract, revision, rows, builds, platform, machine, series, comparison,
                origin='local', collection=None, controls=None, retention_reference='original frozen executable'):
    result = {'schema': 1, 'kind': 'extended-production', 'contract': contract, 'revision': revision,
              'rows': rows, 'builds': builds, 'platform': platform, 'machine': machine, 'series': series,
              'comparison': comparison, 'origin': origin, 'collection': collection,
              'controls': controls or [], 'completed_at': max(row['after']['utc'] for row in rows)}
    result['view_verification'] = {}
    for view in ('traditional', 'anchor'):
        wrapper = view_report(result, view)
        with Path(revision['binary']).open('rb') as executable:
            result['view_verification'][view] = benchmark_provenance.verify_retention(wrapper, executable, retention_reference)
    result['identity'] = observation_identity(result)
    validate(result)
    return result


def retain(root, record, source=None):
    require(record['origin'] != 'ci' or source is not None, 'CI retention requires trusted workflow metadata')
    validate(record, source)
    if source is not None:
        record = dict(record, publication_source={key: source[key] for key in
                      ('id', 'run_attempt', 'created_at', 'head_sha', 'head_branch', 'event',
                       'name', 'path', 'status', 'repository', 'base_sha')})
    destination = root / 'extended' / 'observations' / record['identity'] / 'report.json'
    if destination.exists() or destination.with_suffix('.json.gz').exists():
        require(observation_identity(read(destination)) == observation_identity(record),
                'retained extended observation cannot change')
        return destination
    destination.parent.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.extended-', dir=destination.parent.parent) as temporary:
        stage = Path(temporary) / 'complete'
        stage.mkdir()
        (stage / 'report.json').write_text(json.dumps(record, allow_nan=False) + '\n')
        stage.rename(destination.parent)
    return destination
