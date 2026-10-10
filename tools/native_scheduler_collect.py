#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Native M5 Pro full-grid scheduler A/B with retained, rotated observations."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile

DRIVER = Path(__file__).resolve().parent
sys.path.insert(0, str(DRIVER))
import benchmark_backfill as backfill
import benchmark_compare as compare
import benchmark_fixture as fixture
import benchmark_layouts as layouts
import process_resources as resources

GRID = (10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000)
WORKERS = (1, 3, 10)
SHAPES = ('broad', 'deep')
EXTRA_TASKS = ('files-tree', 'files-collect', 'files-stat-lazy', 'files-stat-eager')
ORDERS = ('declared', 'reverse', 'seeded-shuffle-20261010')


def now():
    return datetime.now(timezone.utc).isoformat()


def save(path, value):
    temporary = path.with_suffix('.pending')
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')
    temporary.replace(path)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def context():
    return {'utc': now(), 'load_average': os.getloadavg()}


def no_builds_or_profiles():
    active = subprocess.check_output(['ps', '-A', '-o', 'comm='], text=True).splitlines()
    forbidden = {'clang', 'clang++', 'clang-tidy', 'clang-format', 'ld.lld',
                 'heaptrack', 'heaptrack_print', 'perf', 'bazelisk', 'bazel', 'sample', 'xctrace', 'Instruments'}
    found = sorted(set(Path(name.strip()).name for name in active) & forbidden)
    require(not found, 'build/lint/profiler activity overlaps collection: ' + ', '.join(found))


def reference_tools():
    paths = {name: shutil.which(name) for name in ('find', 'rg', 'fzf')}
    paths['sort'] = shutil.which('gsort') or shutil.which('sort')
    tools = {name: compare.tool_info(name, path) for name, path in paths.items()}
    require(all(tool['status'] == 'available' for tool in tools.values()), 'missing reference tool')
    probe = subprocess.run([tools['sort']['path'], '-z'], input=b'b\0a\0', capture_output=True, check=False)
    require(probe.returncode == 0 and not probe.stderr and probe.stdout == b'a\0b\0',
            'tree-order control requires NUL-capable GNU sort; install coreutils (gsort)')
    return tools


def scenarios(anchor, rows, tools, workers):
    ordinary = list(compare.scenarios(anchor, rows, tools, workers))
    for name, expected, pipelines in ordinary:
        yield name, expected, pipelines, False, {'kind': 'published scenario'}
    expected, common = next((oracle, commands) for name, oracle, commands in ordinary if name == 'files')
    for task in EXTRA_TASKS:
        command = list(common['xff'][0])
        oracle = expected
        ordered = task == 'files-tree'
        reference = common['find']
        if ordered:
            command[command.index('--sort=none')] = '--sort=tree'
            oracle = sorted(expected, key=lambda value: tuple(value.split(b'/')))
            require(oracle == sorted(expected), 'GNU byte sort differs from hierarchy order on fixture')
            reference = [*common['find'], [tools['sort']['path'], '-z']]
        elif task == 'files-collect':
            command.insert(-1, '-collect:audit')
        elif task == 'files-stat-lazy':
            command[-1:-1] = ['-size', '+0']
        elif task == 'files-stat-eager':
            command[-3:] = ['-size', '+0', '-type', 'f', '-print0']
        if task.startswith('files-stat-'):
            oracle = [os.fsencode('./' + path.relative_to(anchor).as_posix())
                      for path, contents in rows if contents]
            reference = [common['find'][0][:-1] + ['-size', '+0c', '-print0']]
        label = 'find+sort' if ordered else 'find-output-control'
        yield task, oracle, {'xff': [command], label: reference}, ordered, {
            'kind': 'additional scheduler control',
            'reference_scope': 'same fixture output; not equivalent collection state or eager metadata policy',
            'tree_order': 'hierarchy and C-locale byte order independently checked equal on this fixture' if ordered else None}


def validate(sample, expected, expected_counter, pipeline, ordered):
    payload = sample['output']
    require(not sample['stderr'] and sample['stderr_bytes'] == 0, 'unexpected command diagnostics')
    for command, code in zip(pipeline, sample['exit_codes'], strict=True):
        require(code == 0 or (code == 1 and not expected and Path(command[0]).name in ('rg', 'fzf')),
                'unexpected command exit: ' + str(command))
    require(not payload or payload.endswith(b'\0'), 'unterminated output')
    actual = payload[:-1].split(b'\0') if payload else []
    require(Counter(actual) == expected_counter, 'output differs from regenerated oracle')
    if ordered:
        require(actual == expected, 'tree order differs from hierarchical oracle')
    require(sample['stdout_bytes'] == len(payload), 'resource/output byte count disagreement')
    require(math.isfinite(sample['elapsed_seconds']) and sample['elapsed_seconds'] > 0,
            'invalid elapsed time')
    first = sample['first_stdout_seconds']
    require((not payload and first is None) or
            (payload and first is not None and 0 <= first <= sample['elapsed_seconds']),
            'invalid first-output time')
    return payload


