#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Discover and run every first-party fuzz campaign.

    tools/fuzz_targets.py --list
    tools/fuzz_targets.py --campaign-seconds=60

The BUILD files are the single source of truth. Core targets use ordinary labels; targets in a
composable extra are translated to that extra's module label through bazelmod/extras.MODULE.bazel.
This keeps CI campaigns complete when a new cc_fuzz_test is added.

The driver controls time and toolchain setup, not a target's capabilities. Harnesses that evaluate
arbitrary expressions must follow AGENTS.md's fuzz-test isolation rules; a command-line `--safe`
flag is not a substitute for an in-memory filesystem and safety-class filtering inside the harness.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import subprocess
import sys
import signal
import tempfile
import threading
import time

import extras

_REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
_NAME = re.compile(r'\bname\s*=\s*"([^"]+)"')


def rule_blocks(source: str, rule: str) -> list[str]:
    """Returns balanced call bodies for `rule(...)`, ignoring parentheses inside strings."""
    blocks: list[str] = []
    marker = f"{rule}("
    start = 0
    while (found := source.find(marker, start)) >= 0:
        pos = found + len(marker)
        depth = 1
        quote = ""
        escaped = False
        while pos < len(source) and depth:
            char = source[pos]
            if quote:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == quote:
                    quote = ""
            elif char in "\"'":
                quote = char
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            pos += 1
        if depth:
            raise ValueError(f"unterminated {rule} call")
        blocks.append(source[found + len(marker) : pos - 1])
        start = pos
    return blocks


def discover_targets(root: pathlib.Path) -> list[str]:
    """Returns the stable list of generated `*_run` campaign labels below `root`."""
    declared = extras.extras((root / "bazelmod" / "extras.MODULE.bazel").read_text(encoding="utf-8"))
    extra_by_path = {pathlib.PurePosixPath(path): module for module, path in declared.items()}
    targets: list[str] = []
    for build in root.rglob("BUILD.bazel"):
        relative = build.relative_to(root)
        if any(part.startswith("bazel-") or part in {".git", ".cache"} for part in relative.parts):
            continue
        package = relative.parent
        owner: tuple[pathlib.PurePosixPath, str] | None = next(
            ((path, module) for path, module in extra_by_path.items() if package == path or path in package.parents),
            None,
        )
        for block in rule_blocks(build.read_text(encoding="utf-8"), "cc_fuzz_test"):
            match = _NAME.search(block)
            if match is None:
                raise ValueError(f"cc_fuzz_test without a literal name in {relative}")
            name = f"{match.group(1)}_run"
            if owner is None:
                targets.append(f"//{package.as_posix()}:{name}")
            else:
                extra_path, module = owner
                module_package = package.relative_to(extra_path).as_posix()
                prefix = f"@{module}//{module_package}" if module_package != "." else f"@{module}//"
                targets.append(f"{prefix}:{name}")
    return sorted(targets)


def campaign_environment() -> dict[str, str]:
    """Returns the fuzz environment, including the required Darwin runtime compatibility setting."""
    environment = dict(os.environ)
    if sys.platform == "darwin":
        # The hermetic libc++ annotates vector capacity, while libFuzzer's own runtime is not built
        # with matching annotations. Disable only that incompatible runtime check; ASan's ordinary
        # heap, stack, bounds, and lifetime checks remain active for xff.
        options = [part for part in environment.get("ASAN_OPTIONS", "").split(":") if part]
        options = [part for part in options if not part.startswith("detect_container_overflow=")]
        options.append("detect_container_overflow=0")
        environment["ASAN_OPTIONS"] = ":".join(options)
    return environment


