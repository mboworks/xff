# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""A fixed host series builds before measuring and resumes without mixing contracts."""

import argparse
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import benchmark_backfill as backfill


class BenchmarkBackfillTest(unittest.TestCase):
    def args(self, root):
        return argparse.Namespace(repo=root, output=root / 'results', series='test-mac',
                                  revision=['main'], revisions_file=None, history_root=None,
                                  files=[10], cpus=[1, 4], depth=4, repetitions=3, keep=2,
                                  require_cpu_affinity=False, fixture_parent=root,
                                  require_memory=False, disk_cache=None)

    def batch(self):
        return {'identity': 'batch', 'contract': {
            'series': 'test-mac', 'environment': {'host': 'same'},
            'revisions': [{'sha': 'a' * 40, 'subject': '<first>'}, {'sha': 'b' * 40, 'subject': 'second'}],
            'files': [10], 'cpus': [1, 4], 'depth': 4, 'repetitions': 3, 'retained': 2,
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

    def test_history_selection_uses_only_macos_comparison_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for platform in ('macos', 'linux'):
                folder = root / 'runs/1/1' / platform
                folder.mkdir(parents=True)
                (folder / 'report.json').write_text(json.dumps({
                    'head': platform, 'platform': platform, 'tool_comparisons': {'contract': {}}}))
            with mock.patch.object(backfill, 'git', side_effect=['a' * 40, '2026-10-01T00:00:00+00:00', 'subject']) as git:
                self.assertEqual(len(backfill.revisions(root, [], root)), 1)
                self.assertEqual(git.call_args_list[0].args[-1], 'macos^{commit}')

    def test_plan_is_immutable_and_changed_host_or_grid_refuses_resume(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = self.args(root)
            with mock.patch.object(backfill, 'revisions', return_value=self.batch()['contract']['revisions']), \
                    mock.patch.object(backfill, 'environment', return_value={'host': 'one'}) as environment, \
                    mock.patch.object(backfill, 'driver_identity', return_value={'driver': 'same'}), \
                    mock.patch.object(backfill.compare, 'cpu_allocation'), \
                    mock.patch.object(backfill.compare, 'fixture_storage', return_value={'parent': '/tmp'}), \
                    mock.patch.object(backfill.shutil, 'which', return_value='/usr/bin/true'), \
                    mock.patch.object(backfill.subprocess, 'check_output', return_value='bazel version'):
                first = backfill.prepare(args)
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

    def test_build_phase_precedes_all_measurements_and_completed_reports_are_reused(self):
        with tempfile.TemporaryDirectory() as directory:
            args, batch = self.args(Path(directory)), self.batch()
            args.output.mkdir()
            calls = []
            def build(repo, output, revision, contract, cache):
                calls.append('build-' + revision['sha'][0])
                return Path('/binary'), {'sha256': 'binary', 'configuration': {'compiler': 'revision'}}
            def measure(*args, **kwargs):
                calls.append('measure')
                self.assertEqual(kwargs['cpu_counts'], [1, 4])
                return self.report()
            with mock.patch.object(backfill, 'build', side_effect=build), \
                    mock.patch.object(backfill, 'check_environment'), \
                    mock.patch.object(backfill.compare, 'collect_scales', side_effect=measure) as collect, \
                    mock.patch.object(backfill.compare, 'render_document', return_value='<h1>Report</h1>'):
                self.assertEqual(backfill.run(args, batch), 0)
                self.assertEqual(calls, ['build-a', 'build-b', 'measure', 'measure'])
                collect.reset_mock()
                self.assertEqual(backfill.run(args, batch), 0)
                collect.assert_not_called()
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
