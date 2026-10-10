#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Account for direct pipeline commands through a small native launch boundary."""

import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

MEMORY_METHOD = "native pipeline fork/wait4 v1; native pre-exec footprint included; launcher usage excluded"
WALL_METHOD = "monotonic wall time includes native launcher startup, output draining and teardown"


def resolve_launcher(launcher=None):
    """Resolve a declared runfile or an explicit binary; never build during measurement."""
    if launcher is not None:
        path = Path(launcher).resolve(strict=True)
    else:
        path = None
        try:
            from python.runfiles import runfiles
            resolver = runfiles.Create()
            # This canonical main-repository path needs no caller-based mapping.
            # Direct-script subprocesses may import this module through its source
            # symlink, outside the runfiles root used by CurrentRepository().
            location = resolver.Rlocation("_main/tools/process_resources_native", source_repo="") if resolver else None
            if location:
                path = Path(location)
        except ImportError:
            pass
        if path is None or not path.is_file():
            raise ValueError("native resource launcher unavailable; build //tools:process_resources_native "
                             "before measurement and supply its path explicitly")
        path = path.resolve(strict=True)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise ValueError("native resource launcher must be an executable file")
    return path


def launcher_identity(launcher=None):
    path = resolve_launcher(launcher)
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "memory_method": MEMORY_METHOD, "wall_method": WALL_METHOD}


def validate_launcher_identity(identity):
    if not isinstance(identity, dict) or set(identity) != {"path", "sha256", "memory_method", "wall_method"}:
        raise ValueError("invalid native resource launcher identity")
    if any(not isinstance(value, str) or not value for value in identity.values()):
        raise ValueError("invalid native resource launcher identity fields")
    if re.fullmatch("[0-9a-f]{64}", identity["sha256"]) is None:
        raise ValueError("invalid native resource launcher hash")


def validate_accounting_contract(identity, method):
    """Preserve legacy reports; require matching method/hash for native reports."""
    if identity is None and method is None:
        return
    validate_launcher_identity(identity)
    if method != {key: value for key, value in identity.items() if key != "path"}:
        raise ValueError("resource accounting contract does not match launcher identity")


def validate_usage(usage, count):
    if not isinstance(usage, dict):
        raise ValueError("invalid native resource report")
    for key in ("rss_bytes", "exit_codes"):
        values = usage.get(key)
        if not isinstance(values, list) or len(values) != count or any(type(value) is not int for value in values):
            raise ValueError(f"invalid native {key} report")
    if any(value < 0 for value in usage["rss_bytes"]):
        raise ValueError("negative native peak RSS")
    for key in ("user_cpu_seconds", "system_cpu_seconds"):
        value = usage.get(key)
        if type(value) not in {int, float} or not math.isfinite(value) or value < 0:
            raise ValueError(f"invalid native {key} report")


def measure_pipeline(commands, *, source=None, cwd=None, environment=None, launcher=None,
                     retain_output=True, stderr_limit=None):
    """Capture output and direct-command usage; the caller owns timeout/process-group policy.

    The launcher inherits the caller's process group so an outer fresh-worker timeout
    kills the entire pipeline. It handles termination by killing and reaping its own
    commands. Wall time deliberately includes this extra native launch boundary;
    reports must record that method and not normalize against the old launch method.
    """
    if sys.platform not in {"linux", "darwin"}:
        raise ValueError("resource accounting supports macOS and Linux only")
    if (not isinstance(commands, (list, tuple)) or not 1 <= len(commands) <= 16
            or any(not isinstance(command, (list, tuple)) or not command for command in commands)):
        raise ValueError("expected between one and sixteen nonempty commands")
    if any(not isinstance(arg, str) or "\0" in arg for command in commands for arg in command):
        raise ValueError("command arguments must be strings without NUL")
    if any(not command[0] for command in commands):
        raise ValueError("command executable must not be empty")
    if stderr_limit is not None and (type(stderr_limit) is not int or stderr_limit < 0):
        raise ValueError("stderr limit must be a nonnegative integer")
    binary = resolve_launcher(launcher)
    output_read, output_write = os.pipe()
    chunks, first, output_bytes = [], None, 0
    with os.fdopen(output_read, "rb") as output, tempfile.TemporaryFile() as errors:
        argv = [str(binary), str(output_write), "" if cwd is None else os.fspath(cwd)]
        for command in commands:
            argv.extend([str(len(command)), *command])
        child = None
        try:
            started = time.monotonic()
            try:
                child = subprocess.Popen(argv, stdin=source, stdout=subprocess.PIPE, stderr=errors,
                                         env=environment, pass_fds=(output_write,))
            finally:
                os.close(output_write)
            byte = output.read(1)
            if byte:
                first = time.monotonic() - started
                output_bytes = 1
                if retain_output:
                    chunks.append(byte)
            while chunk := output.read(65536):
                output_bytes += len(chunk)
                if retain_output:
                    chunks.append(chunk)
            report = child.stdout.read()
            status = child.wait()
            elapsed = time.monotonic() - started
        finally:
            if child is not None:
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
                child.stdout.close()
        errors.seek(0, os.SEEK_END)
        stderr_bytes = errors.tell()
        errors.seek(0 if stderr_limit is None else max(0, stderr_bytes - stderr_limit))
        stderr = errors.read().decode("utf-8", errors="replace")
    if status:
        raise ValueError(f"native resource launcher failed ({status}): {stderr}")
    usage = json.loads(report)
    validate_usage(usage, len(commands))
    rss = usage["rss_bytes"]
    return {"elapsed_seconds": elapsed, "first_stdout_seconds": first,
            "user_cpu_seconds": usage["user_cpu_seconds"], "system_cpu_seconds": usage["system_cpu_seconds"],
            "peak_child_rss_bytes": rss[0] if len(rss) == 1 else None,
            "sum_child_peak_rss_bytes": sum(rss), "child_peak_rss_bytes": rss,
            "stdout_bytes": output_bytes, "exit_codes": usage["exit_codes"],
            "stderr_bytes": stderr_bytes, "stderr": stderr, "output": b"".join(chunks)}
