# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Keep historical source and binary provenance fail closed without invoking Bazel."""

from pathlib import Path
import json
import io
import shutil
import tempfile
import unittest
from unittest import mock

import benchmark_extended_build as build


class BenchmarkExtendedBuildTest(unittest.TestCase):
    def test_clean_source_records_exact_revision_and_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            with mock.patch.object(build, 'git', side_effect=['revision', '', 'tree']) as git:
                self.assertEqual(build.clean_source(Path(temporary), 'revision'),
                                 {'revision': 'revision', 'tree': 'tree', 'clean': True})
                self.assertEqual([call.args[1:] for call in git.call_args_list],
                                 [('rev-parse', 'HEAD'), ('status', '--porcelain'), ('rev-parse', 'HEAD^{tree}')])

    def test_dirty_or_wrong_checkout_and_local_bazel_override_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for answers in (['wrong'], ['revision', ' M source.cc']):
                with mock.patch.object(build, 'git', side_effect=answers), self.assertRaises(ValueError):
                    build.clean_source(root, 'revision')
            (root / '.bazelrc.user').touch()
            with mock.patch.object(build, 'git', side_effect=['revision', '']), self.assertRaisesRegex(ValueError, 'override'):
                build.clean_source(root, 'revision')

    def test_identity_hash_changes_with_artifact_contents(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'xff'
            path.write_bytes(b'old')
            old = build.digest(path)
            path.write_bytes(b'new')
            self.assertNotEqual(build.digest(path), old)
            build.write(Path(temporary) / 'identity.json', {'hash': old})
            self.assertFalse((Path(temporary) / 'identity.json.tmp').exists())

    def test_each_configured_build_is_copied_before_any_later_bazel_invocation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout, directory = root / 'checkout', root / 'frozen'
            artifact = 'bazel-out/production/bin/xff/cli/xff_full'
            built = checkout / artifact
            built.parent.mkdir(parents=True)
            frozen_bytes = b'synthetic production binary'
            events = []
            copy = shutil.copy2

            def run(command, **kwargs):
                if 'build' in command:
                    events.append('build')
                    built.write_bytes(frozen_bytes)
                else:
                    events.append('aquery')
                    self.assertEqual((directory / 'xff_full').read_bytes(), frozen_bytes)
                    kwargs['stdout'].write(json.dumps({'actions': [
                        {'arguments': ['clang++', '-flto=thin', '-o', artifact]}]}))
                    built.write_bytes(b'alias changed after the frozen copy')

            def logged(command, checkout, log):
                run(command)

            def query(command, **kwargs):
                events.append('configured query')
                self.assertIn('--config=clang_release', command)
                self.assertIn('--config=xff_full', command)
                self.assertIn('config(//xff/cli:xff_full, target)', command)
                return artifact + '\n'

            def freeze(source, destination):
                events.append('copy')
                return copy(source, destination)

            with mock.patch.object(build, 'clean_source', return_value={'revision': 'revision', 'tree': 'tree', 'clean': True}), \
                    mock.patch.object(build.subprocess, 'run', side_effect=run), \
                    mock.patch.object(build, 'build_logged', side_effect=logged), \
                    mock.patch.object(build.subprocess, 'check_output', side_effect=query), \
                    mock.patch.object(build.shutil, 'copy2', side_effect=freeze):
                saved = build.freeze_build(checkout, directory, 'revision', root / 'cache')
            self.assertEqual(events, ['build', 'configured query', 'copy', 'aquery'])
            self.assertEqual(saved['sha256'], build.digest(directory / 'xff_full'))
            self.assertNotEqual(saved['sha256'], build.digest(built))

    def test_build_progress_is_visible_retained_and_failure_propagates(self):
        for status in (0, 1):
            with self.subTest(status=status):
                process = mock.Mock(stdout=io.StringIO('building source.cc\nlinking xff_full\n'), returncode=status)
                process.wait.return_value = status
                process.__enter__ = mock.Mock(return_value=process)
                process.__exit__ = mock.Mock(return_value=False)
                output, log = io.StringIO(), io.StringIO()
                with mock.patch.object(build.subprocess, 'Popen', return_value=process), \
                        mock.patch.object(build.sys, 'stderr', output):
                    if status:
                        with self.assertRaises(build.subprocess.CalledProcessError):
                            build.build_logged(['bazel', 'build'], Path('.'), log)
                    else:
                        build.build_logged(['bazel', 'build'], Path('.'), log)
                self.assertEqual(log.getvalue(), 'building source.cc\nlinking xff_full\n')
                self.assertEqual(output.getvalue(), log.getvalue())


if __name__ == '__main__':
    unittest.main()
