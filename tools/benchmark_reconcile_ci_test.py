# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Recover independently valid reports without trusting workflow or artifact claims."""

import copy
from datetime import datetime, timezone
import gzip
import io
import json
import subprocess
import unittest
from unittest import mock
import zipfile

import benchmark_extended_records as records
import benchmark_extended_campaign as campaign
import benchmark_reconcile_ci as reconcile
from benchmark_extended_test_support import ExtendedFixture


def archive(value):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as zipped:
        zipped.writestr('observation.json', json.dumps(value))
    return output.getvalue()


def plan_archive(plan):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as zipped:
        zipped.writestr('campaign.json', json.dumps(plan))
    return output.getvalue()


class FakeAPI:
    def __init__(self, fixture, value):
        self.repository = 'owner/repo'
        self.source = fixture.source()
        self.commit = {'parents': [{'sha': self.source['base_sha']}],
                       'commit': {'committer': {'date': '2026-11-01T00:00:00Z'}}}
        self.items = [{'name': 'benchmark-extended-report-linux', 'id': 101,
                       'created_at': '2026-11-08T00:00:00Z', 'expired': False}]
        self.raw = {101: archive(value)}
        self.requests = []

    def request(self, path, raw=False):
        self.requests.append(path)
        if path.startswith('actions/artifacts/'):
            return self.raw[int(path.split('/')[2])]
        if path == 'actions/runs/12/attempts/1':
            return copy.deepcopy(self.source)
        if path.startswith('commits/'):
            return copy.deepcopy(self.commit)
        raise AssertionError(path)

    def pages(self, path, key=None):
        self.requests.append(path)
        if path == 'actions/runs/12/artifacts' and key == 'artifacts':
            return copy.deepcopy(self.items)
        raise AssertionError(path)


