#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Measure broad/deep xff workloads, retaining invocation records in a JSON report."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


SCENARIOS = {
    "listing": [],
    "summary": ["--summary=ext"],
    "hash": ["--template={hash}"],
    "hash_twice": ["--template={hash} {hash}"],
    "hash_lines": ["--template={hash} {lines}"],
    "compare": None,
}


def positive(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def make_root(root: Path, files: int, depth: int) -> None:
    root.mkdir(parents=True)
    counts = [files] if depth == 0 else [files // depth + (level < files % depth) for level in range(depth)]
    current = root
    index = 0
    for count in counts:
        if depth:
            current /= "d"
            current.mkdir()
        for _ in range(count):
            (current / f"{index:05}.txt").write_bytes(b"abc\n" * 256)
            index += 1


def fingerprint(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="xff executable to measure")
    parser.add_argument("--output", required=True, type=Path, help="JSON report destination (overwritten)")
    parser.add_argument("--read-counter", type=Path, help="Optional read_benchmark executable; runs each workload separately to count logical VFS bytes")
    parser.add_argument("--directory", type=Path, help="Parent for disposable fixtures, e.g. a network mount")
    parser.add_argument("--files", type=positive, default=10000, help="Files per root")
    parser.add_argument("--depth", type=positive, default=100, help="Nested directories in the deep fixture")
    parser.add_argument("--repetitions", type=positive, default=3)
    parser.add_argument("--jobs", type=positive, default=1)
    parser.add_argument("--build-label", default="unspecified", help="Source revision and build configuration")
    args = parser.parse_args()
    if args.depth > args.files:
        parser.error("--depth must not exceed --files")
    binary = args.binary.resolve(strict=True)
    read_counter = args.read_counter.resolve(strict=True) if args.read_counter else None
    report = {
        "binary": str(binary),
        "read_counter": str(read_counter) if read_counter else None,
        "binary_sha256": fingerprint(binary),
        "read_counter_sha256": fingerprint(read_counter) if read_counter else None,
        "build_label": args.build_label,
        "fixture": {
            "files_per_root": args.files,
            "bytes_per_file": 1024,
            "deep_levels": args.depth,
            "cache_preparation": "just-written files; no cache flushing; reuse not controlled",
            "jobs": args.jobs,
            "repetitions": args.repetitions,
        },
        "measurements": [],
    }
    # Bazel declares an executable dependency; direct script use retains its interpreter.
    if os.environ.get("RUNFILES_DIR") or os.environ.get("RUNFILES_MANIFEST_FILE"):
        from python.runfiles import runfiles
        harness = [runfiles.Create().Rlocation("_main/tools/measure_resources")]
    else:
        harness = [sys.executable, str(Path(__file__).with_name("measure_resources.py"))]
    with tempfile.TemporaryDirectory(prefix="xff-resources-", dir=args.directory) as temporary:
        base = Path(temporary).resolve()
        report["fixture"]["directory"] = str(base)
        for shape, depth in (("broad", 0), ("deep", args.depth)):
            roots = [base / shape / side for side in ("left", "right")]
            for root in roots:
                make_root(root, args.files, depth)
            for repetition in range(args.repetitions):
                for scenario, flags in SCENARIOS.items():
                    command = [str(binary), "--no-config", "--no-pager", f"--jobs={args.jobs}"]
                    if flags is None:
                        command += ["--compare=summary", *map(str, roots), "-type", "f"]
                    else:
                        command += [str(roots[0]), "-type", "f", *flags]
                    result = subprocess.run(
                        [*harness, "--cache-state=just-written;reuse-not-controlled", "--", *command],
                        text=True, capture_output=True, check=False,
                    )
                    if not result.stdout:
                        print(result.stderr, file=sys.stderr, end="")
                        return result.returncode or 1
                    data = json.loads(result.stdout)
                    data.update(shape=shape, scenario=scenario, repetition=repetition)
                    if read_counter and not result.returncode:
                        observed = subprocess.run(
                            [str(read_counter), scenario, str(args.jobs), *map(str, roots if flags is None else roots[:1])],
                            text=True, capture_output=True, check=False,
                        )
                        if not observed.stdout:
                            print(observed.stderr, file=sys.stderr, end="")
                            return observed.returncode or 1
                        data["logical_read_run"] = json.loads(observed.stdout)
                        data["logical_read_run"]["separate_invocation"] = True
                        if observed.returncode:
                            report["measurements"].append(data)
                            args.output.write_text(json.dumps(report, indent=2) + "\n")
                            print(observed.stderr, file=sys.stderr, end="")
                            return observed.returncode
                    report["measurements"].append(data)
                    args.output.write_text(json.dumps(report, indent=2) + "\n")
                    print(shape, scenario, repetition, f"{data['elapsed_seconds']:.3f}s", flush=True)
                    if result.returncode:
                        print(data["stderr_tail"], file=sys.stderr, end="")
                        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
