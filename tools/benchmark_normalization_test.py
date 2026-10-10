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
                lambda report: report['contract'].update(runner_class='different-host'),
                lambda report: report['contract'].update(storage={'filesystem': 'disk'}),
                lambda report: report['contract'].update(affinity_by_cpu_count={'1': None}),
                lambda report: report['contract'].update(resource_accounting={
                    'sha256': 'a' * 64, 'memory_method': 'native fork/wait4 v1', 'wall_method': 'native boundary included'}),
                lambda report: report['tasks'][0]['participants']['rg'].update(pipeline=[['rg', '--different']])):
            with self.subTest(change=change):
                inputs = records(6)
                change(inputs[-1][1]['tool_comparisons'])
                values = normalization.reference_windows(inputs)
                self.assertEqual(next(iter(values[inputs[-1][0]].values()))['status'], 'unavailable')
                first = next(iter(values[inputs[0][0]].values()))
                self.assertEqual(first['reference_mean_seconds'], 6)

    def test_ci_backfill_and_main_runner_names_share_reference_history(self):
        for platform, runner in (('linux', 'ubuntu-latest'), ('macos', 'macos-latest')):
            with self.subTest(platform=platform):
                inputs = records(6)
                for index, (_, record) in enumerate(inputs):
                    record['platform'] = platform
                    record['source'].update(event='push', head_branch='main')
                    contract = record['tool_comparisons']['contract']
                    if index < 5:
                        record.update(kind='backfill', purpose='ci-replacement',
                                      series='github-ci-' + platform,
                                      revision={'date': record['source']['created_at']})
                        contract['runner_class'] = 'github-ci-' + platform
                    else:
                        contract['runner_class'] = 'github-hosted ' + runner
                original = copy.deepcopy(inputs)
                values = normalization.reference_windows(inputs)
                current = next(iter(values[inputs[-1][0]].values()))
                self.assertEqual(current['status'], 'available')
                self.assertEqual(current['reference_mean_seconds'], 8)
                self.assertEqual([row['report'] for row in current['window']],
                                 [identity for identity, _ in inputs[1:]])
                self.assertEqual(inputs, original)
                inputs[-1][1]['tool_comparisons']['contract']['runner_class'] = 'github-hosted different-runner'
                values = normalization.reference_windows(inputs)
                self.assertEqual(next(iter(values[inputs[-1][0]].values()))['status'], 'unavailable')

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

    def test_local_layout_versions_keep_independent_reference_windows(self):
        legacy, versioned = [], []
        for identity, record in records(5):
            record.update(kind='backfill', purpose='local-addition', series='linux-zen5',
                          completed_at=record['source']['created_at'],
                          revision={'date': record['source']['created_at']})
            legacy.append(('local/legacy/' + identity, record))
            newer = copy.deepcopy(record)
            newer['completed_at'] = record['completed_at'].replace('T00:', 'T01:')
            newer['tool_comparisons']['contract']['layouts'] = [{'name': 'broad', 'revision': 2}]
            newer['tool_comparisons']['tasks'][0]['fixture_identity'] = 'versioned-fixture'
            newer['tool_comparisons']['tasks'][0]['participants']['rg']['samples'][0]['elapsed_seconds'] *= 3
            versioned.append(('local/versioned/' + identity, newer))
        inputs = [*legacy, *versioned]
        original = copy.deepcopy(inputs)
        values = normalization.reference_windows(inputs)
        for series in (legacy, versioned):
            isolated = normalization.reference_windows(series)
            for identity, _ in series:
                self.assertEqual(values[identity], isolated[identity])
                self.assertEqual(next(iter(values[identity].values()))['status'], 'available')
        self.assertEqual(normalization.reference_windows(list(reversed(inputs))), values)
        self.assertEqual(inputs, original)

    def test_duplicate_commit_preserves_cells_absent_from_newer_report(self):
        inputs = records(5)
        for _, record in inputs:
            larger = copy.deepcopy(record['tool_comparisons']['tasks'][0])
            larger.update(files=20, input_files=20, fixture_identity='larger-fixture')
            record['tool_comparisons']['tasks'].append(larger)
        original_identity, original = inputs[-1]
        duplicate = copy.deepcopy(original)
        duplicate['source']['run_attempt'] = 2
        duplicate['tool_comparisons']['tasks'] = duplicate['tool_comparisons']['tasks'][:1]
        inputs.append(('runs/5/2/linux/report.json', duplicate))
        values = normalization.reference_windows(inputs)
        shared = normalization.cell_key(original['tool_comparisons']['tasks'][0], 'rg')
        retained = normalization.cell_key(original['tool_comparisons']['tasks'][1], 'rg')
        self.assertEqual(values[original_identity][shared]['status'], 'unavailable')
        self.assertEqual(values[inputs[-1][0]][shared]['status'], 'available')
        self.assertEqual(values[original_identity][retained]['status'], 'available')
        self.assertEqual(values[original_identity][retained]['window'][-1]['report'], original_identity)

    def test_previews_never_change_merged_or_other_preview_windows(self):
        merged = records(5)
        preview = records(6)[-1]
        preview[1]['source']['event'] = 'pull_request'
        other = records(7)[-1]
        other[1]['kind'] = 'pr-preview'
        original = normalization.reference_windows(merged)
        values = normalization.reference_windows([*merged, preview, other])
        for identity, _ in merged:
            self.assertEqual(values[identity], original[identity])
        for candidate, excluded in ((preview, other), (other, preview)):
            expected = normalization.reference_windows([*merged, candidate])
            self.assertEqual(values[candidate[0]], expected[candidate[0]])
            window = next(iter(values[candidate[0]].values()))['window']
            self.assertNotIn(excluded[0], [row['report'] for row in window])
        # Several unmerged PRs cannot fill a short merged history for each other.
        short = normalization.reference_windows([*merged[:3], preview, other])
        self.assertEqual(next(iter(short[preview[0]].values()))['status'], 'unavailable')
        self.assertEqual(next(iter(short[other[0]].values()))['status'], 'unavailable')

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
