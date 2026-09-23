# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Verify deletion failures report completed work without claiming disk reclamation."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from python.runfiles import runfiles


class DeletionDiagnosticsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.binary = runfiles.Create().Rlocation(EXECUTABLE)
        self.env = dict(os.environ, XFF_TEST_SYSTEM_CONFIG=str(self.root / 'absent-system'),
                        XFF_TEST_USER_CONFIG=str(self.root / 'absent-user'))

    def run_xff(self, *args):
        return subprocess.run([self.binary, '--sort=tree', str(self.source), *args],
                              env=self.env, capture_output=True, text=True, check=False)

    def test_failure_counts_only_successful_removals(self):
        outside = self.root / 'retained-hardlink'
        outside.write_bytes(b'12345')
        os.link(outside, self.source / 'a-file')
        (self.source / 'b-empty-dir').mkdir()
        (self.source / 'c-link').symlink_to(outside)
        (self.source / 'z-nonempty').mkdir()
        (self.source / 'z-nonempty' / 'keep').write_bytes(b'must remain')
        result = self.run_xff('-mindepth', '1', '!', '-name', 'keep', '-delete')
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn(str(self.source / 'z-nonempty'), result.stderr)
        self.assertIn('Directory not empty', result.stderr)
        self.assertIn('1 regular files, 1 directories, 1 other entries', result.stderr)
        self.assertIn('5 known file bytes removed (logical size, not disk space reclaimed)', result.stderr)
        self.assertEqual(outside.read_bytes(), b'12345')
        self.assertEqual((self.source / 'z-nonempty' / 'keep').read_bytes(), b'must remain')
        self.assertFalse((self.source / 'a-file').exists())
        self.assertFalse((self.source / 'b-empty-dir').exists())
        self.assertFalse((self.source / 'c-link').is_symlink())

    def test_no_successes_and_dry_run(self):
        directory = self.source / 'foo'
        directory.mkdir()
        (directory / 'keep').write_bytes(b'x')
        result = self.run_xff('-type', 'd', '-name', 'foo', '-prune', '-delete')
        self.assertEqual(result.returncode, 2)
        self.assertIn('0 regular files, 0 directories, 0 other entries', result.stderr)
        self.assertIn('0 known file bytes removed', result.stderr)
        preview = self.run_xff('--dry-run', '-delete')
        self.assertEqual(preview.returncode, 0, preview.stderr)
        self.assertEqual(preview.stderr, '')
        self.assertTrue((directory / 'keep').exists())


if __name__ == '__main__':
    EXECUTABLE = sys.argv.pop(1)
    unittest.main()
