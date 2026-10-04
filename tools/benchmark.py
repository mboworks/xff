#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Discover this machine's benchmark history and measure missing main revisions."""

import argparse
from contextlib import contextmanager
import copy
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import subprocess
import sys
import tarfile
import tempfile

import benchmark_backfill as batch
import benchmark_campaign as campaign
import benchmark_records as records


def machine_identity():
    """Hash a stable OS identity; never publish a raw hardware or installation ID."""
    if batch.host_platform() == 'macos':
        data = subprocess.check_output(['/usr/sbin/ioreg', '-rd1', '-c', 'IOPlatformExpertDevice', '-a'])
        identifier = plistlib.loads(data)[0]['IOPlatformUUID']
    else:
        identifier = Path('/etc/machine-id').read_text().strip()
    if not identifier:
        raise ValueError('machine identity is unavailable')
    return hashlib.sha256(('xff-benchmark:' + batch.host_platform() + ':' + identifier).encode()).hexdigest()


@contextmanager
def published_history(args):
    """Read only local observation JSON from the Pages ref, never its site files."""
    if args.history_root:
        yield args.history_root
        return
    probe = subprocess.run(['git', '-C', str(args.repo), 'rev-parse', '--verify', args.history_ref],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    if probe.returncode:
        print('No published history ref available; checking local batches only.', file=sys.stderr)
        yield None
        return
    paths = batch.git(args.repo, 'ls-tree', '-r', '--name-only', args.history_ref, '--', 'benchmarks/local').splitlines()
    paths = [path for path in paths if path.endswith(('/batch.json', '/status.json', '/report.json', '/report.json.gz'))]
    with tempfile.TemporaryDirectory(prefix='xff-benchmark-inventory-') as directory:
        root = Path(directory)
        if paths:
            with tempfile.TemporaryFile() as archive:
                subprocess.run(['git', '-C', str(args.repo), 'archive', args.history_ref, '--', *paths],
                               stdout=archive, check=True)
                archive.seek(0)
                with tarfile.open(fileobj=archive) as contents:
                    contents.extractall(root, filter='data')
        yield root / 'benchmarks'


def available_revisions(repo, main_ref):
    """All first-parent main revisions since comparison benchmarks were introduced."""
    tip = batch.git(repo, 'rev-parse', '--verify', '--end-of-options', main_ref + '^{commit}')
    introductions = batch.git(repo, 'log', '--first-parent', '--reverse', '--diff-filter=A', '--format=%H',
                              tip, '--', 'tools/benchmark_compare.py').splitlines()
    if not introductions:
        raise ValueError('main history has no comparison benchmarks; fetch complete Git history')
    rows = batch.git(repo, 'log', '--first-parent', '--reverse', '--format=%H%x09%cI%x09%s',
                     tip).splitlines()
    revisions = [dict(zip(('sha', 'date', 'subject'), row.split('\t', 2))) for row in rows]
    start = next(index for index, revision in enumerate(revisions) if revision['sha'] == introductions[0])
    return revisions[start:]


def load_batches(root, history_root):
    paths = {*root.glob('*/batch.json'), *root.glob('batches/*/batch.json')}
    if history_root:
        paths.update(history_root.glob('local/*/*/batch.json'))
    result = []
    for path in sorted(paths):
        value = records.read(path)
        if value['identity'] != campaign.identity(value['contract']):
            raise ValueError(f'batch identity changed: {path}')
        if value['contract']['purpose'] == 'local-addition':
            result.append((path.parent, value))
    return result


def select_series(batches, machine_id, series=None):
    """Adopt legacy series only with a matching recorded host, architecture and CPU count."""
    host_id = hashlib.sha256(platform.node().encode()).hexdigest()
    known = set()
    for _, value in batches:
        contract = value['contract']
        environment = contract['environment']
        same = contract.get('machine_id') == machine_id
        if 'machine_id' not in contract:
            same = (environment.get('host_id') == host_id and environment['machine'] == platform.machine()
                    and environment['cpu_count'] == os.cpu_count() and contract['platform'] == batch.host_platform())
        if same:
            known.add(contract['series'])
    if series is None and len(known) > 1:
        raise ValueError('multiple series belong to this machine; select --series: ' + ', '.join(sorted(known)))
    selected = series or next(iter(known), f'{batch.host_platform()}-{platform.machine().lower()}-{machine_id[:12]}')
    if not re.fullmatch('[a-z0-9][a-z0-9-]*', selected) or selected.startswith('github-ci-'):
        raise ValueError('invalid local series name')
    for _, value in batches:
        contract = value['contract']
        if contract['series'] != selected:
            continue
        if contract.get('machine_id', machine_id) != machine_id or selected not in known:
            raise ValueError('series belongs to another or unidentified machine: ' + selected)
    return selected, selected in known


def report_status(directory, value, revision, requested):
    contract = value['contract']
    if (not set(requested.files).issubset(contract['files']) or not set(requested.cpus).issubset(contract['cpus'])
            or (requested.depth, requested.repetitions, requested.keep)
            != (contract['depth'], contract['repetitions'], contract['retained'])):
        return 'incompatible grid/sampling'
    sha = revision['sha']
    arch = contract['environment']['machine'].lower()
    local = directory / 'reports' / sha / f"benchmark-report-{contract['platform']}-{arch}.json"
    published = directory / sha / 'report.json'
    report_path = local if records.exists(local) else published
    if not records.exists(report_path):
        status_path = directory / 'status.json'
        status = records.read(status_path).get(sha, {}) if records.exists(status_path) else {}
        return status.get('status', 'pending') if status.get('status') != 'complete' else 'missing report'
    tools = {'xff': {'path': 'xff'}, 'find': {}, 'rg': {}, 'fzf': {}}
    tasks = {name: sorted(participants) for name, _, participants in batch.compare.scenarios(Path('.'), [], tools)}
    try:
        campaign.validate_measurements(records.read(report_path), value, revision, tasks)
    except (OSError, ValueError, KeyError, TypeError) as error:
        return 'invalid report: ' + str(error)
    return 'complete'


def inventory(revisions, batches, series, requested):
    observations = {}
    for directory, value in batches:
        if value['contract']['series'] != series:
            continue
        for revision in value['contract']['revisions']:
            state = report_status(directory, value, revision, requested)
            observations.setdefault(revision['sha'], []).append((state, directory))
    rows = []
    for revision in revisions:
        found = observations.get(revision['sha'], [])
        state = 'complete' if any(state == 'complete' for state, _ in found) else 'missing'
        detail = ', '.join(sorted({state for state, _ in found})) if found else 'no measurements'
        rows.append({**revision, 'state': state, 'detail': detail})
    return rows


def show_inventory(rows, series, known, args):
    print(f"Machine series: {series} ({'recognized' if known else 'new machine'})")
    print('Workers: ' + ', '.join(map(str, args.cpus)) + f'; fastest {args.keep} of {args.repetitions}')
    print(f"History: {args.main_ref}; {len(rows)} revisions; "
          f"{sum(row['state'] == 'missing' for row in rows)} missing")
    print('Date        Revision    State     PR / subject (missing-data reason)')
    for row in rows:
        suffix = f" ({row['detail']})" if row['state'] == 'missing' else ''
        print(f"{row['date'][:10]}  {row['sha'][:10]}  {row['state']:8}  {row['subject']}{suffix}")


def batch_options(args, series, machine_id, revisions):
    result = copy.copy(args)
    result.series, result.machine_id = series, machine_id
    result.purpose = 'local-addition'
    result.revision = [revision['sha'] for revision in revisions]
    result.revisions_file = result.history_root = result.history_platform = None
    result.output = args.root / 'batches'
    return result


def execution_plan(args, series, machine_id, rows, batches):
    """Resume compatible incomplete batches; freeze a new batch for uncovered revisions."""
    missing = {row['sha'] for row in rows if row['state'] == 'missing'}
    plans = []
    for directory, value in batches:
        revisions = value['contract']['revisions']
        shas = {revision['sha'] for revision in revisions}
        if not missing.intersection(shas) or value['contract']['series'] != series or not (directory / 'checkout').exists():
            continue
        options = batch_options(args, series, machine_id, revisions)
        options.output = directory
        candidate = batch.make_contract(options)
        if candidate != value['contract']:
            continue
        options.expected_contract = candidate
        plans.append(options)
        missing.difference_update(shas)
    if missing:
        options = batch_options(args, series, machine_id, [row for row in rows if row['sha'] in missing])
        contract = batch.make_contract(options)
        options.output /= campaign.identity(contract)
        options.expected_contract = contract
        plans.append(options)
    return plans


@contextmanager
def collection_lock(root, machine_id):
    root.mkdir(parents=True, exist_ok=True)
    with (root / ('.collection-' + machine_id + '.lock')).open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError('another backfill is already running for this machine') from error
        yield


def parser_for_cli():
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        'Checks saved local batches and published local observations for this machine. '
        'CI measurements never satisfy local coverage. Nothing is uploaded automatically. '
        'Use "help backfill" for examples and confirmation rules.'))
    commands = parser.add_subparsers(dest='command')
    help_parser = commands.add_parser('help', help='explain the tool or one subcommand')
    help_parser.add_argument('topic', nargs='?', choices=('list', 'backfill'))
    for command in ('list', 'backfill'):
        sub = commands.add_parser(command, help=('show available revisions and missing data' if command == 'list'
                                                else 'confirm and measure missing revisions'),
                                  description=('List main revisions since comparison benchmarks began. Check all local '
                                               'batches and published local reports for this machine and requested grid.'),
                                  epilog=('Examples: tools/benchmark.py list; tools/benchmark.py backfill; '
                                          'tools/benchmark.py backfill -Y. Backfill shows the plan and asks [y/N] '
                                          'before building or measuring; EOF cancels. Linux pins physical cores; '
                                          'macOS requests workers. Completed reports are validated before reuse.'))
        sub.add_argument('--repo', type=Path, default=Path(os.environ.get('BUILD_WORKSPACE_DIRECTORY', Path.cwd())),
                         help='source checkout (default: current workspace; never switched or cleaned)')
        sub.add_argument('--root', type=Path, default=Path.home() / 'xff-benchmarks', help='local batch storage')
        sub.add_argument('--main-ref', default='origin/main', help='first-parent revision history')
        sub.add_argument('--history-ref', default='origin/coverage-pages', help='published local observations')
        sub.add_argument('--history-root', type=Path, help='use benchmarks directory from a Pages checkout instead')
        sub.add_argument('--no-fetch', action='store_true', help='use current local Git refs without refreshing origin')
        sub.add_argument('--series', help='select an existing machine series or name a new one (default: detect)')
        sub.add_argument('--cpus', type=int, action='append', help='worker/core counts (repeatable; default: 1, 3, 10)')
        sub.add_argument('--files', type=int, action='append', help='file counts (repeatable; default: 10 through 100,000)')
        sub.add_argument('--depth', type=int, default=40, help='deep fixture depth (default: 40)')
        sub.add_argument('--repetitions', type=int, default=9, help='samples per case (default: 9)')
        sub.add_argument('--keep', type=int, default=7, help='fastest samples retained (default: 7)')
        sub.add_argument('--fixture-parent', type=Path, help='fixture storage (Linux default: /dev/shm)')
        sub.add_argument('--disk-cache', type=Path, default=Path.home() / '.cache/bazel-disk', help='Bazel action cache')
        if command == 'backfill':
            sub.add_argument('-Y', '--yes', action='store_true', help='answer the execution confirmation with yes')
    return parser, commands


