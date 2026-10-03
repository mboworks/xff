#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Freeze complete-revision CI jobs and atomically retain a validated backfill campaign."""

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile

import benchmark_backfill as backfill
import benchmark_compare as compare
import benchmark_records

PLATFORMS = {'linux': ('ubuntu-latest', 'x86_64'), 'macos': ('macos-latest', 'arm64')}
FILE_COUNTS = [*compare.DEFAULT_FILE_COUNTS, 20000, 50000, 100000]


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def make_plan(repo, history_root):
    tools = {'xff': {'path': 'xff'}, 'find': {}, 'rg': {}, 'fzf': {}}
    jobs = []
    for platform, (runner, arch) in PLATFORMS.items():
        for revision in backfill.revisions(repo, [], history_root, platform):
            jobs.append({'id': platform + '-' + revision['sha'], 'platform': platform,
                         'os': runner, 'arch': arch, 'revision': revision})
    plan = {'schema': 1, 'source_head': backfill.git(repo, 'rev-parse', 'HEAD'),
            'history_head': backfill.git(history_root, 'rev-parse', 'HEAD'),
            'created_at': backfill.now(), 'jobs': jobs,
            'contract': {'files': FILE_COUNTS, 'cpus': [1, 3], 'depth': 40,
                         'repetitions': 9, 'retained': 7, 'driver': backfill.driver_identity(),
                         'tasks': {name: sorted(participants) for name, _, participants in
                                   compare.scenarios(Path('.'), [], tools)}}}
    plan['identity'] = identity(plan)
    validate_plan(plan)
    return plan


def validate_plan(plan):
    require(plan.get('schema') == 1, 'unsupported campaign schema')
    require(plan.get('identity') == identity({key: value for key, value in plan.items() if key != 'identity'}),
            'campaign manifest identity changed')
    for field in ('source_head', 'history_head'):
        require(re.fullmatch('[0-9a-f]{40}', plan[field]), 'invalid campaign commit')
    contract = plan['contract']
    require(contract['files'] == FILE_COUNTS and contract['cpus'] == [1, 3]
            and (contract['depth'], contract['repetitions'], contract['retained']) == (40, 9, 7),
            'CI campaign requires the complete 1/3 grid and fastest seven of nine samples')
    require(contract['tasks'] and contract['driver'], 'campaign tasks and driver identities required')
    require(0 < len(plan['jobs']) <= 256, 'campaign requires 1 through 256 complete-revision jobs')
    ids = set()
    for job in plan['jobs']:
        sha, platform = job['revision']['sha'], job['platform']
        require(re.fullmatch('[0-9a-f]{40}', sha), 'invalid historical revision')
        require(datetime.fromisoformat(job['revision']['date']).tzinfo is not None,
                'historical commit date requires a timezone')
        require(platform in PLATFORMS and (job['os'], job['arch']) == PLATFORMS[platform],
                'invalid campaign platform')
        require(job['id'] == platform + '-' + sha and job['id'] not in ids, 'duplicate or invalid campaign job')
        ids.add(job['id'])
    require({job['platform'] for job in plan['jobs']} == set(PLATFORMS), 'both CI platforms are required')


def run_job(plan, job_id, repo, output, disk_cache, fixture_parent):
    validate_plan(plan)
    require(backfill.git(repo, 'rev-parse', 'HEAD') == plan['source_head'], 'campaign driver checkout changed')
    require(backfill.driver_identity() == plan['contract']['driver'], 'campaign driver files changed')
    job = next((job for job in plan['jobs'] if job['id'] == job_id), None)
    require(job is not None, 'unknown campaign job')
    require(backfill.host_platform() == job['platform'], 'campaign job is on the wrong platform')
    require(backfill.platform.machine().lower() == job['arch'], 'campaign architecture changed')
    contract = plan['contract']
    args = argparse.Namespace(
        repo=repo, output=output, series='github-ci-' + job['platform'], purpose='ci-replacement',
        revision=[job['revision']['sha']], revisions_file=None, history_root=None, history_platform=None,
        files=contract['files'], cpus=contract['cpus'], depth=contract['depth'],
        repetitions=contract['repetitions'], keep=contract['retained'],
        require_cpu_affinity=job['platform'] == 'linux', require_memory=job['platform'] == 'linux',
        fixture_parent=fixture_parent, disk_cache=disk_cache)
    collection = {'id': int(os.environ['GITHUB_RUN_ID']), 'run_attempt': int(os.environ['GITHUB_RUN_ATTEMPT']),
                  'head_sha': os.environ['GITHUB_SHA']}
    require(collection['head_sha'] == plan['source_head'], 'campaign source workflow changed')
    batch = backfill.prepare(args, campaign=plan['identity'], collection=collection)
    require(batch['contract']['revisions'] == [job['revision']], 'historical revision metadata changed')
    return backfill.run(args, batch)


