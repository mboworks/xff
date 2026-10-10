#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Build and freeze the three native scheduler controls before any measurement."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

import benchmark_backfill as backfill
import benchmark_compare as compare
import native_scheduler_collect as collect
import process_resources as resources

VARIANTS = {
    'baseline': '772994ab7905be77915978f06ea90ae5934aaeef',
    'parent': 'af967729eeaf9689367cf19c20ec66eed60ee2ec',
    'candidate': '7e0abb52547a337e78b52ef8ee86cad90063b9a8',
}
TREES = {
    'baseline': '8f797cae4320ba5cb497d580eb4b158f5eaf007b',
    'parent': '9b7a7aa5f35adfd39925360d0c4ce8dd64294152',
    'candidate': 'e832bc196a90e2cc34d91e21826d3fed205e8378',
}
FROZEN_DRIVER = '0e296e40ca6bf523215d80d1dc02deb3503b796c'
CONFIGURATION = ('.bazelrc', '.bazelversion', 'MODULE.bazel', 'bazelmod/llvm.MODULE.bazel')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def native_host():
    require(sys.platform == 'darwin', 'preparation requires native macOS')
    brand = subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip()
    require('M5 Pro' in brand, 'expected native M5 Pro; found ' + brand)
    commands = {'uname': ['uname', '-a'], 'os': ['sw_vers'],
                'topology': ['sysctl', 'hw.model', 'hw.physicalcpu', 'hw.logicalcpu'],
                'performance_efficiency_cores': ['sysctl', 'hw.perflevel0.name', 'hw.perflevel0.physicalcpu',
                                                 'hw.perflevel1.name', 'hw.perflevel1.physicalcpu']}
    observations = {}
    for name, command in commands.items():
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        observations[name] = {'command': command, 'exit_code': result.returncode,
                              'stdout': result.stdout, 'stderr': result.stderr}
    return {'observed_at': datetime.now(timezone.utc).isoformat(), 'cpu_brand': brand,
            'observations': observations, 'affinity_verified': False}


def save(path, value):
    with path.open('x') as handle:
        json.dump(value, handle, indent=2, allow_nan=False)
        handle.write('\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', required=True, type=Path)
    parser.add_argument('--root', required=True, type=Path)
    args = parser.parse_args()
    host = native_host()
    repo = args.repo.resolve(strict=True)
    driver = Path(__file__).resolve().parent.parent
    head = backfill.git(driver, 'rev-parse', 'HEAD')
    source = backfill.checkout_identity(driver, head)
    hashes = backfill.driver_identity()
    for name, digest in hashes.items():
        original = subprocess.check_output(['git', '-C', str(driver), 'show', FROZEN_DRIVER + ':tools/' + name])
        require(hashlib.sha256(original).hexdigest() == digest, 'audit driver differs from frozen Linux driver: ' + name)
    for label, revision in VARIANTS.items():
        require(backfill.git(repo, 'rev-parse', revision + '^{tree}') == TREES[label], 'variant source tree differs')
    tools = {name: shutil.which(name) for name in ('bazel', 'find', 'rg', 'fzf', 'sort')}
    require(all(tools.values()), 'missing bazel or reference tool')
    collect.reference_tools()  # Fail missing GNU-sort capability before expensive builds.
    root = args.root.absolute()
    root.mkdir()  # Fresh root only; never overwrite/resume retained evidence.
    bazel = [tools['bazel'], '--nosystem_rc', '--nohome_rc']
    helper_flags = ['--config=clang', '--compilation_mode=opt']
    command = [*bazel, 'build', *helper_flags, '//tools:process_resources_native']
    with (root / 'helper-build.log').open('x') as log:
        backfill.build_logged(command, driver, log)
        query = [*bazel, 'cquery', *helper_flags, 'config(//tools:process_resources_native, target)',
                 '--output=starlark', '--starlark:expr=target.files_to_run.executable.path']
        artifact = subprocess.check_output(query, cwd=driver, stderr=log, text=True).strip()
    require(artifact and len(artifact.splitlines()) == 1, 'expected exactly one configured resource launcher')
    launcher_path = root / 'resource-launcher'
    shutil.copy2(driver / artifact, launcher_path)
    launcher = resources.launcher_identity(launcher_path)
    contract = {'build': {'bazel': tools['bazel'], 'startup_flags': ['--nosystem_rc', '--nohome_rc']}}
    binaries = {}
    for label, revision in VARIANTS.items():
        print('Prepare historical clang_release control: ' + label + ' ' + revision, flush=True)
        folder = root / label
        folder.mkdir()
        binary, manifest = backfill.build(repo, folder, {'sha': revision}, contract, None)
        require(manifest['source']['tree'] == TREES[label], 'built source tree differs')
        binaries[label] = {'path': str(binary), 'build': manifest,
                           'build_log_sha256': compare.digest(binary.with_name('build.log'))}
    for name in CONFIGURATION:
        require(len({item['build']['configuration'][name] for item in binaries.values()}) == 1,
                'historical compiler/release configuration differs: ' + name)
    source_paths = [Path(__file__).resolve(), driver / 'tools/native_scheduler_collect.py',
                    *[driver / 'tools' / name for name in hashes]]
    state = {'status': 'all native variants frozen; collection not started', 'host': host,
             'driver_source': source, 'frozen_driver_revision': FROZEN_DRIVER, 'driver_identity': hashes,
             'variants': VARIANTS, 'variant_trees': TREES, 'binaries': binaries, 'resource_launcher': launcher,
             'source_hashes': {str(path): compare.digest(path) for path in source_paths},
             'limitations': ['No timings collected during preparation.', 'Worker requests are not affinity.',
                             'New Mac executable hashes do not verify historical Linux measurements.']}
    require(backfill.checkout_identity(driver, head) == source and backfill.driver_identity() == hashes, 'driver drifted')
    require(resources.launcher_identity(launcher_path) == launcher, 'launcher drifted')
    for label, item in binaries.items():
        backfill.verify_binary(Path(item['path']), item['build'], VARIANTS[label])
    save(root / 'build-state.json', state)
    print('Prepared; stop all builds/profilers before collection: ' + str(root), flush=True)


if __name__ == '__main__':
    main()
