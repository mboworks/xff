#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Exercise the process boundary and measurements without timing thresholds."""

import json
import pathlib
import subprocess
import sys
import unittest


@unittest.skipUnless(sys.platform in {"darwin", "linux"}, "resource units are platform-specific")
class MeasureResourcesTest(unittest.TestCase):
    def run_measurement(self, code: str) -> tuple[int, dict[str, object]]:
        script = pathlib.Path(__file__).with_name("measure_resources.py")
        result = subprocess.run(
            [sys.executable, str(script), "--cache-state=warm", "--", sys.executable, "-c", code],
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.stderr, "")
        return result.returncode, json.loads(result.stdout)

    def test_output_and_observed_latency(self):
        code = "import sys; sys.stdout.write('abc'); sys.stdout.flush(); sys.stderr.write('diagnostic')"
        status, result = self.run_measurement(code)
        self.assertEqual(status, 0)
        self.assertEqual(result["stdout_bytes"], 3)
        self.assertEqual(result["stderr_tail"], "diagnostic")
        self.assertEqual(result["cache_state"], "warm")
        self.assertGreaterEqual(result["first_stdout_seconds"], 0)
        self.assertLessEqual(result["first_stdout_seconds"], result["elapsed_seconds"])
        self.assertGreater(result["peak_child_rss_bytes"], 0)

    def test_empty_stdout_is_not_zero_latency(self):
        status, result = self.run_measurement("pass")
        self.assertEqual(status, 0)
        self.assertEqual(result["stdout_bytes"], 0)
        self.assertIsNone(result["first_stdout_seconds"])

    def test_failure_and_large_stderr_are_reported_without_pipe_deadlock(self):
        status, result = self.run_measurement("import sys; sys.stderr.write('x' * 1048576); sys.exit(7)")
        self.assertEqual(status, 1)
        self.assertEqual(result["exit_code"], 7)
        self.assertEqual(result["stderr_bytes"], 1048576)
        self.assertEqual(result["stderr_tail"], "x" * 32768)


if __name__ == "__main__":
    unittest.main()
