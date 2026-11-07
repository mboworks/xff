# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Keep historical source and binary provenance fail closed without invoking Bazel."""

from pathlib import Path
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


if __name__ == '__main__':
    unittest.main()
