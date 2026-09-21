#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Correctness-checked comparisons, collected only by the informational benchmark job."""

import argparse
import base64
from collections import Counter
import hashlib
import html
import json
import math
import os
from pathlib import Path
import platform
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import time

METRICS = ("elapsed_seconds", "first_stdout_seconds", "user_cpu_seconds", "system_cpu_seconds",
           "peak_child_rss_bytes", "sum_child_peak_rss_bytes", "stdout_bytes")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def measure(spec):
    """Fresh worker; account for each direct pipeline process, without a shell."""
    if sys.platform not in {"darwin", "linux"}:
        raise ValueError("resource accounting supports macOS and Linux only")
    if spec.get("cpu_affinity") is not None:
        os.sched_setaffinity(0, spec["cpu_affinity"])
    children = []
    chunks = []
    first = None
    with tempfile.TemporaryFile() as errors, open(spec["stdin"], "rb") as source:
        started = time.monotonic()
        try:
            for command in spec["pipeline"]:
                child = subprocess.Popen(command, stdin=source if not children else children[-1].stdout,
                                         stdout=subprocess.PIPE, stderr=errors, env=spec["environment"], cwd=spec.get("cwd"))
                if children:
                    children[-1].stdout.close()
                children.append(child)
            stream = children[-1].stdout
            byte = stream.read(1)
            if byte:
                first = time.monotonic() - started
                chunks.append(byte)
            while chunk := stream.read(65536):
                chunks.append(chunk)
            usages, codes = [], []
            for child in children:
                _, status, usage = os.wait4(child.pid, 0)
                child.returncode = os.waitstatus_to_exitcode(status)
                codes.append(child.returncode)
                usages.append(usage)
            elapsed = time.monotonic() - started
            stream.close()
        finally:
            for child in children:
                if child.returncode is None:
                    child.kill()
                    child.wait()
                if child.stdout and not child.stdout.closed:
                    child.stdout.close()
        errors.seek(0)
        stderr = errors.read().decode("utf-8", errors="replace")
    output = b"".join(chunks)
    rss = [usage.ru_maxrss * (1 if sys.platform == "darwin" else 1024) for usage in usages]
    return {"elapsed_seconds": elapsed, "first_stdout_seconds": first,
            "user_cpu_seconds": sum(usage.ru_utime for usage in usages),
            "system_cpu_seconds": sum(usage.ru_stime for usage in usages),
            "peak_child_rss_bytes": rss[0] if len(rss) == 1 else None,
            "sum_child_peak_rss_bytes": sum(rss), "stdout_bytes": len(output),
            "exit_codes": codes, "stderr": stderr, "output": base64.b64encode(output).decode()}


def invoke(spec, worker):
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json") as request:
        json.dump(spec, request)
        request.flush()
        child = subprocess.Popen([*worker, "--worker", request.name], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, start_new_session=True)
        try:
            stdout, stderr = child.communicate(timeout=60)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.communicate()
            raise ValueError("comparison command exceeded 60 seconds") from None
        if child.returncode:
            raise ValueError(f"comparison worker failed: {stderr.decode(errors='replace')}")
        return json.loads(stdout)


def validate_output(sample, expected, pipeline):
    output = base64.b64decode(sample.pop("output"), validate=True)
    if any(code not in (0, 1) for code in sample["exit_codes"]) or sample["stderr"]:
        raise ValueError(f"failed comparison command: {pipeline}: {sample}")
    # Only search tools may use 1 for no match; producers and xff must still succeed.
    for command, code in zip(pipeline, sample["exit_codes"], strict=True):
        if code and (expected or Path(command[0]).name not in {"rg", "fzf"}):
            raise ValueError(f"unexpected exit status: {command}: {code}")
    if output and not output.endswith(b"\0"):
        raise ValueError("comparison output is not NUL terminated")
    records = output[:-1].split(b"\0") if output else []
    if Counter(records) != Counter(expected):
        raise ValueError(f"comparison output differs from fixture oracle: {pipeline}")


