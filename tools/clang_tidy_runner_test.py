#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

import io
import pathlib
import re
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

from tools import clang_tidy_runner


class ClangTidyRunnerTest(unittest.TestCase):
    def test_default_reserves_one_cpu_and_explicit_jobs_win(self):
        arguments = [
            "--clang-tidy", "clang-tidy",
            "--compile-database", ".",
            "--output", "diagnostics.txt",
            "--test-disabled-checks=",
        ]
        for cpus, expected in ((18, 17), (2, 1), (1, 1), (None, 1)):
            with self.subTest(cpus=cpus), mock.patch.object(
                clang_tidy_runner.os, "cpu_count", return_value=cpus
            ):
                self.assertEqual(clang_tidy_runner.parse_args(arguments).jobs, expected)
                self.assertEqual(
                    clang_tidy_runner.parse_args(arguments + ["--jobs", "8"]).jobs, 8
                )

    def test_header_filter_matches_owned_relative_and_absolute_paths(self):
        root = pathlib.Path("/workspace parent/xff")
        pattern = re.compile(clang_tidy_runner.owned_header_filter(root))
        owned = (
            "xff/engine/walk.h",
            "xff_extras_api/filesystem.h",
            "extra_modules/archive/archive_fs.h",
            "external/xff_archive+/archive_fs.h",
            "external/xff_extras_api+/filesystem.h",
            "bazel-out/platform/bin/xff/config.h",
            "bazel-out/platform/bin/external/xff_archive+/_virtual_includes/api/header.h",
        )
        for path in owned:
            for spelling in (path, "./" + path, str(root / path)):
                with self.subTest(path=spelling):
                    self.assertIsNotNone(pattern.search(spelling))
        self.assertEqual(clang_tidy_runner.header_filter_arguments(root),
                         ("--header-filter=" + pattern.pattern,))

    def test_header_filter_excludes_dependencies_even_inside_a_checkout_named_xff(self):
        root = pathlib.Path("/workspace parent/xff")
        pattern = re.compile(clang_tidy_runner.owned_header_filter(root))
        foreign = (
            "mbo/container/example.h",
            "external/mboworks_mbo+/mbo/container/example.h",
            "external/abseil-cpp+/absl/status/status.h",
            "external/not_xff_archive+/archive_fs.h",
            "external/xff_squashfs++http_archive+libsqsh/include/sqsh.h",
            "external/xff_archive+near_miss/archive_fs.h",
            "bazel-out/platform/bin/external/xff_squashfs++http_archive+libsqsh/include/sqsh.h",
            "xff_near_miss/example.h",
            "xff_extras_api_near_miss/example.h",
            "extra_modules_near_miss/example.h",
            "bazel-out/platform/bin/external/abseil-cpp+/absl/status/status.h",
        )
        for path in foreign:
            for spelling in (path, "./" + path, str(root / path)):
                with self.subTest(path=spelling):
                    self.assertIsNone(pattern.search(spelling))
        self.assertIsNone(pattern.search("/other/project/xff/engine/walk.h"))

    def test_real_header_probe_rejects_suppressed_or_unexpected_diagnostics(self):
        for returncode, output in ((0, ""), (1, "xff/probe.h:1: warning: missing peers"),
                                   (1, "mbo/probe.h:1: warning: dependency diagnostic")):
            with self.subTest(returncode=returncode, output=output), mock.patch.object(
                clang_tidy_runner.subprocess, "run",
                return_value=subprocess.CompletedProcess([], returncode, stdout=output),
            ) as run:
                with self.assertRaisesRegex(RuntimeError, "owned-header visibility probe failed"):
                    clang_tidy_runner.verify_header_visibility("clang-tidy")
                command = run.call_args.args[0]
                self.assertNotIn("--system-headers", command)
                self.assertIn("--checks=-*,modernize-use-nullptr,bugprone-signed-bitwise", command)
                source = pathlib.Path(command[command.index("--") - 1])
                self.assertEqual(source.name, "probe.cc")

    def test_real_probe_checks_all_include_forms_without_enabling_foreign_system_macros(self):
        paths = ("xff/probe0.h", "xff_extras_api/probe1.h", "extra_modules/archive/probe2.h",
                 "external/xff_archive+/probe3.h",
                 "bazel-out/platform/bin/external/xff_extras_api+/_virtual_includes/probe/probe4.h")
        output = "\n".join(f"{path}:1: warning: owned finding" for path in paths)
        with mock.patch.object(clang_tidy_runner.subprocess, "run",
                               return_value=subprocess.CompletedProcess([], 1, stdout=output)) as run:
            clang_tidy_runner.verify_header_visibility("clang-tidy")
        self.assertEqual(run.call_count, 4)
        for call in run.call_args_list:
            command = call.args[0]
            self.assertNotIn("--system-headers", command)
            self.assertIn("--checks=-*,modernize-use-nullptr,bugprone-signed-bitwise", command)

    def test_real_probe_rejects_foreign_system_macro_diagnostics(self):
        paths = ("xff/probe0.h", "xff_extras_api/probe1.h", "extra_modules/archive/probe2.h",
                 "external/xff_archive+/probe3.h",
                 "bazel-out/platform/bin/external/xff_extras_api+/_virtual_includes/probe/probe4.h")
        output = "\n".join(f"{path}:1: warning: owned finding" for path in paths)
        output += "\nprobe.cc:10: error: foreign macro body [bugprone-signed-bitwise]"
        with mock.patch.object(clang_tidy_runner.subprocess, "run",
                               return_value=subprocess.CompletedProcess([], 1, stdout=output)):
            with self.assertRaisesRegex(RuntimeError, "owned-header visibility probe failed"):
                clang_tidy_runner.verify_header_visibility("clang-tidy")

    def test_progress_line(self):
        result = clang_tidy_runner.Result(
            clang_tidy_runner.Task("mbo/file/glob.cc"), 0, "", 4.25
        )
        self.assertEqual(
            clang_tidy_runner.progress_line(42, 187, result),
            "[ 42/187  22.5%] PASS mbo/file/glob.cc (4.2s)",
        )

    def test_parallel_run_reports_completion_and_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            executable = root / "fake-clang-tidy"
            executable.write_text(
                textwrap.dedent(
                    """\
                    #!/usr/bin/env python3
                    import pathlib
                    import sys

                    path = pathlib.Path(sys.argv[-1]).name
                    if path == "bad.cc":
                        print("bad.cc:1:1: error: finding")
                        raise SystemExit(1)
                    print(f"{path}: routine successful output")
                    """
                ),
                encoding="utf-8",
            )
            executable.chmod(executable.stat().st_mode | stat.S_IXUSR)
            output = root / "diagnostics.txt"
            stream = io.StringIO()
            status = clang_tidy_runner.run_all(
                [
                    clang_tidy_runner.Task("slow.cc"),
                    clang_tidy_runner.Task("good.cc"),
                    clang_tidy_runner.Task("bad.cc"),
                ],
                str(executable),
                ".",
                2,
                str(output),
                stream,
            )

            rendered = stream.getvalue()
            self.assertEqual(status, 1)
            self.assertIn("clang-tidy: 3 translation unit(s), 2 worker(s)", rendered)
            self.assertIn("PASS good.cc", rendered)
            self.assertIn("PASS slow.cc", rendered)
            self.assertIn("[3/3 100.0%]", rendered)
            self.assertIn("FAIL bad.cc", rendered)
            self.assertIn("clang-tidy: 2 passed, 1 failed", rendered)
            self.assertIn("bad.cc:1:1: error: finding", output.read_text(encoding="utf-8"))
            self.assertNotIn("routine successful output", rendered)
            self.assertIn("routine successful output", output.read_text(encoding="utf-8"))

    def test_progress_follows_completion_order_not_submission_order(self):
        tasks = [clang_tidy_runner.Task(name) for name in ("slow.cc", "good.cc", "last.cc")]
        futures = [mock.Mock() for _ in tasks]
        for future, task in zip(futures, tasks):
            future.result.return_value = clang_tidy_runner.Result(task, 0, "", 0.0)
        stream = io.StringIO()
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(
            clang_tidy_runner.concurrent.futures, "ThreadPoolExecutor"
        ) as executor, mock.patch.object(
            clang_tidy_runner.concurrent.futures, "as_completed",
            return_value=(futures[1], futures[2], futures[0]),
        ) as completed:
            executor.return_value.__enter__.return_value.submit.side_effect = futures
            status = clang_tidy_runner.run_all(
                tasks, "unused-tool", ".", 2, str(pathlib.Path(directory) / "diagnostics.txt"), stream
            )

        self.assertEqual(status, 0)
        executor.assert_called_once_with(max_workers=2)
        completed.assert_called_once()
        self.assertEqual(list(completed.call_args.args[0]), futures)
        progress = [line for line in stream.getvalue().splitlines() if line.startswith("[")]
        self.assertEqual(progress, [
            "[1/3  33.3%] PASS good.cc (0.0s)",
            "[2/3  66.7%] PASS last.cc (0.0s)",
            "[3/3 100.0%] PASS slow.cc (0.0s)",
        ])

    def test_registry_terminates_active_children(self):
        registry = clang_tidy_runner.ProcessRegistry()
        process = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
        self.assertTrue(registry.add(process))

        registry.terminate_all()

        self.assertIsNotNone(process.poll())
        self.assertFalse(registry.add(process))

    def test_extra_arguments_are_forwarded(self):
        args = clang_tidy_runner.parse_args(
            [
                "--clang-tidy=clang-tidy",
                "--compile-database=.",
                "--output=diagnostics.txt",
                "--test-disabled-checks=-example",
                "--extra-arg-before=-isystem/libcxx",
            ]
        )
        self.assertEqual(args.extra_arg_before, ["-isystem/libcxx"])


if __name__ == "__main__":
    unittest.main()
