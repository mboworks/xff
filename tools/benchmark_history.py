#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Measure paired revisions and publish bounded, reproducible benchmark history."""

import argparse
import html
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import tempfile

SCHEMA = 1
METRICS = ("elapsed_seconds", "first_stdout_seconds", "user_cpu_seconds",
           "system_cpu_seconds", "peak_child_rss_bytes", "stdout_bytes")
SHA = re.compile(r"[0-9a-f]{40}")


def checked_sha(value):
    if not SHA.fullmatch(value):
        raise ValueError("expected a full commit SHA")
    return value


def statistics_for(values):
    values = [value for value in values if value is not None]
    if not values:
        return None
    if any(not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0 for value in values):
        raise ValueError("benchmark values must be finite, nonnegative numbers")
    return {"n": len(values), "median": statistics.median(values), "min": min(values),
            "max": max(values), "stdev": statistics.stdev(values) if len(values) > 1 else 0}


def summarize(record):
    """Compare only the two revisions measured together with one fixture contract."""
    if record["schema"] != SCHEMA:
        raise ValueError("unsupported benchmark schema")
    grouped = {}
    comparable = record["contract"].get("base_build_identity") == record["contract"].get("head_build_identity")
    for side in ("base", "head"):
        samples = record["samples"][side]
        if len(samples) != record["contract"]["repetitions"]:
            raise ValueError("missing repetition")
        expected = None
        binary_hash = samples[0]["binary_sha256"]
        for report in samples:
            if report["binary_sha256"] != binary_hash:
                raise ValueError("binary changed between repetitions")
            fixture = {key: value for key, value in report["fixture"].items() if key != "directory"}
            if fixture != record["contract"]["fixture"]:
                raise ValueError("incompatible fixture contract")
            keys = []
            for row in report["measurements"]:
                if row["exit_code"] != 0:
                    raise ValueError("failed benchmark invocation")
                key = (row["shape"], row["scenario"])
                keys.append(key)
                grouped.setdefault(key, {}).setdefault(side, []).append(row)
            if len(keys) != len(set(keys)) or not keys:
                raise ValueError("missing or duplicate scenarios")
            if expected is not None and set(keys) != expected:
                raise ValueError("inconsistent scenario set")
            expected = set(keys)
    result = []
    for (shape, scenario), sides in sorted(grouped.items()):
        if set(sides) != {"base", "head"}:
            raise ValueError("base/head scenario mismatch")
        metrics = {}
        for metric in METRICS:
            base = statistics_for([row[metric] for row in sides["base"]])
            head = statistics_for([row[metric] for row in sides["head"]])
            ratio = head["median"] / base["median"] if comparable and base and head and base["median"] else None
            metrics[metric] = {"base": base, "head": head, "head_over_base": ratio}
        result.append({"shape": shape, "scenario": scenario, "metrics": metrics})
    return result


def measure_pair(args):
    record = {"schema": SCHEMA, "head": checked_sha(args.head), "base": checked_sha(args.base),
              "contract": {"scenario_version": 1, "fixture_version": 1, "repetitions": args.repetitions,
                           "build": args.build, "runner_class": args.runner_class,
                           "platform": platform.platform(), "machine": platform.machine(),
                           "cpu_count": os.cpu_count(), "python": platform.python_version(),
                           "tools": args.tools, "base_build_identity": args.base_build_identity,
                           "head_build_identity": args.head_build_identity, "order": "alternate base/head, then head/base"},
              "samples": {"base": [], "head": []}}
    with tempfile.TemporaryDirectory(prefix="xff-benchmark-pair-") as temporary:
        for repetition in range(args.repetitions):
            order = ("base", "head") if repetition % 2 == 0 else ("head", "base")
            for side in order:
                output = Path(temporary) / f"{side}-{repetition}.json"
                command = [str(args.driver), str(getattr(args, f"{side}_binary")),
                           f"--output={output}", f"--files={args.files}", f"--depth={args.depth}",
                           "--repetitions=1", f"--jobs={args.jobs}", f"--build-label={record[side]};{args.build}"]
                counter = getattr(args, f"{side}_read_counter", None)
                if counter is not None:
                    command.append(f"--read-counter={counter}")
                subprocess.run(command, check=True)
                report = json.loads(output.read_text())
                record["samples"][side].append(report)
                if "fixture" not in record["contract"]:
                    record["contract"]["fixture"] = {key: value for key, value in report["fixture"].items()
                                                      if key != "directory"}
                args.output.write_text(json.dumps(record, indent=2) + "\n")
    fixture = json.dumps(record["contract"]["fixture"], sort_keys=True)
    record["contract"]["fixture_identity"] = hashlib.sha256(("fixture-v1:" + fixture).encode()).hexdigest()
    record["summary"] = summarize(record)
    args.output.write_text(json.dumps(record, indent=2) + "\n")