def tool_info(name, executable):
    if executable is None:
        return {"status": "unavailable", "reason": f"{name} executable not found"}
    path = str(Path(executable).resolve(strict=True))
    version = subprocess.run([path, "--version"], capture_output=True, text=True, check=False, timeout=10)
    if version.returncode == 0:
        identity = version.stdout.strip()
    elif name == "find" and sys.platform == "darwin":
        identity = "BSD find; " + platform.platform()
    else:
        raise ValueError(f"cannot identify {name}: {version.stderr}")
    return {"status": "available", "path": path, "version": identity, "sha256": digest(path)}


def fixture(root, files, depth):
    """Manifest is the oracle: no tool's traversal or matching defines expected output."""
    rows = []
    for index in range(files):
        parent = root.joinpath(*(["d"] * (index % (depth + 1))))
        parent.mkdir(parents=True, exist_ok=True)
        name = (".hidden_" if index % 11 == 0 else "item_") + f"{index:05}." + ("txt" if index % 2 else "log")
        path = parent / name
        content = (b"needle beta\n" if index % 3 == 0 else b"plain alpha\n") * 96
        path.write_bytes(content)
        rows.append((path, content))
    # Include ignored paths and file/directory symlinks, but never follow symlinks.
    (root / ".gitignore").write_text("*.txt\n")
    rows.append((root / ".gitignore", b"*.txt\n"))
    (root / "file-link").symlink_to(rows[0][0])
    (root / "dir-link").symlink_to(root, target_is_directory=True)
    return rows


def scenarios(root, rows, tools, cpus=1):
    def relative(path):
        return os.fsencode("./" + path.relative_to(root).as_posix())
    paths = [relative(path) for path, _ in rows]
    xff = [tools["xff"]["path"], "--no-config", "--no-pager", "--color=never", f"--jobs={cpus}",
           "--sort=none", "--exact", "--case=sensitive", "--gitignore=off", "--hidden", ".", "-type", "f"]
    find = [tools["find"].get("path", "find"), "-P", ".", "-type", "f"]
    rg = [tools["rg"].get("path", "rg"), "--no-config", "--hidden", "--no-ignore", f"--threads={cpus}", "--color=never"]
    yield "files", paths, {"xff": [xff + ["-print0"]], "find": [find + ["-print0"]],
                           "rg": [rg + ["--files", "-0", "."]]}
    yield "files-safe", paths, {"xff": [[xff[0], "--safe", *xff[1:], "-print0"]]}
    yield "name-txt", [relative(p) for p, _ in rows if p.name.endswith('.txt')], {
        "xff": [xff + ["-name", "*.txt", "-print0"]], "find": [find + ["-name", "*.txt", "-print0"]],
        "rg": [rg + ["--files", "-g", "*.txt", "-0", "."]]}
    for query in ("needle", "absent_marker"):
        yield "content-" + query, [relative(p) for p, data in rows if query.encode() in data], {
            "xff": [xff + ["--regextype=EXACT", "-rxc", query, "-print0"]],
            "rg": [rg + ["--case-sensitive", "--text", "--files-with-matches", "--null", "--fixed-strings", query, "."]]}
    # Plain ASCII query: ordered subsequence acceptance only, not ranking or score equivalence.
    for query in ("itm", "hdn", "zzzz"):
        expected = []
        for path in paths:
            remaining = iter(path.decode())
            if all(any(char == candidate for candidate in remaining) for char in query):
                expected.append(path)
        fzf = [tools["fzf"].get("path", "fzf"), "--read0", "--print0", "--no-sort", "+i", "--filter=" + query]
        yield "fuzzy-discovery-" + query, expected, {
            "xff": [xff + ["-fuzzypath:fzf", query, "-print0"]], "find+fzf": [find + ["-print0"], fzf]}
        yield "fuzzy-list-" + query, expected, {"fzf": [fzf]}


