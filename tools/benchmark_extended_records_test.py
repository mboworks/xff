# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Reject incomplete, changed, incompatible or incorrectly attributed observations."""

import copy
import gzip
import json
import unittest

import benchmark_extended_records as records
from benchmark_extended_test_support import ExtendedFixture


class BenchmarkExtendedRecordsTest(unittest.TestCase):
    def setUp(self):
        self.fixture = ExtendedFixture()
        self.addCleanup(self.fixture.close)

    def test_real_retained_bytes_bind_both_views_without_claiming_ci(self):
        value = self.fixture.record()
        records.validate(value)
        for view in ('traditional', 'anchor'):
            wrapper = records.view_report(value, view)
            self.assertEqual(records.benchmark_provenance.describe(wrapper)['state'], 'Verified against Retention')
            self.assertEqual(wrapper['source']['event'], 'local')
        directory = self.fixture.root / 'binaries' / value['revision']['sha']
        (directory / 'xff_full').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'frozen binary changed'):
            records.build_proof(directory, value['revision'])

    def test_changed_rows_oracle_commands_environment_and_sampling_are_rejected(self):
        value = self.fixture.record()
        mutations = [lambda r: r['rows'].pop(),
                     lambda r: r['rows'][0]['samples'].pop(),
                     lambda r: r['rows'][0].update(expected_records=999),
                     lambda r: r['rows'][0]['environment'].update(TZ='wrong'),
                     lambda r: r['rows'][0]['commands']['xff'].append('--wrong'),
                     lambda r: r['rows'][0]['samples'][0].update(stdout_bytes=1),
                     lambda r: r['rows'][0]['samples'][0].update(exit_codes=[1]),
                     lambda r: r['contract'].update(keep=8),
                     lambda r: r['builds'][r['revision']['sha']]['link_arguments'].remove('-flto=thin')]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                changed = copy.deepcopy(value)
                mutate(changed)
                changed['identity'] = records.observation_identity(changed)
                with self.assertRaises(ValueError):
                    records.validate(changed)

    def test_parent_label_is_stable_and_concrete_parent_is_retained(self):
        value = self.fixture.record(origin='ci', comparison='parent')
        records.validate(value, self.fixture.source())
        view = records.view_report(value, 'anchor')
        self.assertEqual(set(view['tool_comparisons']['tasks'][0]['participants']), {'xff', 'xff-parent'})
        self.assertEqual(view['base'], self.fixture.revisions[-1]['sha'])

    def test_ci_requires_actual_run_attempt_repository_and_first_parent(self):
        value = self.fixture.record(origin='ci', comparison='parent')
        for change in ({'head_sha': 'f' * 40}, {'base_sha': 'f' * 40}, {'run_attempt': 2},
                       {'event': 'pull_request'}, {'repository': {'full_name': 'other/repo'}}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                records.validate(value, dict(self.fixture.source(), **change))

    def test_idempotent_retention_preserves_original_proof_and_supports_packed_records(self):
        value = self.fixture.record()
        root = self.fixture.root / 'site'
        path = records.retain(root, value)
        before = path.read_bytes()
        repeated = self.fixture.record()
        self.assertEqual(repeated['identity'], value['identity'])
        records.retain(root, repeated)
        self.assertEqual(path.read_bytes(), before)
        path.with_suffix('.json.gz').write_bytes(gzip.compress(before, mtime=0))
        path.unlink()
        records.retain(root, repeated)
        self.assertEqual(records.read(path), json.loads(before))

    def test_complete_head_can_be_retained_from_failed_workflow(self):
        value = self.fixture.record(origin='ci', comparison='parent')
        path = records.retain(self.fixture.root / 'site', value, self.fixture.source())
        saved = records.read(path)
        self.assertEqual(saved['publication_source']['id'], 12)
        self.assertEqual(saved['origin'], 'ci')


if __name__ == '__main__':
    unittest.main()
