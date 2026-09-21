# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Sampling and overlap-aware baseline comparisons."""

import json
from pathlib import Path
import tempfile
import unittest

import benchmark_matrix as matrix


def report(times=(1, 2, 3, 4, 5, 6, 7, 80, 100), keep=7):
    return {'contract': {'repetitions': len(times), 'retained': keep, 'estimator': 'mean-fastest',
                         'file_counts': [10, 100], 'cpu_counts': [1, 4], 'build_identity': 'build',
                         'storage': {'parent': '/tmp/a', 'filesystem': 'tmpfs', 'memory_required': True},
                         'affinity_by_cpu_count': {'1': [0], '4': [0, 1, 2, 3]}},
            'tools': {'xff': {'sha256': 'current'}, 'find': {'status': 'available', 'sha256': 'find'}},
            'tasks': [{'dataset': 'broad', 'name': 'files', 'cpus': cpu, 'files': count,
                       'fixture_identity': f'fixture-{count}', 'expected_sha256': f'output-{count}', 'input_files': count,
                       'participants': {'xff': {'samples': [{'elapsed_seconds': value, 'user_cpu_seconds': value * 2}
                                                          for value in times]}}, 'skips': {}}
                      for cpu in (1, 4) for count in (10, 100)]}


class BenchmarkMatrixTest(unittest.TestCase):
    def retain(self, root, data, run=1, event='push', branch='main'):
        path = root / 'runs' / str(run) / '1' / 'report.json'
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps({'head': str(run) * 40, 'tool_comparisons': data,
                                    'source': {'id': run, 'run_attempt': 1, 'created_at': f'2026-09-{run:02}',
                                               'event': event, 'head_branch': branch}}))

    def test_fastest_subset_controls_all_metrics(self):
        data = report()
        entry = data['tasks'][0]['participants']['xff']
        self.assertEqual(matrix.elapsed_mean(data, entry), 4)
        self.assertEqual(sum(sample['user_cpu_seconds'] for sample in matrix.selected_samples(data, entry)), 56)
        self.assertEqual(len(entry['samples']), 9)
        data = report((100, 3, 2), 2)
        self.assertEqual(matrix.elapsed_mean(data, data['tasks'][0]['participants']['xff']), 2.5)
        self.assertEqual(matrix.policy(data), 'mean of fastest 2/3 runs')

    def test_relative_ratios_normalization_and_alarm(self):
        current, previous = report(), report()
        for task in current['tasks']:
            task['participants']['find'] = {'samples': [{'elapsed_seconds': 2} for _ in range(9)]}
        for task in previous['tasks']:
            task['participants']['find'] = {'samples': [{'elapsed_seconds': 2.5} for _ in range(9)]}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.retain(root, previous)
            matrix.attach_baseline(current, root)
        rows = matrix.relative_results(current)
        self.assertEqual(rows[0]['xff_over_reference'], 2)
        self.assertEqual(rows[0]['difference_percent'], 100)
        self.assertEqual(rows[0]['normalized_change_percent'], 25)
        self.assertEqual(len(matrix.regressions(current, 20, minimum_files=10)), 4)
        current['alarm'] = {'threshold_percent': 15, 'blocking': False,
                            'cells': matrix.regressions(current, 15, minimum_files=1)}
        self.assertIn('4 cell(s) exceed the 15%', matrix.alarm_text(current))
        self.assertIn('does not block merging', matrix.render_markdown(current))
        self.assertEqual(matrix.regressions(current, 30, minimum_files=10), [])
        self.assertEqual(matrix.regressions(current, 20), [])
        self.assertIn('2.00</span></td><td style="text-align:right"><span style="color:#a12622">+100.0%</span><br><small>vs main +25.0%</small>', matrix.render_html(current))
        for value in (0, -1, float('nan')):
            with self.subTest(value=value), self.assertRaises(ValueError):
                matrix.regressions(current, value)

    def test_ratio_colors_and_neutral_baseline_delta(self):
        for ratio, color in ((2, '#a12622'), (0.5, '#176b36'), (1, None)):
            with self.subTest(ratio=ratio):
                data = report((ratio,), 1)
                for task in data['tasks']:
                    task['participants']['find'] = {'samples': [{'elapsed_seconds': 1}]}
                page = matrix.render_html(data)
                self.assertIn('>\u0394 %</th>', page)
                if color:
                    self.assertIn(f'<span style="color:{color}">{ratio:.2f}</span>', page)
                else:
                    self.assertNotIn('<span style="color:', page)
        self.assertEqual(matrix.ratio_html('0.500x (-50.0%); vs main +25.0%', 0.5),
                         '<span style="color:#176b36">0.500x (-50.0%)</span>; vs main +25.0%')
        self.assertEqual(matrix.ratio_html('n/a', None), 'n/a')

    def test_invalid_sampling_and_nonfinite_times(self):
        for data in (report(keep=0), report(keep=10), report((1, float('nan')), 1), report((0, 1), 1)):
            with self.subTest(data=data), self.assertRaises(ValueError):
                matrix.elapsed_mean(data, data['tasks'][0]['participants']['xff'])

    def test_latest_compatible_main_and_partial_overlap(self):
        data = report((2, 3, 100), 2)
        data['contract']['storage']['parent'] = '/another/tmp'
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.retain(root, report(), 1)
            partial = report()
            partial['tasks'][0]['fixture_identity'] = 'changed-content'
            partial['contract']['file_counts'].append(1000)
            self.retain(root, partial, 2)
            wrong_build = report()
            wrong_build['contract']['build_identity'] = 'other compiler'
            self.retain(root, wrong_build, 3)
            self.retain(root, report(), 4, event='pull_request', branch='feature')
            matrix.attach_baseline(data, root)
        self.assertEqual(data['baseline']['run'], 2)
        self.assertEqual(data['baseline']['policy'], 'mean of fastest 7/9 runs')
        self.assertNotIn('broad/files/1/10', data['baseline']['averages'])
        self.assertEqual(data['baseline']['averages']['broad/files/4/100']['xff'], 4)
        rendered = matrix.render_markdown(data)
        self.assertIn('2500.00 / 4000.00 (-37.5%)', rendered)
        self.assertIn('2500.00 / n/a', rendered)
        page = matrix.render_html(data)
        self.assertIn('2500.00</td><td style="text-align:right">-1500.00<br><small>-37.5%</small>', page)
        self.assertNotIn('(-37.5%)', page)
        self.assertIn('2222222222222222222222222222222222222222', rendered)

    def test_changed_contract_and_absent_baseline_are_explicit(self):
        for change in ('storage', 'fixture'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                data, other = report(), report()
                if change == 'storage':
                    other['contract']['storage']['filesystem'] = 'ext4'
                else:
                    for task in other['tasks']:
                        task['fixture_identity'] = 'changed'
                self.retain(root, other)
                matrix.attach_baseline(data, root)
                self.assertEqual(data['baseline']['status'], 'unavailable')
                self.assertIn('No compatible', matrix.render_html(data))

    def test_reference_upgrade_and_argument_changes_preserve_other_cells(self):
        current, other = report(), report()
        for data in (current, other):
            for task in data['tasks']:
                task['participants']['find'] = {'samples': [{'elapsed_seconds': 1} for _ in range(9)]}
        other['tools']['find']['sha256'] = 'older-version'
        other['tasks'][0]['participants']['xff']['pipeline'] = [['xff', '--changed-setting']]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.retain(root, other)
            matrix.attach_baseline(current, root)
        values = current['baseline']['averages']
        self.assertNotIn('broad/files/1/10', values)
        self.assertEqual(values['broad/files/1/100'], {'xff': 4})
        self.assertTrue(all(row['main_ratio'] is None for row in matrix.relative_results(current)))

    def test_cpu_grouping_alignment_and_html_escaping(self):
        data = report()
        text = matrix.render_markdown(data)
        self.assertLess(text.index('1 CPU / 100 files'), text.index('4 CPU / 10 files'))
        lines = [line for line in text.splitlines() if line.startswith('|')]
        self.assertTrue(all([index for index, char in enumerate(line) if char == '|'] ==
                            [index for index, char in enumerate(lines[0]) if char == '|'] for line in lines))
        page = matrix.render_html(data)
        self.assertIn('<th colspan="4">1 CPU</th>', page)
        self.assertIn('<th colspan="2">10</th>', page)
        self.assertIn('>T [ms]</th>', page)
        self.assertIn('<th scope="row" style="text-align:left">files / xff</th>', page)
        self.assertIn('<td style="text-align:right">4000.00</td><td style="text-align:right">n/a</td>', page)
        self.assertIn('text-align:right', page)
        data['tasks'][0]['dataset'] = '<script>alert(1)</script>'
        self.assertNotIn('<script>', matrix.render_html(data))
        data['tasks'][0]['participants'] = {}
        data['tasks'][0]['skips'] = {'xff': '<not installed>'}
        self.assertIn('Skipped: &lt;not installed&gt;', matrix.render_html(data))

    def test_legacy_reports_remain_readable(self):
        data = report((1, 2, 10), 3)
        data['contract'].pop('estimator')
        self.assertEqual(matrix.elapsed_mean(data, data['tasks'][0]['participants']['xff']), 2)
        for task in data['tasks']:
            task.pop('dataset')
        self.assertEqual(matrix.matrices(data), [])


if __name__ == '__main__':
    unittest.main()
