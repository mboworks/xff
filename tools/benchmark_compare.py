#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Correctness-checked benchmark comparisons and informational main baselines."""

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

import benchmark_fixture
import benchmark_matrix

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


def fixture_entries(files, depth):
    entries = []
    for index in range(files - 1):
        parent = 'd/' * (index % (depth + 1))
        name = (".hidden_" if index % 11 == 0 else "item_") + f"{index:05}." + ("txt" if index % 2 else "log")
        content = (b"needle beta\n" if index % 3 == 0 else b"plain alpha\n") * 96
        entries.append(benchmark_fixture.Entry(parent + name, 'file', content))
    entries.extend([benchmark_fixture.Entry('.gitignore', 'file', b'*.txt\n'),
                    benchmark_fixture.Entry('file-link', 'link', entries[0].name if entries else '.gitignore'),
                    benchmark_fixture.Entry('dir-link', 'link', '.')])
    return entries


def fixture(root, files, depth):
    return benchmark_fixture.materialize(root, fixture_entries(files, depth))


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


class MeasurementProgress:
    """Bound log volume while keeping long-running measurements visibly active."""

    def __init__(self):
        self.last = None

    def __call__(self, message, *, force=False):
        now = time.monotonic()
        if force or self.last is None or now - self.last >= 5:
            print(message, file=sys.stderr, flush=True)
            self.last = now


def collect(binary, files=2000, depth=40, repetitions=9, worker=None, require_tools=False,
            fixture_parent=None, require_memory=False, cpus=1, require_cpu_affinity=False, keep=None, fixtures=None, progress=None):
    progress = progress or MeasurementProgress()
    keep = min(7, repetitions) if keep is None else keep
    if not 1 <= keep <= repetitions:
        raise ValueError("retained samples must be between one and repetitions")
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
        "files": files, "file_counts": [files], "cpu_counts": [cpus], "depth": depth, "storage": storage, "repetitions": repetitions, "fixture_version": 2, "retained": keep, "estimator": "mean-fastest",
        "platform": platform.platform(), "machine": platform.machine(), "cpu_count": os.cpu_count(), "requested_cpus": cpus, "cpu_affinity": affinity,
        "order": "rotate starting participant by task and round", "warmup_rounds": 1,
        "cache": "just written, then reused; no flush; discarded correctness-checked warmup round",
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
        datasets = fixtures or {shape: (fixture_entries(files, levels), None)
                                for shape, levels in (("broad", 0), ("deep", depth))}
        for shape, (entries, source_hash) in datasets.items():
            root = base / shape
            progress(f'Preparing {shape}: {files:,} files, {cpus} requested workers')
            rows = benchmark_fixture.materialize(root, entries)
            progress(f'Fixture ready: {shape}, {len(rows):,} files')
            candidates = base / (shape + ".paths")
            candidates.write_bytes(b"".join(os.fsencode("./" + path.relative_to(root).as_posix()) + b"\0" for path, _ in rows))
            for name, expected, commands in scenarios(root, rows, tools, cpus):
                task = {"shape": shape, "name": name, "expected_count": len(expected),
                        "input_files": len(rows), "files": files, "cpus": cpus, "dataset": shape,
                        "fixture_identity": benchmark_fixture.identity(entries), "fixture_source_sha256": source_hash,
                        "expected_sha256": hashlib.sha256(b"\0".join(sorted(expected))).hexdigest(),
                        "participants": {}, "skips": {}}
                report["tasks"].append(task)
                if name.startswith("content-") and any(b"\0" in data for _, data in rows):
                    task["skips"] = {label: "binary fixture: xff/rg binary-skip semantics differ" for label in commands}
                    continue
                for label, pipeline in commands.items():
                    missing = [tool for tool in label.split("+") if tools[tool]["status"] != "available"]
                    if missing:
                        task["skips"][label] = "unavailable: " + ", ".join(missing)
                        continue
                    spec = {"pipeline": pipeline, "environment": environment, "cwd": str(root), "cpu_affinity": affinity,
                            "stdin": str(candidates if name.startswith("fuzzy-list-") else empty)}
                    task["participants"][label] = {"pipeline": pipeline, "stdin": spec["stdin"], "cwd": str(root), "samples": []}
                labels = list(task["participants"])
                completed = 0
                total = len(labels) * (repetitions + 1)
                for repetition in range(repetitions + 1):
                    offset = (len(report['tasks']) - 1 + repetition) % len(labels) if labels else 0
                    order = labels[offset:] + labels[:offset]
                    for label in order:
                        entry = task["participants"][label]
                        phase = 'warm-up' if repetition == 0 else f'sample {repetition}/{repetitions}'
                        progress(f'Run {completed + 1}/{total}: {shape}/{name}; {files:,} files; '
                              f'{cpus} requested workers; {label}; {phase}')
                        sample = invoke({"pipeline": entry["pipeline"], "stdin": entry["stdin"], "cwd": entry["cwd"], "environment": environment, "cpu_affinity": affinity}, worker)
                        validate_output(sample, expected, entry["pipeline"])
                        completed += 1
                        if repetition:
                            entry["samples"].append(sample)
                progress(f'Completed {completed}/{total}: {shape}/{name}')
    for tool in tools.values():
        if tool["status"] == "available" and digest(tool["path"]) != tool["sha256"]:
            raise ValueError("tool binary changed during measurement")
    return report


