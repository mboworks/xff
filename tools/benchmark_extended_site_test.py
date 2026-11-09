# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Keep extended observations separate, measured, discoverable and reproducible."""

import gzip
import copy
import json
import unittest

import benchmark_extended as suite
import benchmark_extended_records as records
import benchmark_extended_site as site
from benchmark_extended_test_support import ExtendedFixture


class BenchmarkExtendedSiteTest(unittest.TestCase):
    def setUp(self):
        self.fixture = ExtendedFixture()
        self.addCleanup(self.fixture.close)
        self.root = self.fixture.root / 'site'
        self.root.mkdir()
        (self.root / 'index.html').write_text('<!doctype html><html><body><h1>Benchmarks</h1></body></html>')
        self.core = {'platform': 'CI core linux', 'commit': 'c' * 40, 'date': '2026-11-01T00:00:00Z',
                     'report': 'runs/core/linux/', 'figures': 'runs/core/linux/landscape.json',
                     'label': 'Core fixture', 'source': 'merged'}
        (self.root / 'catalog.json').write_text(json.dumps([self.core]))

    def test_both_views_join_existing_browser_without_replacing_core_or_inventing_points(self):
        value = self.fixture.record()
        records.retain(self.root, value)
        self.assertEqual(site.publish(self.root), 2)
        catalog = records.read(self.root / 'catalog.json')
        self.assertEqual(catalog[0], self.core)
        self.assertEqual(len(catalog), 3)
        self.assertIn('XffBenchmarkHistory', (self.root / 'index.html').read_text())
        for entry in catalog[1:]:
            self.assertTrue(entry['local'])
            self.assertTrue(entry['extended'])
            self.assertEqual(entry['binary']['state'], 'Verified against Retention')
            wrapper = records.read(self.root / entry['report'] / 'report.json')
            self.assertEqual(len(wrapper['tool_comparisons']['tasks']), len(self.fixture.cases))
            data = records.read(self.root / entry['figures'])
            surfaces = [trace for trace in data['alphabetical']['percent']['data'] if trace['type'] == 'surface']
            heights = [height for trace in surfaces for row in trace['y'] for height in row if height is not None]
            expected = 50 if 'Traditional tools' in entry['platform'] else -100
            self.assertEqual(heights, [expected] * len(self.fixture.cases))
        datasets = records.read(self.root / 'datasets.json')['datasets']
        self.assertEqual(len(datasets), 2)
        self.assertEqual({item['kind'] for item in datasets}, {'local'})
        self.assertTrue(all(item['recipe']['suite'] == suite.RECIPE for item in datasets))

    def test_repeated_publication_and_packed_observations_are_idempotent(self):
        path = records.retain(self.root, self.fixture.record())
        site.publish(self.root)
        before = records.read(self.root / 'catalog.json')
        path.with_suffix('.json.gz').write_bytes(gzip.compress(path.read_bytes(), mtime=0))
        path.unlink()
        site.publish(self.root)
        self.assertEqual(records.read(self.root / 'catalog.json'), before)
        page = (self.root / 'index.html').read_text()
        self.assertEqual(page.count('<!-- benchmark-explorer:start -->'), 1)
        self.assertEqual(page.count('<!-- benchmark-datasets:start -->'), 1)

    def test_changing_ci_parent_keeps_same_dataset_and_exposes_actual_parent_per_version(self):
        first = self.fixture.record(origin='ci', comparison='parent')
        records.retain(self.root, first, self.fixture.source())
        self.fixture.revisions.reverse()
        self.fixture.contract['anchor'] = self.fixture.revisions[-1]
        second = self.fixture.record(origin='ci', comparison='parent')
        records.retain(self.root, second, self.fixture.source())
        self.assertEqual(site.publish(self.root), 4)
        catalog = [item for item in records.read(self.root / 'catalog.json') if item.get('extended')]
        self.assertEqual(len({item['series_id'] for item in catalog}), 2)
        self.assertEqual({item['comparison_role'] for item in catalog}, {'Parent ' + 'a' * 12, 'Parent ' + 'b' * 12})
        for entry in catalog:
            self.assertFalse(entry['local'])
            self.assertEqual(entry['source'], 'merged')
            self.assertIn('github.com/mboworks/xff/commit/', entry['links'][-1]['href'])
        self.assertEqual(len(records.read(self.root / 'datasets.json')['datasets']), 2)

    def test_publication_rejects_untrusted_ci_and_modified_raw_rows(self):
        value = self.fixture.record(origin='ci', comparison='parent')
        with self.assertRaisesRegex(ValueError, 'trusted workflow metadata'):
            records.retain(self.root, value)
        path = records.retain(self.root, value, self.fixture.source())
        changed = records.read(path)
        changed['publication_source']['event'] = 'pull_request'
        path.write_text(json.dumps(changed))
        with self.assertRaisesRegex(ValueError, 'untrusted extended workflow'):
            site.publish(self.root)

    def test_same_head_observations_prefer_completed_controls_when_measurement_times_match(self):
        value = self.fixture.record()
        records.retain(self.root, value)
        complete = copy.deepcopy(value)
        complete['controls'] = self.fixture.rows(self.fixture.revisions[-1])
        complete['identity'] = records.observation_identity(complete)
        records.retain(self.root, complete)
        selected = site.retained(self.root)
        self.assertEqual(len(selected), 2)
        self.assertTrue(all(item[2]['controls'] for item in selected))

    def test_ci_pr_labels_use_trusted_version_links_when_subject_has_no_pr_suffix(self):
        self.fixture.revisions[0]['pr'] = 0
        value = self.fixture.record(origin='ci', comparison='parent')
        records.retain(self.root, value, self.fixture.source())
        (self.root / 'version-links.json').write_text(json.dumps({value['revision']['sha']: [
            {'label': 'PR #999', 'href': 'https://github.com/mboworks/xff/pull/999'}]}))
        site.publish(self.root)
        entries = [item for item in records.read(self.root / 'catalog.json') if item.get('extended')]
        self.assertTrue(all(item['label'] == 'PR #999 / ' + value['revision']['sha'][:10] for item in entries))

    def test_import_uses_complete_original_cells_and_actual_retained_builds(self):
        campaign = self.fixture.root / 'campaign'
        campaign.mkdir()
        (campaign / 'contract.json').write_text(json.dumps(self.fixture.contract))
        total = len(self.fixture.cases) * len(self.fixture.revisions)
        progress = {'state': 'complete', 'completed': total, 'total': total}
        (campaign / 'progress.json').write_text(json.dumps(progress))
        for revision in self.fixture.revisions:
            for row in self.fixture.rows(revision):
                path = campaign / 'cells' / revision['sha'] / suite.Case(**row['case']).key / 'complete.json'
                path.parent.mkdir(parents=True)
                path.write_text(json.dumps(row))
        imported = site.import_campaign(campaign, self.fixture.root, self.root, 'synthetic-original', 'linux', 'x86_64')
        self.assertEqual(len(imported), len(self.fixture.revisions))
        self.assertEqual(site.publish(self.root), 4)
        (campaign / 'progress.json').write_text(json.dumps(dict(progress, completed=total - 1)))
        with self.assertRaisesRegex(ValueError, 'campaign is incomplete'):
            site.import_campaign(campaign, self.fixture.root, self.root, 'synthetic-original', 'linux', 'x86_64')


if __name__ == '__main__':
    unittest.main()
