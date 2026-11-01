#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Discover this machine's benchmark history and measure missing main revisions."""

import argparse
from contextlib import contextmanager
import copy
from datetime import datetime
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
import benchmark_datasets as datasets
import benchmark_discovery as discovery
import benchmark_local as local
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
    if getattr(args, 'dataset_catalog', None) is not None and not args.no_fetch:
        with tempfile.TemporaryDirectory(prefix='xff-benchmark-observations-') as directory:
            root = Path(directory)
            discovery.download_observations(args, root)
            yield root
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
    architecture = re.sub('[^a-z0-9]+', '-', platform.machine().lower()).strip('-')
    selected = series or next(iter(known), f'{batch.host_platform()}-{architecture}-{machine_id[:12]}')
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
            != (contract['depth'], contract['repetitions'], contract['retained'])
            or list(getattr(requested, 'layouts', None) or []) != contract.get('layouts', [])):
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
            if state == 'incompatible grid/sampling':
                continue
            observations.setdefault(revision['sha'], []).append((state, directory))
    rows = []
    for revision in revisions:
        found = observations.get(revision['sha'], [])
        state = 'complete' if any(state == 'complete' for state, _ in found) else 'missing'
        detail = ', '.join(sorted({state for state, _ in found})) if found else 'no measurements'
        rows.append({**revision, 'state': state, 'detail': detail})
    return rows


def selected_recipe_options(args):
    """Return display options matching the selected dataset, independent of target overrides."""
    selection = getattr(args, 'dataset_selection', None)
    if selection is None or not selection['same_machine']:
        return args
    result = copy.copy(args)
    recipe = selection['dataset']['recipe']
    for key in ('files', 'cpus', 'depth', 'repetitions', 'keep'):
        setattr(result, key, recipe[key])
    result.layouts = ([f"{layout['name']}/v{layout['revision']}" for layout in recipe.get('layouts', [])]
                      if recipe.get('fixture_version') == 3 else [])
    return result


def recipe_identity(args):
    return (tuple(args.files), tuple(args.cpus), args.depth, args.repetitions, args.keep, tuple(args.layouts))


def display_subject(subject):
    match = re.fullmatch(r'(.*) \(#([1-9][0-9]*)\)', subject)
    return f'PR {match.group(2)}: {match.group(1)}' if match else subject


def display_detail(row):
    if row['state'] == 'complete':
        return 'ok'
    detail = row.get('detail')
    return 'n/a' if not detail or detail in ('no measurements', 'pending') else detail


def count_values(values):
    counts = {}
    for value in values:
        counts[value] = counts.get(value, 0) + 1
    return ', '.join(f'{value}:{count}' for value, count in sorted(counts.items()))


def pr_number(subject):
    match = re.fullmatch(r'.* \(#([1-9][0-9]*)\)', subject)
    return int(match.group(1)) if match else None


def grouped_inventory(rows, tail=10, max_groups=10):
    older, recent = rows[:-tail], rows[-tail:]
    if not older:
        return [], recent
    dated = [(datetime.fromisoformat(row['date']), row) for row in older]
    groupings = (
        lambda value: value.date(),
        lambda value: value.isocalendar()[:2],
        lambda value: (value.year, value.month),
        lambda value: value.year,
    )
    groups = []
    for key in groupings:
        groups = []
        for timestamp, row in dated:
            bucket = (key(timestamp), row['state'], display_detail(row))
            if not groups or groups[-1][0] != bucket:
                groups.append((bucket, []))
            groups[-1][1].append(row)
        if len(groups) <= max_groups:
            break
    return [items for _, items in groups], recent


def show_aggregate(rows):
    first, last = rows[0], rows[-1]
    dates = (first['date'][:10] if first['date'][:10] == last['date'][:10]
             else first['date'][:10] + ' - ' + last['date'][:10])
    statuses = count_values(row['state'] for row in rows)
    details = count_values(display_detail(row) for row in rows)
    prs = [number for row in rows if (number := pr_number(row['subject'])) is not None]
    pr_range = ('n/a' if not prs else f'PR {min(prs)}' if min(prs) == max(prs)
                else f'PR {min(prs)} - {max(prs)}')
    print(f'{dates:23}  {len(rows):3} commits  {statuses:20}  {details:24}  {pr_range} ({len(prs)} PRs)')