def retain(root, record, source, keep):
    """Attach trusted run provenance; keep latest attempts with bounded raw storage."""
    record = dict(record)
    record["summary"] = summarize(record)
    checked_sha(record["base"])
    if checked_sha(record["head"]) != source["head_sha"]:
        raise ValueError("report commit does not match source workflow")
    run, attempt = int(source["id"]), int(source["run_attempt"])
    if run < 1 or attempt < 1 or keep < 1:
        raise ValueError("invalid run identity or retention")
    record["source"] = {key: source[key] for key in (
        "id", "run_attempt", "created_at", "head_sha", "head_branch", "event", "pull_requests")}
    if "reference_time" in source:
        record["source"]["reference_time"] = source["reference_time"]
    folder = root / "runs" / str(run) / str(attempt)
    folder.mkdir(parents=True, exist_ok=True)
    destination = folder / "report.json"
    if destination.exists():
        if json.loads(destination.read_text()) != record:
            raise ValueError("an existing run attempt cannot change")
    else:
        destination.write_text(json.dumps(record, indent=2) + "\n")
    paths = sorted(root.glob("runs/*/*/report.json"), key=lambda path: (
        json.loads(path.read_text())["source"]["created_at"], int(path.parent.parent.name), int(path.parent.name)), reverse=True)
    for path in paths[keep:]:
        path.unlink()
        (path.parent / "index.html").unlink(missing_ok=True)
        path.parent.rmdir()
        if not list(path.parent.parent.iterdir()):
            path.parent.parent.rmdir()


def page(title, body):
    return (f'<!doctype html><html lang="en"><meta charset="utf-8"><title>{html.escape(title)}</title>'
            '<style>body{font:16px system-ui;margin:2rem}table{border-collapse:collapse}'
            'td,th{border:1px solid #aaa;padding:.4rem;text-align:right}td:first-child,th:first-child{text-align:left}'
            '</style><h1>' + html.escape(title) + '</h1>' + body + '</html>\n')


def render_report(record):
    rows = []
    for result in summarize(record):
        for metric, values in result["metrics"].items():
            cells = [f'{result["shape"]}/{result["scenario"]}', metric]
            for side in ("base", "head"):
                stats = values[side]
                cells.append("no output" if stats is None else
                             f'{stats["median"]:.6g} [{stats["min"]:.6g}, {stats["max"]:.6g}], sd {stats["stdev"]:.3g}, n={stats["n"]}')
            ratio = values["head_over_base"]
            cells.append("n/a" if ratio is None else f"{ratio:.3f}")
            rows.append("<tr>" + "".join(f"<td>{html.escape(cell)}</td>" for cell in cells) + "</tr>")
    body = ('<p><a href="../../../">Benchmark history</a> | <a href="report.json">Raw observations and provenance</a></p>'
            '<p>Paired on the same runner; medians [min, max], sample standard deviation, and sample count. '
            'Ratios are head/base, not significance tests. Time is seconds; memory/output are bytes. '
            'Peak memory is per measured invocation, not cumulative across repetitions. '
            'No regression threshold is enforced. Ratios are omitted when build identities differ.</p>'
            f'<p>Base: <code>{html.escape(record["base"])}</code>; head: <code>{html.escape(record["head"])}</code>.</p>'
            '<details><summary>Measurement contract</summary><pre>' + html.escape(json.dumps(record["contract"], indent=2)) + '</pre></details>'
            '<table><tr><th>Scenario</th><th>Metric</th><th>Base</th><th>Head</th><th>Ratio</th></tr>' + ''.join(rows) + '</table>')
    return page("Benchmark comparison", body)


def render_site(root, pulls, repository):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("invalid repository")
    by_sha = {pull["merge_commit_sha"]: pull for pull in pulls if pull.get("merged_at")}
    by_number = {pull["number"]: pull for pull in pulls}
    selected = {}
    for path in root.glob("runs/*/*/report.json"):
        record = json.loads(path.read_text())
        source = record["source"]
        phase = ""
        if source["event"] == "pull_request":
            numbers = source.get("pull_requests", [])
            if len(numbers) != 1:
                continue
            number = numbers[0]["number"]
            pull = by_number.get(number, {})
            if pull.get("state") == "closed" and not pull.get("merged_at"):
                continue
            label, phase = f"PR {number}", "pre-merge"
            reference = pull.get("merged_at") or source["created_at"]
        elif re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", source["head_branch"]):
            label, reference = source["head_branch"], source.get("reference_time", source["created_at"])
        elif source["head_sha"] in by_sha:
            pull = by_sha[source["head_sha"]]
            label, phase, reference = f'PR {pull["number"]}', "post-merge", pull["merged_at"]
        else:
            label, reference = "main", source["created_at"]
        key = (label, phase)
        rank = (source["created_at"], source["id"], source["run_attempt"])
        if key not in selected or rank > selected[key][0]:
            selected[key] = (rank, reference, path, record)
    rows = []
    for (label, phase), (_, reference, path, record) in sorted(selected.items(), key=lambda item: (
            item[1][1], item[0][0], item[0][1] == "post-merge"), reverse=True):
        source = record["source"]
        relative = path.parent.relative_to(root).as_posix()
        (path.parent / "index.html").write_text(render_report(record))
        commit = record["head"]
        base = record["base"]
        rows.append(f'<tr><td><a href="{relative}/">{html.escape(label)} {phase}</a></td>'
                    f'<td>{html.escape(reference)}</td><td><a href="https://github.com/{repository}/commit/{commit}">{commit[:7]}</a></td>'
                    f'<td><a href="https://github.com/{repository}/commit/{base}">{base[:7]}</a></td>'
                    f'<td><a href="https://github.com/{repository}/actions/runs/{int(source["id"])}">{int(source["id"])}, attempt {int(source["run_attempt"])}</a></td></tr>')
    return page("Benchmark history", '<p>Informational paired comparisons; no cross-host performance ratios. '
                'Each PR phase/release shows its latest retained attempt. Missing or failed runs produce no result. '
                'Raw records are bounded by publication retention.</p><table><tr><th>Source</th><th>Reference time</th>'
                '<th>Head</th><th>Baseline</th><th>Workflow</th></tr>' + ''.join(rows) + '</table>')


