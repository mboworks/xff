#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Build and measure historical revisions sequentially in one reproducible host series."""

import argparse
from datetime import datetime, timezone
import hashlib
import html
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

import benchmark_compare as compare
import benchmark_fixture
import benchmark_matrix
import benchmark_shards


def now():
    return datetime.now(timezone.utc).isoformat()


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def git(repo, *arguments):
    return subprocess.check_output(['git', '-C', str(repo), *arguments], text=True).strip()


def revisions(repo, references, history_root=None):
    references = list(references)
    if history_root:
        for path in sorted(history_root.glob('runs/*/*/**/report.json')):
            record = json.loads(path.read_text())
            report = record.get('tool_comparisons')
            if report and (record.get('platform') == 'macos'
                           or report['contract'].get('platform', '').lower().startswith(('macos', 'darwin'))):
                references.append(record['head'])
    if not references:
        raise ValueError('select revisions with --revision, --revisions-file or --history-root')
    selected = {}
    for reference in references:
        sha = git(repo, 'rev-parse', '--verify', '--end-of-options', reference + '^{commit}')
        if not re.fullmatch('[0-9a-f]{40}', sha):
            raise ValueError('expected a full commit SHA')
        date = git(repo, 'show', '-s', '--format=%cI', sha)
        selected[sha] = {'sha': sha, 'date': date, 'subject': git(repo, 'show', '-s', '--format=%s', sha)}
    return sorted(selected.values(), key=lambda entry: (datetime.fromisoformat(entry['date']), entry['sha']))


def environment():
    tools = {name: compare.tool_info(name, shutil.which(name)) for name in ('find', 'rg', 'fzf')}
    if any(tool['status'] != 'available' for tool in tools.values()):
        raise ValueError('backfill requires find, rg and fzf')
    return {
        'host_id': hashlib.sha256(platform.node().encode()).hexdigest(),
        'platform': platform.platform(), 'machine': platform.machine(), 'cpu_count': os.cpu_count(),
        'allowed_cpus': sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
        'python': sys.version, 'tools': tools,
    }


def driver_identity():
    return {Path(module.__file__).name: compare.digest(module.__file__)
            for module in (sys.modules[__name__], compare, benchmark_fixture, benchmark_matrix, benchmark_shards)}


def check_environment(expected):
    difference = benchmark_shards.identity_difference(expected, environment())
    if difference:
        raise ValueError('backfill environment changed: ' + difference)


def prepare(args):
    if not re.fullmatch('[a-z0-9][a-z0-9-]*', args.series):
        raise ValueError('series must contain lowercase letters, digits and hyphens')
    counts = args.files or [*compare.DEFAULT_FILE_COUNTS, 20000, 50000, 100000]
    cpus = args.cpus or [1, 4]
    if len(set(counts)) != len(counts) or min(counts) < 1 or len(set(cpus)) != len(cpus) or min(cpus) < 1:
        raise ValueError('file and CPU counts must be unique and positive')
    if args.depth < 1 or not 1 <= args.keep <= args.repetitions:
        raise ValueError('positive depth and 1 <= keep <= repetitions required')
    compare.cpu_allocation(max(cpus), args.require_cpu_affinity)
    repo = args.repo.resolve(strict=True)
    references = list(args.revision)
    if args.revisions_file:
        references.extend(args.revisions_file.read_text().split())
    selected = revisions(repo, references, args.history_root)
    bazel = shutil.which('bazel')
    if bazel is None:
        raise ValueError('Bazel is required to build historical revisions')
    contract = {
        'schema': 1, 'series': args.series, 'revisions': selected,
        'environment': environment(), 'driver': driver_identity(),
        'files': counts, 'cpus': cpus, 'depth': args.depth,
        'repetitions': args.repetitions, 'retained': args.keep,
        'require_cpu_affinity': args.require_cpu_affinity,
        'storage': compare.fixture_storage(args.fixture_parent, args.require_memory),
        'build': {'config': 'clang_release', 'bazel': str(Path(bazel).resolve()),
                  'version': subprocess.check_output([bazel, '--version'], text=True).strip()},
    }
    args.output.mkdir(parents=True, exist_ok=True)
    path = args.output / 'batch.json'
    if path.exists():
        batch = json.loads(path.read_text())
        difference = benchmark_shards.identity_difference(batch['contract'], contract)
        if difference:
            raise ValueError('cannot resume incompatible batch: ' + difference)
        return batch
    if any(args.output.iterdir()):
        raise ValueError('new batch requires an empty output directory')
    batch = {'created_at': now(), 'contract': contract,
             'identity': hashlib.sha256(json.dumps(contract, sort_keys=True).encode()).hexdigest()}
    write_json(path, batch)
    return batch