def collect_scales(binary, file_counts, depth=40, repetitions=9, require_tools=False,
                   fixture_parent=None, require_memory=False, cpu_counts=(1, 4), require_cpu_affinity=False, keep=None, fixtures=None):
    """Retain independent scales without mixing sample populations or tool identities."""
    if not file_counts or len(set(file_counts)) != len(file_counts) or min(file_counts) < 1 or depth < 1:
        raise ValueError("file counts must be unique and positive; depth must be positive")
    if not cpu_counts or len(set(cpu_counts)) != len(cpu_counts) or min(cpu_counts) < 1:
        raise ValueError("CPU counts must be unique and positive")
    combined = None
    invocation = {"files": list(file_counts), "cpus": list(cpu_counts), "depth": depth,
                  "fixtures": [{"dataset": shape, "files": count, "source_sha256": value[1],
                                "tree_sha256": benchmark_fixture.identity(value[0])}
                               for (shape, count), value in sorted((fixtures or {}).items())]}
    progress = MeasurementProgress()
    total_scales = len(cpu_counts) * len(file_counts)
    completed_scales = 0
    for cpus in cpu_counts:
        for files in file_counts:
            progress(f'Scale {completed_scales + 1}/{total_scales}: {files:,} files, {cpus} requested workers', force=True)
            report = collect(binary, files, min(files, depth), repetitions, require_tools=require_tools,
                             fixture_parent=fixture_parent, require_memory=require_memory, cpus=cpus,
                             require_cpu_affinity=require_cpu_affinity, keep=keep, progress=progress,
                             fixtures={shape: value for (shape, count), value in fixtures.items() if count == files}
                             if fixtures is not None else None)
            completed_scales += 1
            progress(f'Completed scale {completed_scales}/{total_scales}', force=True)
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
                combined["contract"]["invocation"] = invocation
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
            selected = benchmark_matrix.selected_samples(report, entry)
            def average_cell(metric, scale):
                values = [sample[metric] for sample in selected if sample[metric] is not None]
                mean = statistics.fmean if report["contract"].get("estimator") == "mean-fastest" else statistics.median
                return f"{mean(values) * scale:.2f}" if values else "n/a"
            elapsed = benchmark_matrix.elapsed_mean(report, entry)
            throughput = f"{task['input_files'] / elapsed:.0f}" if task.get("input_files") and elapsed > 0 else "n/a"
            cells = [task["shape"] + "/" + task["name"], label, str(task["expected_count"]),
                     average_cell("elapsed_seconds", 1000), throughput, average_cell("first_stdout_seconds", 1000),
                     average_cell("peak_child_rss_bytes", 1 / 1048576),
                     average_cell("sum_child_peak_rss_bytes", 1 / 1048576) if len(entry["pipeline"]) > 1 else "n/a",
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
            'Overview: selected averages in ms and MiB (legacy reports use medians); raw-sample details: seconds and bytes. Pipeline memory is a sum of process high-water marks, '
            'not simultaneous peak memory. No cross-scope ratios.</p><details><summary>Tools and contract</summary><pre>' +
            html.escape(json.dumps({"tools": report["tools"], "contract": report["contract"]}, indent=2)) +
            '</pre></details>' + benchmark_matrix.render_html(report) + '<details><summary>Per-task metrics</summary><table><tr><th>Task</th><th>Tool</th><th>Matches</th><th>Elapsed ms</th>'
            '<th>Input files/s</th><th>First output ms</th><th>Peak RSS MiB</th><th>Pipeline sum RSS MiB</th><th>N</th></tr>' +
            ''.join(overview) + '</table></details><details><summary>All metrics and variability</summary><table><tr>'
            '<th>Task</th><th>Tool</th><th>Metric</th><th>Median</th><th>Min</th>'
            '<th>Max</th><th>Stddev</th><th>N</th></tr>' + ''.join(rows) + '</table></details>')