def worker_count(requested: int | None, targets: int, rss_limit_mb: int) -> int:
    """Bound workers by CPUs and physical RAM, reserving capacity for the host."""
    cpus = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else (os.cpu_count() or 1)
    workers = min(requested if requested is not None else max(1, cpus - 1), cpus)
    try:
        memory_mb = os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE") // (1024 * 1024)
        workers = min(workers, max(1, (memory_mb - 2048) // (rss_limit_mb + 512)))
    except (ValueError, OSError):
        workers = 1  # Unknown RAM: run conservatively unless detection can be improved.
    return max(1, min(workers, targets))


def stop_process(process: subprocess.Popen) -> None:
    """Stop the launcher and its descendants after a timeout or interruption."""
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()


def execute_campaign(target: str, script: pathlib.Path, directory: pathlib.Path,
                     seconds: int, environment: dict[str, str], stop: threading.Event) -> dict:
    """Run one prepared launcher with isolated artifacts and a persistent log."""
    if stop.is_set():
        return {"target": target, "status": "skipped"}
    print(f"Starting {target}; log: {directory / 'campaign.log'}", flush=True)
    started = time.monotonic()
    with (directory / "campaign.log").open("w", encoding="utf-8") as log:
        with subprocess.Popen(["/bin/bash", str(script)], cwd=directory, env=environment,
                              stdout=log, stderr=subprocess.STDOUT, start_new_session=True) as process:
            try:
                while True:
                    if stop.is_set():
                        stop_process(process)
                        code = 130
                        break
                    try:
                        code = process.wait(timeout=0.2)
                        break
                    except subprocess.TimeoutExpired:
                        if time.monotonic() - started > seconds + 60:
                            raise
            except subprocess.TimeoutExpired:
                stop_process(process)
                code = 124
            except BaseException:
                stop_process(process)
                raise
    if code:
        stop.set()
    text = (directory / "campaign.log").read_text(encoding="utf-8", errors="replace")
    stats = dict(re.findall(r"stat::(number_of_executed_units|average_exec_per_sec):\s*(\d+)", text))
    result = {"target": target, "status": "passed" if code == 0 else "failed", "returncode": code,
              "elapsed_seconds": round(time.monotonic() - started, 3), **stats}
    (directory / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Finished {target}: {result['status']} ({result['elapsed_seconds']}s)", flush=True)
    return result


def run_campaigns(targets: list[str], seconds: int, bazel_args: list[str], *,
                  jobs: int | None = None, rss_limit_mb: int = 2048,
                  output_dir: pathlib.Path | None = None) -> int:
    """Build together, prepare launchers serially, then fuzz with bounded concurrency."""
    if not targets:
        raise ValueError("no fuzz campaigns discovered")
    root = (output_dir or pathlib.Path(tempfile.mkdtemp(prefix="xff-fuzz-"))).resolve()
    root.mkdir(parents=True, exist_ok=True)
    environment = campaign_environment()
    flags = ["-c", "opt", "--config=xff_docs", "--config=fuzz", *bazel_args]
    result = subprocess.run(["bazel", "build", *flags, *targets], check=False, env=environment)
    if result.returncode:
        return result.returncode
    prepared = []
    for index, target in enumerate(targets):
        directory = root / f"{index:03d}-{target.rsplit(':', 1)[-1]}"
        directory.mkdir(parents=True, exist_ok=True)
        script = directory / "launch.sh"
        result = subprocess.run(
            ["bazel", "run", *flags, f"--script_path={script}", target, "--", "--clean",
             f"--timeout_secs={seconds}", f"--fuzzing_output_root={directory / 'findings'}",
             "--", f"-rss_limit_mb={rss_limit_mb}", "-print_final_stats=1"],
            check=False, env=environment)
        if result.returncode:
            return result.returncode
        prepared.append((target, script, directory))
    workers = worker_count(jobs, len(targets), rss_limit_mb)
    print(f"Running {len(targets)} campaigns with {workers} workers, {seconds}s per target; outputs: {root}",
          flush=True)
    stop = threading.Event()
    started = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(execute_campaign, target, script, directory, seconds, environment, stop)
                   for target, script, directory in prepared]
        try:
            results = [future.result() for future in futures]
        except BaseException:
            stop.set()
            raise
    summary = {"workers": workers, "seconds_per_target": seconds,
               "elapsed_seconds": round(time.monotonic() - started, 3), "campaigns": results}
    (root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return next((item["returncode"] for item in results if item.get("returncode")), 0)


def campaign_duration_seconds(targets: list[str], seconds: int, maximum: int | None = None) -> int:
    """Returns deliberate mutation time, rejecting invalid or over-budget campaigns."""
    if seconds <= 0:
        raise ValueError("--campaign-seconds must be positive")
    total = len(targets) * seconds
    if maximum is not None and total > maximum:
        raise ValueError(f"campaign requests {total} seconds, exceeding the {maximum}-second budget")
    return total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--list", action="store_true", help="print every generated campaign label")
    mode.add_argument("--campaign-seconds", type=int, metavar="N", help="run every campaign for N seconds")
    parser.add_argument(
        "--max-total-seconds",
        type=int,
        metavar="N",
        help="fail before building when target count times campaign seconds exceeds N",
    )
    parser.add_argument("--disk-cache", metavar="PATH", help="use a dedicated Bazel disk cache")
    parser.add_argument(
        "--disk-cache-max-size",
        metavar="SIZE",
        help="bound the dedicated Bazel disk cache (for example `600M`)",
    )
    parser.add_argument("--jobs", type=int, help="maximum concurrent campaigns (default: CPUs minus one)")
    parser.add_argument("--rss-limit-mb", type=int, default=2048, help="libFuzzer RSS limit per worker")
    parser.add_argument("--output-dir", type=pathlib.Path, help="retain per-target logs, corpora, and crash artifacts here")
    args = parser.parse_args()
    if (args.jobs is not None and args.jobs <= 0) or args.rss_limit_mb <= 0:
        parser.error("--jobs and --rss-limit-mb must be positive")
    targets = discover_targets(_REPO_ROOT)
    if args.list:
        print("\n".join(targets))
        return 0
    try:
        campaign_duration_seconds(targets, args.campaign_seconds, args.max_total_seconds)
    except ValueError as error:
        parser.error(str(error))
    bazel_args: list[str] = []
    if args.disk_cache:
        bazel_args.append(f"--disk_cache={pathlib.Path(args.disk_cache).expanduser()}")
    if args.disk_cache_max_size:
        if not args.disk_cache:
            parser.error("--disk-cache-max-size requires --disk-cache")
        bazel_args.extend(
            [
                f"--experimental_disk_cache_gc_max_size={args.disk_cache_max_size}",
                "--experimental_disk_cache_gc_idle_delay=5s",
            ]
        )
    return run_campaigns(targets, args.campaign_seconds, bazel_args, jobs=args.jobs,
                         rss_limit_mb=args.rss_limit_mb, output_dir=args.output_dir)


if __name__ == "__main__":
    sys.exit(main())
