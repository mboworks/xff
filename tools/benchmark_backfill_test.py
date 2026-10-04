# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""A fixed host series builds before measuring and resumes without mixing contracts."""

import argparse
import contextlib
import copy
from datetime import datetime
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import benchmark_backfill as backfill


class BenchmarkBackfillTest(unittest.TestCase):
    def test_progress_uses_local_time_at_emission_and_revision_position(self):
        output = io.StringIO()
        with mock.patch.object(backfill, 'datetime') as clock, contextlib.redirect_stderr(output):
            clock.now.side_effect = [datetime(2026, 10, 2, 20, 57, 13), datetime(2026, 10, 2, 20, 57, 14)]
            backfill.log_progress('Scale 8/39', 10, 17)
            backfill.log_progress('Complete', 10, 17)
            self.assertEqual(clock.now.call_args_list, [mock.call(), mock.call()])
        self.assertEqual(output.getvalue().splitlines(), [
            '20261002 205713 10/17 Scale 8/39', '20261002 205714 10/17 Complete'])

    def test_nested_run_progress_keeps_workers_and_files_before_run_details(self):
        output = io.StringIO()
        with mock.patch.object(backfill, 'datetime') as clock, contextlib.redirect_stderr(output):
            clock.now.return_value = datetime(2026, 10, 4, 13, 1, 36)
            progress = backfill.compare.MeasurementProgress(
                write=lambda message: backfill.log_progress(message, 5, 9))
            progress('Workers: 1 Files: 100,000 Run: 5/10; broad/fuzzy-list-hdn; fzf; sample: 4/9', force=True)
        self.assertEqual(output.getvalue().splitlines(), [
            '20261004 130136 5/9 Workers: 1 Files: 100,000 Run: 5/10; broad/fuzzy-list-hdn; fzf; sample: 4/9'])

    def args(self, root):
        return argparse.Namespace(repo=root, output=root / 'results', series='test-mac',
                                  purpose='local-addition', history_platform=None,
                                  revision=['main'], revisions_file=None, history_root=None,
                                  files=[10], cpus=[1, 3, 10], depth=4, repetitions=3, keep=2,
                                  require_cpu_affinity=False, fixture_parent=root,
                                  require_memory=False, disk_cache=None)

    def batch(self):
        return {'identity': 'batch', 'contract': {
            'series': 'test-mac', 'environment': {'host': 'same'},
            'platform': 'macos', 'purpose': 'local-addition', 'replacement_target': None,
            'allocation': {'kind': 'workers', 'cpu_ids': None, 'required': False},
            'revisions': [{'sha': 'a' * 40, 'subject': '<first>'}, {'sha': 'b' * 40, 'subject': 'second'}],
            'files': [10], 'cpus': [1, 3, 10], 'depth': 4, 'repetitions': 3, 'retained': 2,
            'storage': {'parent': '/tmp', 'memory_required': False}, 'require_cpu_affinity': False}}

    def report(self):
        return {'schema': 1, 'tools': {'xff': {'sha256': 'binary'}}, 'contract': {}, 'tasks': []}

    def test_revisions_are_resolved_deduplicated_and_sorted_by_commit_date(self):
        def git(repo, *args):
            if args[0] == 'rev-parse':
                return {'main^{commit}': 'b' * 40, 'older^{commit}': 'a' * 40}[args[-1]]
            if args[2] == '--format=%cI':
                return '2026-10-02T00:00:00+00:00' if args[-1][0] == 'b' else '2026-10-01T00:00:00+00:00'
            return 'subject'
        with mock.patch.object(backfill, 'git', side_effect=git):
            selected = backfill.revisions(Path('/repo'), ['main', 'older', 'main'])
        self.assertEqual([item['sha'] for item in selected], ['a' * 40, 'b' * 40])
        with self.assertRaisesRegex(ValueError, 'select revisions'):
            backfill.revisions(Path('/repo'), [])

    def test_history_selection_uses_only_the_requested_platform(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for platform in ('macos', 'linux'):
                folder = root / 'runs/1/1' / platform
                folder.mkdir(parents=True)
                (folder / 'report.json').write_text(json.dumps({
                    'head': platform, 'platform': platform, 'tool_comparisons': {'contract': {}}}))
            for platform in ('macos', 'linux'):
                with self.subTest(platform=platform), mock.patch.object(
                        backfill, 'git', side_effect=['a' * 40, '2026-10-01T00:00:00+00:00', 'subject']) as git:
                    self.assertEqual(len(backfill.revisions(root, [], root, platform)), 1)
                    self.assertEqual(git.call_args_list[0].args[-1], platform + '^{commit}')

    def test_plan_is_immutable_and_changed_host_or_grid_refuses_resume(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = self.args(root)
            with mock.patch.object(backfill, 'revisions', return_value=self.batch()['contract']['revisions']), \
                    mock.patch.object(backfill, 'environment', return_value={'host': 'one'}) as environment, \
                    mock.patch.object(backfill, 'driver_identity', return_value={'driver': 'same'}), \
                    mock.patch.object(backfill, 'allocation_policy', return_value=self.batch()['contract']['allocation']), \
                    mock.patch.object(backfill.compare, 'fixture_storage', return_value={'parent': '/tmp'}), \
                    mock.patch.object(backfill.shutil, 'which', return_value='/usr/bin/true'), \
                    mock.patch.object(backfill.subprocess, 'check_output', return_value='bazel version'):
                first = backfill.prepare(args)
                self.assertEqual(first['contract']['purpose'], 'local-addition')
                self.assertIsNone(first['contract']['replacement_target'])
                self.assertEqual(first['contract']['cpus'], [1, 3, 10])
                self.assertEqual(first, backfill.prepare(args))
                environment.return_value = {'host': 'two'}
                with self.assertRaisesRegex(ValueError, r'environment.host'):
                    backfill.prepare(args)
                environment.return_value = {'host': 'one'}
                args.cpus = [1, 8]
                with self.assertRaisesRegex(ValueError, 'cpus'):
                    backfill.prepare(args)

    def test_invalid_plan_never_builds_or_measures(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(Path(directory))
            for field, value in (('series', '../bad'), ('files', [0]), ('cpus', [1, 1]), ('keep', 4)):
                invalid = copy.copy(args)
                setattr(invalid, field, value)
                with self.subTest(field=field), self.assertRaises(ValueError):
                    backfill.prepare(invalid)
            with mock.patch.object(backfill.compare, 'cpu_allocation', side_effect=ValueError('undersized')), \
                    mock.patch.object(backfill, 'revisions') as revisions:
                with self.assertRaisesRegex(ValueError, 'undersized'):
                    backfill.prepare(args)
                revisions.assert_not_called()
            args.cpus = None
            with self.assertRaisesRegex(ValueError, 'explicit --cpus'):
                backfill.prepare(args)

    def test_ci_replacement_requires_hosted_matching_allocations_and_linux_storage(self):
        args = self.args(Path('/unused'))
        args.purpose = 'ci-replacement'
        with mock.patch.object(backfill, 'host_platform', return_value='macos'), \
                mock.patch.object(backfill.compare, 'cpu_allocation', return_value=None), \
                mock.patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, 'GitHub-hosted'):
                backfill.allocation_policy(args, [1, 3])
        for platform, cpus in (('macos', [1, 3]), ('linux', [1, 3])):
            with self.subTest(platform=platform), mock.patch.object(backfill, 'host_platform', return_value=platform), \
                    mock.patch.object(backfill.compare, 'cpu_allocation', return_value=None if platform == 'macos' else [0, 1, 2]), \
                    mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true', 'RUNNER_ENVIRONMENT': 'github-hosted'}):
                args.require_memory = platform == 'linux'
                policy = backfill.allocation_policy(args, cpus)
                self.assertEqual(policy['kind'], 'workers' if platform == 'macos' else 'logical-cpus')
                self.assertEqual(policy['required'], platform == 'linux')
                with self.assertRaisesRegex(ValueError, 'requires allocations'):
                    backfill.allocation_policy(args, [1, 8])
                if platform == 'linux':
                    args.require_memory = False
                    with self.assertRaisesRegex(ValueError, 'memory-backed'):
                        backfill.allocation_policy(args, cpus)

    def test_physical_core_selection_respects_affinity_packages_and_smt(self):
        with tempfile.TemporaryDirectory() as directory:
            topology = Path(directory)
            for cpu, package, core in ((0, 0, 0), (1, 0, 1), (4, 0, 0), (5, 0, 1), (8, 1, 0)):
                path = topology / f'cpu{cpu}/topology'
                path.mkdir(parents=True)
                (path / 'physical_package_id').write_text(str(package))
                (path / 'core_id').write_text(str(core))
            self.assertEqual(backfill.physical_cpus({0, 1, 4, 5, 8}, topology), [0, 1, 8])
            self.assertEqual(backfill.physical_cpus({4, 5, 8}, topology), [4, 5, 8])
            with self.assertRaisesRegex(ValueError, 'cannot identify physical core'):
                backfill.physical_cpus({9}, topology)
            (topology / 'cpu8/topology/core_id').write_text('-1')
            with self.assertRaisesRegex(ValueError, 'unknown physical topology'):
                backfill.physical_cpus({8}, topology)

    def test_local_linux_cannot_use_smt_siblings_to_satisfy_core_count(self):
        args = self.args(Path('/unused'))
        with mock.patch.object(backfill, 'host_platform', return_value='linux'), \
                mock.patch.object(backfill.compare, 'cpu_allocation') as allocation, \
                mock.patch.object(backfill.os, 'sched_getaffinity', return_value=set(range(8)), create=True), \
                mock.patch.object(backfill, 'physical_cpus', return_value=[0, 2, 4, 6]):
            policy = backfill.allocation_policy(args, [1, 4])
            allocation.assert_called_once_with(4, True)
            self.assertEqual(policy, {'kind': 'physical-cores', 'cpu_ids': [0, 2, 4, 6], 'required': True})
            with self.assertRaisesRegex(ValueError, 'only 4 available'):
                backfill.allocation_policy(args, [1, 6])

    def test_pinning_restores_the_original_mask_on_failure(self):
        with mock.patch.object(backfill.os, 'sched_getaffinity', return_value={0, 1, 2, 3}, create=True), \
                mock.patch.object(backfill.os, 'sched_setaffinity', create=True) as pin:
            with self.assertRaisesRegex(ValueError, 'measurement failed'):
                with backfill.pinned_cpus([0, 2]):
                    pin.assert_called_once_with(0, [0, 2])
                    raise ValueError('measurement failed')
            self.assertEqual(pin.call_args_list[-1], mock.call(0, {0, 1, 2, 3}))

    @unittest.skipUnless(sys.platform == 'linux', 'Linux affinity required')
    def test_child_inherits_physical_core_mask(self):
        original = os.sched_getaffinity(0)
        cores = backfill.physical_cpus(original)[:2]
        self.assertTrue(cores)
        with backfill.pinned_cpus(cores):
            actual = json.loads(subprocess.check_output(
                [sys.executable, '-c', 'import json,os; print(json.dumps(sorted(os.sched_getaffinity(0))))'], text=True))
            self.assertEqual(actual, cores)
        self.assertEqual(os.sched_getaffinity(0), original)

    def test_build_phase_precedes_all_measurements_and_completed_reports_are_reused(self):
        with tempfile.TemporaryDirectory() as directory:
            args, batch = self.args(Path(directory)), self.batch()
            args.output.mkdir()
            calls = []
            output = io.StringIO()
            def build(repo, output, revision, contract, cache):
                calls.append('build-' + revision['sha'][0])
                return Path('/binary'), {'sha256': 'binary', 'configuration': {'compiler': 'revision'}}
            def measure(*args, **kwargs):
                calls.append('measure')
                self.assertEqual(kwargs['cpu_counts'], [1, 3, 10])
                kwargs['progress']('Nested fixture progress', force=True)
                return self.report()
            with mock.patch.object(backfill, 'build', side_effect=build), \
                    mock.patch.object(backfill, 'check_environment'), \
                    mock.patch.object(backfill.compare, 'collect_scales', side_effect=measure) as collect, \
                    mock.patch.object(backfill.compare, 'render_document', return_value='<h1>Report</h1>'), \
                    contextlib.redirect_stderr(output):
                self.assertEqual(backfill.run(args, batch), 0)
                self.assertEqual(calls, ['build-a', 'build-b', 'measure', 'measure'])
                collect.reset_mock()
                self.assertEqual(backfill.run(args, batch), 0)
                collect.assert_not_called()
            lines = output.getvalue().splitlines()
            for line in lines:
                self.assertRegex(line, r'^\d{8} \d{6} [12]/2 ')
            self.assertEqual([line.split(' ', 2)[2] for line in lines if 'Nested fixture progress' in line],
                             ['1/2 Nested fixture progress', '2/2 Nested fixture progress'])
            self.assertEqual(sum('Reuse completed report:' in line for line in lines), 2)
            status = json.loads((args.output / 'status.json').read_text())
            self.assertTrue(all(entry['status'] == 'complete' for entry in status.values()))
            self.assertIn('&lt;first&gt;', (args.output / 'index.html').read_text())
            records = sorted(args.output.glob('reports/*/*.json'))
            self.assertEqual(len(records), 2)
            self.assertEqual(json.loads(records[0].read_text())['batch'], batch['identity'])
            self.assertEqual(records[0].with_suffix('.html').read_text(), '<h1>Report</h1>')

    def test_failed_revision_does_not_prevent_later_measurements(self):
        for failure in ('build', 'measure'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                args, batch = self.args(Path(directory)), self.batch()
                args.output.mkdir()
                success = (Path('/binary'), {'sha256': 'binary', 'configuration': {}})
                build = [subprocess.CalledProcessError(1, ['bazel']), success] if failure == 'build' else [success, success]
                measurements = [ValueError('unsupported old flag'), self.report()] if failure == 'measure' else [self.report()]
                with mock.patch.object(backfill, 'build', side_effect=build), \
                        mock.patch.object(backfill, 'check_environment'), \
                        mock.patch.object(backfill.compare, 'collect_scales', side_effect=measurements), \
                        mock.patch.object(backfill.compare, 'render_document', return_value='report'):
                    self.assertEqual(backfill.run(args, batch), 1)
                status = json.loads((args.output / 'status.json').read_text())
                self.assertEqual(status['b' * 40]['status'], 'complete')
                self.assertNotEqual(status['a' * 40]['status'], 'complete')

    def test_host_drift_stops_the_entire_batch(self):
        with mock.patch.object(backfill, 'environment', return_value={'host': 'other'}):
            with self.assertRaisesRegex(ValueError, 'environment changed: host'):
                backfill.check_environment({'host': 'same'})
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(Path(directory))
            args.output.mkdir()
            with mock.patch.object(backfill, 'check_environment', side_effect=ValueError('changed')), \
                    mock.patch.object(backfill, 'build') as build:
                with self.assertRaisesRegex(ValueError, 'changed'):
                    backfill.run(args, self.batch())
                build.assert_not_called()

    def test_saved_binary_hash_must_match(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            revision = {'sha': 'a' * 40}
            folder = output / 'binaries' / revision['sha']
            folder.mkdir(parents=True)
            (folder / 'xff').write_text('binary')
            (folder / 'build.json').write_text(json.dumps({'sha256': 'wrong', 'revision': revision['sha']}))
            with self.assertRaisesRegex(ValueError, 'binary changed'):
                backfill.build(output, output, revision, {}, None)


if __name__ == '__main__':
    unittest.main()
