# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Extend CI viewer history while retaining each workload's original observations."""

import copy
import hashlib
import html
import json
import os
import re

import benchmark_landscape as landscape
import benchmark_matrix as matrix
import benchmark_records as records


def original_catalog(catalog):
    """Unwrap derived entries so publishing twice never composes a view with itself."""
    result = []
    for item in catalog:
        if item.get('ci_history_view'):
            if item.get('core_entry'):
                result.append(item['core_entry'])
        elif not item.get('extended'):
            result.append(item)
    return result


def group_key(wrapper):
    report = wrapper['tool_comparisons']
    return records.platform_key(wrapper), report['contract'].get('machine', '')


def rank(item):
    return (item.get('backfill', False), item.get('measured', ''), item.get('run', 0), item.get('attempt', 0))


def aggregate_binary(observations):
    states = ['Not recorded', 'Not verified', 'Consistent', 'Verified against Retention']
    proofs = [value['binary'] for value in observations]
    state = min((proof['state'] for proof in proofs), key=states.index)
    return dict(state=state, sha256=proofs[0].get('sha256') if len(proofs) == 1 else None,
                binaries=[dict(name=value['name'], **value['binary']) for value in observations],
                reason='; '.join(value['name'] + ': ' + value['binary']['state'] +
                                 (' / ' + value['binary']['sha256'] if value['binary'].get('sha256') else '')
                                 for value in observations))


def workload(root, directory, path, wrapper, name, namespace, normalization):
    report = wrapper['tool_comparisons']
    contract = report['contract']
    relative = path.relative_to(root).as_posix()
    observation = dict(name=name, report=relative,
                       identity=hashlib.sha256(json.dumps(wrapper, sort_keys=True, allow_nan=False).encode()).hexdigest(),
                       href=os.path.relpath(path.parent, directory) + '/',
                       recipe=contract.get('suite', landscape.layout_details(contract)),
                       target=contract.get('production_target', '//xff/cli:xff'),
                       sha256=report.get('tools', {}).get('xff', {}).get('sha256'),
                       sampling=matrix.policy(report), platform=contract.get('platform', 'Not recorded'),
                       measured=wrapper.get('completed_at') or 'Not recorded',
                       contract=copy.deepcopy(contract), binary=landscape.binary_evidence(path, wrapper))
    adjusted = {}
    for key, value in normalization.items():
        coordinate = json.loads(key)
        coordinate[1] = namespace + coordinate[1]
        adjusted[json.dumps(coordinate)] = value
    return dict(name=name, namespace=namespace, observation=observation, report=report), adjusted


def presentation(components, grid):
    """Grid dimensions are presentation metadata; arithmetic uses the source reports."""
    first = components[0]['report']['contract']
    affinity = {}
    for cpu in grid['cpu_counts']:
        masks = [part['report']['contract'].get('affinity_by_cpu_count', {}).get(str(cpu))
                 for part in components if cpu in part['report']['contract']['cpu_counts']]
        affinity[str(cpu)] = masks[0] if masks and masks[0] is not None and all(mask == masks[0] for mask in masks) else None
    contract = dict(platform=first.get('platform', 'Not recorded'), machine=first.get('machine', ''),
                    cpu_counts=grid['cpu_counts'], file_counts=grid['file_counts'], affinity_by_cpu_count=affinity)
    core = next((part['report'] for part in components if not part['namespace']), None)
    return dict(presentation='ci-history-v1', contract=contract, tasks=[], workload_reports=components,
                overview_report=core or dict(tasks=[], contract=contract))


