# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Reference-window presentation corrections; raw measurements are never modified."""

import json
import statistics

import benchmark_matrix as matrix
import benchmark_records


def cell_key(task, reference):
    return json.dumps([task['dataset'], task.get('name', task.get('task')),
                       task['cpus'], task['files'], reference])


def compatibility(record):
    contract = matrix.compatibility(record['tool_comparisons'])
    # XFF's build changes across versions; replay batches identify jobs, not workloads.
    contract.pop('build_identity', None)
    contract.pop('batch', None)
    if not benchmark_records.is_local(record):
        platform = benchmark_records.platform_key(record)
        runner = {'linux': 'ubuntu-latest', 'macos': 'macos-latest'}.get(platform)
        # Ordinary CI and its replacement campaign use the same hosted runner class
        # under different names. Keep every actual machine/workload constraint.
        if runner and contract.get('runner_class') == 'github-hosted ' + runner:
            contract['runner_class'] = 'github-ci-' + platform
    return contract


def reference_windows(records):
    """Use the first five references initially, then the current and four prior."""
    selected = {}
    results = {}
    for identity, record in records:
        report = record.get('tool_comparisons')
        if not report:
            continue
        results[identity] = {cell_key(row, row['reference']): {
            'status': 'unavailable', 'reason': 'Not the selected measurement for this version.'}
            for row in matrix.relative_results(report)}
        local = benchmark_records.is_local(record)
        source = record.get('source', {})
        commit = record.get('head') or source.get('head_sha')
        if not commit or (not local and not all(field in source for field in ('id', 'run_attempt', 'created_at'))):
            for value in results[identity].values():
                value['reason'] = 'Incomplete measurement provenance.'
            continue
        series = (benchmark_records.platform_key(record), report['contract'].get('machine'),
                  record['series'] if local else 'CI')
        rank = ((record['completed_at'], identity) if local else benchmark_records.preference(record))
        key = (series, commit)
        if key not in selected or rank > selected[key][0]:
            selected[key] = (rank, identity, record, series)
    groups = {}
    for _, identity, record, series in selected.values():
        report = record['tool_comparisons']
        for task in report['tasks']:
            if 'xff' not in task['participants']:
                continue
            for reference, entry in task['participants'].items():
                if reference == 'xff':
                    continue
                key = cell_key(task, reference)
                missing = (not all(task.get(field) for field in ('fixture_identity', 'expected_sha256')) or
                           not all(report.get('tools', {}).get(tool, {}).get('sha256')
                                   for tool in reference.split('+')) or
                           not all(field in report['contract'] for field in ('storage', 'affinity_by_cpu_count')))
                if missing:
                    results[identity][key] = {'status': 'unavailable', 'reason': 'Incomplete measurement identity.'}
                    continue
                contract = compatibility(record)
                xff_args = [command[1:] for command in task['participants']['xff'].get('pipeline', [])]
                grouping = json.dumps([series, contract, key, matrix.cell_contract(task),
                                      matrix.participant_contract(report, reference, entry), xff_args], sort_keys=True)
                groups.setdefault(grouping, []).append({
                    'identity': identity, 'key': key,
                    'commit': record.get('head') or record['source']['head_sha'],
                    'date': benchmark_records.reference_time(record),
                    'reference_seconds': matrix.elapsed_mean(report, entry),
                    'xff_seconds': matrix.elapsed_mean(report, task['participants']['xff']),
                })
    for measurements in groups.values():
        measurements.sort(key=lambda item: (item['date'], item['commit']))
        for index, current in enumerate(measurements):
            if len(measurements) < 5:
                results[current['identity']][current['key']] = {
                    'status': 'unavailable', 'reason': f'Only {len(measurements)} compatible measurements; five required.'}
                continue
            window = measurements[:5] if index < 5 else measurements[index - 4:index + 1]
            mean = statistics.fmean(item['reference_seconds'] for item in window)
            factor = mean / current['reference_seconds']
            results[current['identity']][current['key']] = {
                'status': 'available', 'factor': factor, 'reference_mean_seconds': mean,
                'xff_seconds': current['xff_seconds'] * factor,
                'window': [{'commit': item['commit'], 'date': item['date'], 'report': item['identity']}
                           for item in window],
            }
    return results
