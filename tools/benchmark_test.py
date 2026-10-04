# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Automatic local backfill discovery, machine separation and execution consent."""

import contextlib
import copy
import gzip
import hashlib
import io
import json
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest import mock

import benchmark as cli
import benchmark_backfill as batch
import benchmark_campaign as campaign


def fixture():
    revision = dict(sha='a' * 40, date='2026-09-01T12:00:00Z', subject='Merge (#1)')
    tools = {name: dict(status='available', path='/' + name, sha256='b' * 64, version='1')
             for name in ('find', 'rg', 'fzf')}
    storage = dict(filesystem='host filesystem', memory_required=False, parent='/tmp')
    allocation = dict(kind='workers', cpu_ids=None, required=False)
    contract = dict(schema=1, series='macos-test', purpose='local-addition', replacement_target=None,
                    machine_id='machine', platform='macos', revisions=[revision], files=[10], cpus=[1],
                    depth=40, repetitions=1, retained=1, driver={'driver': 'hash'}, allocation=allocation,
                    storage=storage, environment=dict(machine='arm64', cpu_count=18, platform='Darwin', tools=tools))
    value = dict(identity=campaign.identity(contract), contract=contract)
    sample = dict.fromkeys(batch.compare.METRICS, 1)
    sample.update(stderr='', exit_codes=[0])
    tasks = []
    for name, _, participants in batch.compare.scenarios(Path('.'), [], {'xff': {'path': 'xff'}, **tools}):
        for shape in ('broad', 'deep'):
            tasks.append(dict(name=name, files=10, cpus=1, dataset=shape, shape=f'1cpu/10/{shape}',
                              expected_count=10, skips={}, participants={
                                  tool: dict(pipeline=[[tool]], samples=[dict(sample)]) for tool in participants}))
    report = dict(schema=1, tools={**tools, 'xff': dict(sha256='d' * 64)}, tasks=tasks,
                  contract=dict(file_counts=[10], cpu_counts=[1], depth=40, repetitions=1, retained=1,
                                batch=value['identity'], runner_class='macos-test', estimator='mean-fastest',
                                machine='arm64', storage=storage, build_identity='{}', platform='Darwin',
                                cpu_count=18, affinity_by_cpu_count={'1': None}))
    record = dict(schema=1, kind='backfill', head=revision['sha'], revision=revision, batch=value['identity'],
                  build=dict(sha256='d' * 64, revision=revision['sha'], configuration={}), tool_comparisons=report,
                  **{key: contract[key] for key in ('purpose', 'platform', 'series', 'replacement_target', 'allocation')})
    return value, record


def save(root, value, record=None, published=False):
    root.mkdir(parents=True, exist_ok=True)
    (root / 'batch.json').write_text(json.dumps(value))
    if record:
        sha = value['contract']['revisions'][0]['sha']
        path = (root / sha / 'report.json' if published else
                root / 'reports' / sha / 'benchmark-report-macos-arm64.json')
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps(record))
    return root


