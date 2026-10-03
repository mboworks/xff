# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Validate and retain PR benchmark observations without executing artifact content."""

import hashlib
import html
import json
from pathlib import Path
import re

import benchmark_compare


def positive_id(value):
    if type(value) is not int or value < 1:
        raise ValueError('positive integer identity required')
    return value


def commit_sha(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{40}', value):
        raise ValueError('invalid commit identity')
    return value


def current_pull(source, pull):
    """Both arguments are trusted GitHub API metadata, never artifact claims."""
    if source.get('event') != 'pull_request' or pull.get('state') != 'open':
        return False
    head = pull.get('head') or {}
    base = pull.get('base') or {}
    head_repository = (head.get('repo') or {}).get('id')
    base_repository = (base.get('repo') or {}).get('id')
    return (head.get('sha') == source.get('head_sha')
            and type(head_repository) is int and head_repository > 0
            and type(base_repository) is int and base_repository > 0
            and head_repository == (source.get('head_repository') or {}).get('id')
            and base_repository == (source.get('repository') or {}).get('id'))


def completed_comparisons(jobs):
    """Release previews after both platform comparison jobs succeed."""
    completed = {}
    for job in jobs:
        match = re.fullmatch(r'Benchmark comparison \((linux|macos), best \d+ of \d+\)', job['name'])
        if match and job['status'] == 'completed' and job['conclusion'] == 'success':
            platform = match[1]
            if platform in completed:
                raise ValueError('ambiguous platform comparison jobs')
            completed[platform] = job['completed_at']
    return completed if set(completed) == {'linux', 'macos'} else None


def artifact_platform(name):
    match = re.fullmatch(r'benchmark-pr-(linux|macos)-(x86_64|arm64|aarch64)', name)
    if not match:
        raise ValueError('unexpected PR benchmark artifact name')
    return match[1]


def retain(root, raw, source, pull, tested_commit, platform, completed_at, artifact=None):
    """Retain exact artifact bytes and a metadata-enriched record in an immutable location."""
    if not current_pull(source, pull):
        raise ValueError('PR is closed or its head no longer matches this workflow')
    number = positive_id(pull['number'])
    run = positive_id(source['id'])
    attempt = positive_id(source['run_attempt'])
    branch_head = commit_sha(source['head_sha'])
    record = json.loads(raw)
    measured_head = commit_sha(record['head'])
    if measured_head != commit_sha(tested_commit['sha']):
        raise ValueError('artifact does not match the verified tested commit')
    parents = [commit_sha(parent['sha']) for parent in tested_commit['parents']]
    if len(parents) != 2 or parents[1] != branch_head:
        raise ValueError('tested merge commit does not include the workflow branch head')
    if platform not in {'linux', 'macos'}:
        raise ValueError('invalid preview platform')
    if not isinstance(completed_at, str) or not completed_at:
        raise ValueError('measurement completion time required')
    report = record['tool_comparisons']
    # Reuse the complete sample/correctness checks; uploaded HTML is never used.
    benchmark_compare.render(report)
    record.update(kind='pr-preview', platform=platform, pull_number=number, branch_head=branch_head,
                  base=parents[0], completed_at=completed_at, artifact_sha256=hashlib.sha256(raw).hexdigest(),
                  source={key: source[key] for key in ('id', 'run_attempt', 'created_at', 'head_sha',
                                                     'head_branch', 'event')})
    record['source']['pull_requests'] = [{'number': number}]
    record['source']['reference_time'] = tested_commit['committer']['date']
    if artifact is not None:
        record['artifact'] = {key: artifact[key] for key in ('id', 'name', 'created_at', 'size_in_bytes')}
        if artifact.get('digest'):
            record['artifact']['digest'] = artifact['digest']
    destination = Path(root) / 'previews' / str(number) / str(run) / str(attempt) / platform
    normalized = json.dumps(record, indent=2, allow_nan=False) + '\n'
    if destination.exists():
        if ((destination / 'artifact.json').read_bytes() != raw or
                (destination / 'report.json').read_text() != normalized):
            raise ValueError('an existing preview run attempt cannot change')
        return destination
    destination.mkdir(parents=True)
    (destination / 'artifact.json').write_bytes(raw)
    (destination / 'report.json').write_text(normalized)
    return destination


def selected(root, pulls):
    """Expose only the newest compatible artifact for each current PR head/platform."""
    by_number = {pull['number']: pull for pull in pulls if pull.get('state') == 'open'}
    result = {}
    for path in Path(root).glob('previews/*/*/*/*/report.json'):
        record = json.loads(path.read_text())
        pull = by_number.get(record['pull_number'])
        if not pull or pull['head']['sha'] != record['branch_head']:
            continue
        key = record['pull_number'], record['platform']
        source = record['source']
        rank = source['created_at'], source['id'], source['run_attempt']
        if key not in result or rank > result[key][0]:
            result[key] = rank, path
    return [path for _, path in sorted(result.values(), key=lambda item: item[0])]


def render_report(record, repository, history_href):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('invalid repository')
    number = positive_id(record['pull_number'])
    head = commit_sha(record['branch_head'])
    measured = commit_sha(record['head'])
    base = commit_sha(record['base'])
    run = positive_id(record['source']['id'])
    prefix = 'https://github.com/' + repository
    title = f'PR {number} benchmark preview - {record["platform"]}'
    provenance = (f'<p><a href="{html.escape(history_href, quote=True)}">Benchmark history</a> | '
                  f'<a href="{prefix}/pull/{number}">PR #{number}</a> | '
                  f'<a href="{prefix}/actions/runs/{run}">CI run</a> | '
                  '<a href="artifact.json">Original observations</a></p>'
                  '<p>PR preview. Benchmark jobs passed; other CI checks may still be running.</p>'
                  f'<p>Branch head: <a href="{prefix}/commit/{head}">{head[:10]}</a>; '
                  f'tested merge: <a href="{prefix}/commit/{measured}">{measured[:10]}</a>; '
                  f'merged with base <a href="{prefix}/commit/{base}">{base[:10]}</a>.</p>')
    return ('<!doctype html><html lang="en"><head><meta charset="utf-8"><title>' + html.escape(title) + '</title>'
            '<style>body{font:16px system-ui;margin:2rem}table{border-collapse:collapse}'
            'td,th{border:1px solid #aaa;padding:.4rem;text-align:right}'
            'td:first-child,th:first-child{text-align:left}</style></head><body><h1>' + html.escape(title) + '</h1>'
            + provenance + benchmark_compare.render(record['tool_comparisons']) + '</body></html>\n')
