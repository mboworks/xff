# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Bind restored builds to the frozen historical job without running benchmarks."""

import copy
import json
import unittest
from unittest import mock

import benchmark_extended_backfill as backfill
import benchmark_extended_campaign as campaign
import benchmark_extended_records as records
from benchmark_extended_test_support import ExtendedFixture
from test_paths import repository_file


class BenchmarkExtendedBackfillTest(unittest.TestCase):
    def setUp(self):
        self.fixture = ExtendedFixture()
        self.addCleanup(self.fixture.close)
        self.root = self.fixture.root
        (self.root / 'catalog.json').write_text('[]')
        self.revisions = [dict(item, parent=self.fixture.revisions[-1]['sha']) for item in self.fixture.revisions]
        with mock.patch.object(campaign, 'mainline', return_value=self.revisions):
            self.plan = campaign.make_plan(self.root, self.root, 'owner/repo', oldest=self.revisions[0]['sha'])
        self.job = self.plan['jobs'][0]
        self.build_root = self.root / 'retained'
        self.build_root.mkdir()
        (self.build_root / 'campaign.json').write_text(json.dumps(self.plan))
        (self.build_root / 'revisions.json').write_text(json.dumps(self.fixture.revisions))

    def collect(self, **changes):
        options = dict(build_root=self.build_root, root=self.root / 'output', plan=self.plan,
                       job_id=self.job['id'], repository='owner/repo', source_head=self.plan['source_head'],
                       run_id=12, attempt=2, plan_attempt=1)
        options.update(changes)
        return backfill.collect(**options)

    def test_collection_binds_historical_head_and_original_plan_attempt(self):
        with mock.patch.object(backfill.platform, 'machine', return_value='x86_64'), \
                mock.patch.object(backfill.ci, 'collect', return_value='result') as collect:
            self.assertEqual(self.collect(), 'result')
        identity = collect.call_args.args[3]
        self.assertEqual(identity['campaign_identity'], self.plan['identity'])
        self.assertEqual(identity['head_sha'], self.revisions[0]['sha'])
        self.assertEqual(identity['workflow_head_sha'], self.plan['source_head'])
        self.assertEqual(identity['campaign_attempt'], 1)
        self.assertEqual(identity['build_artifact'], 'benchmark-extended-backfill-build-' + self.job['id'] + '-2')

    def test_wrong_source_repository_job_architecture_or_retained_plan_never_measures(self):
        with mock.patch.object(backfill.platform, 'machine', return_value='x86_64'), \
                mock.patch.object(backfill.ci, 'collect') as collect:
            for changes in ({'source_head': 'f' * 40}, {'repository': 'other/repo'}, {'job_id': 'unknown'}):
                with self.subTest(changes=changes), self.assertRaises(ValueError):
                    self.collect(**changes)
            with mock.patch.object(backfill.platform, 'machine', return_value='wrong'), self.assertRaises(ValueError):
                self.collect()
            altered = copy.deepcopy(self.plan)
            altered['oldest'] = 'f' * 40
            (self.build_root / 'campaign.json').write_text(json.dumps(altered))
            with self.assertRaisesRegex(ValueError, 'retained build campaign changed'):
                self.collect()
            collect.assert_not_called()

    def test_wrong_retained_first_parent_never_measures(self):
        changed = copy.deepcopy(self.fixture.revisions)
        changed[-1]['sha'] = 'f' * 40
        (self.build_root / 'revisions.json').write_text(json.dumps(changed))
        with mock.patch.object(backfill.platform, 'machine', return_value='x86_64'), \
                mock.patch.object(backfill.ci, 'collect') as collect, self.assertRaisesRegex(ValueError, 'retained builds'):
            self.collect()
        collect.assert_not_called()

    def test_build_freezes_helper_before_endpoints_and_stops_all_build_servers(self):
        artifact = self.root / 'helper'
        artifact.write_bytes(b'synthetic executable')
        events = []
        real_copy = backfill.shutil.copy2
        def copy(source, destination):
            events.append('copy helper')
            return real_copy(source, destination)
        with mock.patch.object(backfill.build, 'git', return_value=self.plan['source_head']), \
                mock.patch.object(backfill.platform, 'machine', return_value='x86_64'), \
                mock.patch.object(campaign, 'mainline', return_value=self.revisions), \
                mock.patch.object(backfill.subprocess, 'run') as run, \
                mock.patch.object(backfill.subprocess, 'check_output', return_value=str(artifact)), \
                mock.patch.object(backfill.shutil, 'copy2', side_effect=copy), \
                mock.patch.object(backfill.ci, 'build_endpoint', side_effect=lambda *a: events.append(a[2])):
            backfill.freeze(self.root, self.root / 'build', self.plan, self.job['id'], self.root / 'cache')
        self.assertEqual(events, ['copy helper', 'head', 'base'])
        shutdowns = [call for call in run.call_args_list if call.args[0][-1] == 'shutdown']
        self.assertEqual(len(shutdowns), 3)

    def test_build_failure_preserves_checkout_and_stops_servers(self):
        artifact = self.root / 'helper'
        artifact.write_bytes(b'synthetic executable')
        with mock.patch.object(backfill.build, 'git', return_value=self.plan['source_head']), \
                mock.patch.object(backfill.platform, 'machine', return_value='x86_64'), \
                mock.patch.object(campaign, 'mainline', return_value=self.revisions), \
                mock.patch.object(backfill.subprocess, 'run') as run, \
                mock.patch.object(backfill.subprocess, 'check_output', return_value=str(artifact)), \
                mock.patch.object(backfill.ci, 'build_endpoint', side_effect=ValueError('synthetic failed build')), \
                self.assertRaisesRegex(ValueError, 'synthetic failed build'):
            backfill.freeze(self.root, self.root / 'build', self.plan, self.job['id'], self.root / 'cache')
        shutdowns = [call for call in run.call_args_list if call.args[0][-1] == 'shutdown']
        self.assertEqual(len(shutdowns), 2)
        self.assertFalse(any('remove' in call.args[0] for call in run.call_args_list))

    def test_unpublished_checkpoint_satisfies_only_its_validated_platform(self):
        self.fixture.contract['recipe'] = backfill.ci.suite.DENSE_RECIPE
        value = self.fixture.record(origin='ci', comparison='parent')
        site = self.root / 'site'
        records.retain(site, value, self.fixture.source())
        self.assertFalse((site / 'catalog.json').exists())
        self.assertEqual(campaign.checkpoints(site, records), {('linux', self.revisions[0]['sha'])})
        selected = campaign.select_jobs(self.revisions[:1], campaign.checkpoints(site, records), ['linux', 'macos'], 1)
        self.assertEqual([job['platform'] for job in selected], ['macos'])

    def test_workflow_preserves_report_root_and_attempt_specific_build_bytes(self):
        workflow = repository_file('.github/workflows/benchmark_extended_backfill.yml').read_text()
        self.assertIn('max-parallel: 8', workflow)
        self.assertIn("if: needs.plan.outputs.count != '0'", workflow)
        self.assertIn('--extended-only', workflow)
        self.assertIn('benchmark-extended-backfill-plan-${{ needs.plan.outputs.attempt }}', workflow)
        self.assertIn('benchmark-extended-backfill-build-${{ matrix.id }}-${{ github.run_attempt }}', workflow)
        self.assertIn('benchmark-extended-backfill-report-${{ matrix.id }}-${{ github.run_attempt }}', workflow)
        upload = workflow.split('name: benchmark-extended-backfill-report-', 1)[1].split('retention-days:', 1)[0]
        self.assertNotIn('${{ runner.temp }}/historical-runtime', upload)
        self.assertIn('${{ runner.temp }}/extended-measurements/observation.json', upload)
        self.assertIn('${{ runner.temp }}/extended-measurements/runtime', upload)


if __name__ == '__main__':
    unittest.main()
