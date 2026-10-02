#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Import a completed local batch into a Pages checkout without replacing CI data."""

import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import tempfile

import benchmark_campaign as campaign


def retain(root, directory):
    batch = json.loads((directory / 'batch.json').read_text())
    status = json.loads((directory / 'status.json').read_text())
    contract = batch['contract']
    require = campaign.require
    require(batch['identity'] == campaign.identity(contract), 'batch identity changed')
    require(contract['schema'] == 1 and contract['purpose'] == 'local-addition'
            and contract['replacement_target'] is None, 'expected an additional local series')
    require(re.fullmatch('[a-z0-9][a-z0-9-]*', contract['series'])
            and not contract['series'].startswith('github-ci-'), 'invalid local series name')
    require(contract['platform'] in campaign.PLATFORMS, 'unsupported local platform')
    for key in ('files', 'cpus'):
        values = contract[key]
        require(values and all(type(value) is int and value > 0 for value in values)
                and len(values) == len(set(values)), 'invalid ' + key + ' grid')
    require(contract['depth'] > 0 and 1 <= contract['retained'] <= contract['repetitions'], 'invalid sample contract')
    require(contract['driver'], 'missing measurement driver identity')
    revisions = contract['revisions']
    shas = [revision['sha'] for revision in revisions]
    require(shas and len(shas) == len(set(shas)) and set(shas) == set(status), 'incomplete revision set')
    arch = contract['environment']['machine'].lower()
    require(re.fullmatch('[a-z0-9_]+', arch), 'invalid machine architecture')
    expected_paths = {directory / 'reports' / sha / f"benchmark-report-{contract['platform']}-{arch}.json"
                      for sha in shas}
    require(set(directory.glob('reports/*/*.json')) == expected_paths, 'unexpected report set')
    tools = {'xff': {'path': 'xff'}, 'find': {}, 'rg': {}, 'fzf': {}}
    tasks = {name: sorted(participants) for name, _, participants in
             campaign.compare.scenarios(Path('.'), [], tools)}
    payloads = {'batch.json': batch, 'status.json': status}
    for revision in revisions:
        sha = revision['sha']
        require(re.fullmatch('[0-9a-f]{40}', sha), 'invalid revision')
        require(datetime.fromisoformat(revision['date']).tzinfo is not None, 'revision date needs a timezone')
        require(status[sha]['status'] == 'complete', 'incomplete local batch')
        path = directory / 'reports' / sha / f"benchmark-report-{contract['platform']}-{arch}.json"
        record = json.loads(path.read_text())
        require('source' not in record, 'local observations cannot claim CI workflow provenance')
        started, completed = (datetime.fromisoformat(record[key]) for key in ('started_at', 'completed_at'))
        require(started.tzinfo is not None and completed.tzinfo is not None and completed >= started,
                'invalid measurement timestamps')
        campaign.validate_measurements(record, batch, revision, tasks)
        payloads[sha + '/report.json'] = record
    destination = root / 'local' / contract['series'] / batch['identity']
    if destination.exists():
        for name, value in payloads.items():
            path = destination / name
            require(path.is_file() and json.loads(path.read_text()) == value, 'existing local batch cannot change')
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.local-', dir=destination.parent) as temporary:
        stage = Path(temporary) / 'complete'
        for name, value in payloads.items():
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            campaign.backfill.write_json(path, value)
        stage.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True, help='benchmarks directory in a Pages checkout')
    parser.add_argument('--batch', type=Path, required=True, help='completed local backfill directory')
    args = parser.parse_args()
    print(retain(args.root, args.batch))


if __name__ == '__main__':
    main()