def main(argv=None):
    parser, commands = parser_for_cli()
    args = parser.parse_args(argv)
    if args.command in (None, 'help'):
        topic = getattr(args, 'topic', None)
        (commands.choices[topic] if topic else parser).print_help()
        return 0
    try:
        args.cpus = args.cpus or [1, 3, 10]
        args.files = args.files or campaign.FILE_COUNTS
        for values in (args.cpus, args.files):
            if min(values) < 1 or len(set(values)) != len(values):
                raise ValueError('CPU and file counts must be unique and positive')
        if args.depth < 1 or not 1 <= args.keep <= args.repetitions:
            raise ValueError('positive depth and 1 <= keep <= repetitions required')
        args.require_memory = args.require_cpu_affinity = batch.host_platform() == 'linux'
        if args.require_memory and args.fixture_parent is None:
            args.fixture_parent = Path('/dev/shm')
        if not args.no_fetch:
            subprocess.run(['git', '-C', str(args.repo), 'fetch', 'origin', 'main', 'coverage-pages'], check=True)
        revisions = available_revisions(args.repo, args.main_ref)
        identity = machine_identity()
        with published_history(args) as history:
            batches = load_batches(args.root, history)
            series, known = select_series(batches, identity, args.series)
            rows = inventory(revisions, batches, series, args)
            show_inventory(rows, series, known, args)
            if args.command == 'list' or all(row['state'] == 'complete' for row in rows):
                return 0
            plans = execution_plan(args, series, identity, rows, batches)
        for plan in plans:
            print(f'Batch: {plan.output} ({len(plan.revision)} revisions)')
        if not args.yes:
            try:
                answer = input('Build and measure the missing revisions? [y/N] ')
            except EOFError:
                answer = ''
            if answer.strip().lower() not in ('y', 'yes'):
                print('Cancelled; no builds or measurements started.')
                return 0
        result = 0
        with collection_lock(args.root, identity):
            for plan in plans:
                if batch.make_contract(plan) != plan.expected_contract:
                    raise ValueError('measurement environment changed after confirmation; run backfill again')
                result = max(result, batch.run(plan, batch.prepare(plan)))
        print('Local reports saved. Upload/publication is a separate step; see docs/benchmark-backfill.md.')
        return result
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    raise SystemExit(main())
