# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Trusted PR preview coordination; artifacts are bounded JSON data, never code."""

import argparse
from datetime import datetime
import io
import json
from pathlib import Path
import re
import subprocess
import time
import zipfile

import benchmark_overview
import benchmark_preview


class GitHub:
    def __init__(self, repository):
        if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
            raise ValueError('invalid repository')
        self.repository = repository

    def request(self, path, body=None, method=None, raw=False):
        command = ['gh', 'api', f'repos/{self.repository}/{path}']
        if body is not None:
            command += ['--method', method or 'POST', '--input', '-']
        result = subprocess.run(command, input=json.dumps(body).encode() if body is not None else None,
                                stdout=subprocess.PIPE, check=True, timeout=120)
        return result.stdout if raw else json.loads(result.stdout)

    def pages(self, path, key=None):
        rows = []
        for page in range(1, 101):
            separator = '&' if '?' in path else '?'
            response = self.request(f'{path}{separator}per_page=100&page={page}')
            items = response[key] if key else response
            rows.extend(items)
            if len(items) < 100:
                return rows
        raise ValueError('GitHub pagination limit exceeded')


def context(api, run, attempt):
    """Refresh head and attempt before every privileged publication or comment."""
    source = api.request(f'actions/runs/{benchmark_preview.positive_id(run)}')
    if source['run_attempt'] != attempt or source.get('event') != 'pull_request' or source.get('name') != 'Test':
        return None
    pulls = source.get('pull_requests', [])
    if len(pulls) != 1:
        return None
    number = benchmark_preview.positive_id(pulls[0]['number'])
    pull = api.request(f'pulls/{number}')
    return (source, pull) if benchmark_preview.current_pull(source, pull) else None


def ready(api, run, attempt):
    current = context(api, run, attempt)
    if current is None:
        return 'obsolete', None
    source, pull = current
    jobs = api.pages(f'actions/runs/{run}/attempts/{attempt}/jobs', 'jobs')
    completed = benchmark_preview.completed_comparisons(jobs)
    if completed:
        return 'ready', (source, pull, jobs)
    if any(job['name'].startswith('Benchmark comparison (') and job['status'] == 'completed'
           and job['conclusion'] != 'success' for job in jobs):
        return 'finished', None
    return ('finished' if source['status'] == 'completed' else 'waiting'), None


def wait(api, run, attempt, timeout=7200):
    deadline = time.monotonic() + timeout
    while True:
        state, _ = ready(api, run, attempt)
        print(f'Benchmark preview {run}/{attempt}: {state}', flush=True)
        if state != 'waiting':
            return state == 'ready'
        if time.monotonic() >= deadline:
            raise TimeoutError('benchmark preview comparison jobs did not finish in time')
        time.sleep(30)


def artifact_json(raw, name):
    """Read one expected JSON member without extracting paths or uploaded HTML."""
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError('compressed preview artifact exceeds 16 MiB')
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        matches = [entry for entry in archive.infolist() if entry.filename == name + '.json']
        if len(matches) != 1 or matches[0].file_size > 64 * 1024 * 1024:
            raise ValueError('preview needs one bounded JSON report')
        entry = matches[0]
        if (entry.external_attr >> 16) & 0o170000 == 0o120000:
            raise ValueError('preview report cannot be a symlink')
        return archive.read(entry)


def matching_artifacts(artifacts, jobs):
    selected = {}
    for artifact in artifacts:
        name = artifact['name']
        if not name.startswith('benchmark-pr-') or artifact.get('expired'):
            continue
        platform = benchmark_preview.artifact_platform(name)
        job = next((job for job in jobs if job['name'].startswith(f'Benchmark comparison ({platform},')
                    and job['status'] == 'completed' and job['conclusion'] == 'success'), None)
        created = datetime.fromisoformat(artifact['created_at'])
        if not job or not datetime.fromisoformat(job['started_at']) <= created <= datetime.fromisoformat(job['completed_at']):
            continue
        if platform in selected:
            raise ValueError('multiple artifacts for a platform in this attempt')
        selected[platform] = artifact, job['completed_at']
    if set(selected) != {'linux', 'macos'}:
        raise ValueError('both current-attempt platform artifacts are required')
    return selected


