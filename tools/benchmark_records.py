# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Retained benchmark paths and preference, without changing raw observations."""

from datetime import datetime, timezone


def paths(root):
    return sorted([*root.glob('runs/*/*/**/report.json'),
                   *root.glob('backfills/*/*/*/*/report.json')])


def is_backfill(record):
    return record.get('kind') == 'backfill' and record.get('purpose') == 'ci-replacement'


def platform_key(record):
    if record.get('platform'):
        return record['platform']
    contract = record.get('tool_comparisons', record).get('contract', {})
    value = contract.get('platform', '').lower()
    if value.startswith('linux'):
        return 'linux'
    if value.startswith(('macos', 'darwin')):
        return 'macos'
    return ''


def preference(record):
    source = record['source']
    return (is_backfill(record), source['created_at'], int(source['id']), int(source['run_attempt']))


def reference_time(record):
    if is_backfill(record):
        value = record['revision']['date']
    else:
        value = record['source'].get('reference_time', record['source']['created_at'])
    return datetime.fromisoformat(value).astimezone(timezone.utc).isoformat().replace('+00:00', 'Z')