def show_inventory_row(row):
    print(f"{row['date'][:10]}  {row['date'][11:16]}  {row['sha'][:10]}  {row['state']:8}  "
          f"{display_detail(row):18}  {display_subject(row['subject'])}")


def show_inventory(rows, series, known, args):
    print()
    selection = getattr(args, 'dataset_selection', None)
    if selection:
        dataset = selection['dataset']
        print(f"Measurement inventory for dataset {args.dataset_number}: {dataset['id'][:12]}  {dataset['series']}")
    else:
        print(f'Measurement inventory for local series {series}; published dataset entry: none')
    print(f"Machine series: {series} ({'recognized' if known else 'new machine'})")
    print('Workers: ' + ', '.join(map(str, args.cpus)) + f'; fastest {args.keep} of {args.repetitions}')
    print(f"History: {args.main_ref}; {len(rows)} revisions; "
          f"{sum(row['state'] == 'missing' for row in rows)} missing")
    if not args.full:
        print(f'Display: compact; use --full to show all {len(rows)} revisions.')
    print()
    if not args.full:
        groups, recent = grouped_inventory(rows)
        if groups:
            print('Earlier revisions (Date range; commits; status counts; detail counts; PR range)')
            for group in groups:
                show_aggregate(group)
            print()
            print(f'Latest {len(recent)} revisions')
        rows = recent
    print('Date        Time   Revision    Status    Detail              PR / subject')
    for row in rows:
        show_inventory_row(row)


def batch_options(args, series, machine_id, revisions):
    result = copy.copy(args)
    result.series, result.machine_id = series, machine_id
    result.purpose = 'local-addition'
    if getattr(args, 'dataset_selection', None):
        result.source_dataset = args.dataset_selection['dataset']['id']
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


def upload_batches(root, selections):
    available = sorted(root.glob('batches/*/batch.json'))
    result = []
    for selection in selections:
        path = Path(selection).expanduser()
        if path.is_dir():
            path = path / 'batch.json'
        matches = [candidate for candidate in available if candidate.parent.name.startswith(selection)]
        if path.is_file():
            matches = [path]
        if len(matches) != 1:
            raise ValueError('batch must be a path or unique identity prefix: ' + selection)
        directory = matches[0].parent.resolve()
        if directory not in result:
            result.append(directory)
    return result


def local_dataset_identity(directory, value, status):
    """Match batches using the same recipe identity the published dataset catalog uses."""
    complete = [sha for sha, state in status.items() if state.get('status') == 'complete']
    if not complete:
        return None
    sha = complete[0]
    report_paths = sorted((directory / 'reports' / sha).glob('benchmark-report-*.json'))
    report_paths.extend(path for path in (directory / sha / 'report.json', directory / sha / 'report.json.gz')
                        if path not in report_paths)
    report_path = next((path for path in report_paths if records.exists(path)), None)
    if report_path is None:
        return None
    record = records.read(report_path)
    report = record['tool_comparisons']
    contract = value['contract']
    descriptor = dict(series=record['series'], kind='local', platform=record['platform'],
                      architecture=report['contract'].get('machine'),
                      recipe=datasets.recipe(report, contract))
    return datasets.identity(descriptor)


