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
import benchmark_extended_records
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


def run_artifacts(api, root, run):
    """Avoid re-enumerating settled runs on every coalesced publication request."""
    now = datetime.now(timezone.utc)
    source = {key: run.get(key) for key in ('id', 'run_attempt', 'head_sha', 'updated_at')}
    marker = root / 'reconciliation' / str(run['id']) / str(run['run_attempt']) / 'artifacts.json'
    if marker.exists():
        saved = json.loads(marker.read_text())
        if saved['source'] == source and now - datetime.fromisoformat(saved['checked_at']) < timedelta(hours=24):
            return saved['artifacts']
    inventory = api.pages(f"actions/runs/{run['id']}/artifacts", 'artifacts')
    # Retry attempts get a different key. Recent completions are always refreshed
    # to allow for artifact-index propagation; external deletion/expiry is refreshed daily.
    if (run.get('status') == 'completed' and run.get('updated_at')
            and now - datetime.fromisoformat(run['updated_at']) >= timedelta(minutes=15)):
        marker.parent.mkdir(parents=True, exist_ok=True)
        temporary = marker.with_suffix('.json.tmp')
        temporary.write_text(json.dumps({'source': source, 'checked_at': now.isoformat(), 'artifacts': inventory}) + '\n')
        temporary.replace(marker)
    return inventory


def retain_measurement(api, root, run, inventory=None):
    destination = root / 'runs' / str(run['id']) / str(run['run_attempt'])
    missing = {platform for platform in ('linux', 'macos')
               if not benchmark_records.exists(destination / platform / 'report.json')}
    if not missing:
        return False
    selected = [item for item in latest_artifacts(artifacts(api, run, 'benchmark-report-') if inventory is None else
                                                [item for item in inventory if item['name'].startswith('benchmark-report-')])
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


def retain_extended(api, root, run, inventory=None):
    """Recover complete platforms independently, including successful earlier attempts."""
    selected = latest_artifacts(artifacts(api, run, 'benchmark-extended-report-') if inventory is None else
                                [item for item in inventory if item['name'].startswith('benchmark-extended-report-')])
    retained = False
    for item in selected:
        match = re.fullmatch(r'benchmark-extended-report-(linux|macos)', item['name'])
        if not match:
            continue
        try:
            retained = retain_extended_artifact(api, root, run, item, match.group(1)) or retained
        except (ValueError, KeyError, OSError, zipfile.BadZipFile,
                subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            print(f"::warning::extended run {run['id']} {item['name']}: {error}", flush=True)
    return retained


def retain_extended_artifact(api, root, run, item, platform):
    collections = root / 'extended' / 'collections' / str(run['id'])
    markers = {*collections.glob('*/' + platform + '.json'),
               *(path.with_suffix('') for path in collections.glob('*/' + platform + '.json.gz'))}
    for marker in markers:
        saved = benchmark_extended_records.read(marker)
        observation = root / 'extended' / 'observations' / saved['identity'] / 'report.json'
        if saved['artifact_id'] == item['id'] and benchmark_records.exists(observation):
            return False
    # A rerun of failed jobs does not recreate a successful platform's artifact.
    # Its observation and source must keep the original attempt identity.
    with tempfile.TemporaryDirectory(prefix='xff-extended-report-') as temporary:
        directory = Path(temporary)
        extract(api.request(f"actions/artifacts/{item['id']}/zip", raw=True), directory)
        record = json.loads((directory / 'observation.json').read_text())
    collection = record['collection']
    if (record['origin'] != 'ci' or record['platform'] != platform
            or collection['run_id'] != run['id']
            or type(collection['run_attempt']) is not int
            or not 1 <= collection['run_attempt'] <= run['run_attempt']):
        raise ValueError('extended artifact platform/run/attempt mismatch')
    attempt = collection['run_attempt']
    source = dict(run if attempt == run['run_attempt'] else
                  api.request(f"actions/runs/{run['id']}/attempts/{attempt}"))
    commit = api.request(f"commits/{source['head_sha']}")
    if not commit.get('parents'):
        raise ValueError('extended source has no first parent')
    source['base_sha'] = commit['parents'][0]['sha']
    benchmark_extended_records.retain(root, record, source)
    marker = collections / str(attempt) / (platform + '.json')
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(json.dumps({'identity': record['identity'], 'artifact_id': item['id']}) + '\n')
    return True


def retain_benchmarks(api, root, run):
    """A failed independent job must not discard other valid platform reports."""
    inventory = [item for item in run_artifacts(api, root, run)
                 if not item.get('expired') and (not item.get('expires_at')
                    or datetime.fromisoformat(item['expires_at']) > datetime.now(timezone.utc))]
    retained = False
    for name, retain in (('core', retain_measurement), ('extended', retain_extended)):
        try:
            retained = retain(api, root, run, inventory) or retained
        except (ValueError, KeyError, OSError, zipfile.BadZipFile,
                subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            print(f"::warning::benchmark run {run['id']} {name}: {error}", flush=True)
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
            try:
                target.mkdir()
                extract(api.request(f"actions/artifacts/{item['id']}/zip", raw=True), target)
            except (ValueError, OSError, zipfile.BadZipFile, subprocess.CalledProcessError,
                    subprocess.TimeoutExpired) as error:
                if name == 'benchmark-backfill-plan':
                    raise
                print(f"::warning::backfill artifact {name}: {error}", flush=True)
        benchmark_campaign.retain(root, directory, run, api.repository, allow_partial=True)
    return True


def reconcile(api, root):
    since = (datetime.now(timezone.utc) - timedelta(days=30)).date().isoformat()
    failures = []
    for workflow, event, status, retain in (
            ('benchmarks.yml', 'push', 'completed', retain_benchmarks),
            ('benchmark_backfill.yml', 'workflow_dispatch', 'completed', retain_backfill)):
        runs = api.pages(f'actions/workflows/{workflow}/runs?status={status}&branch=main&created=%3E%3D{since}',
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
