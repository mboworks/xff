# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Plan bounded, newest-first extended CI backfills from retained history."""

import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

import benchmark_extended as suite
import benchmark_records as records
import benchmark_database_storage

# Historical toolchains need ICU 70 and libxml2.so.2 from the 24.04 runtime preparer.
PLATFORMS = {'linux': ('ubuntu-24.04', 'x86_64'), 'macos': ('macos-latest', 'arm64')}
SHA = re.compile('[0-9a-f]{40}')
WORKFLOW = 'Extended benchmark backfill'
WORKFLOW_PATH = '.github/workflows/benchmark_extended_backfill.yml'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def mainline(repo):
    output = subprocess.check_output(['git', '-C', str(repo), 'log', '--first-parent',
                                      '--format=%H%x09%P%x09%cI%x09%s', 'HEAD'], text=True)
    revisions = []
    for line in output.splitlines():
        sha, parents, date, subject = line.split('\t', 3)
        numbers = re.findall(r'\(#([0-9]+)\)', subject)
        revisions.append({'sha': sha, 'parent': parents.split()[0] if parents else None,
                          'date': date, 'subject': subject, 'pr': int(numbers[-1]) if numbers else 0})
    return revisions


def completed(catalog):
    """Only validated dense CI views satisfy CI recovery; local views cannot."""
    result = set()
    for entry in catalog:
        if entry.get('local') or entry.get('source', 'merged') != 'merged' or not entry.get('ci_history_view'):
            continue
        for part in entry.get('workload_observations', []):
            contract = part.get('contract', {})
            if (contract.get('suite') != suite.DENSE_RECIPE
                    or contract.get('file_counts') != list(suite.FILE_COUNTS)
                    or contract.get('cpu_counts') != [1, 3]
                    or (contract.get('repetitions'), contract.get('retained')) != (9, 7)
                    or part.get('binary', {}).get('state') != 'Verified against Retention'):
                continue
            platform = entry['platform'].split(' / ')[0]
            if platform in PLATFORMS and contract.get('machine') == PLATFORMS[platform][1]:
                result.add((platform, entry['commit']))
    return result


def select_jobs(revisions, done, platforms, limit):
    """Choose up to limit revisions, with independent missing-platform jobs."""
    require(type(limit) is int and 1 <= limit <= 100, 'batch size must be between 1 and 100 revisions')
    require(platforms and len(platforms) == len(set(platforms)) and set(platforms) <= set(PLATFORMS),
            'unknown or duplicate platform selection')
    jobs, selected = [], 0
    for revision in revisions:
        missing = [platform for platform in platforms if (platform, revision['sha']) not in done]
        if not missing:
            continue
        for platform in missing:
            runner, arch = PLATFORMS[platform]
            jobs.append({'id': platform + '-' + revision['sha'], 'platform': platform,
                         'os': runner, 'arch': arch, 'revision': revision})
        selected += 1
        if selected == limit:
            break
    return jobs


def make_plan(repo, history, repository, limit=10, platforms=None, oldest=None, recovered=None):
    catalog = records.read(history / 'catalog.json')
    revisions = mainline(repo)
    positions = {revision['sha']: index for index, revision in enumerate(revisions)}
    if oldest is None:
        collected = [positions[entry['commit']] for entry in catalog
                     if not entry.get('local') and entry.get('source', 'merged') == 'merged'
                     and entry.get('platform', '').split(' / ')[0] in PLATFORMS
                     and entry.get('commit') in positions]
        require(bool(collected), 'no retained CI boundary; supply --oldest explicitly')
        stop = max(collected)
    else:
        require(oldest in positions, 'oldest revision is not on the selected main first-parent history')
        stop = positions[oldest]
    candidates = revisions[:stop + 1]
    selected_platforms = platforms or list(PLATFORMS)
    plan = {'schema': 1, 'kind': 'extended-ci-backfill', 'repository': repository,
            'source_head': revisions[0]['sha'], 'oldest': candidates[-1]['sha'],
            'created_at': datetime.now(timezone.utc).isoformat(), 'direction': 'newest-first',
            'batch_revisions': limit, 'platforms': selected_platforms,
            'history_catalog_sha256': identity(catalog),
            'contract': {'recipe': suite.DENSE_RECIPE, 'cases': [asdict(case) for case in suite.cases(suite.DENSE_RECIPE)],
                         'repetitions': 9, 'keep': 7, 'warmup_rounds': 1},
            'jobs': select_jobs(candidates, completed(catalog) | set(recovered or ()), selected_platforms, limit)}
    plan['identity'] = identity(plan)
    validate_plan(plan)
    return plan