def fixture_storage(parent, require_memory=False):
    parent = Path(parent or tempfile.gettempdir()).resolve()
    if not parent.is_dir():
        raise ValueError("fixture parent must be an existing directory")
    kind = "unverified host filesystem"
    if sys.platform == "linux":
        result = subprocess.run(["findmnt", "--noheadings", "--output", "FSTYPE", "--target", str(parent)],
                                capture_output=True, text=True, check=False)
        if result.returncode == 0:
            kind = result.stdout.strip()
    if require_memory and kind != "tmpfs":
        raise ValueError("memory-backed fixtures require a verified Linux tmpfs mount")
    return {"parent": str(parent), "filesystem": kind,
            "memory_required": require_memory,
            "scope": "fixture storage only; filesystem syscalls and production VFS remain measured"}


def collect(binary, files=2000, depth=40, repetitions=5, worker=None, require_tools=False,
            fixture_parent=None, require_memory=False, cpus=1, require_cpu_affinity=False):
    if cpus < 1:
        raise ValueError("CPU count must be positive")
    affinity = None
    if hasattr(os, "sched_getaffinity"):
        available = sorted(os.sched_getaffinity(0))
        if len(available) < cpus:
            raise ValueError(f"requested {cpus} CPUs, only {len(available)} available")
        affinity = available[:cpus]
    elif require_cpu_affinity:
        raise ValueError("CPU affinity enforcement requires Linux")
    storage = fixture_storage(fixture_parent, require_memory)
    tools = {name: tool_info(name, str(binary) if name == "xff" else shutil.which(name))
             for name in ("xff", "find", "rg", "fzf")}
    if require_tools and any(tool["status"] != "available" for tool in tools.values()):
        raise ValueError("comparison tools are required: " + json.dumps(tools))
    worker = worker or [sys.executable, str(Path(__file__).resolve())]
    report = {"schema": 1, "tools": tools, "contract": {
        "files": files, "depth": depth, "storage": storage, "repetitions": repetitions, "fixture_version": 1,
        "platform": platform.platform(), "cpu_count": os.cpu_count(), "requested_cpus": cpus, "cpu_affinity": affinity,
        "order": "rotate participants each repetition", "cache": "just written, then reused; no flush; correctness run first",
        "output": "NUL paths, unordered multiset; drained pipe; validation after timing",
        "memory": "single-process peak RSS; pipeline sum of individual peaks is an upper bound, not simultaneous peak",
        "scope": "batch discovery versus separate precomputed-list filtering; no interactive/ranking equivalence",
        "environment": "inherited platform environment; isolated HOME; LC_ALL=C; FZF defaults removed; rg config disabled",
        "children": "direct leaf commands only; no shell or unaccounted subprocess trees"}, "tasks": []}
    with tempfile.TemporaryDirectory(prefix="xff-tool-comparison-", dir=storage["parent"]) as temporary:
        base = Path(temporary)
        environment = {k: v for k, v in os.environ.items() if not k.startswith("FZF_")}
        environment.update(HOME=str(base), LC_ALL="C", GOMAXPROCS=str(cpus))
        empty = base / "empty"
        empty.write_bytes(b"")
        for shape, levels in (("broad", 0), ("deep", depth)):
            root = base / shape
            rows = fixture(root, files, levels)
            candidates = base / (shape + ".paths")
            candidates.write_bytes(b"".join(os.fsencode("./" + path.relative_to(root).as_posix()) + b"\0" for path, _ in rows))
            for name, expected, commands in scenarios(root, rows, tools, cpus):
                task = {"shape": shape, "name": name, "expected_count": len(expected),
                        "input_files": len(rows),
                        "expected_sha256": hashlib.sha256(b"\0".join(sorted(expected))).hexdigest(),
                        "participants": {}, "skips": {}}
                report["tasks"].append(task)
                for label, pipeline in commands.items():
                    missing = [tool for tool in label.split("+") if tools[tool]["status"] != "available"]
                    if missing:
                        task["skips"][label] = "unavailable: " + ", ".join(missing)
                        continue
                    spec = {"pipeline": pipeline, "environment": environment, "cwd": str(root), "cpu_affinity": affinity,
                            "stdin": str(candidates if name.startswith("fuzzy-list-") else empty)}
                    validate_output(invoke(spec, worker), expected, pipeline)
                    task["participants"][label] = {"pipeline": pipeline, "stdin": spec["stdin"], "cwd": str(root), "samples": []}
                labels = list(task["participants"])
                for repetition in range(repetitions):
                    order = labels[repetition % len(labels):] + labels[:repetition % len(labels)] if labels else []
                    for label in order:
                        entry = task["participants"][label]
                        sample = invoke({"pipeline": entry["pipeline"], "stdin": entry["stdin"], "cwd": entry["cwd"], "environment": environment, "cpu_affinity": affinity}, worker)
                        validate_output(sample, expected, entry["pipeline"])
                        entry["samples"].append(sample)
    for tool in tools.values():
        if tool["status"] == "available" and digest(tool["path"]) != tool["sha256"]:
            raise ValueError("tool binary changed during measurement")
    return report


