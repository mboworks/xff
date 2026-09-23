# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Apply emitted Git patches to an empty tree and verify bytes, links and modes."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from python.runfiles import runfiles


class CreationPatchTest(unittest.TestCase):
    def test_git_apply_round_trip(self):
        binary = runfiles.Create().Rlocation(EXECUTABLE)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source'
            source.mkdir()
            entries = {'empty': b'', 'text': b'first\nsecond\n', 'no-newline': b'last',
                       'crlf': b'a\r\nb\r\n', 'exec': b'#!/bin/sh\nexit 0\n',
                       'binary': bytes(range(256)) * 300,
                       'space \t\n"\\name': b'odd name\n', 'sub/file': b'nested\n'}
            for name, data in entries.items():
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            (source / 'exec').chmod(0o755)
            (source / 'link').symlink_to('text')
            env = dict(os.environ, XFF_TEST_SYSTEM_CONFIG=str(root / 'absent-system'),
                       XFF_TEST_USER_CONFIG=str(root / 'absent-user'))
            outputs = []
            for suffix in ([], ['/dev/null']):
                result = subprocess.run([binary, '--no-config', str(source), '-diff', *suffix],
                                        capture_output=True, env=env, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                outputs.append(result.stdout)
            self.assertEqual(*outputs)
            self.assertIn(b'GIT binary patch', outputs[0])
            target = root / 'target'
            target.mkdir()
            applied = subprocess.run(['git', 'apply', '--whitespace=nowarn', '-'], input=outputs[0],
                                     cwd=target, capture_output=True, check=False)
            self.assertEqual(applied.returncode, 0, applied.stderr)
            for name, data in entries.items():
                self.assertEqual((target / name).read_bytes(), data, name)
            self.assertEqual(os.readlink(target / 'link'), 'text')
            self.assertEqual((target / 'exec').stat().st_mode & 0o111, 0o111)
            self.assertEqual((target / 'text').stat().st_mode & 0o111, 0)
            # A file operand uses its basename; optional operands do not swallow globals/operators.
            result = subprocess.run([binary, str(source / 'empty'), '-diff', '--safe', '-o', '-false'],
                                    capture_output=True, env=env, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(b'"b/empty"', result.stdout)
            silent = subprocess.run([binary, str(source), '-diff:none'],
                                    capture_output=True, env=env, check=False)
            self.assertEqual(silent.returncode, 0, silent.stderr)
            self.assertEqual(silent.stdout, b'')


if __name__ == '__main__':
    EXECUTABLE = sys.argv.pop(1)
    unittest.main()