def validate_plan(plan):
    require(plan.get('schema') == 1 and plan.get('kind') == 'extended-ci-backfill', 'unsupported extended campaign')
    require(plan.get('identity') == identity({key: value for key, value in plan.items() if key != 'identity'}),
            'extended campaign identity changed')
    require(re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', plan['repository']) is not None, 'invalid repository')
    require(plan['direction'] == 'newest-first', 'historical collection must proceed newest first')
    require(all(SHA.fullmatch(plan[key]) is not None for key in ('source_head', 'oldest'))
            and re.fullmatch('[0-9a-f]{64}', plan['history_catalog_sha256']) is not None,
            'invalid campaign source identity')
    require(plan['contract'] == {'recipe': suite.DENSE_RECIPE,
                                'cases': [asdict(case) for case in suite.cases(suite.DENSE_RECIPE)],
                                'repetitions': 9, 'keep': 7, 'warmup_rounds': 1}, 'extended sampling or grid changed')
    require(type(plan['batch_revisions']) is int and 1 <= plan['batch_revisions'] <= 100, 'invalid campaign batch size')
    platforms = plan['platforms']
    require(platforms and len(platforms) == len(set(platforms)) and set(platforms) <= set(PLATFORMS), 'invalid platforms')
    require(len(plan['jobs']) <= 200, 'too many platform jobs')
    seen = set()
    for job in plan['jobs']:
        revision, platform = job['revision'], job['platform']
        # The first campaign used ubuntu-latest. Keep its immutable mixed-platform
        # plan valid so successful macOS reports remain recoverable. New plans
        # always select the pinned runner above; the runtime preparer still checks
        # the actual Linux distribution before any historical build or timing.
        runners = (PLATFORMS.get(platform),)
        if platform == 'linux':
            runners += (('ubuntu-latest', 'x86_64'),)
        require(platform in platforms and (job['os'], job['arch']) in runners, 'wrong historical runner')
        require(SHA.fullmatch(revision['sha']) is not None and SHA.fullmatch(revision['parent'] or '') is not None,
                'invalid historical revision or first parent')
        require(datetime.fromisoformat(revision['date']).tzinfo is not None, 'historical date needs a timezone')
        require(job['id'] == platform + '-' + revision['sha'] and job['id'] not in seen, 'duplicate or invalid job')
        seen.add(job['id'])
    require(len({job['revision']['sha'] for job in plan['jobs']}) <= plan['batch_revisions'], 'batch exceeds revision limit')


def validate_job(plan, job_id, repository, source_head):
    validate_plan(plan)
    require(plan['repository'] == repository and plan['source_head'] == source_head,
            'campaign repository/workflow source changed')
    jobs = [job for job in plan['jobs'] if job['id'] == job_id]
    require(len(jobs) == 1, 'job is not in the frozen campaign')
    return jobs[0]


def checkpoints(history, observations):
    """Validate retained raw checkpoints, including data not yet on the catalog."""
    benchmark_database_storage.expand(history, ('extended/',))
    recovered = set()
    paths = {*history.glob('extended/observations/*/report.json'),
             *(path.with_suffix('') for path in history.glob('extended/observations/*/report.json.gz'))}
    for path in paths:
        record = observations.read(path)
        if record['origin'] != 'ci' or record['contract']['recipe'] != suite.DENSE_RECIPE:
            continue
        observations.require(bool(record.get('publication_source')), 'CI checkpoint lacks trusted provenance')
        observations.validate(record, record['publication_source'])
        if record['machine'] == PLATFORMS[record['platform']][1]:
            recovered.add((record['platform'], record['revision']['sha']))
    return recovered


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--history-root', type=Path, required=True)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--batch-size', type=int, default=10)
    parser.add_argument('--platform', choices=tuple(PLATFORMS), action='append')
    parser.add_argument('--oldest')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    # Recover before planning, without waiting for a Pages publication. Import here
    # to keep the pure planner independent of the observation validator.
    import benchmark_extended_records as observations
    recovered = checkpoints(args.history_root, observations)
    plan = make_plan(args.repo, args.history_root, args.repository, args.batch_size, args.platform, args.oldest, recovered)
    args.output.write_text(json.dumps(plan, indent=2) + '\n')
    print(json.dumps({'include': plan['jobs']}))


if __name__ == '__main__':
    main()