def render_document(report):
    title = html.escape(benchmark_matrix.platform_title(report))
    return ('<!doctype html><html lang="en"><meta charset="utf-8"><title>' + title + '</title>'
            '<style>body{font:16px system-ui;margin:2rem}table{border-collapse:collapse}'
            'td,th{border:1px solid #aaa;padding:.4rem;text-align:right}'
            'td:first-child,th:first-child{text-align:left}</style><h1>' + title + '</h1>' +
            render(report) + '</html>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--render-only', action='store_true',
                        help='Render an existing --report JSON without running measurements; requires --html or --summary')
    parser.add_argument('--worker', type=Path)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--report', type=Path, help='Existing history report to extend')
    parser.add_argument('--files', type=int, action='append', help='Repeat for each scale; default: 10, 100, 1000, 10000, 100000')
    parser.add_argument('--depth', type=int, default=40)
    parser.add_argument('--cpus', type=int, action='append', help='Repeat for CPU allocations; default: 1, 4')
    parser.add_argument('--require-cpu-affinity', action='store_true')
    parser.add_argument('--repetitions', type=int, default=9)
    parser.add_argument('--keep', type=int, default=7)
    parser.add_argument('--fixture', action='append', default=[], help='SHAPE:COUNT=MANIFEST_OR_TAR')
    parser.add_argument('--baseline-root', type=Path, help='Retained main benchmark directory')
    parser.add_argument('--head', default='', help='Measured commit')
    parser.add_argument('--build-identity', default='', help='Comparable compiler/build configuration')
    parser.add_argument('--runner-class', default='local')
    parser.add_argument('--summary', type=Path, help='Write Markdown matrix')
    parser.add_argument('--html', type=Path, help='Write standalone HTML report')
    parser.add_argument('--alarm-percent', type=float, default=15, help='Advisory normalized slowdown threshold; never blocks')
    parser.add_argument('--require-tools', action='store_true')
    parser.add_argument('--fixture-parent', type=Path, help='Existing directory for generated fixtures')
    parser.add_argument('--require-memory', action='store_true', help='Require verified Linux tmpfs fixture storage')
    args = parser.parse_args()
    if args.render_only:
        if not args.report or not (args.html or args.summary):
            parser.error('--render-only requires --report and --html or --summary')
        report = json.loads(args.report.read_text())['tool_comparisons']
        if args.html:
            args.html.write_text(render_document(report))
        if args.summary:
            args.summary.write_text(benchmark_matrix.render_markdown(report))
        return

    if args.worker:
        print(json.dumps(measure(json.loads(args.worker.read_text()))))
        return
    file_counts = args.files or [10, 100, 1000, 10000, 100000]
    cpu_counts = args.cpus or [1, 4]
    if (not args.binary or not args.report or min(*file_counts, args.depth, args.repetitions) < 1
            or len(set(file_counts)) != len(file_counts) or min(cpu_counts) < 1
            or len(set(cpu_counts)) != len(cpu_counts)):
        parser.error('binary/report required; unique positive sizes and positive depth')
    fixtures = benchmark_fixture.mapping(args.fixture) if args.fixture else None
    if fixtures is not None:
        file_counts = sorted({count for _, count in fixtures})
        if args.files and sorted(args.files) != file_counts:
            parser.error('--files must match the custom fixture counts')
    record = json.loads(args.report.read_text()) if args.report.exists() else {"head": args.head}
    record['tool_comparisons'] = collect_scales(
        args.binary, file_counts, args.depth, args.repetitions, require_tools=args.require_tools,
        fixture_parent=args.fixture_parent, require_memory=args.require_memory,
        cpu_counts=cpu_counts, require_cpu_affinity=args.require_cpu_affinity, keep=args.keep, fixtures=fixtures)
    report = record["tool_comparisons"]
    report["contract"].update(build_identity=args.build_identity, runner_class=args.runner_class)
    if args.baseline_root:
        benchmark_matrix.attach_baseline(report, args.baseline_root)
    alarms = benchmark_matrix.regressions(report, args.alarm_percent, minimum_files=1)
    report['alarm'] = {'threshold_percent': args.alarm_percent, 'blocking': False, 'cells': alarms}
    for alarm in alarms:
        print(f"::warning title=Benchmark performance::{alarm['dataset']}/{alarm['task']} "
              f"({alarm['files']} files, {alarm['cpus']} CPUs): xff/{alarm['reference']} "
              f"ratio worsened {alarm['normalized_change_percent']:.1f}% versus main; informational only")
    report['relative_results'] = benchmark_matrix.relative_results(report)
    if args.summary:
        args.summary.write_text(benchmark_matrix.render_markdown(report))
    if args.html:
        args.html.write_text(render_document(report))
    args.report.write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
