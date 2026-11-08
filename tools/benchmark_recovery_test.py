# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

import base64
import io
import json
import unittest
from unittest import mock
import zipfile

import benchmark_recovery as recovery

SHA = 'a' * 40
OTHER = 'b' * 40


class FakeGitHub:
    repository = 'owner/repo'

    def __init__(self):
        self.pulls = [{'number': 7, 'merged_at': '2026-10-01T12:00:00Z', 'merge_commit_sha': SHA,
                       'base': {'ref': 'main'}}]
        self.runs = []
        self.campaigns = []
        self.artifacts = {}
        self.archives = {}
        self.entries = []

    def request(self, path, raw=False):
        if path.startswith('contents/'):
            return {'encoding': 'base64', 'content': base64.b64encode(json.dumps(self.entries).encode()).decode()}
        return self.archives[path]

    def pages(self, path, key=None):
        if path.startswith('pulls?'):
            return self.pulls
        if 'benchmark_backfill.yml' in path:
            return self.campaigns
        if 'benchmarks.yml' in path:
            return self.runs
        return self.artifacts[path]


class BenchmarkRecoveryTest(unittest.TestCase):
    def test_pr_missing_one_platform_is_reported(self):
        api = FakeGitHub()
        api.entries = [{'source': 'merged', 'commit': SHA, 'platform_name': 'linux / x86_64'}]
        self.assertEqual(recovery.inventory(api, '2026-09-19')[0]['missing'], ['macos'])

    def test_active_run_and_available_reports_are_not_backfilled(self):
        api = FakeGitHub()
        api.runs = [{'id': 1, 'head_sha': SHA, 'event': 'push', 'status': 'in_progress', 'conclusion': None}]
        self.assertEqual(recovery.inventory(api, '2026-09-19')[0]['state'], 'measuring')
        api.runs[0].update(status='completed', conclusion='success', run_started_at='2026-10-01T12:00:00Z')
        api.artifacts['actions/runs/1/artifacts'] = [
            {'name': f'benchmark-report-{platform}-arch', 'created_at': '2026-10-01T13:00:00Z', 'expired': False}
            for platform in recovery.PLATFORMS]
        self.assertEqual(recovery.inventory(api, '2026-09-19')[0]['state'], 'awaiting publication')

    def test_retained_completed_campaign_is_not_dispatched_again(self):
        api = FakeGitHub()
        api.campaigns = [{'id': 2}]
        jobs = [{'id': platform + '-' + SHA, 'revision': {'sha': SHA}, 'platform': platform, 'arch': 'arch'}
                for platform in recovery.PLATFORMS]
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, 'w') as output:
            output.writestr('campaign.json', json.dumps({'jobs': jobs}))
        api.archives['actions/artifacts/3/zip'] = archive.getvalue()
        api.artifacts['actions/runs/2/artifacts'] = [{'id': 3, 'name': 'benchmark-backfill-plan'}]
        for index, job in enumerate(jobs, 4):
            api.artifacts['actions/runs/2/artifacts'].append({'id': index, 'name': 'benchmark-backfill-' + job['id']})
            artifact = io.BytesIO()
            with zipfile.ZipFile(artifact, 'w') as output:
                output.writestr('status.json', json.dumps({SHA: {'status': 'complete'}}))
                output.writestr(f"reports/{SHA}/benchmark-report-{job['platform']}-arch.json", '{}')
            api.archives[f'actions/artifacts/{index}/zip'] = artifact.getvalue()
        self.assertEqual(recovery.inventory(api, '2026-09-19')[0]['state'], 'awaiting publication')

    def test_dispatch_refuses_active_campaign(self):
        api = FakeGitHub()
        with mock.patch.object(recovery.benchmark_preview_ci, 'GitHub', return_value=api), \
                mock.patch.object(recovery, 'inventory', return_value=[{'state': 'needs backfill'}]), \
                mock.patch.object(api, 'pages', return_value=[{'status': 'queued'}]), \
                mock.patch.object(recovery.subprocess, 'run') as run, \
                mock.patch('sys.argv', ['benchmark_recovery', '--repository', 'owner/repo', '--dispatch-next']):
            with self.assertRaisesRegex(ValueError, 'already active'):
                recovery.main()
            run.assert_not_called()

    def test_dispatch_uses_the_targeted_main_workflow(self):
        api = FakeGitHub()
        selected = {'state': 'needs backfill', 'pr': 7, 'commit': SHA}
        with mock.patch.object(recovery.benchmark_preview_ci, 'GitHub', return_value=api), \
                mock.patch.object(recovery, 'inventory', return_value=[selected]), \
                mock.patch.object(api, 'pages', return_value=[]), \
                mock.patch.object(recovery.subprocess, 'run') as run, \
                mock.patch('sys.argv', ['benchmark_recovery', '--repository', 'owner/repo', '--dispatch-next']):
            recovery.main()
            run.assert_called_once_with(['gh', 'workflow', 'run', 'benchmark_backfill.yml', '--repo',
                                         'owner/repo', '--ref', 'main', '-f', 'revisions=' + SHA], check=True)


if __name__ == '__main__':
    unittest.main()
