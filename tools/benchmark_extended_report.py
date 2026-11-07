#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Analyze every extended-history cell and export the existing 3D history viewer."""

import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import random
import shutil
import statistics

import benchmark_extended as suite
import benchmark_landscape as landscape
import benchmark_matrix as matrix


def mean_fastest(samples, keep):
    values = [sample['elapsed_seconds'] for sample in samples]
    if not 1 <= keep <= len(values) or any(not math.isfinite(value) or value <= 0 for value in values):
        raise ValueError('invalid timing population')
    return statistics.fmean(sorted(values)[:keep])


def paired_interval(left, right):
    """95% bootstrap interval for paired geometric ratios, using all measured rounds.

    This describes within-cell variability, not independent-machine reproducibility.
    It deliberately differs from the existing chart's fastest-7/9 estimator.
    """
    a = {sample['round']: sample['elapsed_seconds'] for sample in left}
    b = {sample['round']: sample['elapsed_seconds'] for sample in right}
    if len(a) != len(left) or len(b) != len(right) or a.keys() != b.keys() or not a:
        raise ValueError('unpaired or duplicate rounds')
    values = [math.log(a[key] / b[key]) for key in sorted(a)]
    rng = random.Random(20261107)
    estimates = sorted(math.exp(statistics.fmean(rng.choices(values, k=len(values)))) for _ in range(1000))
    return [estimates[24], estimates[974]]


def summarize_cell(row, contract):
    participants = defaultdict(list)
    for sample in row['samples']:
        if sample['phase'] == 'measured':
            participants[sample['label']].append(sample)
    if set(participants) != set(row['commands']):
        raise ValueError('missing participant measurements')
    for samples in participants.values():
        if len(samples) != contract['repetitions'] or {s['round'] for s in samples} != set(range(1, contract['repetitions'] + 1)):
            raise ValueError('missing or repeated sampling rounds')
        if any(s['exit_codes'] != [0] or s['stderr_bytes'] or s['validated_records'] != row['expected_records'] for s in samples):
            raise ValueError('unvalidated sample')
    means = {label: mean_fastest(samples, contract['keep']) for label, samples in participants.items()}
    references = set(means) - {'xff', 'anchor'}
    if len(references) != 1:
        raise ValueError('expected one traditional reference')
    reference = references.pop()
    lower, upper = paired_interval(participants['xff'], participants['anchor'])
    return dict(row['case'], sha=row['revision']['sha'], pr=row['revision']['pr'],
                binary_sha256=row['revision']['binary_sha256'], reference=reference,
                xff_seconds=means['xff'], anchor_seconds=means['anchor'], reference_seconds=means[reference],
                reference_over_xff=means[reference] / means['xff'],
                historical_over_current=means['xff'] / means['anchor'],
                current_time_reduction_percent=100 * (1 - means['anchor'] / means['xff']),
                paired_ratio_ci_low=lower, paired_ratio_ci_high=upper)


def as_report(rows, contract, view):
    """Adapt real cells without filling unmeasured sizes or mixing suite identities."""
    tasks = []
    for row in rows:
        case = row['case']
        participants = {}
        for label, command in row['commands'].items():
            if ((label == 'anchor' and view == 'traditional') or
                    (label not in ('xff', 'anchor') and view == 'anchor')):
                continue
            name = 'xff-at-' + str(contract['anchor']['pr']) if label == 'anchor' else label
            participants[name] = {'pipeline': [command], 'cwd': row['cwd'],
                                 'samples': [sample for sample in row['samples']
                                             if sample['label'] == label and sample['phase'] == 'measured']}
        tasks.append({'name': case['name'], 'dataset': case['shape'], 'shape': case['shape'],
                      'files': case['files'], 'input_files': case['files'], 'cpus': case['cpus'],
                      'expected_count': row['expected_records'], 'expected_sha256': row['expected_sha256'],
                      'participants': participants, 'skips': {}})
    return {'contract': {'suite': contract['recipe'], 'platform': contract['platform'],
                         'cpu_counts': sorted({case['cpus'] for case in contract['cases']}),
                         'file_counts': sorted({case['files'] for case in contract['cases']}),
                         'affinity_by_cpu_count': {str(n): contract['affinity'][:n]
                                                   for n in {case['cpus'] for case in contract['cases']}},
                         'repetitions': contract['repetitions'], 'retained': contract['keep'],
                         'estimator': 'mean-fastest'}, 'tasks': tasks}


