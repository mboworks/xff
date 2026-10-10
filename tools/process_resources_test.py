#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Exercise native command accounting, pipe lifetime and launcher memory isolation."""

import json
import os
from pathlib import Path
import resource
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest

import process_resources as resources


def python_command(code):
    return [sys.executable, "-c", code]


@unittest.skipUnless(sys.platform in {"linux", "darwin"}, "native resource units are platform-specific")
class ProcessResourcesTest(unittest.TestCase):
    def test_output_diagnostics_exit_status_and_latency(self):
        result = resources.measure_pipeline([python_command(
            "import sys; sys.stdout.write('abc'); sys.stdout.flush(); sys.stderr.write('diagnostic'); sys.exit(7)")])
        self.assertEqual(result["output"], b"abc")
        self.assertEqual(result["stdout_bytes"], 3)
        self.assertEqual(result["stderr"], "diagnostic")
        self.assertEqual(result["stderr_bytes"], len("diagnostic"))
        self.assertEqual(result["exit_codes"], [7])
        self.assertGreaterEqual(result["first_stdout_seconds"], 0)
        self.assertLessEqual(result["first_stdout_seconds"], result["elapsed_seconds"])
        self.assertEqual(result["peak_child_rss_bytes"], result["sum_child_peak_rss_bytes"])
        self.assertGreaterEqual(result["user_cpu_seconds"], 0)
        self.assertGreaterEqual(result["system_cpu_seconds"], 0)

    def test_pipeline_input_output_and_individual_peaks(self):
        with tempfile.TemporaryFile() as source:
            source.write(b"hello\0world\0")
            source.seek(0)
            result = resources.measure_pipeline([
                python_command("import sys; sys.stdout.buffer.write(sys.stdin.buffer.read())"),
                python_command("import sys; sys.stdout.buffer.write(sys.stdin.buffer.read().upper())"),
            ], source=source)
        self.assertEqual(result["output"], b"HELLO\0WORLD\0")
        self.assertEqual(result["exit_codes"], [0, 0])
        self.assertIsNone(result["peak_child_rss_bytes"])
        self.assertEqual(len(result["child_peak_rss_bytes"]), 2)
        self.assertEqual(result["sum_child_peak_rss_bytes"], sum(result["child_peak_rss_bytes"]))

    def test_large_output_and_stderr_do_not_deadlock_or_require_output_retention(self):
        result = resources.measure_pipeline([python_command(
            "import sys; sys.stderr.write('e' * 1048576); sys.stdout.write('x' * 1048576)")],
            retain_output=False, stderr_limit=32768)
        self.assertEqual(result["output"], b"")
        self.assertEqual(result["stdout_bytes"], 1048576)
        self.assertEqual(result["stderr_bytes"], 1048576)
        self.assertEqual(result["stderr"], "e" * 32768)
        self.assertEqual(result["exit_codes"], [0])

    def test_empty_output_and_signaled_exit(self):
        result = resources.measure_pipeline([python_command("import os, signal; os.kill(os.getpid(), signal.SIGTERM)")])
        self.assertEqual(result["exit_codes"], [-signal.SIGTERM])
        self.assertEqual(result["stdout_bytes"], 0)
        self.assertIsNone(result["first_stdout_seconds"])

    def test_cwd_and_environment_reach_the_measured_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            environment = dict(os.environ, RESOURCE_TEST_TOKEN="token")
            result = resources.measure_pipeline([python_command(
                "import json, os; print(json.dumps([os.getcwd(), os.environ['RESOURCE_TEST_TOKEN']]))")],
                cwd=temporary, environment=environment)
            self.assertEqual(json.loads(result["output"]), [str(Path(temporary).resolve()), "token"])

    def test_missing_executable_reports_failure_and_diagnostic(self):
        result = resources.measure_pipeline([["/nonexistent-xff-resource-test-command"]])
        self.assertEqual(result["exit_codes"], [127])
        self.assertIn("cannot exec /nonexistent-xff-resource-test-command", result["stderr"])

    def test_invalid_cwd_reports_launcher_failure(self):
        with self.assertRaisesRegex(ValueError, "native resource launcher failed.*chdir"):
            resources.measure_pipeline([python_command("pass")], cwd="/nonexistent-xff-resource-test-cwd")

    def test_pipeline_preserves_closed_stdin_without_aliasing_an_internal_pipe(self):
        # Close stdin immediately before exec, after subprocess has connected
        # its stdio. The helper's first pipe would otherwise occupy descriptor 0.
        commands = [python_command("print('payload')"),
                    python_command("import sys; sys.stdout.write(sys.stdin.read())")]
        output_read, output_write = os.pipe()
        argv = [str(resources.resolve_launcher()), str(output_write), ""]
        for command in commands:
            argv.extend([str(len(command)), *command])
        wrapper = "import os, sys\ntry: os.close(0)\nexcept OSError: pass\nos.execv(sys.argv[1], sys.argv[1:])"
        try:
            child = subprocess.Popen([sys.executable, "-c", wrapper, *argv], stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, pass_fds=(output_write,))
        finally:
            os.close(output_write)
        try:
            with os.fdopen(output_read, "rb") as output:
                ready, _, _ = select.select([output], [], [], 10)
                self.assertTrue(ready, "closed-stdin pipeline did not finish")
                self.assertEqual(output.read(), b"payload\n")
            report, stderr = child.communicate(timeout=10)
            self.assertEqual(child.returncode, 0, stderr)
            self.assertEqual(json.loads(report)["exit_codes"], [0, 0])
        finally:
            if child.poll() is None:
                child.terminate()
            child.communicate(timeout=10)

    @unittest.skipUnless(sys.platform == "linux", "Linux fork/exec inherited-RSS regression")
    def test_python_high_water_mark_is_not_attributed_to_tiny_command(self):
        native_true = shutil.which("true")
        self.assertIsNotNone(native_true)
        # Touch every page and retain it across launch; a direct Python fork/wait4
        # would attribute this footprint to even /bin/true.
        padding = bytearray(96 * 1024 * 1024)
        padding[::4096] = b"x" * (len(padding) // 4096)
        parent_peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * 1024
        result = resources.measure_pipeline([[native_true]])
        self.assertLess(result["peak_child_rss_bytes"], parent_peak // 2)
        self.assertEqual(padding[0], ord("x"))

    def test_actual_command_allocation_is_not_discarded(self):
        result = resources.measure_pipeline([python_command(
            "block = bytearray(64 * 1024 * 1024); "
            "block[::4096] = b'x' * (len(block) // 4096); print(block[0])")])
        self.assertGreaterEqual(result["peak_child_rss_bytes"], 64 * 1024 * 1024)

    @unittest.skipUnless(hasattr(os, "sched_getaffinity"), "native Linux affinity")
    def test_cpu_affinity_is_inherited_by_native_children(self):
        original = os.sched_getaffinity(0)
        restricted = {min(original)}
        try:
            os.sched_setaffinity(0, restricted)
            result = resources.measure_pipeline([python_command(
                "import json, os; print(json.dumps(sorted(os.sched_getaffinity(0))))")])
        finally:
            os.sched_setaffinity(0, original)
        self.assertEqual(json.loads(result["output"]), sorted(restricted))

    def test_termination_reaps_inflight_commands(self):
        output_read, output_write = os.pipe()
        command = python_command(
            "import os, signal; print(os.getpid(), flush=True); signal.pause()")
        argv = [str(resources.resolve_launcher()), str(output_write), "", str(len(command)), *command]
        child = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 pass_fds=(output_write,), start_new_session=True)
        os.close(output_write)
        try:
            with os.fdopen(output_read, "rb") as output:
                ready, _, _ = select.select([output], [], [], 10)
                self.assertTrue(ready, "measured command did not announce its PID")
                pid = int(output.readline())
                child.terminate()
                stdout, stderr = child.communicate(timeout=10)
                self.assertEqual(child.returncode, 2, (stdout, stderr))
                with self.assertRaises(ProcessLookupError):
                    os.kill(pid, 0)
        finally:
            # Own one isolated test process group, including any child left by a failure.
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            child.communicate(timeout=10)


class ProcessResourcesValidationTest(unittest.TestCase):
    def test_accounting_contract_preserves_legacy_and_rejects_partial_or_mismatched_native_identity(self):
        resources.validate_accounting_contract(None, None)
        identity = dict(path="/launcher", sha256="a" * 64,
                        memory_method=resources.MEMORY_METHOD, wall_method=resources.WALL_METHOD)
        method = {key: value for key, value in identity.items() if key != "path"}
        resources.validate_accounting_contract(identity, method)
        resources.validate_accounting_contract(dict(identity, path="/other-host/launcher"), method)
        for launcher, contract in ((None, method), (identity, None),
                                   (dict(identity, sha256="b" * 64), method),
                                   (identity, dict(method, wall_method="old launch boundary"))):
            with self.subTest(launcher=launcher, contract=contract), self.assertRaises(ValueError):
                resources.validate_accounting_contract(launcher, contract)

    def test_invalid_commands_are_rejected_before_launch(self):
        for commands in (None, [], [[]], ["true"], [[""]], [["true"]] * 17, [["true", "x\0y"]], [[b"true"]]):
            with self.subTest(commands=commands), self.assertRaises(ValueError):
                resources.measure_pipeline(commands)

    def test_invalid_stderr_limits_are_rejected_before_launch(self):
        for limit in (-1, True, 1.5, "1"):
            with self.subTest(limit=limit), self.assertRaisesRegex(ValueError, "stderr limit"):
                resources.measure_pipeline([["true"]], stderr_limit=limit)

    def test_invalid_native_metadata_is_not_accepted(self):
        valid = {"rss_bytes": [100], "exit_codes": [0], "user_cpu_seconds": 0, "system_cpu_seconds": 0}
        bad_fields = {"rss_bytes": ([-1], [], [True]), "exit_codes": ([], ["0"]),
                      "user_cpu_seconds": (float("nan"), float("inf"), -1, True)}
        for key, values in bad_fields.items():
            for value in values:
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    resources.validate_usage(dict(valid, **{key: value}), 1)

    def test_invalid_launcher_identities_are_not_accepted(self):
        valid = {"path": "/native-launcher", "sha256": "a" * 64,
                 "memory_method": resources.MEMORY_METHOD, "wall_method": resources.WALL_METHOD}
        resources.validate_launcher_identity(valid)
        for identity in (None, {}, dict(valid, sha256="not-a-hash"), dict(valid, sha256="A" * 64),
                         dict(valid, memory_method=""), dict(valid, path=1), dict(valid, extra="field")):
            with self.subTest(identity=identity), self.assertRaises(ValueError):
                resources.validate_launcher_identity(identity)


if __name__ == "__main__":
    unittest.main()
