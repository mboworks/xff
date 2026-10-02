# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Local publication keeps complete observations distinct from CI replacement data."""

import copy
import json
from pathlib import Path
import tempfile
import unittest

import benchmark_campaign as campaign
import benchmark_history as history
import benchmark_landscape as landscape
import benchmark_local as local


def batch_data():
    revision = dict(sha='a' * 40, date='2026-09-01T12:00:00Z', subject='Historical merge')
    allocation = dict(kind='workers', cpu_ids=None, required=False)
    tools = {name: dict(status='available', path='/' + name, sha256='b' * 64, version='1')
             for name in ('find', 'rg', 'fzf')}
    storage = dict(filesystem='host filesystem', memory_required=False, parent='/tmp')
    contract = dict(schema=1, series='macos-test', purpose='local-addition', replacement_target=None,
                    platform='macos', revisions=[revision], files=[10], cpus=[1, 3, 10], depth=40,
                    repetitions=3, retained=2, driver={'driver.py': 'c' * 64}, allocation=allocation,
                    storage=storage, environment=dict(machine='arm64', cpu_count=18, platform='Darwin', tools=tools))
    batch = dict(identity=campaign.identity(contract), contract=contract)
    sample = {metric: 1 for metric in campaign.compare.METRICS}
    sample.update(stderr='', exit_codes=[0])
    tasks = []
    for name, _, participants in campaign.compare.scenarios(Path('.'), [], {'xff': {'path': 'xff'}, **tools}):
        for cpu in contract['cpus']:
            for shape in ('broad', 'deep'):
                tasks.append(dict(name=name, files=10, input_files=10, cpus=cpu, dataset=shape,
                                  shape=f'{cpu}cpu/10/{shape}', expected_count=10, skips={},
                                  participants={tool: dict(pipeline=[[tool]], samples=[dict(sample) for _ in range(3)])
                                                for tool in participants}))
    report = dict(schema=1, tools={**tools, 'xff': dict(sha256='d' * 64)}, tasks=tasks,
                  contract=dict(file_counts=[10], cpu_counts=[1, 3, 10], depth=40, repetitions=3, retained=2,
                                batch=batch['identity'], runner_class='macos-test', estimator='mean-fastest',
                                machine='arm64', storage=storage, build_identity='{}', platform='Darwin',
                                cpu_count=18, affinity_by_cpu_count={'1': None, '3': None, '10': None}))
    record = dict(schema=1, kind='backfill', head=revision['sha'], revision=revision, batch=batch['identity'],
                  build=dict(sha256='d' * 64, revision=revision['sha'], configuration={}),
                  started_at='2026-10-01T12:00:00Z', completed_at='2026-10-01T13:00:00Z',
                  tool_comparisons=report,
                  **{key: contract[key] for key in ('purpose', 'platform', 'series', 'replacement_target', 'allocation')})
    return batch, record


def write_batch(root, batch, record):
    root.mkdir(parents=True, exist_ok=True)
    (root / 'batch.json').write_text(json.dumps(batch))
    (root / 'status.json').write_text(json.dumps({record['head']: {'status': 'complete'}}))
    path = root / 'reports' / record['head'] / 'benchmark-report-macos-arm64.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(record))
    return path


class BenchmarkLocalTest(unittest.TestCase):
    def test_publish_round_trip_and_homepage_series(self):
        batch, record = batch_data()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_batch(root / 'incoming', batch, record)
            destination = local.retain(root / 'site', root / 'incoming')
            self.assertEqual(json.loads((destination / record['head'] / 'report.json').read_text()), record)
            self.assertEqual(local.retain(root / 'site', root / 'incoming'), destination)
            index = history.render_site(root / 'site', [], 'owner/repo')
            self.assertIn('macos-test', index)
            self.assertNotIn('/actions/runs/0', index)
            (root / 'site/index.html').write_text(index)
            self.assertEqual(landscape.publish(root / 'site', '/* renderer */'), 1)
            index = (root / 'site/index.html').read_text()
            self.assertIn('Local / macos-test / arm64', index)
            self.assertIn('"local": true', index)
            payload = json.loads((destination / record['head'] / 'landscape.json').read_text())
            self.assertEqual(len(payload['allocation_pairs']), 3)
            self.assertEqual(campaign.benchmark_records.replacements(root / 'site'), {})

    def test_reject_incomplete_modified_or_misidentified_batches_before_writing(self):
        mutations = [lambda b, r: r['tool_comparisons']['tasks'].pop(),
                     lambda b, r: r.update(purpose='ci-replacement'),
                     lambda b, r: r.update(source={'id': 1}),
                     lambda b, r: r['build'].update(sha256='e' * 64),
                     lambda b, r: r['tool_comparisons']['contract'].update(cpu_counts=[1, 3]),
                     lambda b, r: b['contract'].update(series='../escape'),
                     lambda b, r: r['tool_comparisons']['tasks'][0]['participants']['xff']['samples'].pop()]
        for mutate in mutations:
            with self.subTest(mutate=mutate), tempfile.TemporaryDirectory() as temporary:
                batch, record = batch_data()
                mutate(batch, record)
                root = Path(temporary)
                write_batch(root / 'incoming', batch, record)
                with self.assertRaises(ValueError):
                    local.retain(root / 'site', root / 'incoming')
                self.assertFalse((root / 'site').exists())

    def test_retained_observations_are_immutable(self):
        batch, record = batch_data()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = write_batch(root / 'incoming', batch, record)
            destination = local.retain(root / 'site', root / 'incoming')
            changed = copy.deepcopy(record)
            changed['tool_comparisons']['tasks'][0]['participants']['xff']['samples'][0]['elapsed_seconds'] = 2
            path.write_text(json.dumps(changed))
            with self.assertRaisesRegex(ValueError, 'existing local batch cannot change'):
                local.retain(root / 'site', root / 'incoming')
            self.assertEqual(json.loads((destination / record['head'] / 'report.json').read_text()), record)


if __name__ == '__main__':
    unittest.main()
