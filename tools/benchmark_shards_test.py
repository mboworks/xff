# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Complete case ownership and strict shard aggregation."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

import benchmark_shards as shards


def records(cpus=(1, 4), count=3):
    plan = shards.make_plan([10, 100], cpus, count, task_names=['files', 'content'])
    result = []
    for index in range(count):
        tasks = []
        for files, cpu, dataset in shards.assigned_fixtures(plan, index):
            for name in plan['task_names']:
                tasks.append(dict(files=files, cpus=cpu, dataset=dataset, name=name,
                                  shape=f'{cpu}cpu/{files}/{dataset}', participants={
                                      'xff': {'samples': [{'elapsed_seconds': 1}]}}, skips={}))
        result.append({'head': 'same-head', 'tool_comparisons': {
            'schema': 1, 'tools': {'xff': {'sha256': 'same-binary'}}, 'tasks': tasks,
            'contract': {'file_counts': [10, 100], 'cpu_counts': list(cpus), 'repetitions': 1, 'retained': 1,
                         'affinity_by_cpu_count': {str(cpu): list(range(cpu)) for cpu in cpus},
                         'shard': {'index': index, 'plan': plan}}}})
    result[0]['base'] = 'paired-base'
    return result


class BenchmarkShardsTest(unittest.TestCase):
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
