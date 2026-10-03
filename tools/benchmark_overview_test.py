# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Overview arithmetic, unavailable evidence, and visible advisory regressions."""

import copy
import unittest

import benchmark_overview as overview


def report():
    tasks = []
    averages = {}
    for files, xff, reference in ((10, 9, 3), (1000, 2, 2), (10000, 8, 2)):
        task = {'dataset': 'broad', 'name': 'files', 'cpus': 1, 'files': files,
                'participants': {name: {'samples': [{'elapsed_seconds': seconds}]}
                                 for name, seconds in (('xff', xff), ('rg', reference))}}
        tasks.append(task)
        averages[overview.matrix.task_key(task)] = {'xff': 2, 'rg': 1}
    return {'contract': {'repetitions': 1, 'retained': 1, 'estimator': 'mean-fastest'},
            'tasks': tasks, 'baseline': {'status': 'available', 'head': 'a' * 40, 'averages': averages}}


class BenchmarkOverviewTest(unittest.TestCase):
    def test_geometric_mean_and_individual_regressions_preserve_evidence(self):
        data = report()
        original = copy.deepcopy(data)
        summary = overview.summarize(data)
        self.assertEqual(data, original)
        self.assertEqual(summary['status'], 'warning')
        self.assertFalse(summary['blocking'])
        self.assertAlmostEqual(summary['all']['change_percent'], (1.5 ** (1 / 3) - 1) * 100)
        self.assertEqual(summary['warning_population'], summary['all'])
        self.assertEqual(summary['warning_count'], 2)
        self.assertEqual(summary['all']['slower'], 2)
        self.assertEqual(summary['all']['faster'], 1)
        self.assertEqual(summary['largest_regressions'][0]['files'], 10000)
        self.assertEqual(summary['largest_regressions'][0]['normalized_change_percent'], 100)
        self.assertIn('WARNING', overview.render_html(data))
        self.assertIn('100.0%', overview.render_html(data))
        self.assertIn('WARNING', overview.render_markdown(data))

    def test_same_reference_drift_cancels_without_averaging_xff_runs(self):
        data = report()
        for task in data['tasks']:
            task['participants']['xff']['samples'][0]['elapsed_seconds'] = 10
            task['participants']['rg']['samples'][0]['elapsed_seconds'] = 5
        summary = overview.summarize(data)
        self.assertEqual(summary['status'], 'available')
        self.assertEqual(summary['all']['change_percent'], 0)
        self.assertEqual(summary['all']['within_threshold'], 3)
        self.assertEqual(summary['largest_regressions'], [])

    def test_missing_baseline_is_unknown_and_partial_baselines_are_counted(self):
        data = report()
        data.pop('baseline')
        summary = overview.summarize(data)
        self.assertEqual(summary['status'], 'unavailable')
        self.assertIsNone(summary['all']['change_percent'])
        self.assertEqual(summary['unmatched_comparisons'], 3)
        self.assertIn('regression status is unknown', overview.render_html(data))
        data = report()
        data['baseline']['averages'].pop(overview.matrix.task_key(data['tasks'][0]))
        summary = overview.summarize(data)
        self.assertEqual(summary['all']['comparisons'], 2)
        self.assertEqual(summary['unmatched_comparisons'], 1)

    def test_small_cases_trigger_the_default_warning_rule(self):
        data = report()
        data['tasks'] = data['tasks'][:1]
        summary = overview.summarize(data)
        self.assertEqual(summary['all']['slower'], 1)
        self.assertEqual(summary['warning_count'], 1)
        summary = overview.summarize(data, minimum_files=1000)
        self.assertEqual(summary['warning_count'], 0)
        self.assertIn('No warning-eligible comparisons', overview.explanation(summary))

    def test_flat_aggregate_does_not_hide_a_severely_regressed_case(self):
        data = report()
        data['tasks'] = data['tasks'][1:]
        summary = overview.summarize(data)
        self.assertAlmostEqual(summary['all']['change_percent'], 0)
        self.assertEqual(summary['status'], 'warning')
        self.assertEqual(summary['warning_count'], 1)

    def test_invalid_policy_and_nonfinite_measurements_are_rejected(self):
        for threshold, minimum in ((0, 1000), (float('nan'), 1000), (15, 0)):
            with self.subTest(threshold=threshold, minimum=minimum), self.assertRaises(ValueError):
                overview.summarize(report(), threshold, minimum)
        data = report()
        data['tasks'][0]['participants']['xff']['samples'][0]['elapsed_seconds'] = float('inf')
        with self.assertRaises(ValueError):
            overview.summarize(data)

    def test_labels_are_escaped_in_html(self):
        data = report()
        task = data['tasks'][0]
        baseline = data['baseline']['averages'].pop(overview.matrix.task_key(task))
        task['name'] = '<script>bad</script>'
        data['baseline']['averages'][overview.matrix.task_key(task)] = baseline
        text = overview.render_html(data)
        self.assertNotIn('<script>', text)
        self.assertIn('&lt;script&gt;', text)


if __name__ == '__main__':
    unittest.main()
