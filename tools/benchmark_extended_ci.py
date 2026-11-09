# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Freeze paired production builds and collect the separate extended CI contract."""

import argparse
from dataclasses import asdict
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

import benchmark_extended as suite
import benchmark_extended_build as build
import benchmark_extended_history as history
import benchmark_extended_records as records
import benchmark_extended_storage as storage
import process_resources


def revision(repo):
    sha = build.git(repo, 'rev-parse', 'HEAD')
    subject = build.git(repo, 'show', '-s', '--format=%s', sha)
    numbers = re.findall(r'\(#([0-9]+)\)', subject)
    return {'sha': sha, 'date': build.git(repo, 'show', '-s', '--format=%cI', sha),
            'subject': subject, 'pr': int(numbers[-1]) if numbers else 0}


def build_endpoint(repo, root, role, disk_cache, jobs):
    selected = revision(repo)
    build.freeze_build(repo, root / 'binaries' / selected['sha'], selected['sha'], disk_cache, jobs)
    history.freeze(root / (role + '.json'), dict(revision=selected, parent=build.git(repo, 'rev-parse', 'HEAD^1')))
    if all((root / (side + '.json')).exists() for side in ('head', 'base')):
        head, base = (records.read(root / (side + '.json')) for side in ('head', 'base'))
        records.require(head['parent'] == base['revision']['sha'], 'baseline must be the first parent')
        history.freeze(root / 'revisions.json', [head['revision'], base['revision']])
        history.freeze(root / 'build-contract.json', {'target': '//xff/cli:xff_full',
                       'configs': ['clang_release', 'xff_full'], 'revisions': [head['revision'], base['revision']],
                       'drivers': {Path(module).name: build.digest(Path(module)) for module in (__file__, build.__file__)}})