def publish(root, catalog, extended):
    """Join only CI records for the same platform, architecture and commit.

    `extended` contains traditional-tool views already validated by the extended
    publisher. Local observations and parent/fixed-anchor comparisons remain separate.
    Derived report files contain links and identities, never duplicate raw samples.
    """
    active = {group_key(wrapper) for _, _, wrapper, _ in extended}
    if not active:
        return catalog
    selected, untouched = {}, []
    for item in catalog:
        path = root / item['report'] / 'report.json'
        if (item.get('local') or item.get('source', 'merged') != 'merged' or not records.exists(path)):
            untouched.append(item)
            continue
        wrapper = records.read(path)
        key = group_key(wrapper)
        if key not in active:
            untouched.append(item)
            continue
        version = key, item['commit']
        if version not in selected or rank(item) > rank(selected[version][0]):
            selected[version] = (item, path, wrapper)
    extended_by_version = {(group_key(wrapper), item['commit']): (item, path, wrapper, normalization)
                           for item, path, wrapper, normalization in extended}
    versions = sorted(set(selected) | set(extended_by_version))
    prepared, grids, rows_by_platform = {}, {}, {}
    for key, commit in versions:
        if re.fullmatch('[0-9a-f]{40}', commit) is None:
            raise ValueError('invalid CI viewer commit')
        series = hashlib.sha256(json.dumps(key).encode()).hexdigest()
        directory = root / 'history' / 'ci' / series / commit
        components, normalization = [], {}
        core = selected.get((key, commit))
        if core:
            item, path, wrapper = core
            norm_path = path.with_name('normalization.json')
            norm = records.read(norm_path) if records.exists(norm_path) else {}
            part, values = workload(root, directory, path, wrapper, 'Core workloads', '', norm)
            components.append(part)
            normalization.update(values)
        extra = extended_by_version.get((key, commit))
        if extra:
            extra_entry, path, wrapper, norm = extra
            part, values = workload(root, directory, path, wrapper, 'Extended workloads',
                                    'extended / ', norm)
            if extra_entry.get('parent_report'):
                part['observation']['parent_report'] = extra_entry['parent_report']
                part['observation']['parent_href'] = os.path.relpath(root / extra_entry['parent_report'], directory) + '/'
            components.append(part)
            normalization.update(values)
        grid = grids.setdefault(key, dict(cpu_counts=set(), file_counts=set()))
        for part in components:
            grid['cpu_counts'].update(part['report']['contract']['cpu_counts'])
            grid['file_counts'].update(part['report']['contract']['file_counts'])
        prepared[key, commit] = (directory, components, normalization, core, extra)
        rows_by_platform.setdefault(key, []).append(matrix.relative_results(presentation(components, dict(
            cpu_counts=sorted(grid['cpu_counts']), file_counts=sorted(grid['file_counts'])))))
    orders = {key: landscape.common_task_order(rows) for key, rows in rows_by_platform.items()}
    for (key, commit), (directory, components, normalization, core, extra) in prepared.items():
        directory.mkdir(parents=True, exist_ok=True)
        grid = {name: sorted(values) for name, values in grids[key].items()}
        report = presentation(components, grid)
        data = landscape.figures(report, normalization, orders[key])
        observations = [part['observation'] for part in components]
        binary = aggregate_binary(observations)
        wrapper = dict(schema=1, kind='ci-history-view', head=commit, platform=key[0], machine=key[1],
                       observations=observations, binary=binary,
                       overview_scope='Core workloads only; extended workloads do not change the historical score.')
        (directory / 'report.json').write_text(json.dumps(wrapper, allow_nan=False) + '\n')
        (directory / 'landscape.json').write_text(json.dumps(data, allow_nan=False) + '\n')
        (directory / 'normalization.json').write_text(json.dumps(normalization, allow_nan=False) + '\n')
        document = landscape.render(report, '', normalization, orders[key])
        asset = html.escape(os.path.relpath(root / 'assets/three-landscape.js', directory), quote=True)
        document = document.replace('<script></script>', '<script src="' + asset + '"></script>')
        introduction = ('<p>CI workload history. Each workload keeps its original binary, fixture, sampling and '
                        'machine evidence. Missing cells remain unmeasured. The aggregate overview retains the '
                        'core workload population.</p>')
        document = document.replace('</h1>', '</h1>' + introduction, 1)
        (directory / 'index.html').write_text(document)
        original = core[0] if core else extra[0]
        entry = dict(original, platform=key[0] + (' / ' + key[1] if key[1] else ''),
                     platform_name=key[0] + (' / ' + key[1] if key[1] else ''),
                     dataset_type='Core + extended workloads' if core and extra else
                                  'Core workloads' if core else 'Extended workloads', ci_history_view=True,
                     core_entry=core[0] if core else None, binary=binary,
                     report=directory.relative_to(root).as_posix() + '/',
                     figures=(directory / 'landscape.json').relative_to(root).as_posix(),
                     workload_observations=observations)
        for field in ('extended', 'series_id', 'comparison_role', 'parent_report'):
            entry.pop(field, None)
        untouched.append(entry)
    return untouched
