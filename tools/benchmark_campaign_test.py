# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Complete-revision campaigns, atomic promotion and preserved historical provenance."""

import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import benchmark_campaign as campaign
import benchmark_history as history
import benchmark_landscape as landscape

HEAD = 'a' * 40
DRIVER = 'b' * 40


def plan():
    jobs = [{'id': platform + '-' + HEAD, 'platform': platform, 'os': runner, 'arch': arch,
             'revision': {'sha': HEAD, 'date': '2026-09-01T12:00:00+00:00', 'subject': 'Historical merge'}}
            for platform, (runner, arch) in campaign.PLATFORMS.items()]
    result = {'schema': 1, 'source_head': DRIVER, 'history_head': 'c' * 40,
              'created_at': '2026-10-01T12:00:00Z', 'jobs': jobs,
              'contract': {'files': campaign.FILE_COUNTS, 'cpus': [1, 3], 'depth': 40,
                           'repetitions': 9, 'retained': 7, 'driver': {'driver.py': 'digest'},
                           'tasks': {'files': ['rg', 'xff'], 'content': ['rg', 'xff']}}}
    result['identity'] = campaign.identity(result)
    return result


def source():
    return {'name': 'Benchmark backfill', 'path': '.github/workflows/benchmark_backfill.yml',
            'event': 'workflow_dispatch', 'head_branch': 'main', 'status': 'completed', 'conclusion': 'success',
            'repository': {'full_name': 'owner/repo'}, 'id': 20, 'run_attempt': 2,
            'created_at': '2026-10-01T12:00:00Z', 'head_sha': DRIVER}


def job_data(selected, job):
    expected = selected['contract']
    sha = job['revision']['sha']
    linux = job['platform'] == 'linux'
    tools = {name: {'status': 'available', 'path': '/' + name, 'sha256': name, 'version': '1'}
             for name in ('find', 'rg', 'fzf')}
    allocation = {'kind': 'logical-cpus' if linux else 'workers',
                  'cpu_ids': [0, 1, 2] if linux else None, 'required': linux}
    storage = {'filesystem': 'tmpfs' if linux else 'unverified host filesystem',
               'memory_required': linux, 'parent': '/tmp'}
    contract = {**{key: expected[key] for key in ('files', 'cpus', 'depth', 'repetitions', 'retained', 'driver')},
                'campaign': selected['identity'], 'purpose': 'ci-replacement', 'platform': job['platform'],
                'series': 'github-ci-' + job['platform'], 'replacement_target': 'github-ci-' + job['platform'],
                'revisions': [job['revision']], 'allocation': allocation, 'storage': storage,
                'collection': {'id': 20, 'run_attempt': 1, 'head_sha': DRIVER},
                'environment': {'machine': job['arch'], 'cpu_count': 4 if linux else 3,
                                'platform': job['platform'], 'tools': tools, 'host_id': job['id']}}
    batch = {'contract': contract, 'identity': campaign.identity(contract)}
    sample = {metric: 1 for metric in campaign.compare.METRICS}
    sample.update(stderr='', exit_codes=[0])
    tasks = []
    for files in expected['files']:
        for cpus in expected['cpus']:
            for shape in ('broad', 'deep'):
                for name in expected['tasks']:
                    tasks.append({'files': files, 'input_files': files, 'cpus': cpus, 'dataset': shape, 'name': name,
                                  'shape': f'{cpus}cpu/{files}/{shape}', 'expected_count': files, 'skips': {},
                                  'participants': {tool: {'pipeline': [[tool]], 'samples': [dict(sample) for _ in range(9)]}
                                                   for tool in ('xff', 'rg')}})
    report = {'schema': 1, 'tools': {**tools, 'xff': {'sha256': 'd' * 64}}, 'tasks': tasks,
              'contract': {'file_counts': expected['files'], 'cpu_counts': [1, 3], 'depth': 40,
                           'repetitions': 9, 'retained': 7, 'batch': batch['identity'],
                           'runner_class': contract['series'], 'estimator': 'mean-fastest', 'machine': job['arch'],
                           'storage': storage, 'build_identity': '{}', 'platform': job['platform'],
                           'cpu_count': contract['environment']['cpu_count'],
                           'affinity_by_cpu_count': {'1': [0], '3': [0, 1, 2]} if linux else {'1': None, '3': None}}}
    record = {'schema': 1, 'kind': 'backfill', 'head': sha, 'revision': job['revision'],
              'build': {'sha256': 'd' * 64, 'revision': sha, 'configuration': {}},
              'batch': batch['identity'], 'tool_comparisons': report,
              **{key: contract[key] for key in ('purpose', 'platform', 'series', 'replacement_target', 'allocation')}}
    return batch, record


