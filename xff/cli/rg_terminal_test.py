# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Check rg's terminal defaults through a real pseudo-terminal."""

import errno
import os
from pathlib import Path
import pty
import select
import subprocess
import sys
import tempfile
import unittest

from python.runfiles import runfiles


class RgTerminalTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "a").write_text("hit\nmiss\n", encoding="ascii")
        (self.root / "b").write_text("miss hit\n", encoding="ascii")
        self.binary = runfiles.Create().Rlocation(EXECUTABLE)
        self.environment = dict(
            os.environ,
            XFF_TEST_SYSTEM_CONFIG=str(self.root / "absent-system"),
            XFF_TEST_USER_CONFIG=str(self.root / "absent-user"),
        )

    def run_search(self, options, terminal):
        command = [str(self.binary), "--rg", "--no-pager", "--color=never", "--sort=dir"]
        command.extend(options)
        command.extend(["hit", "a", "b"])
        if not terminal:
            result = subprocess.run(
                command, cwd=self.root, env=self.environment, stdin=subprocess.DEVNULL,
                capture_output=True, check=True, timeout=30,
            )
            return result.stdout.decode("ascii")
        master, slave = pty.openpty()
        try:
            subprocess.run(
                command, cwd=self.root, env=self.environment, stdin=subprocess.DEVNULL, stdout=slave, stderr=subprocess.PIPE,
                check=True, timeout=30,
            )
            chunks = []
            # Drain before closing the last slave handle: macOS can discard queued
            # terminal output at hangup. The child has exited, so no writes remain.
            while select.select([master], [], [], 0)[0]:
                try:
                    block = os.read(master, 4096)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    break
                if not block:
                    break
                chunks.append(block)
            return b"".join(chunks).decode("ascii").replace("\r\n", "\n")
        finally:
            os.close(master)
            if slave >= 0:
                os.close(slave)

    def test_terminal_defaults_to_headings_and_line_numbers(self):
        self.assertEqual("a\n1:hit\n\nb\n1:miss hit\n", self.run_search([], True))

    def test_pipe_defaults_to_inline_filenames_without_line_numbers(self):
        self.assertEqual("a:hit\nb:miss hit\n", self.run_search([], False))

    def test_explicit_terminal_overrides(self):
        self.assertEqual("a:hit\nb:miss hit\n", self.run_search(["--no-heading", "-N"], True))
        self.assertEqual("a\n1:1:hit\n\nb\n1:6:miss hit\n", self.run_search(["--heading", "--column"], False))

    def test_pretty_and_json_preserve_distinct_output_formats(self):
        self.assertIn("\x1b[1;31mhit\x1b[0m", self.run_search(["--pretty"], False))
        output = self.run_search(["--pretty", "--format=jsonl"], True)
        self.assertNotIn("\x1b", output)
        self.assertNotIn("\n\n", output)
        self.assertIn('"record":"grep"', output)


if __name__ == "__main__":
    EXECUTABLE = sys.argv.pop(1)
    unittest.main()
