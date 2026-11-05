#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Run clang-tidy concurrently while reporting deterministic progress."""

import argparse
import concurrent.futures
import dataclasses
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import threading
import time
from typing import IO, Optional, Sequence

if __package__:
    from .clang_tidy_compdb import normalize_owned_includes, owned_header_filter
else:
    from clang_tidy_compdb import normalize_owned_includes, owned_header_filter


@dataclasses.dataclass(frozen=True)
class Task:
    path: str
    checks: Optional[str] = None


@dataclasses.dataclass(frozen=True)
class Result:
    task: Task
    returncode: int
    output: str
    seconds: float


class ProcessRegistry:
    """Tracks active children so interruption terminates the complete pool."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._processes: set[subprocess.Popen[str]] = set()
        self._stopping = False

    def add(self, process: subprocess.Popen[str]) -> bool:
        with self._lock:
            if self._stopping:
                return False
            self._processes.add(process)
            return True

    def remove(self, process: subprocess.Popen[str]) -> None:
        with self._lock:
            self._processes.discard(process)

    def terminate_all(self) -> None:
        with self._lock:
            self._stopping = True
            processes = list(self._processes)
        for process in processes:
            if process.poll() is None:
                process.terminate()
        deadline = time.monotonic() + 2.0
        for process in processes:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                process.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                pass
        for process in processes:
            if process.poll() is None:
                process.kill()


def header_filter_arguments(repository: pathlib.Path) -> tuple[str]:
    return (f"--header-filter={owned_header_filter(repository)}",)


def verify_header_visibility(clang_tidy: str) -> None:
    """Exercise real diagnostic visibility without enabling unrelated system macros."""
    owned = (
        "xff/probe0.h",
        "xff_extras_api/probe1.h",
        "extra_modules/archive/probe2.h",
        "external/xff_archive+/probe3.h",
        "bazel-out/platform/bin/external/xff_extras_api+/_virtual_includes/probe/probe4.h",
    )
    foreign = (
        "mbo/probe5.h",
        "external/mboworks_mbo+/probe6.h",
        "external/abseil-cpp+/probe7.h",
        "xff_near_miss/probe8.h",
    )
    with tempfile.TemporaryDirectory(prefix="xff-clang-tidy-headers-") as temporary:
        root = pathlib.Path(temporary) / "xff"
        for index, name in enumerate((*owned, *foreign)):
            header = root / name
            header.parent.mkdir(parents=True, exist_ok=True)
            content = f"inline int* probe_{index} = 0;\n"
            if index == 6:
                content += "#define FOREIGN_BITS(value) ((value) & 1)\n"
            header.write_text(content, encoding="utf-8")
        source = root / "probe.cc"
        for include_kind in ("-I", "-isystem"):
            for absolute in (False, True):
                include_arguments = []
                for name in (*owned, *foreign):
                    parent = (root / name).parent if absolute else pathlib.Path(name).parent
                    include_arguments.extend((include_kind, str(parent)))
                source_text = "".join(f"#include <{pathlib.Path(name).name}>\n"
                                      for name in (*owned, *foreign))
                if include_kind == "-isystem":
                    source_text += "unsigned foreign_bits(unsigned value) { return FOREIGN_BITS(value); }\n"
                source.write_text(source_text, encoding="utf-8")
                command = [clang_tidy, "--quiet", *header_filter_arguments(root),
                           "--checks=-*,modernize-use-nullptr,bugprone-signed-bitwise",
                           "--warnings-as-errors=*", str(source), "--", "-std=c++23",
                           *normalize_owned_includes(include_arguments, root, root)]
                result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, check=False, timeout=30)
                seen = {name for name in (*owned, *foreign) if f"{name}:1:" in result.stdout}
                if (result.returncode != 1 or seen != set(owned)
                        or "bugprone-signed-bitwise" in result.stdout):
                    raise RuntimeError("clang-tidy owned-header visibility probe failed: "
                                       f"{include_kind} absolute={absolute}; diagnosed {sorted(seen)}\n"
                                       + result.stdout)


def run_task(
    task: Task,
    clang_tidy: str,
    compile_database: str,
    registry: ProcessRegistry,
    extra_args: Sequence[str],
    header_arguments: Sequence[str],
) -> Result:
    command = [clang_tidy, "--quiet", *header_arguments]
    if task.checks:
        command.append(f"--checks={task.checks}")
    command.extend(extra_args)
    command.extend(["-p", compile_database, task.path])
    started = time.monotonic()
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if not registry.add(process):
        process.terminate()
    try:
        output, _ = process.communicate()
    finally:
        registry.remove(process)
    return Result(task, process.returncode, output, time.monotonic() - started)


def progress_line(completed: int, total: int, result: Result) -> str:
    width = len(str(total))
    percent = 100.0 * completed / total
    status = "PASS" if result.returncode == 0 else "FAIL"
    return (
        f"[{completed:{width}d}/{total} {percent:5.1f}%] "
        f"{status} {result.task.path} ({result.seconds:.1f}s)"
    )


def run_all(
    tasks: Sequence[Task],
    clang_tidy: str,
    compile_database: str,
    jobs: int,
    output_path: str,
    stream: IO[str] = sys.stdout,
    extra_args: Sequence[str] = (),
) -> int:
    total = len(tasks)
    if total == 0:
        print("clang-tidy: no translation units selected", file=stream, flush=True)
        return 0

    workers = max(1, min(jobs, total))
    print(
        f"clang-tidy: {total} translation unit(s), {workers} worker(s)",
        file=stream,
        flush=True,
    )
    registry = ProcessRegistry()
    header_arguments = header_filter_arguments(pathlib.Path.cwd())
    failed = 0
    started = time.monotonic()

    def interrupt(_signum: int, _frame: object) -> None:
        raise KeyboardInterrupt

    old_sigterm = signal.signal(signal.SIGTERM, interrupt)
    try:
        with open(output_path, "w", encoding="utf-8") as output_file:
            with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
                futures = {
                    executor.submit(
                        run_task, task, clang_tidy, compile_database, registry, extra_args,
                        header_arguments
                    ): task
                    for task in tasks
                }
                try:
                    for completed, future in enumerate(
                        concurrent.futures.as_completed(futures), start=1
                    ):
                        result = future.result()
                        print(progress_line(completed, total, result), file=stream, flush=True)
                        if result.output:
                            output_file.write(result.output)
                            output_file.flush()
                        if result.returncode != 0:
                            failed += 1
                            if result.output:
                                print(result.output, end="", file=stream, flush=True)
                except KeyboardInterrupt:
                    registry.terminate_all()
                    for future in futures:
                        future.cancel()
                    print("clang-tidy: interrupted; worker pool terminated", file=stream, flush=True)
                    return 130
    finally:
        signal.signal(signal.SIGTERM, old_sigterm)

    elapsed = time.monotonic() - started
    passed = total - failed
    print(
        f"clang-tidy: {passed} passed, {failed} failed in {elapsed:.1f}s",
        file=stream,
        flush=True,
    )
    return 1 if failed else 0


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang-tidy", required=True)
    parser.add_argument("--compile-database", required=True)
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 1) - 1))
    parser.add_argument("--output", required=True)
    parser.add_argument("--extra-arg-before", action="append", default=[])
    parser.add_argument("--source", action="append", default=[])
    parser.add_argument("--test", action="append", default=[])
    parser.add_argument("--test-disabled-checks", required=True)
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    if args.jobs < 1:
        raise SystemExit("--jobs must be at least 1")
    tasks = [Task(path) for path in args.source]
    tasks.extend(Task(path, args.test_disabled_checks) for path in args.test)
    extra_args = [f"--extra-arg-before={argument}" for argument in args.extra_arg_before]
    if tasks:
        verify_header_visibility(args.clang_tidy)
    return run_all(
        tasks,
        args.clang_tidy,
        args.compile_database,
        args.jobs,
        args.output,
        extra_args=extra_args,
    )


if __name__ == "__main__":
    sys.exit(main())
