#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Find missing merged PR benchmarks and start one targeted CI backfill at a time."""

import argparse
import base64
from datetime import date
import io
import json
import re
import subprocess
import zipfile

import benchmark_preview_ci

PLATFORMS = ('linux', 'macos')


def catalog(api):
    metadata = api.request('contents/benchmarks/catalog.json?ref=coverage-pages')
    if not metadata.get('content') and metadata.get('sha'):
        metadata = api.request('git/blobs/' + metadata['sha'])
    if metadata.get('encoding') != 'base64' or not metadata.get('content'):
        raise ValueError('published benchmark catalog is unavailable')
    value = json.loads(base64.b64decode(metadata['content'], validate=False))
    if not isinstance(value, list):
        raise ValueError('invalid published benchmark catalog')
    return value


def inventory(api, since, number=None):
    entries = catalog(api)
    covered = set()
    for entry in entries:
        if entry.get('source') != 'merged' or entry.get('local'):
            continue
        platform = entry.get('platform_name', '').split(' / ')[0].lower()
        sha = entry.get('commit', '')
        if platform in PLATFORMS and re.fullmatch(r'[0-9a-f]{40}', sha):
            covered.add((sha, platform))
    pulls = api.pages('pulls?state=closed&sort=updated&direction=desc')
    runs = api.pages('actions/workflows/benchmarks.yml/runs?branch=main', 'workflow_runs')
    by_commit = {}
    for run in runs:
        if run.get('event') == 'push':
            by_commit.setdefault(run['head_sha'], []).append(run)
    pending_backfills = set()
    campaigns = api.pages('actions/workflows/benchmark_backfill.yml/runs?branch=main&status=success', 'workflow_runs')
    for run in campaigns:
        artifacts = api.pages(f"actions/runs/{run['id']}/artifacts", 'artifacts')
        available = {item['name']: item for item in artifacts if not item.get('expired')}
        plan = available.get('benchmark-backfill-plan')
        if not plan:
            continue
        raw = api.request(f"actions/artifacts/{plan['id']}/zip", raw=True)
        if len(raw) > 16 * 1024 * 1024:
            raise ValueError('backfill plan artifact exceeds 16 MiB')
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            entry = archive.getinfo('campaign.json')
            if entry.file_size > 16 * 1024 * 1024:
                raise ValueError('backfill plan exceeds 16 MiB')
            manifest = json.loads(archive.read(entry))
        jobs = manifest['jobs']
        if all('benchmark-backfill-' + job['id'] in available for job in jobs):
            pending_backfills.update((job['revision']['sha'], job['platform']) for job in jobs)
    rows = []
    for pull in pulls:
        if number is not None and pull['number'] != number:
            continue
        merged = pull.get('merged_at')
        sha = pull.get('merge_commit_sha')
        if not merged or merged[:10] < since or pull.get('base', {}).get('ref') != 'main':
            continue
        if not isinstance(sha, str) or not re.fullmatch(r'[0-9a-f]{40}', sha):
            raise ValueError(f"PR {pull['number']} has no valid merge commit")
        missing = [platform for platform in PLATFORMS if (sha, platform) not in covered]
        if not missing:
            continue
        source_runs = by_commit.get(sha, [])
        active = any(run['status'] != 'completed' for run in source_runs)
        available = {platform for platform in PLATFORMS if (sha, platform) in pending_backfills}
        for run in source_runs:
            if run.get('conclusion') != 'success':
                continue
            artifacts = api.pages(f"actions/runs/{run['id']}/artifacts", 'artifacts')
            for artifact in artifacts:
                match = re.fullmatch(r'benchmark-report-(linux|macos)-[A-Za-z0-9_.-]+', artifact['name'])
                if match and not artifact.get('expired') and artifact['created_at'] >= run['run_started_at']:
                    available.add(match.group(1))
        state = ('measuring' if active else 'awaiting publication' if set(missing) <= available
                 else 'needs backfill')
        rows.append({'pr': pull['number'], 'commit': sha, 'merged_at': merged,
                     'missing': missing, 'available_artifacts': sorted(available), 'state': state})
    return sorted(rows, key=lambda row: (row['merged_at'], row['pr']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True, help='GitHub owner/repository')
    parser.add_argument('--since', default='2026-09-19', help='Inclusive merge date (YYYY-MM-DD)')
    parser.add_argument('--pr', type=int, help='Limit the inventory to one PR number')
    parser.add_argument('--dispatch-next', action='store_true',
                        help='Start one missing PR backfill after checking for an active campaign')
    args = parser.parse_args()
    date.fromisoformat(args.since)
    api = benchmark_preview_ci.GitHub(args.repository)
    rows = inventory(api, args.since, args.pr)
    print(json.dumps({'since': args.since, 'missing': rows}, indent=2), flush=True)
    if not args.dispatch_next:
        return
    candidates = [row for row in rows if row['state'] == 'needs backfill']
    if not candidates:
        print('No PR requires backfill.', flush=True)
        return
    campaigns = api.pages('actions/workflows/benchmark_backfill.yml/runs?branch=main', 'workflow_runs')
    if any(run['status'] != 'completed' for run in campaigns):
        raise ValueError('a backfill campaign is already active or queued; retry after it finishes')
    selected = candidates[0]
    # The deployed workflow must support the revisions input added by PR #1147.
    subprocess.run(['gh', 'workflow', 'run', 'benchmark_backfill.yml', '--repo', api.repository,
                    '--ref', 'main', '-f', 'revisions=' + selected['commit']], check=True)
    print(f"Dispatched PR #{selected['pr']} ({selected['commit']}) for both platforms.", flush=True)


if __name__ == '__main__':
    main()
