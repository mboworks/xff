# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Check historical ratios, paired uncertainty, and real-only renderer populations."""

import copy
import unittest

import benchmark_extended_report as report


class BenchmarkExtendedReportTest(unittest.TestCase):
    def row(self):
        return {'case': {'name': 'archive-tar', 'shape': 'broad', 'files': 100, 'cpus': 1},
                'revision': {'sha': 'old', 'pr': 1, 'binary_sha256': 'hash'}, 'expected_records': 50,
                'expected_sha256': 'oracle', 'cwd': '/fixture',
                'commands': {'xff': ['/old/xff'], 'anchor': ['/new/xff'], 'tar': ['/bin/tar']},
                'samples': [dict(label=label, round=repetition, phase='measured' if repetition else 'warmup',
                                 elapsed_seconds=seconds if repetition else 500,
                                 exit_codes=[0], stderr_bytes=0, validated_records=50)
                            for repetition in range(4) for label, seconds in [('xff', 4), ('anchor', 2), ('tar', 8)]]}

    def contract(self):
        return {'repetitions': 3, 'keep': 2, 'anchor': {'pr': 2}, 'recipe': 'extended-production-v1',
                'platform': 'Linux', 'cases': [self.row()['case']], 'affinity': [0, 1, 2]}

    def test_known_ratios_and_warmup_exclusion(self):
        result = report.summarize_cell(self.row(), self.contract())
        self.assertEqual(result['xff_seconds'], 4)
        self.assertEqual(result['historical_over_current'], 2)
        self.assertEqual(result['reference_over_xff'], 2)
        self.assertEqual(result['current_time_reduction_percent'], 50)
        self.assertEqual(result['paired_ratio_ci_low'], 2)
        self.assertEqual(result['paired_ratio_ci_high'], 2)

    def test_missing_duplicate_and_incorrect_samples_are_rejected(self):
        row = self.row()
        variants = [copy.deepcopy(row) for _ in range(3)]
        variants[0]['samples'].pop()
        variants[1]['samples'][-1]['round'] = 2
        variants[2]['samples'][-1]['validated_records'] = 49
        for variant in variants:
            with self.assertRaises(ValueError):
                report.summarize_cell(variant, self.contract())

    def test_fastest_estimator_and_control_interval(self):
        self.assertEqual(report.mean_fastest([{'elapsed_seconds': value} for value in (2, 5, 1)], 2), 1.5)
        samples = [{'round': i, 'elapsed_seconds': value} for i, value in enumerate([1, 2, 3])]
        self.assertEqual(report.paired_interval(samples, samples), [1, 1])
        with self.assertRaises(ValueError):
            report.paired_interval(samples, [*samples, samples[-1]])

    def test_renderer_adapter_does_not_invent_cells_or_mix_references(self):
        for view, expected in [('traditional', {'xff', 'tar'}), ('anchor', {'xff', 'xff-at-2'})]:
            adapted = report.as_report([self.row()], self.contract(), view)
            self.assertEqual(len(adapted['tasks']), 1)
            self.assertEqual(adapted['contract']['file_counts'], [100])
            self.assertEqual(adapted['contract']['suite'], 'extended-production-v1')
            task = adapted['tasks'][0]
            self.assertEqual(task['files'], 100)
            self.assertEqual(task['dataset'], 'broad')
            self.assertEqual(set(task['participants']), expected)
            for participant in task['participants'].values():
                self.assertEqual(len(participant['samples']), 3)


if __name__ == '__main__':
    unittest.main()
