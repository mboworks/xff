#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Find missing merged PR benchmarks and start bounded targeted CI backfills."""

import argparse
import base64
from datetime import date
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile

import benchmark_history
import benchmark_preview_ci

PLATFORMS = ('linux', 'macos')


def report_available(api, run, artifact, platform):
    # Retrieval failures propagate: an API timeout is not evidence that costly
    # measurements must be repeated. Only received, invalid data is rejected.
    raw = api.request(f"actions/artifacts/{artifact['id']}/zip", raw=True)
    try:
        if len(raw) > 128 * 1024 * 1024:
            raise ValueError('benchmark report archive exceeds 128 MiB')
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            entry = archive.getinfo(artifact['name'] + '.json')
            if entry.file_size > 128 * 1024 * 1024:
                raise ValueError('benchmark report exceeds 128 MiB')
            report = json.loads(archive.read(entry))
        if report.get('platform') != platform or report.get('head') != run['head_sha'] or not report.get('tool_comparisons'):
            raise ValueError('report lacks matching head/platform/viewer measurements')
        # Use the publisher's actual completeness, command and binary-identity
        # checks, writing only into a disposable local directory.
        with tempfile.TemporaryDirectory(prefix='xff-recovery-report-') as temporary:
            benchmark_history.retain(Path(temporary), report, run)
        return True
    except (ValueError, KeyError, zipfile.BadZipFile) as error:
        print(f"::warning::unpublishable run {run['id']} {platform}: {error}", file=sys.stderr, flush=True)
        return False


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
        if entry.get('source') != 'merged' or entry.get('local') or entry.get('extended'):
            continue
        platform = entry.get('platform_name', '').split(' / ')[0].lower()
        sha = entry.get('commit', '')
        if platform in PLATFORMS and re.fullmatch(r'[0-9a-f]{40}', sha):
            covered.add((sha, platform))
    pulls = api.pages('pulls?state=closed&sort=updated&direction=desc')
    target_shas = {pull.get('merge_commit_sha') for pull in pulls
                   if pull.get('merged_at') and pull['merged_at'][:10] >= since
                   and pull.get('base', {}).get('ref') == 'main'
                   and (number is None or pull['number'] == number)}
    runs = api.pages('actions/workflows/benchmarks.yml/runs?branch=main', 'workflow_runs')
    by_commit = {}
    for run in runs:
        if run.get('event') == 'push':
            by_commit.setdefault(run['head_sha'], []).append(run)
    pending_backfills = set()
    campaigns = api.pages('actions/workflows/benchmark_backfill.yml/runs?branch=main&status=completed', 'workflow_runs')
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
        for job in jobs:
            sha = job['revision']['sha']
            if sha not in target_shas or (sha, job['platform']) in covered:
                continue
            item = available.get('benchmark-backfill-' + job['id'])
            if not item:
                continue
            raw = api.request(f"actions/artifacts/{item['id']}/zip", raw=True)
            if len(raw) > 128 * 1024 * 1024:
                raise ValueError('backfill artifact exceeds 128 MiB')
            with zipfile.ZipFile(io.BytesIO(raw)) as archive:
                try:
                    info = archive.getinfo('status.json')
                    if info.file_size > 16 * 1024 * 1024:
                        raise ValueError('backfill status exceeds 16 MiB')
                    status = json.loads(archive.read(info))
                    report = f"reports/{sha}/benchmark-report-{job['platform']}-{job['arch']}.json"
                    if status.get(sha, {}).get('status') == 'complete' and report in archive.namelist():
                        pending_backfills.add((sha, job['platform']))
                except (KeyError, ValueError):
                    continue
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
            if run.get('status') != 'completed':
                continue
            artifacts = api.pages(f"actions/runs/{run['id']}/artifacts", 'artifacts')
            for artifact in artifacts:
                match = re.fullmatch(r'benchmark-report-(linux|macos)-[A-Za-z0-9_.-]+', artifact['name'])
                if (match and not artifact.get('expired') and artifact['created_at'] >= run['run_started_at']
                        and report_available(api, run, artifact, match.group(1))):
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
                        help='Start a bounded backfill after checking for an active campaign')
    parser.add_argument('--batch-size', type=int, default=4, help='Maximum PRs per campaign (1 through 8)')
    args = parser.parse_args()
    date.fromisoformat(args.since)
    if not 1 <= args.batch_size <= 8:
        parser.error('--batch-size must be between 1 and 8')
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
    # A cross-product of different missing-platform sets would repeat successful
    # configurations. Recover the oldest gap and batch only identical requests.
    def requested(row):
        return tuple(platform for platform in PLATFORMS if platform in row['missing']
                     and platform not in row.get('available_artifacts', []))

    platforms = requested(candidates[0])
    if not platforms:
        raise ValueError('backfill candidate has no unavailable platform')
    selected = [row for row in candidates if requested(row) == platforms][:args.batch_size]
    # The deployed workflow must support the revisions input added by PR #1147.
    subprocess.run(['gh', 'workflow', 'run', 'benchmark_backfill.yml', '--repo', api.repository,
                    '--ref', 'main', '-f', 'revisions=' + ','.join(row['commit'] for row in selected),
                    '-f', 'platforms=' + ','.join(platforms)], check=True)
    print('Dispatched ' + ', '.join(platforms) + ' for '
          + ', '.join(f"PR #{row['pr']}" for row in selected) + '.', flush=True)


if __name__ == '__main__':
    main()