def collect_scales(binary, file_counts, depth=40, repetitions=5, require_tools=False,
                   fixture_parent=None, require_memory=False, cpu_counts=(1, 4), require_cpu_affinity=False):
    """Retain independent scales without mixing sample populations or tool identities."""
    if not file_counts or len(set(file_counts)) != len(file_counts) or min(file_counts) < 1 or depth < 1:
        raise ValueError("file counts must be unique and positive; depth must be positive")
    if not cpu_counts or len(set(cpu_counts)) != len(cpu_counts) or min(cpu_counts) < 1:
        raise ValueError("CPU counts must be unique and positive")
    combined = None
    for cpus in cpu_counts:
        for files in file_counts:
            report = collect(binary, files, min(files, depth), repetitions, require_tools=require_tools,
                             fixture_parent=fixture_parent, require_memory=require_memory, cpus=cpus,
                             require_cpu_affinity=require_cpu_affinity)
            for task in report["tasks"]:
                task["shape"] = f"{cpus}cpu/{files}/{task['shape']}"
            if combined is None:
                combined = report
                combined["contract"].pop("files")
                combined["contract"].pop("requested_cpus", None)
                affinity = combined["contract"].pop("cpu_affinity", None)
                combined["contract"]["cpu_counts"] = list(cpu_counts)
                combined["contract"]["affinity_by_cpu_count"] = {str(cpus): affinity}
                combined["contract"]["file_counts"] = list(file_counts)
                combined["contract"]["depth"] = depth
                combined["contract"]["depth_rule"] = "deep levels = min(files, depth); broad levels = 0"
            else:
                if report["tools"] != combined["tools"]:
                    raise ValueError("tool identity changed between fixture scales")
                combined["contract"]["affinity_by_cpu_count"][str(cpus)] = report["contract"].get("cpu_affinity")
                combined["tasks"].extend(report["tasks"])
    return combined