def reference_pages(root, pulls, repository_path):
    """Resolve stable PR/tag URLs without inventing measurements or rerunning them."""
    records = []
    for path in root.glob("runs/*/*/report.json"):
        record = json.loads(path.read_text())
        source = record["source"]
        records.append((record, path.parent.relative_to(root).as_posix()))
        (path.parent / "index.html").write_text(render_report(record))
    tags = subprocess.check_output(
        ["git", "-C", str(repository_path), "tag", "--list", "v*"], text=True).splitlines()
    references = {}
    for tag in tags:
        if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", tag):
            continue
        commit = subprocess.check_output(
            ["git", "-C", str(repository_path), "rev-parse", f"{tag}^{{commit}}"], text=True).strip()
        references[f"tag/{tag[1:]}"] = (f"Release {tag}", commit, None)
    for pull in pulls:
        number = int(pull["number"])
        if number < 1:
            raise ValueError("invalid PR number")
        references[f"pr/{number}"] = (f"PR {number}", pull.get("merge_commit_sha") if pull.get("merged_at") else None, number)
    for relative, (label, commit, number) in references.items():
        candidates = []
        for record, report_path in records:
            source = record["source"]
            post = source["event"] == "push" and record["head"] == commit
            pre = number is not None and source["event"] == "pull_request" and any(
                pull["number"] == number for pull in source.get("pull_requests", []))
            if post or pre:
                rank = (post, source["created_at"], source["id"], source["run_attempt"])
                candidates.append((rank, report_path))
        folder = root / relative
        folder.mkdir(parents=True, exist_ok=True)
        if candidates:
            target = "../../" + max(candidates)[1] + "/"
            body = (f'<meta http-equiv="refresh" content="0; url={html.escape(target)}">'
                    f'<p><a href="{html.escape(target)}">Open retained benchmark result</a></p>')
        else:
            body = ('<p>No retained benchmark measurement is available for this reference. '
                    'It may not have been measured yet, its run may have failed, or retention may have expired.</p>'
                    '<p><a href="../../">Benchmark history</a></p>')
        (folder / "index.html").write_text(page(label + " benchmarks", body))
    # A removed reference must not keep redirecting to an expired or unrelated report.
    for path in [*root.glob("tag/*/index.html"), *root.glob("pr/*/index.html")]:
        if path.parent.relative_to(root).as_posix() not in references:
            path.write_text(page("Benchmark reference unavailable", '<p><a href="../../">Benchmark history</a></p>'))


def positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    measure = commands.add_parser("measure")
    for name in ("driver", "base-binary", "head-binary", "output"):
        measure.add_argument("--" + name, type=Path, required=True)
    for side in ("base", "head"):
        measure.add_argument(f"--{side}-read-counter", type=Path)
    for name in ("base", "head", "build", "runner-class", "tools", "base-build-identity", "head-build-identity"):
        measure.add_argument("--" + name, required=True)
    for name, default in (("files", 2000), ("depth", 40), ("repetitions", 5), ("jobs", 1)):
        measure.add_argument("--" + name, type=positive, default=default)
    publish = commands.add_parser("publish")
    for name in ("root", "report", "source", "pulls"):
        publish.add_argument("--" + name, type=Path, required=True)
    publish.add_argument("--repository", required=True)
    publish.add_argument("--keep", type=positive, default=100)
    refresh = commands.add_parser("refresh")
    for name in ("root", "pulls", "checkout"):
        refresh.add_argument("--" + name, type=Path, required=True)
    refresh.add_argument("--repository", required=True)
    args = parser.parse_args()
    if args.action == "measure":
        measure_pair(args)
    else:
        args.root.mkdir(parents=True, exist_ok=True)
        if args.action == "publish":
            source = json.loads(args.source.read_text())
            record = json.loads(args.report.read_text())
            retain(args.root, record, source, args.keep)
        pages = json.loads(args.pulls.read_text())
        pulls = [pull for group in pages for pull in group] if pages and isinstance(pages[0], list) else pages
        (args.root / "index.html").write_text(render_site(args.root, pulls, args.repository))
        if args.action == "refresh":
            reference_pages(args.root, pulls, args.checkout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