def write_artifacts(root, selected=None):
    selected = selected or plan()
    folder = root / 'benchmark-backfill-plan'
    folder.mkdir(parents=True)
    (folder / 'campaign.json').write_text(json.dumps(selected))
    for job in selected['jobs']:
        folder = root / ('benchmark-backfill-' + job['id'])
        reports = folder / 'reports' / job['revision']['sha']
        reports.mkdir(parents=True)
        batch, record = job_data(selected, job)
        (folder / 'batch.json').write_text(json.dumps(batch))
        (folder / 'status.json').write_text(json.dumps({job['revision']['sha']: {'status': 'complete'}}))
        (reports / f"benchmark-report-{job['platform']}-{job['arch']}.json").write_text(json.dumps(record))


class BenchmarkCampaignTest(unittest.TestCase):
    def test_planner_freezes_each_complete_revision_once_per_platform(self):
        revisions = [plan()['jobs'][0]['revision'], dict(plan()['jobs'][0]['revision'], sha='e' * 40)]
        with mock.patch.object(campaign.backfill, 'revisions', return_value=revisions), \
                mock.patch.object(campaign.backfill, 'git', side_effect=[DRIVER, 'c' * 40]):
            selected = campaign.make_plan(Path('/repo'), Path('/history'))
        self.assertEqual(len(selected['jobs']), 4)
        self.assertEqual(len({job['id'] for job in selected['jobs']}), 4)
        self.assertEqual(selected['contract']['files'], campaign.FILE_COUNTS)
        self.assertEqual(selected['contract']['cpus'], [1, 3])
        self.assertIn('content-needle', selected['contract']['tasks'])
        campaign.validate_plan(selected)

    def test_plan_rejects_mutation_duplicates_partial_grid_and_missing_platform(self):
        mutations = [lambda value: value['jobs'].append(value['jobs'][0]),
                     lambda value: value['jobs'].pop(),
                     lambda value: value['contract'].update(files=[10]),
                     lambda value: value['jobs'][0].update(id='../unsafe')]
        for mutate in mutations:
            selected = plan()
            mutate(selected)
            with self.subTest(mutate=mutate), self.assertRaisesRegex(ValueError, 'manifest identity'):
                campaign.validate_plan(selected)
            selected['identity'] = campaign.identity({key: value for key, value in selected.items() if key != 'identity'})
            with self.assertRaises(ValueError):
                campaign.validate_plan(selected)

    def test_job_uses_frozen_revision_and_grid_on_one_host(self):
        selected = plan()
        job = selected['jobs'][0]
        batch, _ = job_data(selected, job)
        environment = {'GITHUB_RUN_ID': '20', 'GITHUB_RUN_ATTEMPT': '2', 'GITHUB_SHA': DRIVER}
        with mock.patch.object(campaign.backfill, 'git', return_value=DRIVER), \
                mock.patch.object(campaign.backfill, 'driver_identity', return_value=selected['contract']['driver']), \
                mock.patch.object(campaign.backfill, 'host_platform', return_value='linux'), \
                mock.patch.object(campaign.backfill.platform, 'machine', return_value='x86_64'), \
                mock.patch.dict(campaign.os.environ, environment), \
                mock.patch.object(campaign.backfill, 'prepare', return_value=batch) as prepare, \
                mock.patch.object(campaign.backfill, 'run', return_value=0) as run:
            result = campaign.run_job(selected, job['id'], Path('/repo'), Path('/output'), None, Path('/dev/shm'))
        self.assertEqual(result, 0)
        args = prepare.call_args.args[0]
        self.assertEqual(args.revision, [HEAD])
        self.assertEqual(args.cpus, [1, 3])
        self.assertTrue(args.require_memory)
        self.assertTrue(args.require_cpu_affinity)
        self.assertEqual(prepare.call_args.kwargs['campaign'], selected['identity'])
        run.assert_called_once_with(args, batch)

    def test_complete_campaign_is_retained_atomically_and_replay_is_immutable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifacts, site = root / 'artifacts', root / 'site'
            write_artifacts(artifacts)
            destination = campaign.retain(site, artifacts, source(), 'owner/repo')
            self.assertEqual(destination, site / 'backfills/20/2')
            reports = sorted(destination.glob('*/*/report.json'))
            self.assertEqual(len(reports), 2)
            before = {path.relative_to(destination): path.read_bytes() for path in destination.rglob('*.json')}
            campaign.retain(site, artifacts, source(), 'owner/repo')
            self.assertEqual(before, {path.relative_to(destination): path.read_bytes() for path in destination.rglob('*.json')})

            record = json.loads(reports[0].read_text())
            self.assertEqual(record['head'], HEAD)
            self.assertEqual(record['source']['head_sha'], DRIVER)
            self.assertEqual(record['collection']['run_attempt'], 1)
            self.assertNotIn('base', record)
            incoming = next(artifacts.glob('benchmark-backfill-linux-*/reports/*/*.json'))
            changed = json.loads(incoming.read_text())
            changed['tool_comparisons']['tasks'][0]['participants']['xff']['samples'][0]['elapsed_seconds'] = 2
            incoming.write_text(json.dumps(changed))
            with self.assertRaisesRegex(ValueError, 'existing campaign cannot change'):
                campaign.retain(site, artifacts, source(), 'owner/repo')
            self.assertEqual(before, {path.relative_to(destination): path.read_bytes() for path in destination.rglob('*.json')})

    def test_historical_revisions_can_use_different_hosts_without_splitting_a_revision(self):
        selected = plan()
        job = selected['jobs'][0]
        selected['jobs'].append(dict(job, id='linux-' + 'e' * 40,
                                     revision=dict(job['revision'], sha='e' * 40)))
        selected['identity'] = campaign.identity({key: value for key, value in selected.items() if key != 'identity'})
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_artifacts(root / 'artifacts', selected)
            destination = campaign.retain(root / 'site', root / 'artifacts', source(), 'owner/repo')
            batches = [json.loads(path.read_text()) for path in destination.glob('linux/*/batch.json')]
            self.assertEqual(len(batches), 2)
            self.assertEqual(len({batch['contract']['environment']['host_id'] for batch in batches}), 2)

    def test_missing_artifact_or_failed_revision_never_promotes_a_partial_campaign(self):
        for failure in ('artifact', 'status', 'matrix'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                artifacts, site = root / 'artifacts', root / 'site'
                write_artifacts(artifacts)
                folder = next(artifacts.glob('benchmark-backfill-macos-*'))
                if failure == 'artifact':
                    folder.rename(artifacts / 'unexpected')
                elif failure == 'status':
                    (folder / 'status.json').write_text(json.dumps({HEAD: {'status': 'measurement-failed'}}))
                else:
                    path = next(folder.glob('reports/*/*.json'))
                    record = json.loads(path.read_text())
                    record['tool_comparisons']['tasks'].pop()
                    path.write_text(json.dumps(record))
                with self.assertRaises(ValueError):
                    campaign.retain(site, artifacts, source(), 'owner/repo')
                self.assertFalse(site.exists())

    def test_report_rejects_missing_tasks_references_samples_and_changed_identity(self):
        selected = plan()
        job = selected['jobs'][0]
        changes = [lambda record: record['tool_comparisons']['tasks'].pop(),
                   lambda record: record['tool_comparisons']['tasks'][0]['participants'].pop('rg'),
                   lambda record: record['tool_comparisons']['tasks'][0]['participants']['xff']['samples'].pop(),
                   lambda record: record['tool_comparisons']['tasks'].append(record['tool_comparisons']['tasks'][0]),
                   lambda record: record.update(head=DRIVER),
                   lambda record: record.update(purpose='local-addition'),
                   lambda record: record['build'].update(sha256='e' * 64),
                   lambda record: record['tool_comparisons']['contract'].update(cpu_counts=[1, 4]),
                   lambda record: record['tool_comparisons']['contract'].update(affinity_by_cpu_count={'1': [0], '3': None}),
                   lambda record: record['tool_comparisons']['tasks'][0]['participants']['xff']['samples'][0].update(stderr='error')]
        for change in changes:
            batch, record = job_data(selected, job)
            change(record)
            with self.subTest(change=change), self.assertRaises(ValueError):
                campaign.validate_report(record, batch, job, selected)

    def test_publication_requires_successful_main_workflow_from_this_repository(self):
        for key, value in [('conclusion', 'failure'), ('head_branch', 'feature'), ('event', 'pull_request'),
                           ('repository', {'full_name': 'other/repo'}), ('path', '.github/workflows/other.yml')]:
            incoming = source()
            incoming[key] = value
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'trusted main'):
                campaign.retain(Path('/unused'), Path('/unused'), incoming, 'owner/repo')

    def test_replacement_preserves_original_paired_results_beyond_normal_retention(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifacts, site = root / 'artifacts', root / 'site'
            write_artifacts(artifacts)
            measurement = dict.fromkeys(history.METRICS, 1)
            measurement.update(shape='broad', scenario='listing', exit_code=0)
            sample = {'fixture': {}, 'binary_sha256': 'd' * 64, 'measurements': [measurement]}
            original = {'schema': 1, 'head': HEAD, 'base': 'e' * 40, 'platform': 'linux',
                        'contract': {'repetitions': 1, 'fixture': {}}, 'samples': {'base': [sample], 'head': [sample]},
                        'tool_comparisons': job_data(plan(), plan()['jobs'][0])[1]['tool_comparisons']}
            event = {'id': 1, 'run_attempt': 1, 'created_at': '2026-09-01T13:00:00Z',
                     'event': 'push', 'head_branch': 'main', 'head_sha': HEAD, 'pull_requests': []}
            history.retain(site, original, event)
            history.retain(site, original, dict(event, id=3, head_branch='v1.0.0'))
            path = site / 'runs/1/1/linux/report.json'
            saved = path.read_bytes()
            campaign.retain(site, artifacts, source(), 'owner/repo')
            incoming = dict(original, head='f' * 40)
            history.retain(site, incoming, dict(event, id=2, head_sha='f' * 40, created_at='2026-10-02T00:00:00Z'))
            self.assertEqual(path.read_bytes(), saved)
            pulls = [{'number': 12, 'merge_commit_sha': HEAD, 'merged_at': '2026-09-01T12:00:00Z'}]
            rendered = history.render_site(site, pulls, 'owner/repo')
            self.assertIn('href="backfills/20/2/linux/' + HEAD + '/">v1.0.0', rendered)
            (site / 'index.html').write_text(rendered)
            with mock.patch.object(history.subprocess, 'check_output', return_value=''):
                history.reference_pages(site, pulls, site)
            backfilled = site / 'backfills/20/2/linux' / HEAD / 'index.html'
            text = backfilled.read_text()
            self.assertIn('Original measurements, including paired base/head', text)
            self.assertIn('runs/1/1/linux/', text)
            self.assertIn('<th>Base</th>', path.with_name('index.html').read_text())
            landscape.publish(site, '/* renderer */')
            text = (site / 'index.html').read_text()
            prefix = 'window.XffBenchmarkHistory(document.getElementById("benchmark-explorer"),'
            catalog = json.loads(text.split(prefix)[1].split(');</script>')[0])
            selected = [item for item in catalog if item['commit'] == HEAD and item['platform'].startswith('linux')]
            self.assertEqual(len(selected), 1)
            self.assertTrue(selected[0]['report'].startswith('backfills/'))

    def test_index_references_and_landscape_select_historical_commit_not_driver_commit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifacts, site = root / 'artifacts', root / 'site'
            write_artifacts(artifacts)
            campaign.retain(site, artifacts, source(), 'owner/repo')
            pulls = [{'number': 12, 'merge_commit_sha': HEAD, 'merged_at': '2026-09-01T12:00:00Z'}]
            rendered = history.render_site(site, pulls, 'owner/repo')
            self.assertIn('PR 12 post-merge', rendered)
            self.assertIn('2026-09-01T12:00:00Z', rendered)
            self.assertNotIn('/commit/' + DRIVER, rendered)
            (site / 'index.html').write_text(rendered)
            with mock.patch.object(history.subprocess, 'check_output', side_effect=['v1.0.0\n', HEAD + '\n']):
                history.reference_pages(site, pulls, site)
            for reference in ('pr/12', 'tag/1.0.0'):
                self.assertIn('../../backfills/20/2/linux/' + HEAD + '/', (site / reference / 'index.html').read_text())
            landscape.publish(site, '/* renderer */')
            text = (site / 'index.html').read_text()
            prefix = 'window.XffBenchmarkHistory(document.getElementById("benchmark-explorer"),'
            catalog = json.loads(text.split(prefix)[1].split(');</script>')[0])
            self.assertEqual({item['commit'] for item in catalog}, {HEAD})
            self.assertTrue(all(item['date'] == '2026-09-01T12:00:00Z' for item in catalog))
            self.assertEqual({item['platform'].split(' / ')[0] for item in catalog}, {'linux', 'macos'})
            for path in site.glob('backfills/*/*/*/*/index.html'):
                text = path.read_text()
                self.assertIn('complete file-count', text)
                self.assertIn('href="../../campaign.json"', text)
                self.assertNotIn('<th>Base</th>', text)


if __name__ == '__main__':
    unittest.main()