def upload_candidates(root, pages_root):
    published_revisions = {}
    published_planned = {}
    published_by_identity = {}
    published_batches = {}
    for path in pages_root.glob('local/*/*/batch.json'):
        try:
            value = records.read(path)
            if value['contract']['purpose'] != 'local-addition':
                continue
            status = records.read(path.parent / 'status.json')
            revisions = {sha for sha, state in status.items() if state.get('status') == 'complete'}
            published_by_identity[value['identity']] = revisions
            dataset_id = local_dataset_identity(path.parent, value, status)
            if dataset_id:
                published_revisions.setdefault(dataset_id, set()).update(revisions)
                published_planned.setdefault(dataset_id, set()).update(
                    revision['sha'] for revision in value['contract']['revisions'])
                published_batches.setdefault(dataset_id, []).append(
                    dict(identity=value['identity'], complete=len(revisions),
                         total=len(value['contract']['revisions'])))
        except (OSError, ValueError, KeyError, TypeError):
            continue
    candidates = []
    for path in sorted(root.glob('batches/*/batch.json')):
        try:
            value = records.read(path)
            contract = value['contract']
            status = records.read(path.parent / 'status.json')
            if (value['identity'] != campaign.identity(contract)
                    or contract['purpose'] != 'local-addition'
                    or not any(state.get('status') == 'complete' for state in status.values())):
                continue
            destination = pages_root / 'local' / contract['series'] / value['identity']
            complete = sum(state.get('status') == 'complete' for state in status.values())
            completed_revisions = tuple(sha for sha, state in status.items() if state.get('status') == 'complete')
            dataset_id = local_dataset_identity(path.parent, value, status)
            published_for_dataset = (published_revisions.get(dataset_id, set()) |
                                     published_by_identity.get(value['identity'], set()))
            present_revisions = tuple(sorted(set(completed_revisions) & published_for_dataset))
            candidates.append(dict(directory=path.parent.resolve(), identity=value['identity'],
                                   series=contract['series'], complete=complete,
                                   total=len(contract['revisions']), uploaded=destination.exists(),
                                   created=value.get('created_at', ''),
                                   planned_revisions=tuple(revision['sha'] for revision in contract['revisions']),
                                   completed_revisions=completed_revisions,
                                   dataset_id=dataset_id,
                                   present_revisions=present_revisions,
                                   published_dataset_revisions=len(published_for_dataset),
                                   published_planned_revisions=tuple(sorted(published_planned.get(dataset_id, set()))),
                                   published_completed_revisions=tuple(sorted(published_for_dataset)),
                                   published_batches=published_batches.get(dataset_id, [])))
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return sorted(candidates, key=lambda candidate: (candidate['created'], candidate['identity']), reverse=True)


def choose_upload(candidates):
    if not candidates:
        raise ValueError('no local datasets have completed observations to upload')
    ready = [candidate for candidate in candidates if not candidate['uploaded']]
    displayed = candidates[:10]
    grouped = {}
    for candidate in candidates:
        key = (candidate['series'], candidate.get('dataset_id'))
        grouped.setdefault(key, []).append(candidate)
    for (series, dataset_id), group in grouped.items():
        label = f'[{dataset_id[:12]}]' if dataset_id else '(identity unavailable)'
        print()
        print(f'Dataset: {series} {label}')
        published = {item['identity']: item for candidate in group
                     for item in candidate.get('published_batches', [])}
        published_count = max(candidate.get('published_dataset_revisions', 0) for candidate in group)
        rows = [f"{item['identity'][:12]} {item['complete']}/{item['total']} revisions"
                for item in list(published.values())[:10]]
        suffix = f'; +{len(published) - 10} older uploaded batches' if len(published) > 10 else ''
        if published:
            print(f"  Published: {published_count} revisions across {len(published)} uploaded batches "
                  f"[{', '.join(rows)}{suffix}].")
        elif dataset_id is None:
            print('  Published: dataset matching unavailable.')
        else:
            print('  Published: no matching dataset batches found.')
        local_revisions = {sha for candidate in group for sha in candidate.get('completed_revisions', ())}
        present_revisions = {sha for candidate in group for sha in candidate.get('present_revisions', ())}
        planned = {sha for candidate in group
                   for sha in candidate.get('planned_revisions', candidate.get('completed_revisions', ()))}
        planned.update(sha for candidate in group for sha in candidate.get('published_planned_revisions', ()))
        completed = local_revisions | {sha for candidate in group
                                       for sha in candidate.get('published_completed_revisions', ())}
        print(f'  Dataset completion: {len(planned & completed)}/{len(planned)} planned revisions measured.')
        uploaded = sum(candidate['uploaded'] for candidate in group)
        print(f"  Local batch inventory: {len(group)} batches, {len(local_revisions)} unique completed revisions; "
              f'{uploaded} of these exact batch IDs are on Pages; {len(present_revisions)} revisions overlap.')
        print('  Eligible local measurement batches:')
        for number, candidate in enumerate(displayed, 1):
            if (candidate['series'], candidate.get('dataset_id')) != (series, dataset_id):
                continue
            state = 'uploaded' if candidate['uploaded'] else 'ready'
            present = len(candidate.get('present_revisions', ()))
            print(f"    {number}. {candidate['identity'][:12]}  {candidate['series']}  {state}; "
                  f"{candidate['complete']}/{candidate['total']} revisions complete; {present} already present")
        older = [candidate for candidate in candidates[10:]
                 if (candidate['series'], candidate.get('dataset_id')) == (series, dataset_id)]
        if older:
            older_revisions = {sha for candidate in older for sha in candidate.get('completed_revisions', ())}
            older_uploaded = sum(candidate['uploaded'] for candidate in older)
            print(f'    {len(older)} older batches aggregated: {len(older) - older_uploaded} ready, '
                  f'{older_uploaded} uploaded; {len(older_revisions)} unique completed revisions.')
    try:
        answer = input(f'Select listed batch [1-{len(displayed)}], a for all {len(ready)} ready, '
                       'q to cancel: ').strip().lower()
    except EOFError:
        answer = ''
    if answer in ('', 'q', 'quit', 'cancel'):
        print('Cancelled; nothing was pushed or published.')
        return None
    if answer in ('a', 'all'):
        selected = [candidate['directory'] for candidate in ready]
        if not selected:
            raise ValueError('no ready local measurement batches remain to upload')
        print(f'Selected all {len(selected)} ready batches; already uploaded batches are excluded.')
        return selected
    try:
        selected = int(answer)
    except ValueError as error:
        raise ValueError('batch selection must be a listed number, a, or q') from error
    if not 1 <= selected <= len(displayed):
        raise ValueError('batch selection is outside the listed range')
    return [displayed[selected - 1]['directory']]


