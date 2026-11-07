# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Check historical ratios, paired uncertainty, and real-only renderer populations."""

import copy
import json
from pathlib import Path
import tempfile
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
                                 user_cpu_seconds=seconds / 2, system_cpu_seconds=seconds / 4,
                                 peak_child_rss_bytes=seconds * 1000, first_stdout_seconds=seconds / 10,
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
        self.assertEqual(result['xff_cpu_seconds'], 3)
        self.assertEqual(result['anchor_cpu_seconds'], 1.5)
        self.assertEqual(result['xff_user_cpu_seconds'], 2)
        self.assertEqual(result['xff_system_cpu_seconds'], 1)
        self.assertEqual(result['xff_rss_bytes'], 4000)
        self.assertEqual(result['xff_first_stdout_seconds'], .4)

    def test_export_links_tables_without_an_unrelated_merged_baseline_overview(self):
        row = self.row()
        row['revision']['date'] = '2026-11-07'
        row['after'] = {'utc': '2026-11-07T12:00:00Z'}
        rows = [dict(row, case=dict(row['case'], shape=shape, files=files, cpus=cpus))
                for shape in ('broad', 'deep') for files in (100, 1000) for cpus in (1, 3)]
        contract = dict(self.contract(), cases=[item['case'] for item in rows])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            renderer = root / 'renderer.js'
            renderer.write_text('/* renderer fixture */')
            report.export_viewer(root, contract, [(row['revision'], rows)], renderer)
            document = (root / 'index.html').read_text()
            self.assertNotIn('data-overview', document)
            for name in ('cells.csv', 'history-by-task.csv', 'current-versus-reference.csv'):
                self.assertIn(f'href="{name}"', document)
            self.assertIn('1 complete revisions', document)

    def test_missing_duplicate_and_incorrect_samples_are_rejected(self):
        row = self.row()
        variants = [copy.deepcopy(row) for _ in range(3)]
        variants[0]['samples'].pop()
        variants[1]['samples'][-1]['round'] = 2
        variants[2]['samples'][-1]['validated_records'] = 49
        for variant in variants:
            with self.assertRaises(ValueError):
                report.summarize_cell(variant, self.contract())

    def test_experiments_keep_distinct_endpoints_and_do_not_claim_merged_prs(self):
        candidate = dict(self.row()['revision'], sha='candidate', pr=0, date='2026-11-07')
        baseline = dict(candidate, sha='baseline')
        rows = [dict(self.row(), revision=candidate, after={'utc': '2026-11-07T12:00:00Z'},
                     case=dict(self.row()['case'], files=files, cpus=cpus))
                for files in (100, 1000) for cpus in (1, 3)]
        contract = dict(self.contract(), anchor=baseline, revisions=[candidate, baseline],
                        cases=[row['case'] for row in rows], platform='Darwin')
        summaries = report.endpoint_summaries([dict(sha='candidate'), dict(sha='baseline')], contract)
        self.assertEqual(set(summaries), {'candidate', 'baseline'})
        self.assertEqual(summaries['candidate'], [dict(sha='candidate')])
        adapted = report.as_report(rows, contract, 'anchor')
        self.assertEqual(set(adapted['tasks'][0]['participants']), {'xff', 'xff-at-baseline'})
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            renderer = root / 'renderer.js'
            renderer.write_text('/* renderer fixture */')
            baseline_rows = [dict(row, revision=baseline, after={'utc': '2026-11-07T13:00:00Z'}) for row in rows]
            report.export_viewer(root, contract, [(candidate, rows), (baseline, baseline_rows)], renderer)
            catalog = json.loads((root / 'catalog.json').read_text())
            for platform in {item['platform'] for item in catalog}:
                records = [item for item in catalog if item['platform'] == platform]
                self.assertEqual([item['commit'] for item in records], ['baseline', 'candidate'])
                self.assertEqual([item['comparison_role'] for item in records], ['Baseline', 'Candidate'])
            for item in catalog:
                self.assertEqual(item['source'], 'experiment')
                self.assertEqual(item['links'], [])
                self.assertIn('Experiment ' + item['commit'], item['label'])
                self.assertIn('Darwin', item['platform'])
            document = (root / 'index.html').read_text()
            self.assertNotIn('PR #0', document)
            self.assertNotIn('Linux tmpfs', document)

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

    def test_task_summary_requires_complete_unique_shape_worker_population(self):
        contract = self.contract()
        contract['cases'].append(dict(contract['cases'][0], cpus=3))
        first = report.summarize_cell(self.row(), contract)
        second = dict(first, cpus=3, historical_over_current=8, reference_over_xff=8)
        self.assertEqual(report.task_summaries([first], contract), [])
        group = report.task_summaries([first, second], contract)[0]
        self.assertEqual(group['cells'], 2)
        self.assertEqual(group['historical_over_current_geomean'], 4)
        self.assertEqual(group['xff_over_reference_geomean'], .25)
        self.assertEqual(group['xff_over_reference_cpu_geomean'], .5)
        with self.assertRaises(ValueError):
            report.task_summaries([first, first, second], contract)

    def test_resource_summary_uses_samples_selected_by_wall_time(self):
        row = self.row()
        # A slow timing with tiny CPU must not enter the fastest-two resource mean.
        slow = next(sample for sample in row['samples']
                    if sample['label'] == 'xff' and sample['round'] == 3)
        slow.update(elapsed_seconds=100, user_cpu_seconds=.01, system_cpu_seconds=.01)
        result = report.summarize_cell(row, self.contract())
        self.assertEqual(result['xff_seconds'], 4)
        self.assertEqual(result['xff_cpu_seconds'], 3)

    def test_current_reference_summary_uses_fixed_anchor_and_preserves_cells(self):
        first = report.summarize_cell(self.row(), self.contract())
        second = dict(first, sha='other', xff_seconds=100, historical_over_current=50)
        separate = dict(first, files=1000)
        summaries = report.current_reference_summaries([first, second, separate])
        self.assertEqual(len(summaries), 2)
        self.assertEqual(summaries[0]['historical_contexts'], 2)
        self.assertEqual(summaries[0]['current_over_reference_geomean'], .25)
        self.assertEqual(summaries[0]['current_over_reference_p10'], .25)
        self.assertEqual(summaries[0]['current_over_reference_p90'], .25)
        self.assertEqual(summaries[0]['current_over_reference_cpu_geomean'], .25)
        self.assertEqual(summaries[1]['historical_contexts'], 1)
        self.assertEqual(summaries[1]['current_over_reference_p10'], .25)
        with self.assertRaises(ValueError):
            report.current_reference_summaries([first, first])


if __name__ == '__main__':
    unittest.main()