def export_viewer(output, contract, revisions, renderer):
    assets = output / 'assets'
    assets.mkdir(exist_ok=True)
    shutil.copyfile(renderer, assets / 'three-landscape.js')
    catalog = []
    for view, title in [('traditional', 'Traditional tools'), ('anchor', f'Fixed XFF at PR #{contract["anchor"]["pr"]}')]:
        reports = [(revision, rows, as_report(rows, contract, view)) for revision, rows in revisions]
        order = landscape.common_task_order([matrix.relative_results(report) for _, _, report in reports])
        for revision, rows, report in reports:
            directory = output / revision['sha']
            directory.mkdir(exist_ok=True)
            data = landscape.figures(report, task_order=order)
            (directory / (view + '.json')).write_text(json.dumps(data, allow_nan=False) + '\n')
            document = landscape.render(report, '', task_order=order).replace(
                '<script></script>', '<script src="../assets/three-landscape.js"></script>')
            (directory / (view + '.html')).write_text(document)
            catalog.append({'platform': 'Drogon / ' + contract['recipe'] + ' / ' + title,
                            'platform_name': contract['platform'], 'dataset_type': contract['recipe'] + ' / ' + title,
                            'commit': revision['sha'], 'date': revision['date'],
                            'label': f'PR #{revision["pr"]} / {revision["sha"][:10]}',
                            'source': 'merged', 'source_label': 'Merged history', 'local': True,
                            'measured': rows[-1]['after']['utc'], 'measurement_date': rows[-1]['after']['utc'],
                            'report': revision['sha'] + '/' + view + '.html',
                            'figures': revision['sha'] + '/' + view + '.json',
                            'links': [{'label': f'PR #{revision["pr"]}', 'href': f'https://github.com/mboworks/xff/pull/{revision["pr"]}'}],
                            'binary': {'state': 'Consistent', 'sha256': revision['binary_sha256'],
                                       'reason': 'Clean historical source, configured production ThinLTO build, frozen executable hash checked before and after collection.'},
                            'identity': 'Drogon Linux extended qualification', 'platform_details': contract['platform']})
    (output / 'catalog.json').write_text(json.dumps(catalog, indent=2) + '\n')
    intro = ('<!doctype html><html lang="en"><meta charset="utf-8"><title>Extended benchmark history</title>'
             '<style>body{font:16px system-ui;margin:1.5rem}summary{cursor:pointer}</style>'
             '<h1>Extended benchmark history</h1><p>Independent extended suite; production full binaries on Linux tmpfs. '
             'Each historical run is interleaved with the fixed current binary and a traditional tool. '
             f'{len(revisions)} complete revisions are shown, including regressions and unchanged controls.</p>')
    (output / 'index.html').write_text(intro + landscape.history_panel(catalog) + '</html>')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer-js', type=Path)
    parser.add_argument('--allow-partial', action='store_true')
    args = parser.parse_args()
    root = args.campaign
    if (root / 'invalid.json').exists():
        raise ValueError('campaign identity verification failed')
    progress = json.loads((root / 'progress.json').read_text())
    if not args.allow_partial and progress['state'] != 'complete':
        raise ValueError('measurement campaign is not complete')
    contract = json.loads((root / 'contract.json').read_text())
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    rows, revisions = [], []
    for revision in contract['revisions']:
        cells = []
        for case in contract['cases']:
            path = root / 'cells' / revision['sha'] / suite.Case(**case).key / 'complete.json'
            if not path.exists() and args.allow_partial:
                continue
            row = json.loads(path.read_text())
            if row['case'] != case or row['revision'] != revision or row['anchor'] != contract['anchor']:
                raise ValueError('cell provenance differs from campaign contract')
            rows.append(summarize_cell(row, contract))
            cells.append(row)
        if len(cells) == len(contract['cases']):
            revisions.append((revision, cells))
    if not rows:
        raise ValueError('no completed measurement cells')
    with (output / 'cells.csv').open('w') as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    controls = [row for row in rows if row['binary_sha256'] == contract['anchor']['binary_sha256']]
    noise = sorted(abs(math.log(row['historical_over_current'])) for row in controls)
    summary = {'recipe': contract['recipe'], 'state': progress['state'], 'cells': len(rows),
               'complete_revisions': len(revisions), 'expected_revisions': len(contract['revisions']),
               'identical_binary_control_cells': len(controls),
               'control_p95_factor': math.exp(noise[min(len(noise) - 1, int(len(noise) * .95))]) if noise else None,
               'interval': '95% paired-round bootstrap geometric ratio; within-cell variability only',
               'contract_sha256': hashlib.sha256((root / 'contract.json').read_bytes()).hexdigest(),
               'endpoints': {str(revision['pr']): [row for row in rows if row['sha'] == revision['sha']]
                             for revision in (contract['revisions'][0], contract['revisions'][-1])}}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    if args.renderer_js and revisions:
        export_viewer(output, contract, revisions, args.renderer_js)
    print(json.dumps({key: value for key, value in summary.items() if key != 'endpoints'}, indent=2))


if __name__ == '__main__':
    main()