def upload(args):
    if args.yes and not args.batch:
        raise ValueError('--yes requires an explicit --batch')
    subprocess.run(['git', '-C', str(args.repo), 'fetch', args.remote, args.pages_ref], check=True)
    with tempfile.TemporaryDirectory(prefix='xff-benchmark-upload-') as temporary:
        checkout = Path(temporary) / 'pages'
        subprocess.run(['git', '-C', str(args.repo), 'worktree', 'add', '--detach', str(checkout),
                        f'{args.remote}/{args.pages_ref}'], check=True)
        try:
            interactive = not args.batch
            if interactive:
                directories = choose_upload(upload_candidates(args.root, checkout / 'benchmarks'))
                if directories is None:
                    return 0
            else:
                directories = upload_batches(args.root, args.batch)
            destinations = [local.retain(checkout / 'benchmarks', directory) for directory in directories]
            changed = subprocess.run(['git', '-C', str(checkout), 'status', '--porcelain', '--', 'benchmarks/local'],
                                     text=True, capture_output=True, check=True).stdout.strip()
            if not changed:
                print('Selected benchmark observations are already uploaded.')
            else:
                print('Upload completed observations from:')
                for directory, destination in zip(directories, destinations):
                    print(f'  {directory} -> {destination.relative_to(checkout)}')
                if not interactive and not args.yes:
                    try:
                        answer = input(f'Commit and push to {args.remote}/{args.pages_ref}? [y/N] ')
                    except EOFError:
                        answer = ''
                    if answer.strip().lower() not in ('y', 'yes'):
                        print('Cancelled; nothing was pushed.')
                        return 0
                subprocess.run(['git', '-C', str(checkout), 'add', 'benchmarks/local'], check=True)
                subprocess.run(['git', '-C', str(checkout), 'commit', '-m',
                                'benchmarks: retain completed local observations'], check=True)
                subprocess.run(['git', '-C', str(checkout), 'push', args.remote, f'HEAD:{args.pages_ref}'], check=True)
        finally:
            subprocess.run(['git', '-C', str(args.repo), 'worktree', 'remove', '--force', str(checkout)], check=False)
    if not args.no_publish:
        subprocess.run(['gh', 'workflow', 'run', 'benchmark_pages.yml', '--repo', args.github_repository,
                        '--ref', args.publish_ref], check=True)
    print('Benchmark observations uploaded' + ('; publication not requested.' if args.no_publish
                                                else '; benchmark publication started.'))
    return 0


