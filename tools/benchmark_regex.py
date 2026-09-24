#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Compare equivalent ASCII content regexes with validated NUL-delimited results."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import sys
import tempfile

import benchmark_compare as compare

PATTERN = (r'(?:WARN|ERROR) service=[a-z][a-z0-9_-]{2,15} '
           r'request=[0-9a-f]{8} status=(?:4[0-9]{2}|5[0-9]{2}) '
           r'latency=[0-9]{1,5}\.[0-9]{2}ms(?: retry=[1-9])?')
MATCH = b'ERROR service=api_worker request=12abcdef status=503 latency=123.45ms retry=2\n'
NEAR_MISS = b'ERROR service=api_worker request=12abcdef status=503 latency=123.45xs retry=2\n'


def payload(index, size, population):
    """Late matches and near misses require inspecting almost the entire subject."""
    matched = population == 'mixed' and index % 3 == 0
    tail = MATCH if matched else NEAR_MISS
    padding = (NEAR_MISS * (size // len(NEAR_MISS) + 1))[:max(0, size - len(tail) - 1)]
    return padding + b'\n' + tail, matched


def commands(binary, rg, workers, pattern=PATTERN):
    base = [binary, '--no-config', '--no-pager', '--color=never', '--sort=none',
            f'--jobs={workers}', '--exact', '--case=sensitive', '--gitignore=off', '--hidden']
    result = {f'xff-{engine.lower()}': [*base, f'--regextype={engine}', '.', '-type', 'f',
                                       '-rxc', pattern, '-print0'] for engine in ('RE2', 'PCRE2')}
    result['rg-pcre2'] = [rg, '--no-config', '--pcre2', '--case-sensitive', '--text', '--hidden',
                          '--no-ignore', f'--threads={workers}', '--color=never',
                          '--files-with-matches', '--null', '-e', pattern, '.']
    return result


def collect(binary, rg, counts, workers, size, repetitions, keep, parent=None):
    if not counts or not workers or min(counts) < 1 or min(workers) < 1:
        raise ValueError('file and worker counts must be positive')
    if size < len(MATCH) + 1 or not 1 <= keep <= repetitions:
        raise ValueError('invalid content size or retained sample count')
    tools = {'xff': compare.tool_info('xff', binary), 'rg': compare.tool_info('rg', rg)}
    if tools['rg']['status'] != 'available':
        raise ValueError('ripgrep with PCRE2 support is required')
    storage = compare.fixture_storage(parent)
    report = {'schema': 1, 'tools': tools, 'contract': {
        'pattern': PATTERN, 'fixture_version': 1, 'file_counts': counts, 'workers': workers,
        'bytes_per_file': size, 'repetitions': repetitions, 'retained': keep,
        'estimator': 'mean-fastest', 'warmup_rounds': 1,
        'platform': platform.platform(), 'machine': platform.machine(), 'cpu_count': os.cpu_count(), 'storage': storage,
        'cpu_model': 'affinity-limited on Linux; requested worker counts only elsewhere',
        'semantics': 'case-sensitive ASCII byte substring search; no anchors or newline-spanning matches',
        'scope': 'end-to-end process including startup, compile, production VFS reads and path output',
        'cache': 'freshly written then reused; validated discarded warmup; no cache flush',
        'order': 'rotate first participant every round and task',
        'environment': 'inherited environment except isolated HOME and LC_ALL=C; tool configs disabled',
    }, 'tasks': []}
    progress = compare.MeasurementProgress()
    worker = [sys.executable, str(Path(__file__).resolve())]
    with tempfile.TemporaryDirectory(prefix='xff-regex-', dir=storage['parent']) as temporary:
        home = Path(temporary)
        root = home / 'files'
        root.mkdir()
        empty = home / 'empty'
        empty.write_bytes(b'')
        environment = dict(os.environ, HOME=str(home), LC_ALL='C')
        for count in counts:
            for population in ('mixed', 'absent'):
                expected = []
                digest = hashlib.sha256()
                for index in range(count):
                    name = f'item_{index:06}.txt'
                    data, matched = payload(index, size, population)
                    (root / name).write_bytes(data)
                    digest.update(name.encode() + b'\0' + data)
                    if matched:
                        expected.append(('./' + name).encode())
                for jobs in workers:
                    affinity = None
                    if hasattr(os, 'sched_getaffinity'):
                        available = sorted(os.sched_getaffinity(0))
                        if len(available) < jobs:
                            raise ValueError('insufficient available CPUs')
                        affinity = available[:jobs]
                    task = {'files': count, 'workers': jobs, 'population': population,
                            'expected_count': len(expected), 'fixture_sha256': digest.hexdigest(),
                            'cpu_affinity': affinity, 'participants': {}}
                    report['tasks'].append(task)
                    for label, command in commands(tools['xff']['path'], tools['rg']['path'], jobs).items():
                        task['participants'][label] = {'command': command, 'samples': []}
                    labels = list(task['participants'])
                    for repetition in range(repetitions + 1):
                        offset = (len(report['tasks']) + repetition) % len(labels)
                        for label in labels[offset:] + labels[:offset]:
                            progress(f'{count:,} files / {population} / {jobs} workers: '
                                     f'{label}, round {repetition}/{repetitions}')
                            participant = task['participants'][label]
                            pipeline = [participant['command']]
                            sample = compare.invoke({'pipeline': pipeline, 'stdin': str(empty),
                                                     'cwd': str(root), 'environment': environment,
                                                     'cpu_affinity': affinity}, worker)
                            compare.validate_output(sample, expected, pipeline)
                            if repetition:
                                participant['samples'].append(sample)
                    for participant in task['participants'].values():
                        selected = sorted(participant['samples'], key=lambda row: row['elapsed_seconds'])[:keep]
                        participant['elapsed_ms'] = 1000 * statistics.mean(row['elapsed_seconds'] for row in selected)
                    progress(f'Completed {count:,} files / {population} / {jobs} workers', force=True)
                for path in root.iterdir():
                    path.unlink()
    for tool in tools.values():
        if compare.digest(tool['path']) != tool['sha256']:
            raise ValueError('binary changed during measurement')
    return report


def markdown(report):
    lines = ['| Files | Workers | Population | xff RE2 ms | xff PCRE2 ms | rg PCRE2 ms | PCRE2 / RE2 |',
             '| ---: | ---: | :--- | ---: | ---: | ---: | ---: |']
    for task in report['tasks']:
        values = [task['participants'][name]['elapsed_ms'] for name in ('xff-re2', 'xff-pcre2', 'rg-pcre2')]
        lines.append(f"| {task['files']} | {task['workers']} | {task['population']} | " +
                     ' | '.join(f'{value:.2f}' for value in values) + f' | {values[1] / values[0]:.2f} |')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', type=Path)
    parser.add_argument('--binary')
    parser.add_argument('--rg', default=shutil.which('rg'))
    parser.add_argument('--files', default='10,100,1000,10000')
    parser.add_argument('--workers', default='1,4')
    parser.add_argument('--bytes', type=int, default=16384)
    parser.add_argument('--repetitions', type=int, default=9)
    parser.add_argument('--keep', type=int, default=7)
    parser.add_argument('--fixture-parent')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--build-label', required=False, default='unspecified',
                        help='Source revision and effective build configuration')
    args = parser.parse_args()
    if args.worker:
        print(json.dumps(compare.measure(json.loads(args.worker.read_text()))))
        return
    if not args.binary or not args.output:
        parser.error('--binary and --output are required')
    report = collect(args.binary, args.rg, [int(n) for n in args.files.split(',')],
                     [int(n) for n in args.workers.split(',')], args.bytes, args.repetitions,
                     args.keep, args.fixture_parent)
    report['contract']['build_label'] = args.build_label
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    args.output.with_suffix('.md').write_text(markdown(report))


if __name__ == '__main__':
    main()
