# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Build and measure a frozen historical CI job with the current trusted driver."""

import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess

import benchmark_extended_build as build
import benchmark_extended_campaign as campaign
import benchmark_extended_ci as ci
import benchmark_extended_records as records


def freeze(repo, root, plan, job_id, disk_cache, jobs=3):
    source = build.git(repo, 'rev-parse', 'HEAD')
    job = campaign.validate_job(plan, job_id, plan['repository'], source)
    records.require(platform.machine().lower() == job['arch'], 'historical build architecture mismatch')
    revisions = {item['sha']: item for item in campaign.mainline(repo)}
    selected = job['revision']
    records.require(revisions.get(selected['sha']) == selected, 'historical revision is not on main first-parent history')
    root.mkdir(parents=True, exist_ok=False)
    # Freeze this configuration before either production build changes Bazel's links.
    options = ['--config=clang_opt', '--disk_cache=' + str(disk_cache), '--jobs=' + str(jobs)]
    bazel = ['bazel', '--nosystem_rc', '--nohome_rc']
    subprocess.run([*bazel, 'build', '//tools:process_resources_native', *options], cwd=repo, check=True)
    executable = subprocess.check_output([*bazel, 'cquery', 'config(//tools:process_resources_native, target)',
                                         *options, '--output=starlark',
                                         '--starlark:expr=target.files_to_run.executable.path'], cwd=repo, text=True).strip()
    records.require(bool(executable) and len(executable.splitlines()) == 1, 'ambiguous resource launcher')
    shutil.copy2(repo / executable, root / 'resource-launcher')
    checkouts = []
    try:
        for role, sha in (('head', selected['sha']), ('base', selected['parent'])):
            checkout = root.parent / ('historical-' + role)
            subprocess.run(['git', '-C', str(repo), 'worktree', 'add', '--detach', str(checkout), sha], check=True)
            checkouts.append(checkout)
            ci.build_endpoint(checkout, root, role, disk_cache, jobs)
        (root / 'campaign.json').write_text(json.dumps(plan, indent=2) + '\n')
    finally:
        # No compiler server competes with the timed collection. Leave checkouts
        # and logs in place on failure; never remove another job's data.
        for checkout in [repo, *checkouts]:
            subprocess.run([*bazel, 'shutdown'], cwd=checkout, check=True)


def collect(build_root, root, plan, job_id, repository, source_head, run_id, attempt, plan_attempt):
    job = campaign.validate_job(plan, job_id, repository, source_head)
    records.require(records.read(build_root / 'campaign.json') == plan, 'retained build campaign changed')
    records.require(platform.machine().lower() == job['arch'], 'historical measurement architecture mismatch')
    head, base = records.read(build_root / 'revisions.json')
    records.require((head['sha'], base['sha']) == (job['revision']['sha'], job['revision']['parent']),
                    'retained builds do not match historical job')
    collection = {'run_id': run_id, 'run_attempt': attempt, 'repository': repository,
                  'head_sha': head['sha'], 'workflow_head_sha': source_head,
                  'campaign_identity': plan['identity'], 'campaign_job': job_id, 'campaign_attempt': plan_attempt,
                  'build_artifact': f'benchmark-extended-backfill-build-{job_id}-{attempt}'}
    return ci.collect(build_root, root, build_root / 'resource-launcher', collection)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    freeze_args = modes.add_parser('build')
    freeze_args.add_argument('--repo', type=Path, required=True)
    freeze_args.add_argument('--root', type=Path, required=True)
    freeze_args.add_argument('--disk-cache', type=Path, required=True)
    freeze_args.add_argument('--jobs', type=int, default=3)
    measure = modes.add_parser('collect')
    measure.add_argument('--build-root', type=Path, required=True)
    measure.add_argument('--root', type=Path, required=True)
    measure.add_argument('--repository', required=True)
    measure.add_argument('--source-head', required=True)
    measure.add_argument('--run-id', type=int, required=True)
    measure.add_argument('--run-attempt', type=int, required=True)
    measure.add_argument('--plan-attempt', type=int, required=True)
    for child in (freeze_args, measure):
        child.add_argument('--plan', type=Path, required=True)
        child.add_argument('--job', required=True)
    args = parser.parse_args()
    plan = records.read(args.plan)
    if args.mode == 'build':
        freeze(args.repo.resolve(), args.root.resolve(), plan, args.job, args.disk_cache.resolve(), args.jobs)
    else:
        collect(args.build_root.resolve(), args.root.resolve(), plan, args.job, args.repository,
                args.source_head, args.run_id, args.run_attempt, args.plan_attempt)


if __name__ == '__main__':
    main()