def parser_for_cli():
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        'Checks saved local batches and published local observations for this machine. '
        'CI measurements never satisfy local coverage. Upload is an explicit, confirmed operation. '
        'Use "help backfill" or "help upload" for examples and confirmation rules.'))
    commands = parser.add_subparsers(dest='command')
    help_parser = commands.add_parser('help', help='explain the tool or one subcommand')
    help_parser.add_argument('topic', nargs='?', choices=('list', 'backfill', 'upload'))
    for command in ('list', 'backfill'):
        sub = commands.add_parser(command, help=('show available revisions and missing data' if command == 'list'
                                                else 'confirm and measure missing revisions'),
                                  description=('List main revisions since comparison benchmarks began. Check all local '
                                               'batches and published local reports for this machine and requested grid.'),
                                  epilog=('Examples: bazel run //tools:benchmark -- list; bazel run //tools:benchmark -- backfill; '
                                          'bazel run //tools:benchmark -- backfill -Y. Backfill shows the plan and asks [y/N] '
                                          'before building or measuring; EOF cancels. Linux pins physical cores; '
                                          'macOS requests workers. Completed reports are validated before reuse.'))
        sub.add_argument('--repo', type=Path, default=Path(os.environ.get('BUILD_WORKSPACE_DIRECTORY', Path.cwd())),
                         help='source checkout (default: current workspace; never switched or cleaned)')
        sub.add_argument('--root', type=Path, default=Path.home() / 'xff-benchmarks', help='local batch storage')
        sub.add_argument('--main-ref', default='origin/main', help='first-parent revision history')
        sub.add_argument('--history-ref', default='origin/coverage-pages', help='published local observations')
        sub.add_argument('--history-root', type=Path, help='use benchmarks directory from a Pages checkout instead')
        sub.add_argument('--no-fetch', action='store_true', help='use current local Git refs without refreshing origin')
        sub.add_argument('--catalog-url', default=discovery.CATALOG_URL, help='published dataset metadata URL')
        sub.add_argument('--dataset', help='published dataset ID or unique prefix (default: discover compatible recipes)')
        sub.add_argument('--series', help='select an existing machine series or name a new one (default: detect)')
        sub.add_argument('--cpus', type=int, action='append', help='worker/core counts (repeatable; default: selected dataset, otherwise 1, 3, 10)')
        sub.add_argument('--files', type=int, action='append', help='file counts (repeatable; default: selected dataset, otherwise 10 through 100,000)')
        sub.add_argument('--depth', type=int, help='deep fixture depth (default: selected dataset, otherwise 40)')
        sub.add_argument('--layout-revision', choices=('legacy', 'v2'),
                         help='generated fixture layouts (default: selected dataset, otherwise v2)')
        sub.add_argument('--repetitions', type=int, help='samples per case (default: selected dataset, otherwise 9)')
        sub.add_argument('--keep', type=int, help='fastest samples retained (default: selected dataset, otherwise 7)')
        sub.add_argument('--fixture-parent', type=Path, help='fixture storage (Linux default: /dev/shm)')
        sub.add_argument('--disk-cache', type=Path, default=Path.home() / '.cache/bazel-disk', help='Bazel action cache')
        sub.add_argument('--build-library-path', type=Path,
                         help='runtime libraries for historical build tools; passed to Bazel build actions')
        sub.add_argument('--resource-launcher', type=Path,
                         help='explicit prebuilt native accounting binary for direct script use')
        sub.add_argument('--full', action='store_true', help='show every revision instead of compact history ranges')
        if command == 'backfill':
            sub.add_argument('-Y', '--yes', action='store_true', help='answer the execution confirmation with yes')
    upload_parser = commands.add_parser('upload', help='validate and upload completed local observations',
                                        description=('Import completed observations into a temporary coverage-pages '
                                                     'worktree, commit and push them, then start site publication. '
                                                     'The interactive picker accepts one batch or all ready batches.'))
    upload_parser.add_argument('--repo', type=Path,
                               default=Path(os.environ.get('BUILD_WORKSPACE_DIRECTORY', Path.cwd())),
                               help='source checkout whose remote receives the upload')
    upload_parser.add_argument('--root', type=Path, default=Path.home() / 'xff-benchmarks',
                               help='local batch storage')
    upload_parser.add_argument('--batch', action='append',
                               help='select without the interactive batch picker; directory/path or unique identity prefix; repeatable')
    upload_parser.add_argument('--remote', default='origin', help='Git remote containing the Pages branch')
    upload_parser.add_argument('--pages-ref', default='coverage-pages', help='branch retaining benchmark data')
    upload_parser.add_argument('--github-repository', default=os.environ.get('GITHUB_REPOSITORY', 'mboworks/xff'),
                               help='GitHub repository used to dispatch publication')
    upload_parser.add_argument('--publish-ref', default='main', help='trusted ref used to publish the benchmark site')
    upload_parser.add_argument('--no-publish', action='store_true', help='push observations without starting publication')
    upload_parser.add_argument('-Y', '--yes', action='store_true', help='answer the push confirmation with yes')
    return parser, commands