def render(report):
    if report["schema"] != 1:
        raise ValueError("unsupported comparison schema")
    if report["contract"]["repetitions"] < 1 or not report["tasks"]:
        raise ValueError("empty comparison report")
    rows = []
    overview = []
    keys = set()
    for task in report["tasks"]:
        key = (task["shape"], task["name"])
        if key in keys:
            raise ValueError("duplicate comparison task")
        keys.add(key)
        for label, entry in task["participants"].items():
            if len(entry["samples"]) != report["contract"]["repetitions"]:
                raise ValueError("missing tool comparison samples")
            for sample in entry["samples"]:
                if sample["stderr"] or any(code not in (0, 1) for code in sample["exit_codes"]):
                    raise ValueError("failed comparison sample")
                if any(value is not None and (not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0)
                       for value in (sample[metric] for metric in METRICS)):
                    raise ValueError("invalid comparison metric")
            def median_cell(metric, scale):
                values = [sample[metric] for sample in entry["samples"] if sample[metric] is not None]
                return f"{statistics.median(values) * scale:.2f}" if values else "n/a"
            elapsed = statistics.median(sample["elapsed_seconds"] for sample in entry["samples"])
            throughput = f"{task['input_files'] / elapsed:.0f}" if task.get("input_files") and elapsed > 0 else "n/a"
            cells = [task["shape"] + "/" + task["name"], label, str(task["expected_count"]),
                     median_cell("elapsed_seconds", 1000), throughput, median_cell("first_stdout_seconds", 1000),
                     median_cell("peak_child_rss_bytes", 1 / 1048576),
                     median_cell("sum_child_peak_rss_bytes", 1 / 1048576) if len(entry["pipeline"]) > 1 else "n/a",
                     str(len(entry["samples"]))]
            overview.append('<tr>' + ''.join('<td>' + html.escape(cell) + '</td>' for cell in cells) + '</tr>')
            for metric in METRICS:
                values = [sample[metric] for sample in entry["samples"] if sample[metric] is not None]
                if values:
                    cells = [task["shape"] + "/" + task["name"], label, metric,
                             f"{statistics.median(values):.6g}", f"{min(values):.6g}", f"{max(values):.6g}",
                             f"{statistics.stdev(values) if len(values) > 1 else 0:.6g}", str(len(values))]
                    rows.append('<tr>' + ''.join('<td>' + html.escape(cell) + '</td>' for cell in cells) + '</tr>')
        for label, reason in task["skips"].items():
            skipped = ('<tr><td>' + html.escape(task["shape"] + '/' + task["name"]) + '</td><td>' +
                       html.escape(label) + '</td><td colspan="6">Skipped: ' + html.escape(reason) + '</td></tr>')
            rows.append(skipped)
            overview.append(skipped.replace('colspan="6"', 'colspan="7"'))
    return ('<h2>Tool comparisons</h2><p>Correctness-checked batch tasks; no ranking or interactive comparison. '
            'Overview: medians in ms and MiB; details: seconds and bytes. Pipeline memory is a sum of process high-water marks, '
            'not simultaneous peak memory. No cross-scope ratios.</p><details><summary>Tools and contract</summary><pre>' +
            html.escape(json.dumps({"tools": report["tools"], "contract": report["contract"]}, indent=2)) +
            '</pre></details><table><tr><th>Task</th><th>Tool</th><th>Matches</th><th>Elapsed ms</th>'
            '<th>Input files/s</th><th>First output ms</th><th>Peak RSS MiB</th><th>Pipeline sum RSS MiB</th><th>N</th></tr>' +
            ''.join(overview) + '</table><details><summary>All metrics and variability</summary><table><tr>'
            '<th>Task</th><th>Tool</th><th>Metric</th><th>Median</th><th>Min</th>'
            '<th>Max</th><th>Stddev</th><th>N</th></tr>' + ''.join(rows) + '</table></details>')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', type=Path)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--report', type=Path, help='Existing history report to extend')
    parser.add_argument('--files', type=int, action='append', help='Repeat for each scale; default: 10, 100, 1000, 10000')
    parser.add_argument('--depth', type=int, default=40)
    parser.add_argument('--cpus', type=int, action='append', help='Repeat for CPU allocations; default: 1, 4')
    parser.add_argument('--require-cpu-affinity', action='store_true')
    parser.add_argument('--repetitions', type=int, default=5)
    parser.add_argument('--require-tools', action='store_true')
    parser.add_argument('--fixture-parent', type=Path, help='Existing directory for generated fixtures')
    parser.add_argument('--require-memory', action='store_true', help='Require verified Linux tmpfs fixture storage')
    args = parser.parse_args()
    if args.worker:
        print(json.dumps(measure(json.loads(args.worker.read_text()))))
        return
    file_counts = args.files or [10, 100, 1000, 10000]
    cpu_counts = args.cpus or [1, 4]
    if (not args.binary or not args.report or min(*file_counts, args.depth, args.repetitions) < 1
            or len(set(file_counts)) != len(file_counts) or min(cpu_counts) < 1
            or len(set(cpu_counts)) != len(cpu_counts)):
        parser.error('binary/report required; unique positive sizes and positive depth')
    record = json.loads(args.report.read_text())
    record['tool_comparisons'] = collect_scales(
        args.binary, file_counts, args.depth, args.repetitions, require_tools=args.require_tools,
        fixture_parent=args.fixture_parent, require_memory=args.require_memory,
        cpu_counts=cpu_counts, require_cpu_affinity=args.require_cpu_affinity)
    args.report.write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
