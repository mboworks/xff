# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Recover every available trusted benchmark report before refreshing the site."""

import argparse
from datetime import datetime, timedelta, timezone
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tempfile
import zipfile

import benchmark_campaign
import benchmark_history
import benchmark_preview_ci
import benchmark_records


def extract(raw, destination):
    """Extract bounded artifact data without following links or escaping the destination."""
    if len(raw) > 128 * 1024 * 1024:
        raise ValueError('artifact archive exceeds 128 MiB')
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        entries = archive.infolist()
        if len(entries) > 10000 or sum(item.file_size for item in entries) > 512 * 1024 * 1024:
            raise ValueError('artifact contents exceed limits')
        for item in entries:
            path = PurePosixPath(item.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in item.filename:
                raise ValueError('unsafe artifact path')
            if (item.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('artifact contains a symlink')
            target = destination.joinpath(*path.parts)
            if item.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(item))


def artifacts(api, run, prefix):
    return [item for item in api.pages(f"actions/runs/{run['id']}/artifacts", 'artifacts')
            if item['name'].startswith(prefix) and not item.get('expired')]


def latest_artifacts(items):
    """Select the newest retained artifact for each name across reruns."""
    selected = {}
    for item in items:
        previous = selected.get(item['name'])
        if previous is None or item['created_at'] > previous['created_at']:
            selected[item['name']] = item
    return list(selected.values())


def retain_measurement(api, root, run):
    destination = root / 'runs' / str(run['id']) / str(run['run_attempt'])
    missing = {platform for platform in ('linux', 'macos')
               if not benchmark_records.exists(destination / platform / 'report.json')}
    if not missing:
        return False
    selected = [item for item in latest_artifacts(artifacts(api, run, 'benchmark-report-'))
                if item['created_at'] >= run['run_started_at']]
    reports = {}
    for item in selected:
        match = re.fullmatch(r'benchmark-report-(linux|macos)-[A-Za-z0-9_.-]+', item['name'])
        if not match:
            continue
        platform = match.group(1)
        if platform not in missing:
            continue
        if platform in reports:
            raise ValueError('multiple reports for one platform')
        try:
            raw = api.request(f"actions/artifacts/{item['id']}/zip", raw=True)
            with tempfile.TemporaryDirectory(prefix='xff-report-') as temporary:
                directory = Path(temporary)
                extract(raw, directory)
                path = directory / (item['name'] + '.json')
                reports[platform] = json.loads(path.read_text())
        except (ValueError, KeyError, OSError, zipfile.BadZipFile,
                subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            print(f"::warning::benchmark run {run['id']} {platform}: {error}", flush=True)
    if missing - set(reports):
        print(f"::warning::benchmark run {run['id']} missing artifacts for "
              f"{', '.join(sorted(missing - set(reports)))}", flush=True)
    if not reports:
        return False
    source = dict(run)
    source['reference_time'] = api.request(f"commits/{run['head_sha']}")['commit']['committer']['date']
    retained = False
    for platform, report in reports.items():
        try:
            if report.get('platform') != platform:
                raise ValueError('report platform does not match artifact')
            benchmark_history.retain(root, report, source)
            retained = True
        except (ValueError, KeyError, OSError) as error:
            print(f"::warning::benchmark run {run['id']} {platform}: {error}", flush=True)
    return retained


def retain_backfill(api, root, run):
    destination = root / 'backfills' / str(run['id']) / str(run['run_attempt'])
    if benchmark_records.exists(destination / 'campaign.json'):
        return False
    selected = latest_artifacts(artifacts(api, run, 'benchmark-backfill-'))
    if not selected:
        raise ValueError('backfill artifacts unavailable or expired')
    with tempfile.TemporaryDirectory(prefix='xff-backfill-') as temporary:
        directory = Path(temporary)
        for item in selected:
            name = item['name']
            if not re.fullmatch(r'benchmark-backfill-[A-Za-z0-9_.-]+', name):
                raise ValueError('invalid backfill artifact name')
            target = directory / name
            target.mkdir()
            extract(api.request(f"actions/artifacts/{item['id']}/zip", raw=True), target)
        benchmark_campaign.retain(root, directory, run, api.repository)
    return True


def reconcile(api, root):
    since = (datetime.now(timezone.utc) - timedelta(days=30)).date().isoformat()
    failures = []
    for workflow, event, retain in (
            ('benchmarks.yml', 'push', retain_measurement),
            ('benchmark_backfill.yml', 'workflow_dispatch', retain_backfill)):
        runs = api.pages(f'actions/workflows/{workflow}/runs?status=success&branch=main&created=%3E%3D{since}',
                         'workflow_runs')
        for run in reversed(runs):
            if run.get('event') != event or run.get('head_branch') != 'main':
                continue
            try:
                if retain(api, root, run):
                    print(f"Retained {workflow} run {run['id']}/{run['run_attempt']}", flush=True)
            except (ValueError, KeyError, OSError, zipfile.BadZipFile,
                    subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
                failures.append(f"{workflow} run {run['id']}: {error}")
    # Current PR previews are disposable; recover them even if their original trigger was coalesced.
    pulls = api.pages('pulls?state=open')
    branches = {pull['head']['ref'] for pull in pulls}
    runs = api.pages(f'actions/workflows/main.yml/runs?event=pull_request&created=%3E%3D{since}', 'workflow_runs')
    for run in runs:
        if run.get('head_branch') not in branches:
            continue
        try:
            paths = benchmark_preview_ci.collect(api, run['id'], run['run_attempt'], root)
            if paths:
                print(f"Retained preview {run['id']}/{run['run_attempt']}", flush=True)
        except (ValueError, KeyError, OSError, zipfile.BadZipFile,
                subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            failures.append(f"preview run {run['id']}: {error}")
    for failure in failures:
        print('::warning::' + failure, flush=True)
    print(f'Reconciliation finished with {len(failures)} unavailable or invalid source runs.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    reconcile(benchmark_preview_ci.GitHub(args.repository), args.root)


if __name__ == '__main__':
    main()