def main(argv=None):
    parser, commands = parser_for_cli()
    args = parser.parse_args(argv)
    if args.command in (None, 'help'):
        topic = getattr(args, 'topic', None)
        (commands.choices[topic] if topic else parser).print_help()
        return 0
    try:
        if args.command == 'upload':
            return upload(args)
        if not args.no_fetch:
            subprocess.run(['git', '-C', str(args.repo), 'fetch', 'origin', 'main'], check=True)
        revisions = available_revisions(args.repo, args.main_ref)
        identity = machine_identity()
        args.dataset_catalog = discovery.catalog(args)
        args.dataset_selection = None
        args.layouts = ({'legacy': [], 'v2': ['broad/v2', 'deep/v2']}[args.layout_revision]
                        if args.layout_revision else None)
        local_batches = load_batches(args.root, None)
        if args.dataset_catalog is not None:
            options = discovery.choices(args.dataset_catalog, identity)
            discovery.show_choices(options, revisions)
            args.dataset_selection = discovery.choose(options, args)
            args.dataset_number = (options.index(args.dataset_selection) + 1 if args.dataset_selection else None)
            print()
            if args.command == 'list' and args.dataset_selection is None and any(
                    not option['reasons'] and (not args.series or option['dataset']['series'] == args.series)
                    for option in options):
                print('Select --dataset=ID to inspect a specific revision inventory.')
                return 0
        elif args.dataset:
            raise ValueError('dataset metadata is unavailable')
        if args.series is None and args.dataset_selection is None:
            detected_series, known_machine = select_series(local_batches, identity)
            if known_machine:
                args.series = detected_series
        discovery.apply_recipe(args, args.dataset_selection, local_batches)
        if args.layouts is None:
            args.layouts = ['broad/v2', 'deep/v2']
        args.cpus = args.cpus or [1, 3, 10]
        args.files = args.files or campaign.FILE_COUNTS
        args.depth = 40 if args.depth is None else args.depth
        args.repetitions = 9 if args.repetitions is None else args.repetitions
        args.keep = 7 if args.keep is None else args.keep
        for values in (args.cpus, args.files):
            if min(values) < 1 or len(set(values)) != len(values):
                raise ValueError('CPU and file counts must be unique and positive')
        if args.depth < 1 or not 1 <= args.keep <= args.repetitions:
            raise ValueError('positive depth and 1 <= keep <= repetitions required')
        args.require_memory = args.require_cpu_affinity = batch.host_platform() == 'linux'
        if args.require_memory and args.fixture_parent is None:
            args.fixture_parent = Path('/dev/shm')
        if not args.no_fetch and args.dataset_catalog is None and not args.history_root:
            subprocess.run(['git', '-C', str(args.repo), 'fetch', 'origin', 'coverage-pages'], check=True)
        with published_history(args) as history:
            batches = load_batches(args.root, history)
            series, known = select_series(batches, identity, args.series)
            history_args = selected_recipe_options(args)
            display_args = args
            if recipe_identity(history_args) == recipe_identity(args):
                rows = inventory(revisions, batches, series, args)
            else:
                rows = inventory(revisions, batches, series, args)
                display_args = copy.copy(args)
                display_args.dataset_selection = None
            show_inventory(rows, series, known, display_args)
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