def deadline(signum, frame):
    raise TimeoutError('native command exceeded 60 seconds')


def measure(pipeline, stdin, anchor, mask, launcher, environment):
    previous = signal.signal(signal.SIGALRM, deadline)
    try:
        with stdin.open('rb') as source:
            require(not mask, 'macOS must not claim Linux CPU affinity')
            signal.setitimer(signal.ITIMER_REAL, 60)
            try:
                return resources.measure_pipeline(pipeline, source=source, cwd=anchor,
                                                  environment=environment, launcher=launcher)
            finally:
                signal.setitimer(signal.ITIMER_REAL, 0)
    finally:
        signal.signal(signal.SIGALRM, previous)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--plan-only', action='store_true')
    parser.add_argument('--fixture-parent', required=True, type=Path)
    parser.add_argument('--storage-label', required=True)
    args = parser.parse_args()
    require(sys.platform == 'darwin', 'collection requires native macOS')
    root = args.root.resolve(strict=True)
    state_path = root / 'build-state.json'
    state = json.loads(state_path.read_bytes())
    fixture_parent = args.fixture_parent.resolve(strict=True)
    require(fixture_parent.is_dir(), 'fixture parent must be an existing directory')
    brand = subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip()
    require('M5 Pro' in brand and brand == state['host']['cpu_brand'], 'native M5 Pro identity differs')
    binaries = state['binaries']
    require(set(binaries) == {'baseline', 'parent', 'candidate'}, 'missing build variant')
    for name, variant in binaries.items():
        require(compare.digest(variant['path']) == variant['build']['sha256'], 'executable drift: ' + name)
    launcher = resources.launcher_identity(state['resource_launcher']['path'])
    require(launcher == state['resource_launcher'], 'native launcher drift')
    require(backfill.driver_identity() == state['driver_identity'], 'frozen audit driver drift')
    tools = reference_tools()
    tools['xff'] = compare.tool_info('xff', binaries['baseline']['path'])
    cpus = []  # Requests are allowances, not pinned CPU IDs on macOS.
    models = {shape: layouts.prepare(shape + '/v2', GRID) for shape in SHAPES}
    definitions = {}
    task_names = None
    for shape, model in models.items():
        definition = dict(model.identity, generator_identity=layouts.generator_identity(model),
                          anchor_hashes={str(count): value for count, value in model.anchor_hashes.items()})
        definitions[shape] = definition
        for count in GRID:
            anchor = Path('/fixture-plan') / model.anchors[count]
            rows = [(Path('/fixture-plan') / entry.name, entry.value) for entry in model.entries
                    if entry.kind == 'file' and entry.name.startswith(model.anchors[count] + '/')]
            require(len(rows) == count, 'planned anchor file count mismatch')
            current_names = []
            for task, expected, pipelines, ordered, scope in scenarios(anchor, rows, tools, 1):
                current_names.append(task)
            task_names = current_names if task_names is None else task_names
            require(current_names == task_names, 'scenario population varies by anchor')
    cells = [(shape, count, workers, task) for shape in SHAPES for count in GRID
             for workers in WORKERS for task in task_names]
    require(len(task_names) == 15 and len(cells) == 1170, 'incomplete full-grid scenario population')
    plan = {'full_grid': GRID, 'workers': WORKERS, 'tasks': task_names,
            'sessions': ORDERS, 'cells_per_session': len(cells), 'layouts': definitions}
    if args.plan_only:
        print(json.dumps(plan), flush=True)
        return
    no_builds_or_profiles()
    output = root / 'collection'
    output.mkdir()  # Refuse to resume or overwrite any prior collection.
    blobs = output / 'stdout'
    blobs.mkdir()
    module_files = [Path(__file__), *[DRIVER / name for name in backfill.driver_identity()]]
    frozen = {str(path): compare.digest(path) for path in module_files}
    for path, digest in state['source_hashes'].items():
        require(compare.digest(path) == digest, 'build support source drift: ' + path)
        frozen[path] = digest
    save(output / 'source-snapshots.json', {path: Path(path).read_text() for path in frozen})
    manifest = {'status': 'collecting repeated full-grid macOS scheduler controls; not cross-platform acceptance',
                'created_at': now(), 'plan': plan, 'build_state_path': str(state_path),
                'build_state_sha256': compare.digest(state_path), 'binaries': binaries,
                'resource_launcher': launcher, 'tools': tools, 'reported_cpu_ids': cpus,
                'cpu_allocation_scope': 'macOS scheduler; worker requests are not affinity',
                'host': state['host'], 'fixture_storage': compare.fixture_storage(fixture_parent),
                'storage_label': args.storage_label, 'slow_storage_verified': False, 'driver_hashes': frozen,
                'kernel': {'release': platform.release(), 'machine': platform.machine()},
                'observer_interpreter': {'path': sys.executable, 'version': sys.version,
                                         'sha256': compare.digest(sys.executable)},
                'sampling': {'warmups': 1, 'samples': 9, 'keep': 7, 'estimator': 'mean-fastest',
                             'rotation': 'rotate participant order by cell/session/repetition',
                             'cache': 'fresh full maximum fixtures per session on chosen storage; reused warm within session; no cache flush'},
                'measurement': {'observer': 'single Python observer reused; fresh native launcher/commands; no affinity',
                                'deadline_seconds': 60, 'output': 'complete stdout retained losslessly by SHA-256',
                                'wall_method': launcher['wall_method'], 'memory_method': launcher['memory_method']},
                'limitations': ['Worker requests do not identify performance/efficiency core placement.',
                                'Slow-storage verification and Instruments/allocation/boundary controls remain separate gates.',
                                'Extra task references prove fixture output, not internal state/demand equivalence.',
                                'Do not pool historical samples from a different observer/launch context.',
                                'Fixture/oracle generation reuses the pinned model, not an independent workload specification.',
                                'Validation, output hashing/compression and metadata capture happen outside measured intervals.'],
                'sessions': [], 'start_context': context()}
    save(output / 'manifest.json', manifest)
    blob_sizes = {}
    for session_index, order in enumerate(ORDERS):
        folder = output / f'session-{session_index + 1}-{order}'
        folder.mkdir()
        ordered_cells = list(cells)
        if order == 'reverse':
            ordered_cells.reverse()
        elif order.startswith('seeded-shuffle'):
            random.Random(20261010).shuffle(ordered_cells)
        session = {'status': 'collecting', 'order': order, 'start_context': context(), 'fixtures': {}, 'cells': []}
        save(folder / 'report.json', session)
        with tempfile.TemporaryDirectory(prefix='xff-scheduler-ab-', dir=fixture_parent) as temporary:
            roots = {}
            for shape, model in models.items():
                print(f'Session {session_index + 1}: materialize full-grid {shape}', flush=True)
                maximum = Path(temporary) / shape
                rows = fixture.materialize(maximum, model.entries)
                identity = fixture.tree_identity(maximum)
                require(identity == fixture.materialized_identity(model.entries), 'materialized maximum fixture differs')
                roots[shape] = (maximum, rows)
                session['fixtures'][shape] = {'root': str(maximum), 'tree_sha256': identity,
                                             'storage': compare.fixture_storage(temporary), 'initial_verified': True}
            empty = Path(temporary) / 'empty'
            empty.write_bytes(b'')
            for cell_index, (shape, count, workers, task) in enumerate(ordered_cells):
                no_builds_or_profiles()
                model = models[shape]
                maximum, all_rows = roots[shape]
                anchor = maximum / model.anchors[count]
                rows = [(path, data) for path, data in all_rows if path.as_posix().startswith(anchor.as_posix() + '/')]
                expected, commands, ordered, scope = next((expected, commands, ordered, scope)
                    for name, expected, commands, ordered, scope in scenarios(anchor, rows, tools, workers) if name == task)
                expected_counter = Counter(expected)
                candidates = Path(temporary) / 'paths'
                input_bytes = b''.join(os.fsencode('./' + path.relative_to(anchor).as_posix()) + b'\0' for path, _ in rows)
                candidates.write_bytes(input_bytes)
                stdin = candidates if task.startswith('fuzzy-list-') else empty
                pipelines = {}
                for label, pipeline in commands.items():
                    if label == 'xff':
                        require(len(pipeline) == 1, 'XFF scenario unexpectedly became a pipeline')
                        for variant in ('baseline', 'parent', 'candidate'):
                            pipelines[variant] = [[binaries[variant]['path'], *pipeline[0][1:]]]
                    else:
                        pipelines[label] = pipeline
                record = {'dataset': shape, 'files': count, 'workers': workers, 'task': task,
                          'cpu_ids': [], 'worker_request': workers, 'affinity_verified': False, 'cwd': str(anchor), 'ordered': ordered, 'scope': scope,
                          'anchor_sha256': model.anchor_hashes[count], 'expected_count': len(expected),
                          'expected_sha256': hashlib.sha256(b'\0'.join(sorted(expected))).hexdigest(),
                          'stdin_sha256': hashlib.sha256(input_bytes if stdin == candidates else b'').hexdigest(),
                          'start_context': context(), 'participants': {label: {'pipeline': pipeline, 'samples': []}
                                                                    for label, pipeline in pipelines.items()},
                          'round_orders': []}
                print(f'Session {session_index + 1} {cell_index + 1}/{len(cells)}: {shape}/{count}/{workers}/{task}', flush=True)
                labels = list(pipelines)
                environment = {'PATH': os.defpath, 'LC_ALL': 'C', 'LANG': 'C', 'TZ': 'UTC',
                               'NO_COLOR': '1', 'GOMAXPROCS': str(workers)}
                record['environment'] = environment
                cell_path = folder / f'cell-{cell_index + 1:04}.json'
                save(cell_path, record)
                for repetition in range(10):
                    offset = (session_index + cell_index + repetition) % len(labels)
                    rotated = labels[offset:] + labels[:offset]
                    record['round_orders'].append(rotated)
                    for label in rotated:
                        sample = measure(pipelines[label], stdin, anchor, cpus[:workers], launcher['path'], environment)
                        payload = validate(sample, expected, expected_counter, pipelines[label], ordered)
                        digest = hashlib.sha256(payload).hexdigest()
                        blob = blobs / (digest + '.gz')
                        if digest not in blob_sizes:
                            with blob.open('xb') as destination:
                                destination.write(gzip.compress(payload, compresslevel=6, mtime=0))
                            blob_sizes[digest] = len(payload)
                        sample.pop('output')
                        sample['output_sha256'] = digest
                        sample['stdout_blob'] = str(blob.relative_to(output))
                        sample['observed_at'] = now()
                        participant = record['participants'][label]
                        if repetition:
                            participant['samples'].append(sample)
                        else:
                            participant['warmup'] = sample
                    record['completed_rounds'] = repetition + 1
                    save(cell_path, record)
                for participant in record['participants'].values():
                    participant['fastest_seven_seconds'] = statistics.fmean(sorted(
                        sample['elapsed_seconds'] for sample in participant['samples'])[:7])
                record['end_context'] = context()
                record['status'] = 'complete correctness-checked cell'
                save(cell_path, record)
                session['cells'].append({'path': cell_path.name, 'sha256': compare.digest(cell_path),
                                         'dataset': shape, 'files': count, 'workers': workers, 'task': task})
                save(folder / 'report.json', session)
            for shape, (maximum, _) in roots.items():
                require(fixture.tree_identity(maximum) == session['fixtures'][shape]['tree_sha256'],
                        'fixture changed during collection')
                session['fixtures'][shape]['final_verified'] = True
        session['status'] = 'complete correctness-checked macOS session; not cross-platform acceptance'
        session['end_context'] = context()
        save(folder / 'report.json', session)
        manifest['sessions'].append({'path': str(folder.relative_to(output) / 'report.json'),
                                     'sha256': compare.digest(folder / 'report.json'), 'cells': len(session['cells'])})
        save(output / 'manifest.json', manifest)
    for name, variant in binaries.items():
        require(compare.digest(variant['path']) == variant['build']['sha256'], 'final executable drift: ' + name)
    require(compare.digest(state_path) == manifest['build_state_sha256'], 'final build-state drift')
    require(resources.launcher_identity(launcher['path']) == launcher, 'final native launcher drift')
    for path, digest in frozen.items():
        require(compare.digest(path) == digest, 'final source drift: ' + path)
    for tool in tools.values():
        require(compare.digest(tool['path']) == tool['sha256'], 'reference tool drift')
    manifest['status'] = 'complete repeated correctness-checked macOS scheduler controls; not cross-platform acceptance'
    manifest['end_context'] = context()
    manifest['stdout_blobs'] = blob_sizes
    save(output / 'manifest.json', manifest)
    print('Completed: ' + str(output), flush=True)


if __name__ == '__main__':
    main()
