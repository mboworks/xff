# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Publish discoverable measurement recipes and immutable observation provenance."""

import hashlib
import html
import json
from pathlib import PurePosixPath

import benchmark_records as records
import benchmark_layouts


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def recipe(report, local_contract=None):
    contract = report['contract']
    tasks = {}
    for task in report['tasks']:
        tasks.setdefault(task['name'], set()).update(task['participants'])
    allocation = (local_contract or {}).get('allocation', {}).get('kind')
    if allocation is None:
        masks = contract.get('affinity_by_cpu_count', {})
        allocation = 'logical-cpus' if masks and all(mask is not None for mask in masks.values()) else 'workers'
    result = dict(files=contract.get('file_counts'), cpus=contract.get('cpu_counts'), depth=contract.get('depth'),
                  repetitions=contract.get('repetitions'), keep=contract.get('retained'),
                  estimator=contract.get('estimator'), fixture_version=contract.get('fixture_version'),
                  custom_fixtures=bool(contract.get('invocation', {}).get('fixtures')) or
                  any(task.get('fixture_source_sha256') for task in report['tasks']),
                  trees=sorted({task['dataset'] for task in report['tasks']}),
                  tasks={key: sorted(value) for key, value in sorted(tasks.items())},
                  allocation=allocation, memory_required=contract.get('storage', {}).get('memory_required'),
                  warmup_rounds=contract.get('warmup_rounds'), order=contract.get('order'),
                  cache=contract.get('cache'), output=contract.get('output'))
    if contract.get('layouts'):
        result['layouts'] = [dict(layout) for layout in contract['layouts']]
    return result


def build_catalog(root):
    paths = {*records.run_paths(root), *root.glob('backfills/*/*/*/*/report.json'),
             *root.glob('local/*/*/*/report.json')}
    paths.update(path.with_suffix('') for pattern in ('backfills/*/*/*/*/report.json.gz', 'local/*/*/*/report.json.gz')
                 for path in root.glob(pattern))
    datasets = {}
    for path in sorted(paths):
        record = records.read(path)
        report = record.get('tool_comparisons')
        commit = record.get('head') or record.get('source', {}).get('head_sha')
        if not report or not commit or records.is_preview(record):
            continue
        local = records.is_local(record)
        batch_path = path.parent.parent / 'batch.json' if local else path.parent / 'batch.json'
        manifest = records.read(batch_path) if records.exists(batch_path) else {}
        contract = manifest.get('contract', {})
        environment = contract.get('environment', {})
        description = dict(series=record.get('series', 'github-ci-' + records.platform_key(record)),
                           kind='local' if local else 'ci', platform=records.platform_key(record),
                           architecture=report['contract'].get('machine'), recipe=recipe(report, contract))
        key = identity(description)
        dataset = datasets.setdefault(key, dict(id=key, **description, machines=[], methods={}, observations=[]))
        machine = dict(machine_id=contract.get('machine_id'), host_id=environment.get('host_id'),
                       architecture=environment.get('machine'), cpu_count=environment.get('cpu_count'))
        if local and machine not in dataset['machines']:
            dataset['machines'].append(machine)
        method = dict(driver=contract.get('driver'), tools={name: {key: value for key, value in tool.items()
                                                                if key in ('sha256', 'version', 'status')}
                                                          for name, tool in report.get('tools', {}).items() if name != 'xff'},
                      build=record.get('build', report['contract'].get('build_identity')),
                      sampling=report['contract'].get('sampling'),
                      environment=report['contract'].get('environment'),
                      platform=report['contract'].get('platform'),
                      cpu_count=report['contract'].get('cpu_count'),
                      affinity=report['contract'].get('affinity_by_cpu_count'),
                      storage=report['contract'].get('storage'))
        method_id = identity(method)
        dataset['methods'][method_id] = method
        dataset['observations'].append(dict(commit=commit, date=records.reference_time(record),
                                             measured=record.get('completed_at', record.get('source', {}).get('created_at')),
                                             report=path.relative_to(root).as_posix(), report_identity=identity(record),
                                             batch=batch_path.relative_to(root).as_posix() if manifest else None,
                                             batch_identity=identity(manifest) if manifest else None, method=method_id))
    result = dict(schema=1, datasets=sorted(datasets.values(), key=lambda item: (item['series'], item['id'])))
    validate(result)
    return result


def relative_path(value):
    """Catalog links are data paths below the benchmark directory, never URLs or commands."""
    path = PurePosixPath(value)
    if (not value or path.is_absolute() or str(path) != value or any(part in ('.', '..') for part in path.parts)
            or any(char in value for char in ('\\', ':', '?', '#', '%'))):
        raise ValueError('invalid dataset data path: ' + value)
    return value


def validate(catalog):
    try:
        validate_structure(catalog)
    except (AttributeError, KeyError, TypeError) as error:
        raise ValueError('malformed benchmark dataset catalog') from error


def validate_structure(catalog):
    if catalog.get('schema') != 1 or not isinstance(catalog.get('datasets'), list):
        raise ValueError('unsupported benchmark dataset catalog')
    seen = set()
    for dataset in catalog['datasets']:
        description = {key: dataset[key] for key in ('series', 'kind', 'platform', 'architecture', 'recipe')}
        if dataset['id'] != identity(description) or dataset['id'] in seen:
            raise ValueError('invalid or duplicate dataset identity')
        seen.add(dataset['id'])
        if dataset['kind'] not in ('ci', 'local'):
            raise ValueError('unknown dataset origin')
        for key, method in dataset['methods'].items():
            if key != identity(method):
                raise ValueError('measurement method identity changed')
        for observation in dataset['observations']:
            relative_path(observation['report'])
            if observation.get('batch'):
                relative_path(observation['batch'])
            if observation['method'] not in dataset['methods']:
                raise ValueError('unknown measurement method')


