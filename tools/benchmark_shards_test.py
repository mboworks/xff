# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Complete case ownership and strict shard aggregation."""
import copy
import gzip
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import benchmark_shards as shards


def records(cpus=(1, 4), count=3, partition='balanced', repetitions=9, keep=7):
    plan = shards.make_plan([10, 100], cpus, count, task_names=['files', 'content'],
                            partition=partition, repetitions=repetitions, keep=keep)
    rounds = repetitions // count if partition == 'samples' else 1
    result = []
    for index in range(count):
        tasks = []
        for files, cpu, dataset in shards.assigned_fixtures(plan, index):
            for name in plan['task_names']:
                tasks.append(dict(files=files, cpus=cpu, dataset=dataset, name=name,
                                  input_files=files, expected_count=files, expected_sha256='same-oracle',
                                  fixture_identity=f'{dataset}/{files}',
                                  shape=f'{cpu}cpu/{files}/{dataset}', participants={
                                      'xff': {'pipeline': [['xff', '--jobs=' + str(cpu), '.']],
                                              'cwd': f'/tmp/shard-{index}/{dataset}', 'stdin': f'/tmp/shard-{index}/empty',
                                              'samples': [{'elapsed_seconds': index * rounds + number + 1}
                                                          for number in range(rounds)]}}, skips={}))
        result.append({'head': 'same-head', 'tool_comparisons': {
            'schema': 1, 'tools': {'xff': {'sha256': 'same-binary'}}, 'tasks': tasks, 'host_id': f'host-{index}',
            'contract': {'file_counts': [10, 100], 'cpu_counts': list(cpus), 'repetitions': rounds, 'retained': rounds,
                         'estimator': 'mean-fastest',
                         'storage': {'parent': '/tmp', 'filesystem': 'host'},
                         'affinity_by_cpu_count': {str(cpu): list(range(cpu)) for cpu in cpus},
                         'shard': {'index': index, 'plan': plan}}}})
    result[0]['base'] = 'paired-base'
    return result


