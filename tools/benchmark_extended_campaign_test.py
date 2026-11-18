# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Keep historical recovery newest-first, bounded and isolated by platform."""

import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import benchmark_extended as suite
import benchmark_extended_campaign as campaign


def revision(letter):
    return {'sha': letter * 40, 'parent': 'a' * 40, 'date': '2026-11-16T00:00:00Z',
            'subject': 'Synthetic historical merge', 'pr': 1}


def complete_entry(platform, sha):
    return {'platform': platform + ' / ' + campaign.PLATFORMS[platform][1], 'commit': sha,
            'ci_history_view': True, 'source': 'merged', 'workload_observations': [
                {'contract': {'suite': suite.DENSE_RECIPE, 'file_counts': list(suite.FILE_COUNTS),
                              'cpu_counts': [1, 3], 'repetitions': 9, 'retained': 7,
                              'machine': campaign.PLATFORMS[platform][1]},
                 'binary': {'state': 'Verified against Retention'}}]}


class BenchmarkExtendedCampaignTest(unittest.TestCase):
    def setUp(self):
        self.revisions = [revision(letter) for letter in ('e', 'd', 'c', 'b')]

    def test_latest_missing_revisions_precede_older_history(self):
        jobs = campaign.select_jobs(self.revisions, set(), ['linux', 'macos'], 2)
        self.assertEqual([job['revision']['sha'] for job in jobs], ['e' * 40] * 2 + ['d' * 40] * 2)
        self.assertEqual([job['platform'] for job in jobs], ['linux', 'macos'] * 2)

    def test_new_linux_jobs_pin_the_historical_runtime_distribution(self):
        plan = self.plan([], oldest='b' * 40, limit=1)
        self.assertEqual([(job['platform'], job['os'], job['arch']) for job in plan['jobs']],
                         [('linux', 'ubuntu-24.04', 'x86_64'), ('macos', 'macos-latest', 'arm64')])

    def test_original_mixed_plan_remains_valid_for_successful_macos_recovery(self):
        plan = self.plan([], oldest='b' * 40, limit=1)
        plan['jobs'][0]['os'] = 'ubuntu-latest'
        plan['identity'] = campaign.identity({key: value for key, value in plan.items() if key != 'identity'})
        frozen = copy.deepcopy(plan)
        self.assertEqual(campaign.validate_job(plan, plan['jobs'][1]['id'], 'owner/repo', plan['source_head']),
                         plan['jobs'][1])
        self.assertEqual(plan, frozen)

    def test_other_runner_or_architecture_is_not_accepted_as_a_legacy_plan(self):
        for runner, arch in [('ubuntu-26.04', 'x86_64'), ('ubuntu-latest', 'arm64'), ('macos-latest', 'x86_64')]:
            plan = self.plan([], oldest='b' * 40, limit=1)
            plan['jobs'][0].update(os=runner, arch=arch)
            plan['identity'] = campaign.identity({key: value for key, value in plan.items() if key != 'identity'})
            with self.assertRaisesRegex(ValueError, 'wrong historical runner'):
                campaign.validate_plan(plan)

    def test_completed_platform_does_not_repeat_when_its_partner_failed(self):
        jobs = campaign.select_jobs(self.revisions, {('linux', 'e' * 40)}, ['linux', 'macos'], 1)
        self.assertEqual([(job['platform'], job['revision']['sha']) for job in jobs], [('macos', 'e' * 40)])
        done = {(platform, 'e' * 40) for platform in campaign.PLATFORMS}
        jobs = campaign.select_jobs(self.revisions, done, ['linux', 'macos'], 1)
        self.assertEqual({job['revision']['sha'] for job in jobs}, {'d' * 40})

    def test_batch_size_counts_revisions_and_stays_below_matrix_limit(self):
        revisions = [dict(revision('b'), sha=f'{index:040x}') for index in range(150)]
        self.assertEqual(len(campaign.select_jobs(revisions, set(), list(campaign.PLATFORMS), 100)), 200)
        for count in (0, 101, True):
            with self.assertRaises(ValueError):
                campaign.select_jobs(revisions, set(), ['linux'], count)

    def test_only_complete_dense_ci_can_satisfy_the_history(self):
        entry = complete_entry('linux', 'e' * 40)
        self.assertEqual(campaign.completed([entry]), {('linux', 'e' * 40)})
        mutations = [lambda r: r.update(local=True), lambda r: r.update(source='pr-1'),
                     lambda r: r.update(ci_history_view=False),
                     lambda r: r['workload_observations'][0]['contract'].update(suite=suite.RECIPE),
                     lambda r: r['workload_observations'][0]['contract']['file_counts'].pop(),
                     lambda r: r['workload_observations'][0]['contract'].update(machine='wrong'),
                     lambda r: r['workload_observations'][0]['binary'].update(state='Not verified')]
        for mutate in mutations:
            changed = copy.deepcopy(entry)
            mutate(changed)
            self.assertEqual(campaign.completed([changed]), set())

    def plan(self, catalog, **kwargs):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'catalog.json').write_text(json.dumps(catalog))
            with mock.patch.object(campaign, 'mainline', return_value=self.revisions):
                return campaign.make_plan(root, root, 'owner/repo', **kwargs)

    def test_collection_boundary_covers_missing_merges_between_retained_versions(self):
        catalog = [{'platform': 'linux / x86_64', 'commit': 'c' * 40, 'source': 'merged'},
                   {'platform': 'macos / arm64', 'commit': 'c' * 40, 'source': 'merged'}]
        plan = self.plan(catalog)
        self.assertEqual(plan['oldest'], 'c' * 40)
        self.assertEqual([job['revision']['sha'] for job in plan['jobs']],
                         [letter * 40 for letter in ('e', 'e', 'd', 'd', 'c', 'c')])

    def test_unknown_boundary_is_explicit_and_completed_batches_end_cleanly(self):
        with self.assertRaisesRegex(ValueError, 'no retained CI boundary'):
            self.plan([])
        with self.assertRaisesRegex(ValueError, 'not on the selected main'):
            self.plan([], oldest='f' * 40)
        catalog = [complete_entry(platform, r['sha']) for r in self.revisions for platform in campaign.PLATFORMS]
        plan = self.plan(catalog)
        self.assertEqual(plan['jobs'], [])
        campaign.validate_plan(plan)

    def test_platform_subset_and_tampered_plan_are_rejected_or_bound(self):
        plan = self.plan([], oldest='b' * 40, platforms=['macos'], limit=1)
        self.assertEqual([job['platform'] for job in plan['jobs']], ['macos'])
        plan['jobs'][0]['revision']['parent'] = 'f' * 40
        with self.assertRaisesRegex(ValueError, 'manifest identity|campaign identity'):
            campaign.validate_plan(plan)
        for platforms in ([], ['linux', 'linux'], ['windows']):
            with self.assertRaises(ValueError):
                campaign.select_jobs(self.revisions, set(), platforms, 10)


if __name__ == '__main__':
    unittest.main()