def build(repo, output, revision, contract, disk_cache):
    folder = output / 'binaries' / revision['sha']
    folder.mkdir(parents=True, exist_ok=True)
    binary, identity = folder / 'xff', folder / 'build.json'
    if identity.exists():
        saved = json.loads(identity.read_text())
        if saved['revision'] != revision['sha'] or saved['sha256'] != compare.digest(binary):
            raise ValueError('saved benchmark binary changed: ' + revision['sha'])
        return binary, saved
    checkout = output / 'checkout'
    if not checkout.exists():
        subprocess.run(['git', 'clone', '--shared', '--no-checkout', str(repo), str(checkout)], check=True)
    subprocess.run(['git', '-C', str(checkout), 'checkout', '--detach', revision['sha']], check=True)
    command = [contract['build']['bazel'], 'build', '--config=clang_release', '//xff/cli:xff']
    if disk_cache:
        command.append('--disk_cache=' + str(disk_cache.resolve()))
    with (folder / 'build.log').open('w') as log:
        subprocess.run(command, cwd=checkout, stdout=log, stderr=subprocess.STDOUT, check=True)
    # Use this revision's Bazel/toolchain configuration, then copy its executable out before checkout changes.
    shutil.copy2(checkout / 'bazel-bin/xff/cli/xff', binary)
    saved = {'sha256': compare.digest(binary), 'revision': revision['sha'], 'command': command,
             'configuration': {name: compare.digest(checkout / name) for name in
                               ('.bazelrc', '.bazelversion', 'MODULE.bazel', 'MODULE.bazel.lock', 'bazelmod/llvm.MODULE.bazel')
                               if (checkout / name).is_file()}}
    write_json(identity, saved)
    return binary, saved


def render_index(output, batch, status):
    rows = []
    for revision in batch['contract']['revisions']:
        sha = revision['sha']
        state = status.get(sha, {'status': 'pending'})
        label = html.escape(sha[:10] + ' ' + revision['subject'])
        if state['status'] == 'complete':
            label = f'<a href="{html.escape(state["report"])}">{label}</a>'
        rows.append(f'<tr><td>{label}</td><td>{html.escape(state["status"])}</td>'
                    f'<td>{html.escape(state.get("error", ""))}</td></tr>')
    title = 'XFF benchmark backfill: ' + html.escape(batch['contract']['series'])
    (output / 'index.html').write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8"><title>' + title + '</title>'
        '<style>body{font:16px system-ui;margin:2rem}td,th{padding:.4rem;text-align:left}</style>'
        '<h1>' + title + '</h1><p><a href="batch.json">Frozen measurement contract</a> | '
        '<a href="status.json">Batch status</a></p><p>Separate host series. Raw observations are retained; '
        'no cross-host or cross-allocation normalization is applied.</p>'
        '<table><tr><th>Revision</th><th>Status</th><th>Diagnostic</th></tr>' + ''.join(rows) + '</table>')


