#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Measure one command in a fresh process; emit one JSON record instead of its stdout."""

import argparse
import json
import os
import platform
import resource
import subprocess
import sys
import tempfile
import time


def measure(command: list[str]) -> dict[str, object]:
    """Drain stdout without retaining it; stderr goes to disk to avoid pipe deadlock."""
    started = time.monotonic()
    first_output = None
    output_bytes = 0
    usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    with tempfile.TemporaryFile() as errors:
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors) as child:
            assert child.stdout is not None
            first = child.stdout.read(1)
            if first:
                first_output = time.monotonic() - started
                output_bytes = 1
            while chunk := child.stdout.read(65536):
                output_bytes += len(chunk)
            exit_code = child.wait()
        elapsed = time.monotonic() - started
        errors.seek(0, os.SEEK_END)
        stderr_bytes = errors.tell()
        errors.seek(max(0, stderr_bytes - 32768))
        stderr_tail = errors.read().decode("utf-8", errors="replace")
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    # getrusage reports bytes on Darwin, KiB on Linux. No other platform is supported.
    memory_scale = 1 if sys.platform == "darwin" else 1024
    return {
        "command": command,
        "cwd": os.getcwd(),
        "platform": platform.platform(),
        "elapsed_seconds": elapsed,
        "first_stdout_seconds": first_output,
        "stdout_bytes": output_bytes,
        "stderr_bytes": stderr_bytes,
        "stderr_tail": stderr_tail,
        "exit_code": exit_code,
        "user_cpu_seconds": usage.ru_utime - usage_before.ru_utime,
        "system_cpu_seconds": usage.ru_stime - usage_before.ru_stime,
        "peak_child_rss_bytes": usage.ru_maxrss * memory_scale,
        "cache_state": "unspecified",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-state", default="unspecified", help="User-supplied label; no cache flushing is done")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="Command and arguments, after --")
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a command is required after --")
    if sys.platform not in {"darwin", "linux"}:
        parser.error("memory units are supported only on macOS and Linux")
    try:
        result = measure(command)
    except OSError as error:
        parser.exit(2, f"cannot start command: {error}\n")
    result["cache_state"] = args.cache_state
    print(json.dumps(result, sort_keys=True))
    return 0 if result["exit_code"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