def compatibility(dataset, platform, architecture, capacity, tasks):
    """Can the current driver collect this recipe here? This does not assert equal timings."""
    value = dataset['recipe']
    reasons = []
    if (dataset['platform'], dataset['architecture']) != (platform, architecture):
        reasons.append('different OS or architecture')
    for key in ('cpus', 'files'):
        values = value.get(key)
        if (not isinstance(values, list) or not values or any(type(item) is not int or item < 1 for item in values)
                or len(set(values)) != len(values)):
            reasons.append('invalid or unknown ' + key + ' grid')
    if not reasons and max(value['cpus']) > capacity:
        reasons.append(f'requires {max(value["cpus"])} CPUs; {capacity} available')
    layouts = value.get('layouts', [])
    supported_layouts = [(layout.get('name'), layout.get('revision')) for layout in layouts]
    if value.get('fixture_version') not in (2, 3) or value.get('custom_fixtures'):
        reasons.append('unsupported or unknown fixture recipe')
    if layouts and value.get('fixture_version') != 3:
        reasons.append('layout definitions require fixture version 3')
    if value.get('fixture_version') == 3 and supported_layouts != [('broad', 2), ('deep', 2)]:
        reasons.append('unsupported or unknown layout revision')
    if value.get('fixture_version') == 3 and isinstance(value.get('files'), list):
        expected = []
        try:
            for name, revision in supported_layouts:
                prepared = benchmark_layouts.prepare(f'{name}/v{revision}', tuple(value['files']))
                definition = prepared.identity
                definition['generator_identity'] = benchmark_layouts.generator_identity(prepared)
                definition['anchor_hashes'] = {str(key): item for key, item in prepared.anchor_hashes.items()}
                expected.append(definition)
        except ValueError:
            expected = None
        if layouts != expected:
            reasons.append('layout recipe does not match the registered definition')
    if value.get('trees') != ['broad', 'deep'] or value.get('tasks') != tasks:
        reasons.append('different tree/task/participant definitions')
    samples = [value.get(key) for key in ('depth', 'repetitions', 'keep')]
    if any(type(item) is not int or item < 1 for item in samples) or samples[2] > samples[1]:
        reasons.append('invalid or unknown depth/sampling')
    if value.get('estimator') != 'mean-fastest' or value.get('warmup_rounds') != 1:
        reasons.append('unsupported estimator or warm-up policy')
    if value.get('order') != 'rotate starting participant by task and round':
        reasons.append('unsupported participant scheduling')
    if type(value.get('memory_required')) is not bool:
        reasons.append('unknown fixture storage policy')
    if platform == 'linux' and (value.get('allocation') != 'physical-cores' or not value.get('memory_required')):
        reasons.append('local Linux requires physical-core affinity and memory-backed fixtures')
    if platform == 'macos' and (value.get('allocation') != 'workers' or value.get('memory_required')):
        reasons.append('local macOS requires worker allocations and host fixtures')
    return reasons


def render(catalog):
    rows = []
    for dataset in catalog['datasets']:
        value = dataset['recipe']
        files = value.get('files') or []
        cells = [dataset['id'][:12], dataset['series'], dataset['kind'],
                 f"{dataset['platform']} / {dataset['architecture'] or 'unknown'}",
                 ', '.join(f"{layout['name']}/v{layout['revision']}" for layout in value.get('layouts', []))
                 or 'broad/v1, deep/v1 (legacy)',
                 ', '.join(map(str, value.get('cpus') or [])),
                 f'{min(files):,} to {max(files):,} ({len(files)} sizes)' if files else 'Unknown',
                 f"{value.get('keep')} of {value.get('repetitions')}; {value.get('estimator')}",
                 str(len({item['commit'] for item in dataset['observations']}))]
        rows.append('<tr>' + ''.join('<td>' + html.escape(cell) + '</td>' for cell in cells) + '</tr>')
    return ('<details><summary>Measurement datasets and methods</summary>'
            '<p>Each dataset records its machine series and workload recipe. '
            'The same recipe on another machine produces a separate series. '
            'Driver, compiler/build identity, reference tools, allocation, storage and sampling provenance '
            'remain attached to each observation. Compatibility for collection does not imply interchangeable timings.</p>'
            '<p><a href="datasets.json">Download dataset metadata</a>. '
            'Run <code>bazel run //tools:benchmark -- list</code> to discover recipes compatible with your machine; '
            '<code>bazel run //tools:benchmark -- backfill</code> selects a recipe and asks before measuring missing revisions.</p>'
            '<table><tr><th>Dataset</th><th>Series</th><th>Origin</th><th>Platform</th><th>Layouts</th><th>CPUs/workers</th>'
            '<th>File counts</th><th>Sampling</th><th>Revisions</th></tr>' + ''.join(rows) + '</table></details>')


def publish(root):
    catalog = build_catalog(root)
    (root / 'datasets.json').write_text(json.dumps(catalog, indent=2, allow_nan=False) + '\n')
    page = root / 'index.html'
    if page.exists():
        text = page.read_text()
        start, end = '<!-- benchmark-datasets:start -->', '<!-- benchmark-datasets:end -->'
        if start in text:
            before, _, rest = text.partition(start)
            _, separator, after = rest.partition(end)
            if not separator:
                raise ValueError('incomplete dataset section')
            text = before + after
        section = start + render(catalog) + end
        text = text.replace('</body>', section + '</body>') if '</body>' in text else text + section
        page.write_text(text)
    return catalog