class BenchmarkReconcileCiTest(unittest.TestCase):
    def setUp(self):
        self.fixture = ExtendedFixture()
        self.addCleanup(self.fixture.close)
        self.record = self.fixture.record(origin='ci', comparison='parent')
        self.api = FakeAPI(self.fixture, self.record)
        self.root = self.fixture.root / 'site'

    def test_failed_workflow_keeps_valid_extended_head_even_when_core_retention_fails(self):
        with mock.patch.object(reconcile, 'retain_measurement', side_effect=ValueError('no core agreement')):
            self.assertTrue(reconcile.retain_benchmarks(self.api, self.root, self.api.source))
        path = self.root / 'extended/observations' / self.record['identity'] / 'report.json'
        saved = records.read(path)
        self.assertEqual(saved['publication_source']['status'], 'completed')
        self.assertEqual(saved['publication_source']['base_sha'], self.fixture.revisions[-1]['sha'])
        self.assertEqual(len(list(self.root.glob('extended/observations/*/report.json'))), 1)

    def test_successful_earlier_attempt_is_preserved_and_repeated_import_needs_no_download(self):
        latest = dict(self.api.source, run_attempt=2)
        self.assertTrue(reconcile.retain_extended(self.api, self.root, latest))
        self.assertIn('actions/runs/12/attempts/1', self.api.requests)
        marker = self.root / 'extended/collections/12/1/linux.json'
        self.assertTrue(marker.is_file())
        path = self.root / 'extended/observations' / self.record['identity'] / 'report.json'
        for file in (marker, path):
            file.with_suffix('.json.gz').write_bytes(gzip.compress(file.read_bytes(), mtime=0))
            file.unlink()
        self.api.requests.clear()
        self.assertFalse(reconcile.retain_extended(self.api, self.root, latest))
        self.assertEqual(self.api.requests, ['actions/runs/12/artifacts'])
        self.assertEqual(records.read(path)['publication_source']['run_attempt'], 1)

    def test_invalid_platform_does_not_discard_other_valid_platform(self):
        self.api.items.insert(0, dict(self.api.items[0], id=102, name='benchmark-extended-report-macos'))
        self.api.raw[102] = archive(self.record)
        self.assertTrue(reconcile.retain_extended(self.api, self.root, self.api.source))
        self.assertTrue((self.root / 'extended/collections/12/1/linux.json').exists())
        self.assertFalse((self.root / 'extended/collections/12/1/macos.json').exists())

    def test_wrong_parent_head_repository_attempt_and_untrusted_workflow_are_rejected(self):
        for key, replacement in (('base_sha', 'f' * 40), ('head_sha', 'f' * 40),
                                 ('repository', {'full_name': 'other/repo'}),
                                 ('run_attempt', 0), ('event', 'pull_request'), ('status', 'in_progress')):
            with self.subTest(key=key):
                source = dict(self.api.source, **{key: replacement})
                api = FakeAPI(self.fixture, self.record)
                if key == 'base_sha':
                    api.commit['parents'][0]['sha'] = replacement
                self.assertFalse(reconcile.retain_extended(api, self.root, source))
                self.assertFalse(list(self.root.glob('extended/observations/*/report.json')))

    def test_invalid_extended_artifact_does_not_lose_valid_core_result(self):
        self.api.raw[101] = b'not a zip'
        with mock.patch.object(reconcile, 'retain_measurement', return_value=True) as core:
            self.assertTrue(reconcile.retain_benchmarks(self.api, self.root, self.api.source))
        core.assert_called_once()

    def test_extract_rejects_escape_and_symlinks(self):
        for name, mode in (('../outside', 0), ('/outside', 0), ('dir\\outside', 0), ('link', 0o120777)):
            with self.subTest(name=name):
                output = io.BytesIO()
                with zipfile.ZipFile(output, 'w') as zipped:
                    info = zipfile.ZipInfo(name)
                    info.external_attr = mode << 16
                    zipped.writestr(info, 'data')
                with self.assertRaises(ValueError):
                    reconcile.extract(output.getvalue(), self.fixture.root / 'extracted')

    def test_settled_inventory_is_cached_but_retry_and_recent_completion_are_rechecked(self):
        source = dict(self.api.source, updated_at='2026-11-01T00:00:00Z')
        self.assertEqual(reconcile.run_artifacts(self.api, self.root, source), self.api.items)
        self.api.requests.clear()
        self.assertEqual(reconcile.run_artifacts(self.api, self.root, source), self.api.items)
        self.assertEqual(self.api.requests, [])
        reconcile.run_artifacts(self.api, self.root, dict(source, run_attempt=2))
        self.assertEqual(self.api.requests, ['actions/runs/12/artifacts'])
        self.api.requests.clear()
        recent = dict(source, run_attempt=3, updated_at=datetime.now(timezone.utc).isoformat())
        reconcile.run_artifacts(self.api, self.root, recent)
        reconcile.run_artifacts(self.api, self.root, recent)
        self.assertEqual(self.api.requests, ['actions/runs/12/artifacts', 'actions/runs/12/artifacts'])

    def test_expired_cached_artifacts_are_not_downloaded_and_old_cache_is_refreshed(self):
        source = dict(self.api.source, updated_at='2026-11-01T00:00:00Z')
        self.api.items[0]['expires_at'] = '2000-01-01T00:00:00Z'
        reconcile.run_artifacts(self.api, self.root, source)
        self.api.requests.clear()
        with mock.patch.object(reconcile, 'retain_measurement', return_value=False) as core, \
                mock.patch.object(reconcile, 'retain_extended', return_value=False) as extended:
            self.assertFalse(reconcile.retain_benchmarks(self.api, self.root, source))
        self.assertEqual(core.call_args.args[-1], [])
        self.assertEqual(extended.call_args.args[-1], [])
        self.assertEqual(self.api.requests, [])
        marker = self.root / 'reconciliation/12/1/artifacts.json'
        cached = json.loads(marker.read_text())
        cached['checked_at'] = '2000-01-01T00:00:00Z'
        marker.write_text(json.dumps(cached))
        reconcile.run_artifacts(self.api, self.root, source)
        self.assertEqual(self.api.requests, ['actions/runs/12/artifacts'])

    def historical(self):
        self.fixture.contract['recipe'] = records.suite.DENSE_RECIPE
        value = self.fixture.record(origin='ci', comparison='parent')
        (self.fixture.root / 'catalog.json').write_text('[]')
        revision = {key: value['revision'][key] for key in ('sha', 'date', 'subject', 'pr')}
        revision['parent'] = value['contract']['anchor']['sha']
        driver = dict(revision, sha='f' * 40, parent=revision['sha'])
        with mock.patch.object(campaign, 'mainline', return_value=[driver, revision]):
            plan = campaign.make_plan(self.fixture.root, self.fixture.root, 'owner/repo', oldest=revision['sha'])
        job_id = 'linux-' + revision['sha']
        value['collection'].update(campaign_identity=plan['identity'], campaign_job=job_id,
                                   campaign_attempt=1, workflow_head_sha=driver['sha'])
        value['identity'] = records.observation_identity(value)
        self.api.source.update(name=campaign.WORKFLOW, path=campaign.WORKFLOW_PATH,
                               event='workflow_dispatch', head_sha=driver['sha'])
        self.api.items = [{'name': 'benchmark-extended-backfill-report-' + job_id + '-1', 'id': 101,
                           'created_at': '2026-11-08T00:00:00Z', 'expired': False},
                          {'name': 'benchmark-extended-backfill-plan-1', 'id': 102,
                           'created_at': '2026-11-08T00:00:00Z', 'expired': False}]
        self.api.raw = {101: archive(value), 102: plan_archive(plan)}
        original = self.api.request
        def request(path, raw=False):
            if path.startswith('compare/'):
                return {'status': 'ahead'}
            return original(path, raw)
        self.api.request = request
        return value, plan

    def test_historical_batch_keeps_successful_previous_attempt_before_publication(self):
        value, _ = self.historical()
        self.assertTrue(reconcile.retain_extended_backfill(self.api, self.root, dict(self.api.source, run_attempt=2)))
        retained = records.read(self.root / 'extended/observations' / value['identity'] / 'report.json')
        self.assertEqual(retained['publication_source']['head_sha'], 'f' * 40)
        view = records.view_report(retained, 'traditional')
        self.assertEqual(view['source']['head_sha'], value['revision']['sha'])
        self.assertEqual(view['source']['workflow_head_sha'], 'f' * 40)
        self.assertEqual(records.benchmark_provenance.describe(view)['state'], 'Verified against Retention')
        self.assertEqual(campaign.checkpoints(self.root, records), {('linux', value['revision']['sha'])})
        self.assertFalse(reconcile.retain_extended_backfill(self.api, self.root, dict(self.api.source, run_attempt=2)))

    def test_historical_plan_job_parent_or_workflow_cannot_be_substituted(self):
        value, plan = self.historical()
        for change in ('plan', 'parent', 'workflow', 'artifact', 'missing-plan'):
            with self.subTest(change=change):
                modified = copy.deepcopy(plan)
                source = dict(self.api.source)
                if change == 'plan':
                    modified['jobs'][0]['revision']['sha'] = 'e' * 40
                    modified['identity'] = campaign.identity({k: v for k, v in modified.items() if k != 'identity'})
                elif change == 'parent':
                    self.api.commit['parents'][0]['sha'] = 'e' * 40
                elif change == 'workflow':
                    source['event'] = 'pull_request'
                elif change == 'artifact':
                    self.api.items[0]['name'] = 'benchmark-extended-backfill-report-linux-' + 'e' * 40 + '-1'
                elif change == 'missing-plan':
                    self.api.items = self.api.items[:1]
                self.api.raw[102] = plan_archive(modified)
                self.assertFalse(reconcile.retain_extended_backfill(self.api, self.root, source))
                self.assertFalse(list(self.root.glob('extended/observations/*/report.json')))
                value, plan = self.historical()
                self.api.commit['parents'][0]['sha'] = value['contract']['anchor']['sha']

    def test_recovery_transport_failure_aborts_planning_instead_of_repeating_measurements(self):
        self.historical()
        original = self.api.request
        def request(path, raw=False):
            if path.startswith('actions/artifacts/'):
                raise subprocess.CalledProcessError(1, ['gh', 'api'], stderr=b'transient HTTP 503')
            return original(path, raw)
        self.api.request = request
        with self.assertRaises(subprocess.CalledProcessError):
            reconcile.retain_extended_backfill(self.api, self.root, self.api.source, strict_network=True)
        self.assertFalse(list(self.root.glob('extended/observations/*/report.json')))

    def test_unrelated_historical_commit_cannot_enter_ci_history(self):
        self.historical()
        original = self.api.request
        self.api.request = lambda path, raw=False: ({'status': 'diverged'} if path.startswith('compare/') else
                                                     original(path, raw))
        self.assertFalse(reconcile.retain_extended_backfill(self.api, self.root, self.api.source))
        self.assertFalse(list(self.root.glob('extended/observations/*/report.json')))


if __name__ == '__main__':
    unittest.main()