class BenchmarkTest(unittest.TestCase):
    def repository(self, root):
        def git(*args):
            return subprocess.check_output(['git', '-C', str(root), *args], text=True,
                                           stderr=subprocess.DEVNULL).strip()
        git('init', '-b', 'main')
        git('config', 'commit.gpgsign', 'false')
        git('config', 'core.hooksPath', '/dev/null')
        git('config', 'core.excludesFile', '/dev/null')
        git('config', 'user.email', 'test@example.invalid')
        git('config', 'user.name', 'Test')
        return git

    def options(self, root):
        parser, _ = cli.parser_for_cli()
        args = parser.parse_args(['backfill', '--root=' + str(root), '--no-fetch', '--cpus=1', '--files=10',
                                  '--repetitions=1', '--keep=1', '--depth=40'])
        args.require_memory = args.require_cpu_affinity = False
        return args

    def test_machine_identity_hashes_stable_platform_id_not_hostname(self):
        data = plistlib.dumps([{'IOPlatformUUID': 'private-hardware-id'}])
        with mock.patch.object(batch, 'host_platform', return_value='macos'), \
                mock.patch.object(cli.subprocess, 'check_output', return_value=data):
            result = cli.machine_identity()
            self.assertEqual(result, hashlib.sha256(b'xff-benchmark:macos:private-hardware-id').hexdigest())
        with mock.patch.object(batch, 'host_platform', return_value='linux'), \
                mock.patch.object(Path, 'read_text', return_value='linux-id\n'):
            self.assertEqual(cli.machine_identity(), hashlib.sha256(b'xff-benchmark:linux:linux-id').hexdigest())
        with mock.patch.object(batch, 'host_platform', return_value='linux'), \
                mock.patch.object(Path, 'read_text', return_value=''):
            with self.assertRaisesRegex(ValueError, 'unavailable'):
                cli.machine_identity()

    def test_known_new_renamed_and_conflicting_machines(self):
        value, _ = fixture()
        saved = [(Path('/saved'), value)]
        self.assertEqual(cli.select_series(saved, 'machine'), ('macos-test', True))
        with mock.patch.object(batch, 'host_platform', return_value='macos'), \
                mock.patch.object(cli.platform, 'machine', return_value='arm64'):
            self.assertEqual(cli.select_series(saved, 'new'), ('macos-arm64-new', False))
        with self.assertRaisesRegex(ValueError, 'another or unidentified'):
            cli.select_series(saved, 'other', 'macos-test')
        other = copy.deepcopy(value)
        other['contract']['series'] = 'macos-second'
        with self.assertRaisesRegex(ValueError, 'multiple series'):
            cli.select_series([*saved, (Path('/other'), other)], 'machine')
        with mock.patch.object(cli.platform, 'node', return_value='renamed-host'):
            self.assertEqual(cli.select_series(saved, 'machine'), ('macos-test', True))

    def test_new_series_names_accept_supported_architectures(self):
        for system, architecture, expected in (('linux', 'x86_64', 'linux-x86-64-machine'),
                                               ('macos', 'arm64', 'macos-arm64-machine')):
            with self.subTest(system=system, architecture=architecture), \
                    mock.patch.object(batch, 'host_platform', return_value=system), \
                    mock.patch.object(cli.platform, 'machine', return_value=architecture):
                self.assertEqual(cli.select_series([], 'machine'), (expected, False))
                self.assertEqual(cli.select_series([], 'machine', 'custom-series'), ('custom-series', False))

    def test_legacy_series_adoption_requires_matching_host_and_capacity(self):
        value, _ = fixture()
        del value['contract']['machine_id']
        value['contract']['environment']['host_id'] = hashlib.sha256(b'host').hexdigest()
        with mock.patch.object(cli.platform, 'node', return_value='host'), \
                mock.patch.object(cli.platform, 'machine', return_value='arm64'), \
                mock.patch.object(cli.os, 'cpu_count', return_value=18), \
                mock.patch.object(batch, 'host_platform', return_value='macos'):
            self.assertEqual(cli.select_series([(Path('/saved'), value)], 'machine'), ('macos-test', True))
            value['contract']['environment']['cpu_count'] = 24
            self.assertEqual(cli.select_series([(Path('/saved'), value)], 'machine')[1], False)

    def test_complete_reports_across_local_and_published_batches_are_reused(self):
        value, record = fixture()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            save(root / 'old', value, record)
            save(root / 'site/local/macos-test/batch', value, record, published=True)
            found = cli.load_batches(root, root / 'site')
            self.assertEqual(len(found), 2)
            revisions = [*value['contract']['revisions'], dict(sha='b' * 40, date='date', subject='next')]
            rows = cli.inventory(revisions, found, 'macos-test', self.options(root))
            self.assertEqual([row['state'] for row in rows], ['complete', 'missing'])
            self.assertEqual(cli.inventory(revisions, found, 'other-machine', self.options(root))[0]['state'], 'missing')
            (root / 'old/batch.json').write_text(json.dumps({**value, 'identity': 'wrong'}))
            with self.assertRaisesRegex(ValueError, 'identity changed'):
                cli.load_batches(root, None)

    def test_missing_corrupt_incomplete_and_incompatible_reports_remain_missing(self):
        mutations = [lambda r: r['tool_comparisons']['tasks'].pop(),
                     lambda r: r.update(head='b' * 40),
                     lambda r: r['tool_comparisons']['tasks'][0]['participants']['xff']['samples'].clear()]
        for mutate in mutations:
            with self.subTest(mutate=mutate), tempfile.TemporaryDirectory() as temporary:
                value, record = fixture()
                mutate(record)
                root = save(Path(temporary), value, record)
                state = cli.report_status(root, value, value['contract']['revisions'][0], self.options(root))
                self.assertTrue(state.startswith('invalid report:'), state)
        with tempfile.TemporaryDirectory() as temporary:
            value, record = fixture()
            root = save(Path(temporary), value)
            revision = value['contract']['revisions'][0]
            self.assertEqual(cli.report_status(root, value, revision, self.options(root)), 'pending')
            (root / 'status.json').write_text(json.dumps({revision['sha']: {'status': 'complete'}}))
            self.assertEqual(cli.report_status(root, value, revision, self.options(root)), 'missing report')
            args = self.options(root)
            args.cpus = [1, 3]
            self.assertEqual(cli.report_status(root, value, revision, args), 'incompatible grid/sampling')

    def test_available_revisions_follow_main_and_exclude_unmerged_branch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            git = self.repository(root)
            git('commit', '--allow-empty', '-m', 'Before benchmarks')
            (root / 'tools').mkdir()
            (root / 'tools/benchmark_compare.py').write_text('# benchmark\n')
            git('add', '.')
            git('commit', '-m', 'Introduce benchmarks (#1)')
            first = git('rev-parse', 'HEAD')
            git('commit', '--allow-empty', '-m', 'New merge (#2)')
            second = git('rev-parse', 'HEAD')
            git('checkout', '-b', 'unmerged')
            git('commit', '--allow-empty', '-m', 'Unmerged work')
            rows = cli.available_revisions(root, 'main')
            self.assertEqual([row['sha'] for row in rows], [first, second])

    def test_published_inventory_extracts_json_only_and_handles_packed_reports(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            git = self.repository(root)
            value, record = fixture()
            folder = save(root / 'benchmarks/local/macos-test/batch', value, record, published=True)
            report = folder / record['head'] / 'report.json'
            report.with_suffix('.json.gz').write_bytes(gzip.compress(report.read_bytes()))
            report.unlink()
            (folder / 'huge.html').write_text('not needed')
            git('add', '.')
            git('commit', '-m', 'Published data')
            args = self.options(root / 'incoming')
            args.repo, args.history_ref = root, 'main'
            with cli.published_history(args) as history:
                self.assertEqual(list(history.rglob('*.html')), [])
                found = cli.load_batches(args.root, history)
                rows = cli.inventory(value['contract']['revisions'], found, 'macos-test', args)
                self.assertEqual(rows[0]['state'], 'complete')
            args.history_ref = 'nonexistent'
            with cli.published_history(args) as history:
                self.assertIsNone(history)
            args.history_root = root / 'benchmarks'
            with cli.published_history(args) as history:
                self.assertEqual(history, args.history_root)

    def test_new_batch_plan_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = self.options(root)
            value, _ = fixture()
            with mock.patch.object(batch, 'revisions', return_value=value['contract']['revisions']), \
                    mock.patch.object(batch, 'environment', return_value=value['contract']['environment']), \
                    mock.patch.object(batch, 'allocation_policy', return_value=value['contract']['allocation']), \
                    mock.patch.object(batch.compare, 'fixture_storage', return_value=value['contract']['storage']), \
                    mock.patch.object(batch.shutil, 'which', return_value='/bin/true'), \
                    mock.patch.object(batch.subprocess, 'check_output', return_value='bazel'):
                options = cli.batch_options(args, 'macos-test', 'machine', value['contract']['revisions'])
                contract = batch.make_contract(options)
            self.assertEqual(contract['machine_id'], 'machine')
            self.assertFalse(options.output.exists())

    def test_resumes_matching_batch_and_separately_plans_new_merges(self):
        value, _ = fixture()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            saved = save(root / 'old', value)
            (saved / 'checkout').mkdir()
            rows = [{**value['contract']['revisions'][0], 'state': 'missing'},
                    dict(sha='b' * 40, state='missing')]
            with mock.patch.object(batch, 'make_contract', return_value=value['contract']):
                plans = cli.execution_plan(self.options(root), 'macos-test', 'machine', rows, [(saved, value)])
            self.assertEqual(len(plans), 2)
            self.assertEqual(plans[0].output, saved)
            self.assertEqual(plans[0].revision, ['a' * 40])
            self.assertEqual(plans[1].revision, ['b' * 40])
            with mock.patch.object(batch, 'make_contract', return_value={'changed': True}):
                plans = cli.execution_plan(self.options(root), 'macos-test', 'machine', rows, [(saved, value)])
            self.assertEqual(len(plans), 1)
            self.assertEqual(plans[0].revision, ['a' * 40, 'b' * 40])

    def test_concurrent_collection_is_rejected_and_lock_releases(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with cli.collection_lock(root, 'machine'):
                with self.assertRaisesRegex(ValueError, 'already running'):
                    with cli.collection_lock(root, 'machine'):
                        self.fail('second collector started')
            with cli.collection_lock(root, 'machine'):
                pass

    def test_help_and_list_do_not_build_or_prompt(self):
        with mock.patch.object(batch, 'run') as run, mock.patch('builtins.input') as prompt:
            for argv in ([], ['help'], ['help', 'list'], ['help', 'backfill']):
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(cli.main(argv), 0)
                    self.assertIn('backfill', output.getvalue())
            run.assert_not_called()
            prompt.assert_not_called()

    def test_published_recipe_drives_list_defaults_and_reuses_observations(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            value, record = fixture()
            record['tool_comparisons']['contract'].update(
                fixture_version=2, warmup_rounds=1, order='rotate starting participant by task and round')
            history = root / 'site'
            save(history / 'local/macos-test/batch', value, record, published=True)
            cli.discovery.datasets.publish(history)
            with mock.patch.object(cli, 'available_revisions', return_value=value['contract']['revisions']), \
                    mock.patch.object(cli, 'machine_identity', return_value='machine'), \
                    mock.patch.object(batch, 'host_platform', return_value='macos'), \
                    mock.patch.object(cli.platform, 'machine', return_value='arm64'), \
                    mock.patch.object(cli.os, 'cpu_count', return_value=18), \
                    mock.patch.object(batch, 'run') as run, mock.patch('builtins.input') as prompt, \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(cli.main(['list', '--no-fetch', '--root=' + str(root / 'new'),
                                           '--history-root=' + str(history)]), 0)
            self.assertIn('1 revisions; 0 missing', output.getvalue())
            self.assertIn('complete  Merge (#1)', output.getvalue())
            self.assertIn('macos-test', output.getvalue())
            run.assert_not_called()
            prompt.assert_not_called()

    def test_confirmation_cancel_eof_yes_and_automatic_yes(self):
        cases = [('list', [], 'yes', False), ('backfill', [], '', False), ('backfill', [], EOFError, False),
                 ('backfill', [], 'yes', True), ('backfill', ['-Y'], None, True),
                 ('backfill', ['--yes'], None, True)]
        for command, flags, answer, expected in cases:
            with self.subTest(command=command, flags=flags, answer=answer), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                value, _ = fixture()
                plan = self.options(root)
                plan.expected_contract = value['contract']
                plan.output = root / 'batch'
                plan.revision = ['a' * 40]
                with mock.patch.object(cli.discovery, 'catalog', return_value=None), \
                        mock.patch.object(cli, 'available_revisions', return_value=value['contract']['revisions']), \
                        mock.patch.object(cli, 'machine_identity', return_value='machine'), \
                        mock.patch.object(cli, 'published_history', return_value=contextlib.nullcontext(None)), \
                        mock.patch.object(cli, 'execution_plan', return_value=[plan]), \
                        mock.patch.object(batch, 'make_contract', return_value=value['contract']), \
                        mock.patch.object(batch, 'prepare', return_value=value) as prepare, \
                        mock.patch.object(batch, 'run', return_value=0) as run, \
                        mock.patch('builtins.input', side_effect=answer if answer is EOFError else None,
                                   return_value=answer) as prompt, contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(cli.main([command, '--no-fetch', '--root=' + str(root), *flags]), 0)
                    self.assertEqual(run.called, expected)
                    self.assertEqual(prepare.called, expected)
                    if flags or command == 'list':
                        prompt.assert_not_called()


if __name__ == '__main__':
    unittest.main()