def collect(api, run, attempt, root):
    state, current = ready(api, run, attempt)
    if state != 'ready':
        return []
    source, pull, jobs = current
    artifacts = matching_artifacts(api.pages(f'actions/runs/{run}/artifacts', 'artifacts'), jobs)
    downloaded = []
    for platform, (artifact, completed) in sorted(artifacts.items()):
        identity = benchmark_preview.positive_id(artifact['id'])
        if not 0 < artifact['size_in_bytes'] <= 16 * 1024 * 1024:
            raise ValueError('compressed preview artifact exceeds 16 MiB')
        raw = artifact_json(api.request(f'actions/artifacts/{identity}/zip', raw=True), artifact['name'])
        measured = benchmark_preview.commit_sha(json.loads(raw)['head'])
        commit = api.request(f'git/commits/{measured}')
        downloaded.append((platform, raw, commit, completed, artifact))
    # Downloads can take time; refuse to select a superseded PR head afterwards.
    if context(api, run, attempt) is None:
        return []
    return [benchmark_preview.retain(root, raw, source, pull, commit, platform, completed, artifact)
            for platform, raw, commit, completed, artifact in downloaded]


COMMENT_MARKER = '<!-- xff-benchmark-preview -->'


def comment_body(root, paths, repository, site_url):
    if not re.fullmatch(r'https://[A-Za-z0-9.-]+/[A-Za-z0-9_./-]*', site_url):
        raise ValueError('invalid public benchmark URL')
    rows = [COMMENT_MARKER, '## Benchmark preview',
            'Advisory: changes over 15% are warnings, not merge failures. '
            'Positive reference-adjusted change means slower; negative means faster.']
    for path in paths:
        record = json.loads((path / 'report.json').read_text())
        number = record['pull_number']
        run, attempt = record['source']['id'], record['source']['run_attempt']
        relative = path.relative_to(root).as_posix()
        rows += [f'### {record["platform"]}',
                 benchmark_overview.render_markdown(record['tool_comparisons']),
                 f'[Interactive preview]({site_url.rstrip("/")}/{relative}/) | '
                 f'[CI run](https://github.com/{repository}/actions/runs/{run}/attempts/{attempt})',
                 f'Tested merge `{record["head"][:10]}`; PR #{number} head `{record["branch_head"][:10]}`.']
    return '\n\n'.join(rows) + '\n'


def update_comment(api, run, attempt, root, site_url):
    current = context(api, run, attempt)
    if current is None:
        return False
    source, pull = current
    paths = [path.parent for path in benchmark_preview.selected(root, [pull])]
    if len(paths) != 2 or any((json.loads((path / 'report.json').read_text())['source'][key] != source[key])
                             for path in paths for key in ('id', 'run_attempt')):
        return False
    number = pull['number']
    comments = api.pages(f'issues/{number}/comments')
    own = [comment for comment in comments if comment.get('user', {}).get('login') == 'github-actions[bot]'
           and comment.get('user', {}).get('type') == 'Bot' and comment.get('body', '').startswith(COMMENT_MARKER)]
    body = comment_body(root, paths, api.repository, site_url)
    if context(api, run, attempt) is None:
        return False
    if own:
        comment = min(own, key=lambda value: value['id'])
        if comment['body'] != body:
            api.request(f'issues/comments/{benchmark_preview.positive_id(comment["id"])}', {'body': body}, 'PATCH')
    else:
        api.request(f'issues/{number}/comments', {'body': body})
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('wait', 'collect', 'comment'))
    parser.add_argument('--repository', required=True)
    parser.add_argument('--run', type=int, required=True)
    parser.add_argument('--attempt', type=int, required=True)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--site-url')
    args = parser.parse_args()
    api = GitHub(args.repository)
    benchmark_preview.positive_id(args.run)
    benchmark_preview.positive_id(args.attempt)
    if args.operation == 'wait':
        available = wait(api, args.run, args.attempt)
    elif args.root is None:
        parser.error('--root is required')
    elif args.operation == 'collect':
        available = bool(collect(api, args.run, args.attempt, args.root))
    elif not args.site_url:
        parser.error('--site-url is required')
    else:
        available = update_comment(api, args.run, args.attempt, args.root, args.site_url)
    if args.output:
        with args.output.open('a') as stream:
            stream.write(f'available={str(available).lower()}\n')


if __name__ == '__main__':
    main()
