#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Tests for tools/fuzz_targets.py."""

import os
import json
import shlex
import pathlib
import sys
import tempfile
import threading
import subprocess
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_paths import repository_file
import fuzz_targets  # noqa: E402


class FuzzTargetsTest(unittest.TestCase):
    def test_parses_balanced_rules_with_nested_calls_and_parentheses_in_strings(self):
        self.assertEqual(
            fuzz_targets.rule_blocks(
                'cc_fuzz_test(name = "one", corpus = glob(["(*)"]))\ncc_fuzz_test(name = "two")',
                "cc_fuzz_test",
            ),
            ['name = "one", corpus = glob(["(*)"])', 'name = "two"'],
        )

    def test_discovers_core_and_extra_campaign_labels(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "bazelmod").mkdir()
            (root / "bazelmod" / "extras.MODULE.bazel").write_text(
                'local_path_override(module_name = "xff_arc", path = "extra_modules/arc")',
                encoding="utf-8",
            )
            (root / "xff" / "parser").mkdir(parents=True)
            (root / "xff" / "parser" / "BUILD.bazel").write_text(
                'cc_fuzz_test(name = "parser_fuzz_test", corpus = glob(["corpus/**"]))',
                encoding="utf-8",
            )
            (root / "extra_modules" / "arc" / "nested").mkdir(parents=True)
            (root / "extra_modules" / "arc" / "nested" / "BUILD.bazel").write_text(
                'cc_fuzz_test(name = "archive_fuzz_test")', encoding="utf-8"
            )
            self.assertEqual(
                fuzz_targets.discover_targets(root),
                ["//xff/parser:parser_fuzz_test_run", "@xff_arc//nested:archive_fuzz_test_run"],
            )

    def test_campaign_duration_tracks_target_count(self):
        self.assertEqual(fuzz_targets.campaign_duration_seconds(["one", "two", "three"], 60, 180), 180)

    def test_campaign_duration_rejects_nonpositive_seconds(self):
        with self.assertRaisesRegex(ValueError, "must be positive"):
            fuzz_targets.campaign_duration_seconds(["one"], 0)

    def test_campaign_duration_rejects_over_budget_target_growth(self):
        with self.assertRaisesRegex(ValueError, "240 seconds, exceeding the 180-second budget"):
            fuzz_targets.campaign_duration_seconds(["one", "two", "three", "four"], 60, 180)

    def test_campaign_builds_once_and_prepares_before_parallel_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            program = """
import pathlib, time
here = pathlib.Path.cwd()
(here / 'started').write_text('ready')
deadline = time.monotonic() + 5
while len(list(here.parent.glob('*/started'))) != 2:
    if time.monotonic() > deadline:
        raise SystemExit(9)
    time.sleep(0.01)
print('stat::number_of_executed_units: 42')
print('stat::average_exec_per_sec: 21')
"""
            def prepare(command, **unused):
                if command[1] == "run":
                    script = next(arg.split("=", 1)[1] for arg in command if arg.startswith("--script_path="))
                    pathlib.Path(script).write_text(f"exec {shlex.quote(sys.executable)} -c {shlex.quote(program)}\n")
                    self.assertTrue(any(arg.startswith("--fuzzing_output_root=") for arg in command))
                    self.assertIn("--timeout_secs=1", command)
                    self.assertIn("-print_final_stats=1", command)
                return mock.Mock(returncode=0)
            targets = ["//one:first_run", "@extra//two:second_run"]
            with mock.patch("fuzz_targets.subprocess.run", side_effect=prepare) as run, \
                 mock.patch("fuzz_targets.worker_count", return_value=2):
                self.assertEqual(fuzz_targets.run_campaigns(targets, 1, ["--disk_cache=/cache"],
                                                           output_dir=root, build_profile=root / "trace.json.gz"), 0)
            commands = [call.args[0] for call in run.call_args_list]
            self.assertEqual([command[1] for command in commands], ["build", "run", "run"])
            self.assertEqual(commands[0][-2:], targets)
            self.assertTrue(any(arg.startswith("--profile=") for arg in commands[0]))
            self.assertTrue(all(not any(arg.startswith("--profile=") for arg in command)
                                for command in commands[1:]))
            for command in commands:
                self.assertIn("--disk_cache=/cache", command)
            summary = json.loads((root / "summary.json").read_text())
            self.assertEqual(summary["workers"], 2)
            self.assertEqual([result["number_of_executed_units"] for result in summary["campaigns"]], ["42", "42"])
            self.assertEqual(len(list(root.glob("*/campaign.log"))), 2)

    def test_build_failure_prevents_preparation_and_execution(self):
        with tempfile.TemporaryDirectory() as temporary, \
             mock.patch("fuzz_targets.subprocess.run", return_value=mock.Mock(returncode=7)) as run:
            self.assertEqual(fuzz_targets.run_campaigns(["//one:run"], 1, [], output_dir=pathlib.Path(temporary)), 7)
            self.assertEqual(run.call_count, 1)

    def test_failure_stops_queued_campaigns_and_preserves_logs(self):
        def prepare(command, **unused):
            if command[1] == "run":
                script = next(arg.split("=", 1)[1] for arg in command if arg.startswith("--script_path="))
                pathlib.Path(script).write_text("echo crash-details; exit 17\n")
            return mock.Mock(returncode=0)
        with tempfile.TemporaryDirectory() as temporary, \
             mock.patch("fuzz_targets.subprocess.run", side_effect=prepare), \
             mock.patch("fuzz_targets.worker_count", return_value=1):
            root = pathlib.Path(temporary)
            self.assertEqual(fuzz_targets.run_campaigns(["//one:run", "//two:run"], 1, [], output_dir=root), 17)
            results = json.loads((root / "summary.json").read_text())["campaigns"]
            self.assertEqual([result["status"] for result in results], ["failed", "skipped"])
            self.assertIn("crash-details", (root / "000-run/campaign.log").read_text())

    def test_campaign_timeout_stops_process_group_and_records_failure(self):
        with tempfile.TemporaryDirectory() as temporary, \
             mock.patch("fuzz_targets.subprocess.Popen") as launch, \
             mock.patch("fuzz_targets.stop_process") as stop_process, \
             mock.patch("fuzz_targets.time.monotonic", side_effect=[0, 62, 63]):
            process = launch.return_value.__enter__.return_value
            process.wait.side_effect = subprocess.TimeoutExpired("campaign", 0.2)
            root = pathlib.Path(temporary)
            stop = threading.Event()
            result = fuzz_targets.execute_campaign("//one:run", root / "launch.sh", root, 1, {}, stop)
            self.assertEqual(result["returncode"], 124)
            self.assertTrue(stop.is_set())
            stop_process.assert_called_once_with(process)
            self.assertTrue(launch.call_args.kwargs["start_new_session"])

    @mock.patch("fuzz_targets.os.cpu_count", return_value=8)
    @mock.patch("fuzz_targets.os.sysconf", side_effect=lambda name: 4096 if name == "SC_PAGE_SIZE" else 4 * 1024 * 1024)
    def test_worker_limits_cover_cpus_memory_and_target_count(self, unused_memory, unused_cpu):
        with mock.patch("fuzz_targets.os.sched_getaffinity", return_value=set(range(8)), create=True):
            self.assertEqual(fuzz_targets.worker_count(8, 10, 2048), 5)
            self.assertEqual(fuzz_targets.worker_count(2, 10, 2048), 2)
            self.assertEqual(fuzz_targets.worker_count(8, 1, 2048), 1)
        with mock.patch("fuzz_targets.os.sysconf", side_effect=ValueError):
            self.assertEqual(fuzz_targets.worker_count(8, 10, 2048), 1)

    def test_both_workflows_use_parallel_driver_and_keep_failure_artifacts(self):
        for name in ("main", "fuzz"):
            workflow = (repository_file(f".github/workflows/{name}.yml")).read_text()
            self.assertIn('--jobs="$(getconf _NPROCESSORS_ONLN)"', workflow)
            self.assertIn('--output-dir="${RUNNER_TEMP}/fuzz-campaigns"', workflow)
            self.assertIn("name: Preserve fuzz campaign logs and findings\n        if: always()", workflow)


if __name__ == "__main__":
    unittest.main()