def run(args, batch):
    contract, output = batch['contract'], args.output
    status_path = output / 'status.json'
    status = json.loads(status_path.read_text()) if status_path.exists() else {}
    binaries = {}
    # Finish compilation before measuring so it does not compete with or alternate between measurements.
    for index, revision in enumerate(contract['revisions'], 1):
        sha = revision['sha']
        check_environment(contract['environment'])
        print(f'Build {index}/{len(contract["revisions"])}: {sha}', flush=True)
        try:
            binaries[sha] = build(args.repo.resolve(), output, revision, contract, args.disk_cache)
        except subprocess.CalledProcessError as error:
            status[sha] = {'status': 'build-failed', 'error': str(error)}
            write_json(status_path, status)
            render_index(output, batch, status)
    for index, revision in enumerate(contract['revisions'], 1):
        sha = revision['sha']
        if sha not in binaries:
            continue
        check_environment(contract['environment'])
        binary, build_record = binaries[sha]
        folder = output / 'reports' / sha
        folder.mkdir(parents=True, exist_ok=True)
        basename = 'benchmark-report-' + ('macos' if sys.platform == 'darwin' else 'linux') + '-' + platform.machine().lower()
        path = folder / (basename + '.json')
        if path.exists():
            record = json.loads(path.read_text())
            if (record['batch'] != batch['identity'] or record['head'] != sha
                    or record['tool_comparisons']['tools']['xff']['sha256'] != build_record['sha256']):
                raise ValueError('saved report does not match batch/binary: ' + sha)
            compare.render_document(record['tool_comparisons'])  # Validate retained observations before reuse.
        else:
            print(f'Measure {index}/{len(contract["revisions"])}: {sha}', flush=True)
            started = now()
            try:
                report = compare.collect_scales(
                    binary, contract['files'], depth=contract['depth'], repetitions=contract['repetitions'],
                    keep=contract['retained'], cpu_counts=contract['cpus'], require_tools=True,
                    fixture_parent=contract['storage']['parent'], require_memory=contract['storage']['memory_required'],
                    require_cpu_affinity=contract['require_cpu_affinity'])
                check_environment(contract['environment'])
                report['contract'].update(runner_class=contract['series'], batch=batch['identity'],
                                          build_identity=json.dumps(build_record['configuration'], sort_keys=True))
                record = {'schema': 1, 'kind': 'backfill', 'head': sha, 'series': contract['series'],
                          'batch': batch['identity'], 'revision': revision, 'build': build_record,
                          'started_at': started, 'completed_at': now(), 'tool_comparisons': report}
                write_json(path, record)
            except (ValueError, subprocess.SubprocessError) as error:
                check_environment(contract['environment'])  # Host/tool drift stops the whole series.
                status[sha] = {'status': 'measurement-failed', 'error': str(error)}
                write_json(status_path, status)
                render_index(output, batch, status)
                continue
        page = path.with_suffix('.html')
        page.write_text(compare.render_document(record['tool_comparisons']))
        status[sha] = {'status': 'complete', 'report': page.relative_to(output).as_posix()}
        write_json(status_path, status)
        render_index(output, batch, status)
    return 0 if all(status.get(item['sha'], {}).get('status') == 'complete' for item in contract['revisions']) else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(os.environ.get('BUILD_WORKSPACE_DIRECTORY', Path.cwd())))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--series', required=True, help='Stable host-series label, e.g. macos-m5-pro or linux-zen5')
    parser.add_argument('--revision', action='append', default=[])
    parser.add_argument('--revisions-file', type=Path, help='Whitespace-separated Git references; resolved and frozen to commits')
    parser.add_argument('--history-root', type=Path, help='Select all retained macOS comparison commits below this benchmark directory')
    parser.add_argument('--files', action='append', type=int)
    parser.add_argument('--cpus', action='append', type=int, help='Default: 1 and 4; requests are checked against capacity')
    parser.add_argument('--depth', type=int, default=40)
    parser.add_argument('--repetitions', type=int, default=9)
    parser.add_argument('--keep', type=int, default=7)
    parser.add_argument('--require-cpu-affinity', action='store_true')
    parser.add_argument('--fixture-parent', type=Path)
    parser.add_argument('--require-memory', action='store_true')
    parser.add_argument('--disk-cache', type=Path)
    parser.add_argument('--run', action='store_true', help='Build and measure; otherwise only write/validate the batch plan')
    args = parser.parse_args()
    args.output = args.output.resolve()
    try:
        batch = prepare(args)
        print(f'{len(batch["contract"]["revisions"])} revisions; allocations {batch["contract"]["cpus"]}; '
              f'batch {batch["identity"]}; output {args.output}', flush=True)
        return run(args, batch) if args.run else 0
    except (ValueError, subprocess.SubprocessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    raise SystemExit(main())
