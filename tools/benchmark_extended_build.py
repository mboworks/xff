#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Freeze historical full production executables before extended measurements."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def git(checkout, *args):
    return subprocess.check_output(['git', '-C', str(checkout), *args], text=True).strip()


def clean_source(checkout, sha):
    if git(checkout, 'rev-parse', 'HEAD') != sha or git(checkout, 'status', '--porcelain'):
        raise ValueError('historical checkout must exactly match its clean revision')
    if any((checkout / name).exists() for name in ('.bazelrc.user', '.bazelrc.user.end')):
        raise ValueError('workspace Bazel override is forbidden')
    return {'revision': sha, 'tree': git(checkout, 'rev-parse', 'HEAD^{tree}'), 'clean': True}


def freeze_build(checkout, directory, sha, disk_cache, jobs=6, build_library_path=None):
    """Build and immediately freeze one exact checkout without selecting another revision."""
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / 'xff_full'
    if (directory / 'build.json').exists():
        saved = json.loads((directory / 'build.json').read_text())
        if saved['source'] != clean_source(checkout, sha) or saved['sha256'] != digest(executable):
            raise ValueError('retained executable mismatch: ' + sha)
        return saved
    started = time.monotonic()
    source = clean_source(checkout, sha)
    bazel = ['bazel', '--nosystem_rc', '--nohome_rc']
    options = ['--config=clang_release', '--config=xff_full',
               '--disk_cache=' + str(disk_cache.resolve()), '--jobs=' + str(jobs)]
    if build_library_path:
        options.append('--action_env=LD_LIBRARY_PATH=' + str(build_library_path.resolve()))
    command = [*bazel, 'build', '//xff/cli:xff_full', *options]
    with (directory / 'build.log').open('w') as log:
        subprocess.run(command, cwd=checkout, stdout=log, stderr=subprocess.STDOUT, check=True)
        query = [*bazel, 'cquery', 'config(//xff/cli:xff_full, target)', *options,
                 '--output=starlark', '--starlark:expr=target.files_to_run.executable.path']
        artifact = subprocess.check_output(query, cwd=checkout, stderr=log, text=True).strip()
        if not artifact or len(artifact.splitlines()) != 1:
            raise ValueError('configured executable query is ambiguous')
        if clean_source(checkout, sha) != source:
            raise ValueError('source identity changed during build')
        built = checkout / artifact
        binary_hash = digest(built)
        shutil.copy2(built, executable)
        if digest(executable) != binary_hash:
            raise ValueError('frozen executable copy mismatch')
        # Link actions record the actual production LTO command for this revision.
        with (directory / 'link-actions.json').open('w') as actions:
            subprocess.run([*bazel, 'aquery', 'mnemonic("CppLink", //xff/cli:xff_full)',
                            *options, '--output=jsonproto'], cwd=checkout, stdout=actions,
                           stderr=log, check=True)
    saved = {'source': source, 'sha256': binary_hash, 'bytes': executable.stat().st_size,
             'command': command, 'query': query, 'artifact': artifact,
             'elapsed_seconds': time.monotonic() - started,
             'completed_at': datetime.now(timezone.utc).isoformat(),
             'configuration': {name: digest(checkout / name) for name in
                               ('.bazelrc', '.bazelversion', 'MODULE.bazel', 'MODULE.bazel.lock',
                                'bazelmod/llvm.MODULE.bazel') if (checkout / name).is_file()}}
    write(directory / 'build.json', saved)
    return saved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--revisions', type=Path, required=True)
    parser.add_argument('--disk-cache', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--build-library-path', type=Path)
    args = parser.parse_args()
    root, checkout = args.root.resolve(), args.root.resolve() / 'checkout'
    revisions = json.loads(args.revisions.read_text())
    policy = {'revisions': revisions, 'target': '//xff/cli:xff_full',
              'configs': ['clang_release', 'xff_full'], 'driver_sha256': digest(Path(__file__)),
              'startup_flags': ['--nosystem_rc', '--nohome_rc'], 'jobs': args.jobs,
              'disk_cache': str(args.disk_cache.resolve())}
    if args.build_library_path:
        library_path = args.build_library_path.resolve(strict=True)
        policy['build_library_path'] = str(library_path)
        policy['build_libraries'] = {p.name: digest(p) for p in sorted(library_path.iterdir()) if p.is_file()}
    contract_path = root / 'build-contract.json'
    if contract_path.exists() and json.loads(contract_path.read_text()) != policy:
        raise ValueError('cannot resume a changed build contract')
    write(contract_path, policy)
    status_path = root / 'build-status.json'
    status = json.loads(status_path.read_text()) if status_path.exists() else {}
    # Endpoints first permit functional qualification while the remaining builds run.
    order = [revisions[-1], *revisions[:-1]]
    for index, revision in enumerate(order, 1):
        sha = revision['sha']
        directory = root / 'binaries' / sha
        directory.mkdir(parents=True, exist_ok=True)
        identity, executable = directory / 'build.json', directory / 'xff_full'
        if identity.exists():
            saved = json.loads(identity.read_text())
            if saved['source']['revision'] != sha or saved['sha256'] != digest(executable):
                raise ValueError('retained executable mismatch: ' + sha)
            status[sha] = {'status': 'built', 'sha256': saved['sha256']}
            write(status_path, status)
            continue
        started = time.monotonic()
        print(f'BUILD {index}/{len(order)} PR #{revision["pr"]} {sha}', flush=True)
        status[sha] = {'status': 'building', 'started_at': datetime.now(timezone.utc).isoformat()}
        write(status_path, status)
        try:
            subprocess.run(['git', '-C', str(checkout), 'checkout', '--detach', sha], check=True)
            saved = freeze_build(checkout, directory, sha, args.disk_cache, args.jobs, args.build_library_path)
            binary_hash = saved['sha256']
            status[sha] = {'status': 'built', 'sha256': binary_hash, 'seconds': saved['elapsed_seconds']}
            print(f'BUILT {index}/{len(order)} {sha[:10]} {binary_hash} {saved["elapsed_seconds"]:.1f}s', flush=True)
        except (subprocess.CalledProcessError, ValueError, OSError) as error:
            status[sha] = {'status': 'build-failed', 'error': str(error),
                           'seconds': time.monotonic() - started}
            print(f'FAILED {sha}: {error}', flush=True)
            write(status_path, status)
            raise
        write(status_path, status)
    print('BUILD PHASE COMPLETE', json.dumps({state: sum(v['status'] == state for v in status.values())
                                             for state in sorted({v['status'] for v in status.values()})}), flush=True)


if __name__ == '__main__':
    main()
