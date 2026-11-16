# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Compose CI presentation without changing any retained measurement population."""

import copy
import json
import unittest

import benchmark_extended_records
import benchmark_history_views as history
import benchmark_landscape as landscape
import benchmark_matrix as matrix
import benchmark_overview as overview
import benchmark_records as records
from benchmark_extended_test_support import ExtendedFixture


class BenchmarkHistoryViewsTest(unittest.TestCase):
    def setUp(self):
        self.fixture = ExtendedFixture()
        self.addCleanup(self.fixture.close)
        self.root = self.fixture.root / 'site'
        self.root.mkdir()
        value = self.fixture.record(origin='ci', comparison='parent')
        # Use a fully validated source view for both component kinds. Give the core
        # its own sampling contract below to guard against accidental repooling.
        self.wrapper = benchmark_extended_records.view_report(value, 'traditional')
        self.extended_path = self.root / 'extended/views/series' / value['revision']['sha'] / 'report.json'
        self.extended_path.parent.mkdir(parents=True)
        self.extended_path.write_text(json.dumps(self.wrapper))
        self.entry = {'platform': 'CI / linux / extended', 'commit': value['revision']['sha'],
                      'date': value['revision']['date'], 'label': 'Synthetic extended',
                      'source': 'merged', 'extended': True,
                      'report': self.extended_path.parent.relative_to(self.root).as_posix() + '/',
                      'figures': 'extended/landscape.json', 'parent_report': 'extended/parent/'}
        self.core_path = self.root / 'runs/1/1/linux/report.json'
        self.core_path.parent.mkdir(parents=True)
        self.core = copy.deepcopy(self.wrapper)
        self.core.pop('binary_verification', None)
        report = self.core['tool_comparisons']
        report['contract'].update(repetitions=1, retained=1)
        for task in report['tasks']:
            for participant in task['participants'].values():
                participant['samples'] = [participant['samples'][0]]
        self.core_path.write_text(json.dumps(self.core))
        self.core_entry = dict(self.entry, platform='linux / x86_64', extended=False,
                               report='runs/1/1/linux/', figures='runs/1/1/linux/landscape.json')

    def extra(self, normalization=None, wrapper=None, entry=None):
        return [(entry or self.entry, self.extended_path, wrapper or self.wrapper, normalization or {})]

    def test_same_ci_history_keeps_source_bytes_sampling_ratios_and_binary_evidence(self):
        originals = {path: path.read_bytes() for path in (self.core_path, self.extended_path)}
        core_report = self.core['tool_comparisons']
        core_rows = matrix.relative_results(core_report)
        extended_rows = matrix.relative_results(self.wrapper['tool_comparisons'])
        catalog = history.publish(self.root, [self.core_entry], self.extra())
        self.assertEqual(len(catalog), 1)
        entry = catalog[0]
        self.assertEqual(entry['platform'], 'linux / x86_64')
        self.assertEqual(entry['core_entry'], self.core_entry)
        derived = records.read(self.root / entry['report'] / 'report.json')
        self.assertNotIn('tool_comparisons', derived)
        self.assertEqual([part['sampling'] for part in derived['observations']],
                         [matrix.policy(core_report), matrix.policy(self.wrapper['tool_comparisons'])])
        self.assertEqual(derived['binary']['state'], 'Consistent')
        self.assertEqual(len(derived['binary']['binaries']), 2)
        self.assertEqual([part['binary']['state'] for part in derived['observations']],
                         ['Consistent', 'Verified against Retention'])
        data = records.read(self.root / entry['figures'])
        for mode in landscape.ORDER_LABELS:
            traces = data[mode]['percent']['data']
            heights = [value for trace in traces for row in trace['y'] for value in row if value is not None]
            self.assertEqual(sorted(heights), sorted(100 * (1 - row['xff_over_reference'])
                                                    for row in core_rows + extended_rows))
        self.assertEqual(data['overview'], overview.summarize(core_report))
        self.assertEqual({path: path.read_bytes() for path in originals}, originals)

    def test_republication_unwraps_core_and_never_duplicates_extended_tasks(self):
        first = history.publish(self.root, [self.core_entry], self.extra())
        second = history.publish(self.root, history.original_catalog(first), self.extra())
        self.assertEqual(first, second)
        self.assertEqual(len(second[0]['workload_observations']), 2)
        self.assertEqual(history.original_catalog(first + [self.entry]), [self.core_entry])

    def test_local_and_other_architectures_are_not_folded_into_ci_history(self):
        local = dict(self.core_entry, local=True, platform='Local / local-series')
        other_path = self.root / 'runs/2/1/linux/report.json'
        other_path.parent.mkdir(parents=True)
        other = copy.deepcopy(self.core)
        other['tool_comparisons']['contract']['machine'] = 'aarch64'
        other_path.write_text(json.dumps(other))
        other_entry = dict(self.core_entry, platform='linux / aarch64', report='runs/2/1/linux/')
        catalog = history.publish(self.root, [local, other_entry], self.extra())
        self.assertEqual(len(catalog), 3)
        self.assertIn(local, catalog)
        self.assertIn(other_entry, catalog)
        composed = next(item for item in catalog if item.get('ci_history_view'))
        self.assertEqual(composed['dataset_type'], 'Extended workloads')
        self.assertIsNone(composed['core_entry'])

    def test_shared_axes_keep_unmeasured_cells_empty_and_normalization_namespaced(self):
        older = copy.deepcopy(self.core)
        older['head'] = 'c' * 40
        older['tool_comparisons']['tasks'] = [task for task in older['tool_comparisons']['tasks'] if task['files'] == 3]
        older['tool_comparisons']['contract']['file_counts'] = [3]
        path = self.root / 'runs/3/1/linux/report.json'
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps(older))
        old_entry = dict(self.core_entry, commit='c' * 40, report='runs/3/1/linux/')
        key = json.dumps(['broad', 'files-exact', 1, 3, 'find'])
        norm = {key: {'status': 'available', 'factor': 2, 'xff_seconds': 4, 'reference_mean_seconds': 8,
                      'window': [{'date': '2026-11-01T00:00:00Z'}]}}
        catalog = history.publish(self.root, [old_entry], self.extra(norm))
        self.assertEqual(len(catalog), 2)
        newer = next(item for item in catalog if item['commit'] == self.entry['commit'])
        adjusted = records.read(self.root / newer['report'] / 'normalization.json')
        self.assertIn(json.dumps(['broad', 'extended / files-exact', 1, 3, 'find']), adjusted)
        older_entry = next(item for item in catalog if item['commit'] == 'c' * 40)
        self.assertEqual(older_entry['dataset_type'], 'Core workloads')
        data = records.read(self.root / older_entry['figures'])
        for mode in landscape.ORDER_LABELS:
            heights = [value for trace in data[mode]['percent']['data'] for row in trace['y'] for value in row]
            self.assertEqual(sum(value is not None for value in heights), len(older['tool_comparisons']['tasks']))
            self.assertGreater(sum(value is None for value in heights), 0)

    def test_affinity_is_claimed_only_when_all_components_record_the_same_mask(self):
        components = [{'namespace': '', 'report': {'contract': {'cpu_counts': [1], 'affinity_by_cpu_count': {'1': mask}}}}
                      for mask in ([0], [1])]
        data = history.presentation(components, {'cpu_counts': [1], 'file_counts': [3]})
        self.assertIsNone(data['contract']['affinity_by_cpu_count']['1'])
        components[1]['report']['contract']['affinity_by_cpu_count']['1'] = [0]
        self.assertEqual(history.presentation(components, {'cpu_counts': [1], 'file_counts': [3]})[
            'contract']['affinity_by_cpu_count']['1'], [0])


if __name__ == '__main__':
    unittest.main()