def validate_report(record, batch, job, plan):
    contract, expected = batch['contract'], plan['contract']
    require(batch['identity'] == identity(contract), 'batch identity changed')
    require(contract['campaign'] == plan['identity'], 'batch belongs to another campaign')
    require(contract['purpose'] == 'ci-replacement', 'local series cannot replace CI history')
    for key, value in {'platform': job['platform'], 'series': 'github-ci-' + job['platform'],
                       'replacement_target': 'github-ci-' + job['platform'],
                       'revisions': [job['revision']], **{key: expected[key] for key in
                       ('files', 'cpus', 'depth', 'repetitions', 'retained', 'driver')}}.items():
        require(contract[key] == value, 'batch contract mismatch: ' + key)
    require(contract['environment']['machine'].lower() == job['arch'], 'campaign architecture changed')
    validate_measurements(record, batch, job['revision'], expected['tasks'])


def validate_measurements(record, batch, revision, tasks):
    """Validate observations shared by CI replacement and independent local series."""
    contract = batch['contract']
    expected = contract
    arch = contract['environment']['machine'].lower()
    require(record['schema'] == 1 and record['kind'] == 'backfill', 'expected a backfill report')
    for key in ('platform', 'series', 'purpose', 'replacement_target', 'allocation'):
        require(record[key] == contract[key], 'report contract mismatch: ' + key)
    sha = revision['sha']
    require(record['head'] == sha and record['revision'] == revision
            and record['build']['revision'] == sha and record['batch'] == batch['identity'],
            'report revision or batch mismatch')
    report = record['tool_comparisons']
    require(record['build']['sha256'] == report['tools']['xff']['sha256'], 'report binary hash mismatch')
    require(re.fullmatch('[0-9a-f]{64}', record['build']['sha256']), 'invalid binary hash')
    actual = report['contract']
    for key, value in {'file_counts': expected['files'], 'cpu_counts': expected['cpus'],
                       'depth': expected['depth'], 'repetitions': expected['repetitions'],
                       'retained': expected['retained'], 'batch': batch['identity'],
                       'runner_class': contract['series'], 'estimator': 'mean-fastest',
                       'machine': arch, 'storage': contract['storage'],
                       'build_identity': json.dumps(record['build']['configuration'], sort_keys=True)}.items():
        require(actual[key] == value, 'measurement contract mismatch: ' + key)
    environment, allocation = contract['environment'], contract['allocation']
    require(environment['cpu_count'] >= max(expected['cpus']),
            'insufficient or different measurement host')
    require(actual['platform'] == environment['platform'] and actual['cpu_count'] == environment['cpu_count'],
            'measurement host changed')
    for name in ('find', 'rg', 'fzf'):
        require(report['tools'][name] == environment['tools'][name]
                and report['tools'][name]['status'] == 'available', 'reference tool changed or missing: ' + name)
    masks = actual['affinity_by_cpu_count']
    if contract['platform'] == 'linux':
        ids = allocation['cpu_ids']
        kind = 'physical-cores' if contract['purpose'] == 'local-addition' else 'logical-cpus'
        require(allocation['kind'] == kind and allocation['required'] and ids is not None
                and len(ids) == len(set(ids)) == max(expected['cpus']), 'Linux requires pinned CPU allocations')
        require(masks == {str(cpu): ids[:cpu] for cpu in expected['cpus']}, 'Linux measurement affinity changed')
        require(contract['storage']['filesystem'] == 'tmpfs' and contract['storage']['memory_required'],
                'Linux requires verified tmpfs fixtures')
    else:
        require(allocation == {'kind': 'workers', 'cpu_ids': None, 'required': False}
                and masks == {str(cpu): None for cpu in expected['cpus']}, 'macOS must record worker requests without pinning')
    expected_keys = {(files, cpus, shape, name) for files in expected['files'] for cpus in expected['cpus']
                     for shape in ('broad', 'deep') for name in tasks}
    keys = set()
    for task in report['tasks']:
        key = (task['files'], task['cpus'], task['dataset'], task['name'])
        require(key in expected_keys and key not in keys, 'unexpected or duplicate campaign task')
        keys.add(key)
        require(task['shape'] == f"{task['cpus']}cpu/{task['files']}/{task['dataset']}", 'task coordinates changed')
        require(not task['skips'] and sorted(task['participants']) == tasks[task['name']],
                'missing campaign participant')
    require(keys == expected_keys, 'incomplete campaign task matrix')
    compare.render(report)  # Validate all samples, successful commands and finite measurements.