class BenchmarkShardsTest(unittest.TestCase):
    def test_sample_shards_each_own_the_entire_matrix(self):
        plan = shards.make_plan([10, 100, 1000], [1, 3], 3, task_names=['files'], partition='samples')
        expected = {(files, cpu, shape) for files in (10, 100, 1000) for cpu in (1, 3) for shape in ('broad', 'deep')}
        for index in range(3):
            self.assertEqual(shards.assigned_fixtures(plan, index), expected)
        self.assertEqual(len({shard['estimated_cost'] for shard in plan['shards']}), 1)
        self.assertEqual(shards.sample_policy(plan), (3, 9, 7))

    def test_pool_raw_rounds_then_select_fastest_globally(self):
        for repetitions, keep, mean in ((9, 7, 4), (3, 2, 1.5)):
            with self.subTest(repetitions=repetitions):
                inputs = records(cpus=(1, 3), partition='samples', repetitions=repetitions, keep=keep)
                original = copy.deepcopy(inputs)
                result = shards.merge_reports(inputs)
                self.assertEqual(inputs, original)
                report = result['tool_comparisons']
                self.assertEqual(len(report['tasks']), 16)
                self.assertEqual(report['contract']['repetitions'], repetitions)
                self.assertEqual(report['contract']['retained'], keep)
                self.assertNotIn('host_id', report)
                self.assertEqual([shard['host_id'] for shard in result['measurement_shards']], ['host-0', 'host-1', 'host-2'])
                for task in report['tasks']:
                    entry = task['participants']['xff']
                    self.assertEqual(len(entry['samples']), repetitions)
                    self.assertEqual({sample['shard_index'] for sample in entry['samples']}, {0, 1, 2})
                    self.assertEqual([sample['elapsed_seconds'] for sample in entry['samples']], list(range(1, repetitions + 1)))
                    self.assertEqual(shards.benchmark_matrix.elapsed_mean(report, entry), mean)
                    self.assertEqual([source['cwd'] for source in entry['sources']],
                                     [f"/tmp/shard-{index}/{task['dataset']}" for index in range(3)])
                self.assertIn('Raw samples are pooled', shards.benchmark_matrix.render_markdown(report))
                self.assertIn('Raw samples are pooled', shards.benchmark_matrix.render_html(report))
                self.assertEqual(shards.merge_reports(list(reversed(inputs))), result)

    def test_sample_shards_reject_missing_changed_or_prefiltered_observations(self):
        def participant(value):
            return value[1]['tool_comparisons']['tasks'][0]['participants']['xff']
        for change, message in (
                (lambda value: value.pop(), 'missing shard'),
                (lambda value: value.__setitem__(1, copy.deepcopy(value[0])), 'duplicate shard'),
                (lambda value: value[1]['tool_comparisons']['tasks'].pop(), 'task set'),
                (lambda value: value[1]['tool_comparisons']['tasks'].append(copy.deepcopy(value[1]['tool_comparisons']['tasks'][0])), 'duplicate'),
                (lambda value: participant(value)['samples'].pop(), 'missing tool comparison samples'),
                (lambda value: participant(value)['samples'][0].update(elapsed_seconds=float('nan')), 'finite'),
                (lambda value: participant(value)['pipeline'][0].append('--changed'), 'command'),
                (lambda value: participant(value).update(stdin='/tmp/shard-1/unexpected.paths'), 'command'),
                (lambda value: value[1]['tool_comparisons']['tasks'][0].update(fixture_identity='other'), 'task identity'),
                (lambda value: value[1]['tool_comparisons']['tasks'][0]['participants'].pop('xff'), 'participants'),
                (lambda value: value[1]['tool_comparisons'].pop('host_id'), 'host identity'),
                (lambda value: value[1]['tool_comparisons']['contract'].update(cpu_count=5), 'contract.cpu_count'),
                (lambda value: value[1]['tool_comparisons']['tools']['xff'].update(sha256='other'), 'tools'),
                (lambda value: [record['tool_comparisons']['contract'].update(retained=2) for record in value], 'retain every')):
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                value = records(partition='samples')
                change(value)
                shards.merge_reports(value)

    def test_main_baseline_is_attached_after_pooling_pr_samples(self):
        previous = shards.merge_reports(records(partition='samples'))
        previous['source'] = {'id': 42, 'created_at': '2026-10-03', 'run_attempt': 1,
                              'event': 'push', 'head_branch': 'main'}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'runs' / '42' / '1' / 'linux' / 'report.json'
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps(previous))
            current = shards.merge_reports(records(partition='samples', repetitions=3, keep=2), root)
        baseline = current['tool_comparisons']['baseline']
        self.assertEqual(baseline['status'], 'available')
        self.assertEqual(baseline['policy'], 'mean of fastest 7/9 runs')
        self.assertEqual(baseline['averages']['broad/files/1/10']['xff'], 4)
        self.assertEqual(shards.benchmark_matrix.policy(current['tool_comparisons']), 'mean of fastest 2/3 runs')

    def test_sample_plan_rejects_incomplete_grids_and_unequal_rounds(self):
        for repetitions, keep in ((8, 7), (0, 0), (3, 4), (9, 0)):
            with self.subTest(repetitions=repetitions, keep=keep), self.assertRaisesRegex(ValueError, 'sample shards'):
                shards.make_plan([10], [1, 3], 3, task_names=['files'], partition='samples', repetitions=repetitions, keep=keep)
        for change in ('missing', 'duplicate', 'partition'):
            with self.subTest(change=change):
                plan = records(partition='samples')[0]['tool_comparisons']['contract']['shard']['plan']
                if change == 'missing':
                    plan['shards'][1]['fixtures'].pop()
                elif change == 'duplicate':
                    plan['shards'][1]['fixtures'][0] = copy.deepcopy(plan['shards'][1]['fixtures'][1])
                else:
                    plan['partition'] = 'unknown'
                with self.assertRaises(ValueError):
                    shards.assigned_fixtures(plan, 0)

    def test_every_fixture_has_one_owner(self):
        plan = shards.make_plan([10, 20, 50, 100], [1, 4], 3, task_names=['files'])
        owned = [shards.assigned_fixtures(plan, index) for index in range(3)]
        self.assertEqual(sum(map(len, owned)), 16)
        self.assertEqual(len(set.union(*owned)), 16)
        self.assertTrue(all(owned))
        self.assertEqual(plan, shards.make_plan([10, 20, 50, 100], [1, 4], 3, task_names=['files']))

    def test_measured_costs_inform_assignment(self):
        reference = records()[0]['tool_comparisons']
        plan = shards.make_plan([10, 100], [1, 4], 3, reference)
        self.assertEqual(plan['task_names'], ['content', 'files'])
        self.assertTrue(all(shard['estimated_cost'] > 0 for shard in plan['shards']))

    def test_merge_retains_all_cases_and_provenance(self):
        inputs = records()
        original = copy.deepcopy(inputs)
        result = shards.merge_reports(list(reversed(inputs)))
        self.assertEqual(result['base'], 'paired-base')
        self.assertEqual(len(result['tool_comparisons']['tasks']), 16)
        self.assertEqual(len(result['measurement_shards']), 3)
        self.assertNotIn('shard', result['tool_comparisons']['contract'])
        self.assertEqual(inputs, original)
        self.assertEqual(result['tool_comparisons']['relative_results'], [])
        self.assertEqual(result['tool_comparisons']['alarm']['cells'], [])

    def test_missing_duplicate_and_incompatible_inputs_fail(self):
        cases = []
        value = records()
        value[1]['tool_comparisons']['baseline'] = {}
        cases.append(value)
        value = records()
        value.pop()
        cases.append(value)
        value = records()
        value[1] = copy.deepcopy(value[0])
        cases.append(value)
        value = records()
        value[1]['head'] = 'different'
        cases.append(value)
        value = records()
        value[1]['tool_comparisons']['tools']['xff']['sha256'] = 'other'
        cases.append(value)
        value = records()
        value[1]['tool_comparisons']['tasks'].pop()
        cases.append(value)
        value = records()
        for record in value:
            record['tool_comparisons']['tasks'] = [task for task in record['tool_comparisons']['tasks'] if task['name'] == 'files']
        cases.append(value)
        for value in cases:
            with self.subTest(value=value), self.assertRaises(ValueError):
                shards.merge_reports(value)

    def test_macos_cpu_capacity_mismatch_identifies_field_and_shard(self):
        inputs = records()
        for index, record in enumerate(inputs):
            record['tool_comparisons']['contract'].update(
                cpu_count=5 if index == 0 else 3, affinity_by_cpu_count={'1': None, '4': None})
        with self.assertRaisesRegex(ValueError, r'incompatible shard 1: contract\.cpu_count: expected 5, got 3'):
            shards.merge_reports(inputs)

    def test_single_host_three_worker_grid_retains_every_case(self):
        inputs = records(cpus=(1, 3), count=1)
        inputs[0]['tool_comparisons']['contract'].update(
            cpu_count=3, affinity_by_cpu_count={'1': None, '3': None})
        result = shards.merge_reports(inputs)
        self.assertEqual(len(result['tool_comparisons']['tasks']), 16)
        self.assertEqual(result['tool_comparisons']['contract']['cpu_counts'], [1, 3])
        self.assertEqual(len(result['measurement_shards']), 1)

    def test_merge_directory_accepts_flat_and_per_artifact_downloads(self):
        for count, nested in ((1, False), (1, True), (3, True)):
            with self.subTest(count=count, nested=nested), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                incoming = root / 'shards'
                inputs = records(cpus=(1, 3), count=count)
                for index, record in enumerate(inputs):
                    folder = incoming / f'artifact-{index}' if nested else incoming
                    folder.mkdir(parents=True, exist_ok=True)
                    (folder / 'benchmark-shard.json').write_text(json.dumps(record))
                # Other JSON artifacts are not measurement shards.
                (incoming / 'metadata.json').write_text('{}')
                output = root / 'merged.json'
                with mock.patch('sys.argv', ['benchmark_shards.py', '--merge-directory', str(incoming),
                                             '--output', str(output)]):
                    shards.main()
                self.assertEqual(json.loads(output.read_text()), shards.merge_reports(inputs))

    def test_merge_directory_rejects_missing_and_duplicate_shards_without_output(self):
        for failure in ('empty', 'missing', 'duplicate'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                inputs = [] if failure == 'empty' else records(cpus=(1, 3), count=3)
                if failure == 'missing':
                    inputs.pop()
                elif failure == 'duplicate':
                    inputs[1] = copy.deepcopy(inputs[0])
                for index, record in enumerate(inputs):
                    folder = root / f'artifact-{index}'
                    folder.mkdir()
                    (folder / 'benchmark-shard.json').write_text(json.dumps(record))
                output = root / 'merged.json'
                with mock.patch('sys.argv', ['benchmark_shards.py', '--merge-directory', str(root),
                                             '--output', str(output)]), self.assertRaises(ValueError):
                    shards.main()
                self.assertFalse(output.exists())

    def test_identity_diagnostic_includes_nested_and_missing_fields(self):
        self.assertEqual(shards.identity_difference({'tools': {'rg': {'sha256': 'old'}}},
                                                    {'tools': {'rg': {'sha256': 'new'}}}),
                         "tools.rg.sha256: expected 'old', got 'new'")
        self.assertEqual(shards.identity_difference({'cpu_count': None}, {}),
                         'cpu_count: missing (expected None)')
        self.assertEqual(shards.identity_difference({}, {'cpu_count': 3}),
                         'cpu_count: unexpected value 3')

    def test_contract_affinity_and_task_mismatches_fail(self):
        for change, message in (
                (lambda value: value[1]['tool_comparisons']['contract'].update(repetitions=2), 'contract'),
                (lambda value: value[1]['tool_comparisons']['contract']['affinity_by_cpu_count'].update({'1': [1]}), 'affinity'),
                (lambda value: value[1]['tool_comparisons']['tasks'].append(
                    copy.deepcopy(value[1]['tool_comparisons']['tasks'][0])), 'duplicate'),
                (lambda value: value[1]['tool_comparisons']['tasks'][0].update(name='unexpected'), 'task set')):
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                inputs = records()
                change(inputs)
                shards.merge_reports(inputs)
        with self.assertRaisesRegex(ValueError, 'no shard'):
            shards.merge_reports([])

    def test_reference_uses_latest_main_measurement_for_this_architecture(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for run, machine, branch in ((1, 'arm64', 'main'), (2, 'x86_64', 'main'),
                                         (3, 'arm64', 'main'), (4, 'arm64', 'feature')):
                destination = root / 'runs' / str(run) / '1' / 'macos'
                destination.mkdir(parents=True)
                (destination / 'report.json').write_text(json.dumps({
                    'source': {'id': run, 'created_at': str(run), 'event': 'push', 'head_branch': branch},
                    'tool_comparisons': {'contract': {'machine': machine}, 'marker': run}}))
            packed = root / 'runs/3/1/macos/report.json'
            packed.with_suffix('.json.gz').write_bytes(gzip.compress(packed.read_bytes()))
            packed.unlink()
            self.assertEqual(shards.latest_reference(root, 'arm64')['marker'], 3)
            self.assertIsNone(shards.latest_reference(root, 'other'))

    def test_invalid_plan_fails(self):
        for counts, cpus, count in (([], [1], 1), ([0], [1], 1), ([1, 1], [1], 1), ([1], [1], 9)):
            with self.subTest(counts=counts), self.assertRaises(ValueError):
                shards.make_plan(counts, cpus, count, task_names=['files'])
        plan = records()[0]['tool_comparisons']['contract']['shard']['plan']
        plan['shards'][0]['fixtures'].pop()
        with self.assertRaisesRegex(ValueError, 'fixture plan'):
            shards.assigned_fixtures(plan, 0)


if __name__ == '__main__':
    unittest.main()
