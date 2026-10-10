#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Measure one command in a fresh process; emit one JSON record instead of its stdout."""

import argparse
import json
import os
import platform
import sys

import process_resources


def measure(command: list[str], *, launcher=None) -> dict[str, object]:
    """Account for this command, not Python or an earlier command's high-water mark."""
    identity = process_resources.launcher_identity(launcher)
    sample = process_resources.measure_pipeline([command], launcher=identity["path"],
                                               retain_output=False, stderr_limit=32768)
    if process_resources.launcher_identity(identity["path"]) != identity:
        raise ValueError("native resource launcher changed during measurement")
    return {
        "command": command,
        "cwd": os.getcwd(),
        "platform": platform.platform(),
        "elapsed_seconds": sample["elapsed_seconds"],
        "first_stdout_seconds": sample["first_stdout_seconds"],
        "stdout_bytes": sample["stdout_bytes"],
        "stderr_bytes": sample["stderr_bytes"],
        "stderr_tail": sample["stderr"],
        "exit_code": sample["exit_codes"][0],
        "user_cpu_seconds": sample["user_cpu_seconds"],
        "system_cpu_seconds": sample["system_cpu_seconds"],
        "peak_child_rss_bytes": sample["peak_child_rss_bytes"],
        "resource_launcher": identity,
        "cache_state": "unspecified",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-state", default="unspecified", help="User-supplied label; no cache flushing is done")
    parser.add_argument("--resource-launcher", help="Explicit prebuilt native accounting binary for direct script use")
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
        result = measure(command, launcher=args.resource_launcher)
    except (OSError, ValueError) as error:
        parser.exit(2, f"cannot start command: {error}\n")
    result["cache_state"] = args.cache_state
    print(json.dumps(result, sort_keys=True))
    return 0 if result["exit_code"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
