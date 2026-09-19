#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

test::runtime_reads_and_driver_reports() {
  local root
  root="$(test_tmpdir reads)"
  python3 - "${TEST_SRCDIR}/${TEST_WORKSPACE}" "${root}" <<'PYTEST'
import json
import os
from pathlib import Path
import subprocess
import sys

workspace, root = map(Path, sys.argv[1:])
binary = workspace / "xff/engine/read_benchmark"
left, right = root / "left", root / "right"
for folder in (left, right):
    folder.mkdir()
    (folder / "same.txt").write_text("abc\n")

def run(*args):
    return subprocess.run([str(binary), *map(str, args)], text=True, capture_output=True, check=False)

for scenario in ("listing", "summary", "hash", "hash_twice", "hash_lines", "compare"):
    result = run(scenario, 2, left, *([right] if scenario == "compare" else []))
    assert result.returncode == 0, result.stderr
    report = json.loads(result.stdout)
    assert report["errors"] == 0 and report["output_bytes"] > 0, report
    assert 0 <= report["first_output_seconds"] <= report["elapsed_seconds"], report
    size = report["whole"]["bytes"] + report["range"]["bytes"]
    assert size == 0 if scenario in ("listing", "summary") else size >= 4, report

for args in ((), ("unknown", 1, left), ("hash", "bad", left), ("hash", 0, left),
             ("hash", 1, "--no-safe"), ("compare", 1, left)):
    result = run(*args)
    assert result.returncode == 2 and result.stderr, (args, result)
result = run("hash", 1, root / "missing")
assert result.returncode == 1, result
report = json.loads(result.stdout)
assert report["errors"] > 0 and report["first_output_seconds"] is None, report

env = dict(os.environ, XFF_TEST_SYSTEM_CONFIG=str(root / "no-system"), XFF_TEST_USER_CONFIG=str(root / "no-user"))
output = root / "report.json"
result = subprocess.run([
    sys.executable, str(workspace / "tools/benchmark_resources.py"),
    str(workspace / "xff/cli/testing/xff"), "--read-counter", str(binary),
    "--files=4", "--depth=2", "--repetitions=1", "--jobs=2", "--output", str(output),
], text=True, capture_output=True, check=False, env=env)
assert result.returncode == 0, (result.stdout, result.stderr)
report = json.loads(output.read_text())
assert len(report["measurements"]) == 12, report
assert all(row["logical_read_run"]["separate_invocation"] for row in report["measurements"]), report
assert all(row["logical_read_run"]["errors"] == 0 for row in report["measurements"]), report
PYTEST
}