def retain(root, artifacts, source, repository):
    """Validate the entire campaign before making any replacement visible."""
    require(source['name'] == 'Benchmark backfill' and source['path'] == '.github/workflows/benchmark_backfill.yml'
            and source['event'] == 'workflow_dispatch' and source['head_branch'] == 'main'
            and source['status'] == 'completed' and source['conclusion'] == 'success'
            and source['repository']['full_name'] == repository, 'expected a successful trusted main backfill workflow')
    run, attempt = int(source['id']), int(source['run_attempt'])
    require(run > 0 and attempt > 0, 'invalid source workflow identity')
    plan = json.loads((artifacts / 'benchmark-backfill-plan/campaign.json').read_text())
    validate_plan(plan)
    require(plan['source_head'] == source['head_sha'], 'campaign source commit mismatch')
    expected_artifacts = {'benchmark-backfill-plan', *('benchmark-backfill-' + job['id'] for job in plan['jobs'])}
    require({path.name for path in artifacts.iterdir()} == expected_artifacts, 'incomplete or unexpected campaign artifacts')
    trusted = {key: source[key] for key in ('id', 'run_attempt', 'created_at', 'head_sha', 'head_branch', 'event')}
    trusted['pull_requests'] = []
    destination = root / 'backfills' / str(run) / str(attempt)
    originals = {}
    for path in benchmark_records.run_paths(root):
        old = benchmark_records.read(path)
        originals.setdefault((benchmark_records.platform_key(old), old['head']), []).append(path.relative_to(root).as_posix())
    records = []
    for job in plan['jobs']:
        folder = artifacts / ('benchmark-backfill-' + job['id'])
        batch = json.loads((folder / 'batch.json').read_text())
        status = json.loads((folder / 'status.json').read_text())
        sha = job['revision']['sha']
        require(set(status) == {sha} and status[sha]['status'] == 'complete', 'incomplete campaign revision')
        reports = list(folder.glob('reports/*/*.json'))
        expected = folder / 'reports' / sha / f"benchmark-report-{job['platform']}-{job['arch']}.json"
        require(reports == [expected], 'unexpected campaign report set')
        record = json.loads(expected.read_text())
        validate_report(record, batch, job, plan)
        collection = batch['contract']['collection']
        require(collection['id'] == run and 1 <= collection['run_attempt'] <= attempt
                and collection['head_sha'] == source['head_sha'], 'batch source workflow mismatch')
        record.update(source=trusted, collection=collection, campaign=plan['identity'])
        saved = destination / job['platform'] / sha / 'report.json'
        record['original_reports'] = (benchmark_records.read(saved)['original_reports'] if benchmark_records.exists(saved)
                                      else sorted(originals.get((job['platform'], sha), [])))
        records.append((job, record, batch, status))
    payloads = {'campaign.json': plan, 'source.json': trusted}
    for job, record, batch, status in records:
        prefix = job['platform'] + '/' + job['revision']['sha'] + '/'
        payloads.update({prefix + 'report.json': record, prefix + 'batch.json': batch, prefix + 'status.json': status})
    if destination.exists():
        for name, value in payloads.items():
            path = destination / name
            require(benchmark_records.exists(path) and benchmark_records.read(path) == value, 'existing campaign cannot change')
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.campaign-', dir=destination.parent) as temporary:
        stage = Path(temporary) / 'complete'
        for name, value in payloads.items():
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            backfill.write_json(path, value)
        stage.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    plan = modes.add_parser('plan')
    for name in ('repo', 'history-root', 'output'):
        plan.add_argument('--' + name, type=Path, required=True)
    run = modes.add_parser('run')
    for name in ('plan', 'repo', 'output', 'fixture-parent'):
        run.add_argument('--' + name, type=Path, required=True)
    run.add_argument('--job', required=True)
    run.add_argument('--disk-cache', type=Path)
    publish = modes.add_parser('publish')
    for name in ('root', 'artifacts', 'source'):
        publish.add_argument('--' + name, type=Path, required=True)
    publish.add_argument('--repository', required=True)
    args = parser.parse_args()
    if args.mode == 'plan':
        result = make_plan(args.repo, args.history_root)
        backfill.write_json(args.output, result)
        print(json.dumps({'include': [{key: job[key] for key in ('id', 'os', 'platform', 'arch')}
                                     for job in result['jobs']]}))
    elif args.mode == 'run':
        return run_job(json.loads(args.plan.read_text()), args.job, args.repo.resolve(), args.output.resolve(),
                       args.disk_cache, args.fixture_parent)
    else:
        print(retain(args.root, args.artifacts, json.loads(args.source.read_text()), args.repository))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