def collect(build_root, root, launcher_path, collection=None, series=None, existing_fixture=None):
    platform_name = {'linux': 'linux', 'darwin': 'macos'}.get(sys.platform)
    records.require(platform_name is not None, 'requires Linux or macOS')
    revisions = records.read(build_root / 'revisions.json')
    records.require(len(revisions) == 2, 'requires exactly head and first-parent builds')
    builds = {}
    for selected in revisions:
        directory = build_root / 'binaries' / selected['sha']
        manifest = records.read(directory / 'build.json')
        selected.update(binary=str(directory / 'xff_full'), binary_sha256=manifest['sha256'],
                        build_sha256=build.digest(directory / 'build.json'),
                        link_actions_sha256=build.digest(directory / 'link-actions.json'))
        builds[selected['sha']] = records.build_proof(directory, selected)
    head, anchor = revisions
    origin = 'ci' if collection is not None else 'local'
    records.require(collection is None or existing_fixture is None, 'CI must create its own isolated fixture')
    if collection is not None:
        records.require(collection['head_sha'] == head['sha'], 'workflow head/build mismatch')
        collection = dict(collection, base_sha=anchor['sha'])
    series = series or ('github-ci-' + platform_name)
    cases = suite.cases()
    affinity = history.cpu_allocation(max(case.cpus for case in cases))
    tools = {}
    for name in ('find', 'rg', 'tar'):
        executable = shutil.which({'find': 'gfind', 'tar': 'gtar'}.get(name, name) if sys.platform == 'darwin' else name)
        records.require(executable is not None, 'missing reference tool: ' + name)
        path = Path(executable).resolve(strict=True)
        tools[name] = {'path': str(path), 'sha256': build.digest(path),
                       'version': subprocess.check_output([str(path), '--version'], text=True).strip()}
    launcher = process_resources.launcher_identity(launcher_path)
    output = root / 'measurements'
    output.mkdir(parents=True, exist_ok=True)
    controls, control_error = [], None
    with storage.fixture(root, existing_fixture) as (fixture, filesystem):
        contract = {'recipe': suite.RECIPE, 'cases': [asdict(case) for case in cases], 'revisions': revisions,
                    'repetitions': 9, 'keep': 7, 'estimator': 'mean-fastest', 'warmup_rounds': 1,
                    'anchor': anchor, 'fixture': str(fixture),
                    'filesystem': 'tmpfs' if sys.platform == 'linux' else filesystem,
                    'tools': tools, 'launcher': launcher, 'affinity': affinity, 'platform': platform.platform(),
                    'comparison': 'parent', 'python': sys.version,
                    'drivers': {Path(module).name: build.digest(Path(module)) for module in
                                (__file__, suite.__file__, history.__file__, storage.__file__, process_resources.__file__)},
                    'build_contract_sha256': build.digest(build_root / 'build-contract.json'),
                    'order': 'head cells then identical-parent controls; participant start rotates every round',
                    'output_retention': 'all outputs checked; successful hash/count retained; failed output gzip retained'}
        history.freeze(output / 'contract.json', contract)
        if existing_fixture is None:
            suite.prepare(fixture, cases)
        manifest = suite.verify(fixture, cases)
        history.freeze(output / 'fixtures.json', manifest)
        rows = []
        for index, case in enumerate(cases):
            print(f'HEAD {index + 1}/{len(cases)} {case.key}', flush=True)
            rows.append(history.collect_cell(case, head, anchor, output, fixture, {k: v['path'] for k, v in tools.items()},
                                             launcher['path'], affinity, index, 9))
        # Head results remain usable if an independent diagnostic control fails.
        try:
            for index, case in enumerate(cases):
                print(f'CONTROL {index + 1}/{len(cases)} {case.key}', flush=True)
                controls.append(history.collect_cell(case, anchor, anchor, output, fixture, {k: v['path'] for k, v in tools.items()},
                                                     launcher['path'], affinity, index + len(cases), 9))
        except (ValueError, OSError) as error:
            control_error = repr(error)
            controls = []
        try:
            records.require(suite.verify(fixture, cases) == manifest, 'fixture identity changed')
            for selected in revisions:
                records.build_proof(build_root / 'binaries' / selected['sha'], selected)
            for tool in tools.values():
                records.require(build.digest(Path(tool['path'])) == tool['sha256'], 'reference executable changed')
            records.require(process_resources.launcher_identity(launcher_path) == launcher, 'resource launcher changed')
        except BaseException as error:
            history.write(output / 'invalid.json', {'error': repr(error)})
            raise
    record = records.make_record(contract, head, rows, builds, platform_name, platform.machine().lower(), series,
                                 'parent', origin, collection, controls,
                                 ('https://github.com/' + collection['repository'] + '/actions/runs/'
                                  + str(collection['run_id']) + '/attempts/' + str(collection['run_attempt'])
                                  + ' (benchmark-extended-build-' + platform_name + ')') if collection is not None
                                 else 'original frozen full-production executable used for local paired validation',
                                 control_error=control_error)
    record['storage_release'] = records.read(root / 'storage-release.json')
    record['control_error'] = control_error
    record['identity'] = records.observation_identity(record)
    records.validate(record)
    history.write(root / 'observation.json', record)
    history.write(output / 'progress.json', {'state': 'complete' if control_error is None else 'head-complete-controls-failed',
                                           'head_cells': len(rows), 'control_cells': len(controls)})
    if control_error is not None:
        raise ValueError('head measurements retained; control diagnostic failed: ' + control_error)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    freeze = modes.add_parser('build')
    freeze.add_argument('--repo', type=Path, required=True)
    freeze.add_argument('--root', type=Path, required=True)
    freeze.add_argument('--disk-cache', type=Path, required=True)
    freeze.add_argument('--role', choices=('head', 'base'), required=True)
    freeze.add_argument('--jobs', type=int, default=3)
    measure = modes.add_parser('collect')
    measure.add_argument('--build-root', type=Path, required=True)
    measure.add_argument('--root', type=Path, required=True)
    measure.add_argument('--resource-launcher', type=Path, required=True)
    measure.add_argument('--collection', type=Path, help='Trusted workflow identity JSON; absence means local validation')
    measure.add_argument('--series', help='Explicit series for local paired validation')
    measure.add_argument('--fixture', type=Path, help='Reuse a verified tmpfs fixture for local validation; never removed')
    args = parser.parse_args()
    if args.mode == 'build':
        build_endpoint(args.repo.resolve(), args.root.resolve(), args.role, args.disk_cache.resolve(), args.jobs)
    else:
        if args.collection is None and not args.series:
            parser.error('local collection requires an explicit --series')
        collect(args.build_root.resolve(), args.root.resolve(), args.resource_launcher.resolve(),
                records.read(args.collection) if args.collection else None, args.series, args.fixture)


if __name__ == '__main__':
    main()
