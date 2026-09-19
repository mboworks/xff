# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Test benchmark compatibility, source association, rendering, and retention."""

import argparse
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import benchmark_history as history
from test_paths import repository_file

HEAD = "a" * 40
BASE = "b" * 40


def sample(value=2):
    row = {metric: value for metric in history.METRICS}
    row.update(shape="broad", scenario="listing", exit_code=0)
    return {"fixture": {"files_per_root": 2, "directory": "/temporary"},
            "binary_sha256": "binary", "measurements": [row]}


def record():
    return {"schema": 1, "head": HEAD, "base": BASE,
            "contract": {"repetitions": 2, "fixture": {"files_per_root": 2}},
            "samples": {"base": [sample(2), sample(4)], "head": [sample(4), sample(8)]}}


def source(run=1, attempt=1, event="pull_request", sha=HEAD):
    return {"id": run, "run_attempt": attempt, "created_at": f"2026-09-19T10:{run:02}:00Z",
            "head_sha": sha, "head_branch": "feature", "event": event, "pull_requests": [{"number": 9}]}


class BenchmarkHistoryTest(unittest.TestCase):
    def test_workflow_keeps_measurement_unprivileged_and_publication_serialized(self):
        measure = repository_file(".github/workflows/benchmarks.yml").read_text()
        publish = repository_file(".github/workflows/benchmark_pages.yml").read_text()
        self.assertIn("permissions:\n  contents: read", measure)
        self.assertIn("persist-credentials: false", measure)
        self.assertIn("github.event.pull_request.base.sha", measure)
        self.assertIn("--config=clang_release", measure)
        self.assertIn("target.files_to_run.executable.path", measure)
        self.assertIn("namespace: default", measure)
        self.assertNotIn("bazel-cache-save", measure)
        self.assertIn("group: coverage-pages\n  queue: max\n  cancel-in-progress: false", publish)
        self.assertIn("ref: main\n          path: source", publish)
        self.assertIn("--keep=100", publish)
        self.assertIn("retention-days: 30", measure)
        self.assertIn("git -C site add benchmarks", publish)

    def test_tag_on_a_merge_commit_remains_a_release_and_uses_commit_time(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            event = source(event="push")
            event.update(head_branch="v1.0.0", reference_time="2026-09-18T10:00:00Z")
            history.retain(root, record(), event, 100)
            pulls = [{"number": 9, "merged_at": "2026-09-19T12:00:00Z", "merge_commit_sha": HEAD}]
            rendered = history.render_site(root, pulls, "owner/repo")
            self.assertIn("v1.0.0", rendered)
            self.assertIn("2026-09-18T10:00:00Z", rendered)
            self.assertNotIn("PR 9", rendered)

    def test_statistics_reconcile_with_raw_records(self):
        result = history.summarize(record())[0]["metrics"]["elapsed_seconds"]
        self.assertEqual(result["base"]["median"], 3)
        self.assertEqual(result["base"]["min"], 2)
        self.assertEqual(result["base"]["max"], 4)
        self.assertAlmostEqual(result["base"]["stdev"], 2 ** .5)
        self.assertEqual(result["head_over_base"], 2)
        self.assertEqual(result["head"]["n"], 2)

    def test_invalid_or_incompatible_records_are_rejected(self):
        for change in (
            lambda value: value.update(schema=2),
            lambda value: value["samples"]["base"].pop(),
            lambda value: value["samples"]["head"][0]["fixture"].update(files_per_root=3),
            lambda value: value["samples"]["head"][0]["measurements"][0].update(exit_code=1),
            lambda value: value["samples"]["head"][0]["measurements"].clear(),
            lambda value: value["samples"]["head"][0]["measurements"].append(sample()["measurements"][0]),
            lambda value: value["samples"]["head"][0]["measurements"][0].update(scenario="hash"),
            lambda value: value["samples"]["head"][0]["measurements"][0].update(elapsed_seconds=float("nan")),
            lambda value: value["samples"]["head"][0]["measurements"][0].update(elapsed_seconds=-1),
        ):
            with self.subTest(change=change):
                value = record()
                change(value)
                with self.assertRaises(ValueError):
                    history.summarize(value)

    def test_changed_build_identity_omits_ratios(self):
        value = record()
        value["contract"].update(base_build_identity="old", head_build_identity="new")
        metrics = history.summarize(value)[0]["metrics"]
        self.assertTrue(all(metric["head_over_base"] is None for metric in metrics.values()))
        self.assertEqual(metrics["elapsed_seconds"]["head"]["median"], 6)
        value["samples"]["base"][1]["binary_sha256"] = "changed"
        with self.assertRaisesRegex(ValueError, "binary changed"):
            history.summarize(value)

    def test_missing_first_output_and_zero_baseline_have_no_ratio(self):
        value = record()
        for side in value["samples"].values():
            for report in side:
                report["measurements"][0].update(first_stdout_seconds=None, elapsed_seconds=0)
        metrics = history.summarize(value)[0]["metrics"]
        self.assertIsNone(metrics["first_stdout_seconds"]["head"])
        self.assertIsNone(metrics["elapsed_seconds"]["head_over_base"])
        self.assertIn("no output", history.render_report(value))

    def test_measurement_order_and_failure_do_not_create_successful_results(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = argparse.Namespace(head=HEAD, base=BASE, build="optimized", runner_class="test", tools="test",
                                      base_build_identity="same", head_build_identity="same", repetitions=2, driver=Path("driver"), base_binary=Path("base"),
                                      head_binary=Path("head"), files=2, depth=1, jobs=1, output=root / "result.json")
            calls = []
            history.platform.platform()  # Resolve host metadata before mocking command execution.

            def run(command, check):
                self.assertTrue(check)
                calls.append(command[1])
                output = Path(next(arg.removeprefix("--output=") for arg in command if arg.startswith("--output=")))
                output.write_text(json.dumps(sample()))

            with mock.patch.object(history.subprocess, "run", side_effect=run):
                history.measure_pair(args)
            self.assertEqual(calls, ["base", "head", "head", "base"])
            measured = json.loads(args.output.read_text())
            self.assertEqual(measured["head"], HEAD)
            self.assertEqual(measured["base"], BASE)
            self.assertEqual(measured["contract"]["fixture"], {"files_per_root": 2})
            self.assertTrue(measured["summary"])
            with mock.patch.object(history.subprocess, "run", side_effect=RuntimeError("failed")):
                with self.assertRaises(RuntimeError):
                    history.measure_pair(args)

    def test_retention_keeps_newest_and_preserves_run_attempt_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            history.retain(root, record(), source(2), keep=2)
            history.render_site(root, [], "owner/repo")
            history.retain(root, record(), source(1), keep=2)
            history.retain(root, record(), source(3), keep=2)
            self.assertFalse((root / "runs/1").exists())
            self.assertTrue((root / "runs/2/1/report.json").exists())
            history.retain(root, record(), source(3), keep=2)
            changed = record()
            changed["samples"]["head"][0]["measurements"][0]["elapsed_seconds"] = 10
            with self.assertRaisesRegex(ValueError, "cannot change"):
                history.retain(root, changed, source(3), keep=2)
            history.retain(root, record(), source(3, 2), keep=2)
            self.assertFalse((root / "runs/2").exists())
            self.assertTrue((root / "runs/3/2/report.json").exists())

    def test_source_commit_must_match_and_paths_are_not_supplied_by_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "does not match"):
                history.retain(root, record(), source(sha=BASE), 2)
            for sha in ("short", "../../outside", "<script>"):
                value = record()
                value["base"] = sha
                with self.assertRaises(ValueError):
                    history.retain(root, value, source(), 2)
            with self.assertRaises(ValueError):
                history.retain(root, record(), source(run=0), 2)
            with self.assertRaises(ValueError):
                history.render_site(root, [], "../../outside")

    def test_phases_reruns_and_merge_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pulls = [{"number": 9, "merged_at": "2026-09-19T12:00:00Z", "merge_commit_sha": BASE, "state": "closed"}]
            history.retain(root, record(), source(5), 100)
            history.retain(root, record(), source(6, 2), 100)
            post = record()
            post["head"], post["base"] = BASE, HEAD
            history.retain(root, post, source(3, event="push", sha=BASE), 100)
            rendered = history.render_site(root, pulls, "owner/repo")
            self.assertEqual(rendered.count("PR 9 pre-merge"), 1)
            self.assertEqual(rendered.count("PR 9 post-merge"), 1)
            self.assertLess(rendered.index("post-merge"), rendered.index("pre-merge"))
            self.assertIn('href="runs/6/2/"', rendered)
            self.assertNotIn('href="runs/5/1/"', rendered)
            self.assertIn(f"/commit/{BASE}", rendered)
            self.assertIn('href="report.json"', (root / "runs/6/2/index.html").read_text())
            pulls[0].update(merged_at=None)
            self.assertNotIn("PR 9", history.render_site(root, pulls, "owner/repo"))

    def test_html_escapes_provenance_and_scenario_text(self):
        value = record()
        value["contract"]["build"] = "<script>evil</script>"
        for side in value["samples"].values():
            for report in side:
                report["measurements"][0]["scenario"] = "<img>"
        rendered = history.render_report(value)
        self.assertNotIn("<script>", rendered)
        self.assertIn("&lt;script&gt;", rendered)
        self.assertIn("&lt;img&gt;", rendered)


if __name__ == "__main__":
    unittest.main()
