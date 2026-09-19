# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Exercise benchmark workloads and report provenance through declared executables."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from python.runfiles import runfiles


class ReadBenchmarkTest(unittest.TestCase):
    def test_runtime_reads_and_driver_reports(self):
        resolver = runfiles.Create()
        binary, xff, driver = [Path(resolver.Rlocation(path)) for path in EXECUTABLES]
        with tempfile.TemporaryDirectory() as temporary:
            self.check_reports(binary, xff, driver, Path(temporary))

    def check_reports(self, binary, xff, driver, root):
        left, right = root / "left", root / "right"
        for folder in (left, right):
            folder.mkdir()
            (folder / "same.txt").write_text("abc\n")

        def run(*args):
            return subprocess.run([str(binary), *map(str, args)], text=True, capture_output=True, check=False)

        for scenario in ("listing", "summary", "hash", "hash_twice", "hash_lines", "compare"):
            result = run(scenario, 2, left, *([right] if scenario == "compare" else []))
            self.assertTrue(result.returncode == 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertTrue(report["errors"] == 0 and report["output_bytes"] > 0, report)
            self.assertTrue(0 <= report["first_output_seconds"] <= report["elapsed_seconds"], report)
            size = report["whole"]["bytes"] + report["range"]["bytes"]
            self.assertTrue(size == 0 if scenario in ("listing", "summary") else size >= 4, report)

        for args in ((), ("unknown", 1, left), ("hash", "bad", left), ("hash", 0, left),
                     ("hash", 1, "--no-safe"), ("compare", 1, left)):
            result = run(*args)
            self.assertTrue(result.returncode == 2 and result.stderr, (args, result))
        result = run("hash", 1, root / "missing")
        self.assertTrue(result.returncode == 1, result)
        report = json.loads(result.stdout)
        self.assertTrue(report["errors"] > 0 and report["first_output_seconds"] is None, report)

        env = dict(os.environ, XFF_TEST_SYSTEM_CONFIG=str(root / "no-system"), XFF_TEST_USER_CONFIG=str(root / "no-user"))
        output = root / "report.json"
        result = subprocess.run([
            str(driver),
            str(xff), "--read-counter", str(binary),
            "--files=4", "--depth=2", "--repetitions=1", "--jobs=2", "--output", str(output),
        ], text=True, capture_output=True, check=False, env=env)
        self.assertTrue(result.returncode == 0, (result.stdout, result.stderr))
        report = json.loads(output.read_text())
        self.assertTrue(len(report["measurements"]) == 12, report)
        self.assertTrue(all(row["logical_read_run"]["separate_invocation"] for row in report["measurements"]), report)
        self.assertTrue(all(row["logical_read_run"]["errors"] == 0 for row in report["measurements"]), report)


if __name__ == "__main__":
    EXECUTABLES = sys.argv[1:4]
    del sys.argv[1:4]
    unittest.main()
