# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Reference normalization preserves individual XFF results and series boundaries."""

import copy
import unittest

import benchmark_normalization as normalization


def records(count=7):
    return [(f'runs/{index}/1/linux/report.json', {
        'head': str(index) * 40, 'platform': 'linux',
        'source': {'id': index, 'run_attempt': 1, 'created_at': f'2026-10-{index:02d}T00:00:00Z'},
        'tool_comparisons': {
            'contract': {'repetitions': 1, 'retained': 1, 'estimator': 'mean-fastest', 'cpu_count': 4,
                         'storage': {'filesystem': 'tmpfs'}, 'affinity_by_cpu_count': {'1': [0]},
                         'machine': 'x86_64', 'build_identity': str(index), 'batch': f'job-{index}'},
            'tools': {'rg': {'sha256': 'same-reference', 'version': '1', 'status': 'available'}},
            'tasks': [{'name': 'content', 'dataset': 'broad', 'cpus': 1, 'files': 10,
                       'fixture_identity': 'fixture', 'expected_sha256': 'output', 'input_files': 10,
                       'participants': {
                           'xff': {'samples': [{'elapsed_seconds': index * index}], 'pipeline': [['xff', '.']]},
                           'rg': {'samples': [{'elapsed_seconds': index * 2}], 'pipeline': [['rg', '.']]},
                       }}],
        }}) for index in range(1, count + 1)]


class BenchmarkNormalizationTest(unittest.TestCase):
    def test_reference_window_scales_each_xff_result_without_smoothing_it(self):
        inputs = records()
        original = copy.deepcopy(inputs)
        values = normalization.reference_windows(inputs)
        key = normalization.cell_key(inputs[0][1]['tool_comparisons']['tasks'][0], 'rg')
        for index, (identity, _) in enumerate(inputs, 1):
            item = values[identity][key]
            expected_mean = 6 if index <= 5 else (index - 2) * 2
            self.assertEqual(item['reference_mean_seconds'], expected_mean)
            self.assertEqual(item['factor'], expected_mean / (index * 2))
            self.assertEqual(item['xff_seconds'], index * index * item['factor'])
            self.assertEqual(len(item['window']), 5)
            self.assertEqual(item['window'][0]['commit'], str(max(1, index - 4)) * 40)
        self.assertEqual(inputs, original)
        self.assertEqual(normalization.reference_windows(list(reversed(inputs))), values)

    def test_insufficient_or_missing_identity_is_explicit(self):
        inputs = records(4)
        values = normalization.reference_windows(inputs)
        self.assertTrue(all(item['status'] == 'unavailable' for report in values.values() for item in report.values()))
        inputs[0][1]['tool_comparisons']['tools']['rg'].pop('sha256')
        values = normalization.reference_windows(inputs)
        self.assertIn('Incomplete', next(iter(values[inputs[0][0]].values()))['reason'])
        inputs[0][1].pop('source')
        inputs[0][1].pop('head')
        values = normalization.reference_windows(inputs)
        self.assertIn('provenance', next(iter(values[inputs[0][0]].values()))['reason'])

    def test_changed_reference_fixture_allocation_or_machine_does_not_enter_window(self):
        for change in (
                lambda report: report['tools']['rg'].update(sha256='new-tool'),
                lambda report: report['tasks'][0].update(fixture_identity='different'),
                lambda report: report['tasks'][0].update(cpus=3),
                lambda report: report['contract'].update(machine='arm64'),
                lambda report: report['tasks'][0]['participants']['rg'].update(pipeline=[['rg', '--different']])):
            with self.subTest(change=change):
                inputs = records(6)
                change(inputs[-1][1]['tool_comparisons'])
                values = normalization.reference_windows(inputs)
                self.assertEqual(next(iter(values[inputs[-1][0]].values()))['status'], 'unavailable')
                first = next(iter(values[inputs[0][0]].values()))
                self.assertEqual(first['reference_mean_seconds'], 6)

    def test_duplicate_commit_uses_latest_attempt_once(self):
        inputs = records(5)
        duplicate = copy.deepcopy(inputs[-1][1])
        duplicate['source']['run_attempt'] = 2
        inputs.append(('runs/5/2/linux/report.json', duplicate))
        values = normalization.reference_windows(inputs)
        self.assertEqual(next(iter(values[inputs[-2][0]].values()))['status'], 'unavailable')
        current = next(iter(values[inputs[-1][0]].values()))
        self.assertEqual(current['reference_mean_seconds'], 6)
        self.assertEqual(current['window'][-1]['report'], inputs[-1][0])

    def test_local_machine_series_and_ci_remain_separate(self):
        inputs = records(5)
        local = copy.deepcopy(inputs[0][1])
        local.update(kind='backfill', purpose='local-addition', series='local-host',
                     completed_at='2026-10-01T00:00:00Z', revision={'date': '2026-10-01T00:00:00Z'})
        inputs.append(('local/host/report.json', local))
        values = normalization.reference_windows(inputs)
        self.assertEqual(next(iter(values[inputs[-1][0]].values()))['status'], 'unavailable')
        self.assertEqual(next(iter(values[inputs[0][0]].values()))['status'], 'available')


if __name__ == '__main__':
    unittest.main()
