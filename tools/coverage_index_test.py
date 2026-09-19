#!/usr/bin/env python3
"""Tests for tools/coverage_index.py."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))
import coverage_index  # noqa: E402


def _summary(percent: float) -> dict:
    metric = {"covered": 9, "total": 10, "percent": percent}
    return {
        "measurements": {"overall": {name: metric for name in ("lines", "functions", "branches")}},
        "minimums": {"overall": {name: 80 for name in ("lines", "functions", "branches")}},
        "targets": {"overall": {name: 90 for name in ("lines", "functions", "branches")}},
        "enforcement": {"overall": {name: "medium" for name in ("lines", "functions", "branches")}},
    }


class CoverageIndexTest(unittest.TestCase):
    def test_report_contains_policy_table_and_lcov_link(self):
        rendered = coverage_index.render_report(_summary(95.0), "pr/42")
        self.assertIn("xff coverage: pr/42", rendered)
        self.assertIn("95.00%", rendered)
        self.assertIn('href="lcov/"', rendered)
        self.assertIn('href="coverage-summary.json"', rendered)
        self.assertIn('href="coverage-meta.json"', rendered)
        self.assertIn('href="../../"', rendered)

    def test_report_status_is_ok_between_minimum_and_target(self):
        rendered = coverage_index.render_report(_summary(85.0), "pr/42")
        self.assertIn('<td class="status-ok">OK</td>', rendered)
        self.assertIn('<td class="medium" title="medium; enforce medium;', rendered)
        self.assertIn('<td class="policyCell">80/90&middot;M</td>', rendered)

    def test_report_status_identifies_metrics_below_minimum(self):
        summary = _summary(95.0)
        summary["measurements"]["overall"]["functions"] = {"covered": 7, "total": 10, "percent": 70.0}
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('<td class="status-bad">BAD: F</td>', rendered)

    def test_high_enforcement_rejects_a_medium_rating(self):
        summary = _summary(85.0)
        summary["enforcement"]["overall"]["branches"] = "high"
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('<td class="status-bad">BAD: B</td>', rendered)

    def test_dense_table_shows_effective_override_policy_per_row(self):
        summary = _summary(95.0)
        summary["measurements"]["extensions / new"] = {
            name: {"covered": 8, "total": 10, "percent": 80.0}
            for name in ("lines", "functions", "branches")
        }
        summary["minimums"]["extensions / new"] = {
            "lines": 75,
            "functions": 80,
            "branches": 60,
        }
        summary["targets"]["extensions / new"] = {
            "lines": 90,
            "functions": 90,
            "branches": 85,
        }
        summary["enforcement"]["extensions / new"] = {
            "lines": "medium",
            "functions": "medium",
            "branches": "high",
        }
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('<td class="status-bad">BAD: B</td>', rendered)
        self.assertIn('<td class="policyCell policy-weaker">lower<br>75/90&middot;M</td>', rendered)
        self.assertIn('<td class="policyCell policy-mixed">mixed<br>60/85&middot;H</td>', rendered)
        self.assertIn('<td class="policyCell">default</td>', rendered)
        self.assertIn(">8</td>", rendered)
        self.assertIn(">10</td>", rendered)
        self.assertLess(rendered.index('colspan="3">Lines'), rendered.index('colspan="3">Branches'))
        self.assertLess(rendered.index('colspan="3">Branches'), rendered.index('colspan="3">Functions'))

    def test_full_table_orders_groups_and_categories(self):
        summary = _summary(95.0)
        overall = summary["measurements"]["overall"]
        for category in (
            "extensions / zeta",
            "extensions / alpha",
            "program / zeta",
            "program / alpha",
        ):
            summary["minimums"][category] = summary["minimums"]["overall"]
            summary["targets"][category] = summary["targets"]["overall"]
            summary["enforcement"][category] = summary["enforcement"]["overall"]
        summary["measurements"].update(
            {
                "extensions / zeta": overall,
                "extensions / alpha": overall,
                "program / zeta": overall,
                "program / alpha": overall,
            }
        )
        rendered = coverage_index.render_report(summary, "pr/42")
        order = [
            rendered.index(name)
            for name in (
                "overall",
                "program / alpha",
                "program / zeta",
                "extensions / alpha",
                "extensions / zeta",
            )
        ]
        self.assertEqual(order, sorted(order))

    def test_rate_covered_and_total_share_the_rating_background(self):
        rendered = coverage_index.render_report(_summary(95.0), "pr/42")
        self.assertIn(
            '<td class="high" title="high; enforce medium; medium at 80%; high at 90%">'
            "95.00%</td>",
            rendered,
        )
        self.assertGreaterEqual(
            rendered.count(
                '<td class="high" title="high; enforce medium; medium at 80%; high at 90%">9</td>'
            ),
            3,
        )
        self.assertGreaterEqual(
            rendered.count(
                '<td class="high" title="high; enforce medium; medium at 80%; high at 90%">10</td>'
            ),
            3,
        )
        self.assertIn(".coverageTable { border-collapse: separate; border-spacing: 1px;", rendered)
        self.assertIn("background: #6688d4; color: #fff", rendered)
        self.assertIn(".coverageTable thead tr:first-child th { font-size: 120%; text-align: center; }", rendered)

    def test_policy_footnote_explains_notation_and_comparison(self):
        rendered = coverage_index.render_report(_summary(95.0), "pr/42")
        self.assertIn("Policy vs default<sup>*</sup>", rendered)
        self.assertIn("medium/high&middot;enforced-band", rendered)
        self.assertIn("90/92&middot;M", rendered)
        self.assertIn("compare policy strictness", rendered)
        self.assertIn("not\n    measured coverage", rendered)
        self.assertIn(".policyNote { font-size: 12px;", rendered)
        self.assertIn("text-align: left;", rendered)

    def test_equal_boundaries_use_one_compact_limit(self):
        policy = coverage_index.coverage_policy.MetricPolicy(98, 98, "high")
        self.assertEqual("98&middot;H", coverage_index._compact_policy(policy))

    def test_patch_uses_its_resolved_policy(self):
        summary = _summary(95.0)
        summary["patch"] = {
            "lines": {"covered": 9, "total": 10, "percent": 90.0},
            "functions": {"covered": 0, "total": 0, "percent": None},
            "branches": {"covered": 9, "total": 10, "percent": 90.0},
        }
        summary["patch_policy"] = {
            "minimum": {"lines": 80, "branches": 80},
            "target": {"lines": 95, "branches": 95},
            "enforce": {"lines": "high", "branches": "medium"},
        }
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('<td class="status-bad">BAD: L</td>', rendered)
        self.assertEqual(2, rendered.count('<td class="medium">90.00%</td>'))

    def test_patch_without_branches_renders_that_metric_as_not_applicable(self):
        summary = _summary(95.0)
        summary["patch"] = {
            "lines": {"covered": 38, "total": 38, "percent": 100.0},
            "functions": {"covered": 0, "total": 0, "percent": None},
            "branches": {"covered": 0, "total": 0, "percent": None},
        }
        summary["patch_policy"] = {
            "minimum": {"lines": 95},
            "target": {"lines": 98},
            "enforce": {"lines": "medium"},
        }
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('<td class="status-good">GOOD</td>', rendered)
        self.assertIn('<td class="high">100.00%</td><td>n/a</td>', rendered)

    def test_override_reason_is_available_on_the_compact_policy_row(self):
        summary = _summary(95.0)
        summary["measurements"]["extensions / new"] = summary["measurements"]["overall"]
        summary["minimums"]["extensions / new"] = {name: 70 for name in coverage_index._METRICS}
        summary["targets"]["extensions / new"] = {name: 90 for name in coverage_index._METRICS}
        summary["enforcement"]["extensions / new"] = {name: "medium" for name in coverage_index._METRICS}
        summary["reasons"] = {"extensions / new": "New module onboarding."}
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('title="New module onboarding.">extensions / new</td>', rendered)

    def test_site_interleaves_prs_and_releases_by_main_history_with_main_first(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for report in ("main", "pr/9", "pr/42", "tag/0.9.0", "tag/0.10.0"):
                target = root / report
                target.mkdir(parents=True)
                (target / "index.html").touch()
                (target / "coverage-summary.json").write_text(json.dumps(_summary(95.0)))
                metadata = coverage_index.report_metadata(
                    _summary(95.0),
                    report,
                    "2026-08-22T09:59:00Z",
                    "2026-08-22T10:00:00Z",
                    "2026-08-22T10:01:00Z",
                    1,
                    1,
                    "abc",
                )
                positions = {"main": 0, "pr/9": 3, "pr/42": 1, "tag/0.9.0": 2, "tag/0.10.0": 0}
                metadata["history"] = {"position": positions[report], "commit": "abc"}
                (target / "coverage-meta.json").write_text(json.dumps(metadata))

            rendered = coverage_index.render_site(root)

            self.assertIn("PR 42", rendered)
            self.assertIn("PR 9", rendered)
            self.assertIn("release 0.10.0", rendered)
            self.assertIn("release 0.9.0", rendered)
            self.assertIn(
                'href="https://github.com/mboworks/xff/releases/tag/v0.10.0">release v0.10.0</a>',
                rendered,
            )
            self.assertIn('href="https://github.com/mboworks/xff/pull/42">PR #42</a>', rendered)
            self.assertIn('href="pr/42/coverage-summary.json">JSON</a>', rendered)
            self.assertIn("2026-08-22 10:01:00 UTC", rendered)
            self.assertIn('href="https://github.com/mboworks/xff/commit/abc"><code>abc</code></a>', rendered)
            self.assertIn('href="https://github.com/mboworks/xff/actions/runs/1">run 1</a>', rendered)
            self.assertIn("font-variant-numeric: tabular-nums", rendered)
            order = ["main", "pr/9", "tag/0.9.0", "pr/42", "tag/0.10.0"]
            offsets = [rendered.index(f'href="{target}/"') for target in order]
            self.assertEqual(offsets, sorted(offsets))
            self.assertNotIn("<ul>", rendered)

    def test_history_uses_merge_and_peeled_tag_commits_and_refreshes_old_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repository = root / "source"
            repository.mkdir()
            def git(*args):
                return subprocess.check_output(
                    ["git", "-C", str(repository), "-c", "user.name=Coverage Test",
                     "-c", "user.email=coverage@example.invalid", "-c", "commit.gpgsign=false",
                     "-c", "tag.gpgsign=false", "-c", "core.hooksPath=/dev/null", *args], text=True,
                    stderr=subprocess.DEVNULL,
                ).strip()
            git("init")
            git("commit", "--allow-empty", "-m", "older merge")
            older = git("rev-parse", "HEAD")
            git("tag", "v0.9.0")
            git("commit", "--allow-empty", "-m", "release commit")
            release = git("rev-parse", "HEAD")
            git("tag", "-a", "v0.10.0", "-m", "release")
            git("commit", "--allow-empty", "-m", "newer merge")
            newer = git("rev-parse", "HEAD")
            reports = root / "reports"
            for target in ("main", "pr/900", "pr/1", "pr/2", "tag/0.9.0", "tag/0.10.0", "tag/9.9.9"):
                folder = reports / target
                folder.mkdir(parents=True)
                metadata = coverage_index.report_metadata(
                    _summary(95), target, "2026-08-22T10:00:00Z", "2026-08-22T10:00:00Z",
                    "2026-08-22T10:01:00Z", 1, 1, "tested-pr-head",
                )
                metadata["history"] = {"position": 999, "commit": "stale"}
                (folder / "coverage-meta.json").write_text(json.dumps(metadata))
            pulls = [
                {"number": 900, "merged_at": "2026-08-20T10:00:00Z", "merge_commit_sha": older},
                {"number": 1, "merged_at": "2026-08-21T10:00:00Z", "merge_commit_sha": newer},
                {"number": 2, "merged_at": None, "merge_commit_sha": older},
            ]
            pages = root / "pulls.json"
            pages.write_text(json.dumps([pulls[:1], pulls[1:]]))
            with mock.patch.object(sys, "argv", [
                "coverage_index.py", "history", str(reports), str(repository), str(pages)
            ]):
                self.assertEqual(coverage_index.main(), 0)
            expected = {"pr/900": (0, older), "tag/0.9.0": (0, older),
                        "tag/0.10.0": (1, release), "pr/1": (2, newer)}
            for target, (position, commit) in expected.items():
                metadata = json.loads((reports / target / "coverage-meta.json").read_text())
                self.assertEqual(metadata["history"], {"position": position, "commit": commit})
                self.assertEqual(metadata["source"]["head_sha"], "tested-pr-head")
            for target in ("main", "pr/2", "tag/9.9.9"):
                metadata = json.loads((reports / target / "coverage-meta.json").read_text())
                self.assertIsNone(metadata["history"])
            rendered = coverage_index.render_site(reports)
            order = ["main", "pr/1", "tag/0.10.0", "tag/0.9.0", "pr/900"]
            offsets = [rendered.index(f'href="{target}/"') for target in order]
            self.assertEqual(offsets, sorted(offsets))
            self.assertLess(offsets[-1], rendered.index('href="pr/2/"'))
            # A PR report can be published before the PR is merged. Refreshing must move it
            # into the main chronology without replacing its coverage or workflow identity.
            pulls[-1]["merged_at"] = "2026-08-22T10:00:00Z"
            coverage_index.update_history(reports, repository, pulls)
            refreshed = json.loads((reports / "pr/2/coverage-meta.json").read_text())
            self.assertEqual(refreshed["history"]["position"], 0)

    def test_aggregated_prs_keep_individual_reports_at_the_first_main_integration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repository = root / "source"
            repository.mkdir()

            def git(*args):
                return subprocess.check_output(
                    ["git", "-C", str(repository), "-c", "user.name=Coverage Test",
                     "-c", "user.email=coverage@example.invalid", "-c", "commit.gpgsign=false",
                     "-c", "tag.gpgsign=false", "-c", "core.hooksPath=/dev/null", *args],
                    text=True, stderr=subprocess.DEVNULL,
                ).strip()

            git("init")
            git("commit", "--allow-empty", "-m", "initial")
            main = git("branch", "--show-current")
            git("tag", "v1.0.0")
            git("checkout", "-b", "integration")
            git("checkout", "-b", "first")
            git("commit", "--allow-empty", "-m", "first feature")
            git("checkout", "integration")
            git("merge", "--no-ff", "first", "-m", "merge first PR")
            first = git("rev-parse", "HEAD")
            git("checkout", "-b", "second")
            git("commit", "--allow-empty", "-m", "second feature")
            git("checkout", "integration")
            git("merge", "--no-ff", "second", "-m", "merge second PR")
            second = git("rev-parse", "HEAD")
            git("checkout", "-b", "unmerged")
            git("commit", "--allow-empty", "-m", "not integrated")
            unmerged = git("rev-parse", "HEAD")
            git("checkout", main)
            git("commit", "--allow-empty", "-m", "release before integration")
            git("tag", "v2.0.0")
            reports = root / "reports"
            originals = {}
            targets = ("main", "pr/1", "pr/99", "pr/5", "pr/77", "pr/88", "tag/1.0.0", "tag/2.0.0")
            for run, target in enumerate(targets, 1):
                folder = reports / target
                (folder / "lcov").mkdir(parents=True)
                metadata = coverage_index.report_metadata(
                    _summary(90 + run), target, "2026-08-22T10:00:00Z", "2026-08-22T10:00:00Z",
                    "2026-08-22T10:01:00Z", run, 1, f"measured-head-{run}",
                )
                originals[target] = metadata
                (folder / "coverage-meta.json").write_text(json.dumps(metadata))
                (folder / "lcov/index.html").write_text(f"details-{run}")
            pulls = [
                {"number": 1, "merged_at": "2026-08-20T10:00:00Z", "merge_commit_sha": first},
                {"number": 99, "merged_at": "2026-08-20T11:00:00Z", "merge_commit_sha": second},
                {"number": 77, "merged_at": "2026-08-20T12:00:00Z", "merge_commit_sha": unmerged},
                {"number": 88, "merged_at": "2026-08-20T12:00:00Z", "merge_commit_sha": "f" * 40},
            ]
            coverage_index.update_history(reports, repository, pulls)
            for target in ("pr/1", "pr/99"):
                metadata = json.loads((reports / target / "coverage-meta.json").read_text())
                self.assertIsNone(metadata["history"])
            git("merge", "--no-ff", "integration", "-m", "merge aggregation PR")
            integration = git("rev-parse", "HEAD")
            git("commit", "--allow-empty", "-m", "later main commit")
            pulls.append({"number": 5, "merged_at": "2026-08-21T12:00:00Z", "merge_commit_sha": integration})
            coverage_index.update_history(reports, repository, pulls)
            for target, commit in (("pr/1", first), ("pr/99", second)):
                metadata = json.loads((reports / target / "coverage-meta.json").read_text())
                self.assertEqual(metadata["history"]["commit"], commit)
                self.assertEqual(metadata["history"]["integration_commit"], integration)
                self.assertEqual(metadata["history"]["position"], 2)
            for target in ("pr/77", "pr/88"):
                metadata = json.loads((reports / target / "coverage-meta.json").read_text())
                self.assertIsNone(metadata["history"])
            for run, target in enumerate(targets, 1):
                metadata = json.loads((reports / target / "coverage-meta.json").read_text())
                self.assertEqual(metadata["source"], originals[target]["source"])
                self.assertEqual(metadata["coverage"], originals[target]["coverage"])
                self.assertEqual((reports / target / "lcov/index.html").read_text(), f"details-{run}")
            rendered = coverage_index.render_site(reports)
            ordered = ["main", "pr/5", "pr/99", "pr/1", "tag/2.0.0", "tag/1.0.0"]
            offsets = [rendered.index(f'href="{target}/"') for target in ordered]
            self.assertEqual(offsets, sorted(offsets))
            self.assertIn("Aggregated PRs retain their own reports", rendered)

            # Squashing the aggregation removes nested merges from main ancestry.
            # PR metadata identifies the parent; its recorded head must contain each child.
            git("checkout", "-b", "squashed-main", f"{integration}^1")
            git("commit", "--allow-empty", "-m", "squash aggregation")
            squashed = git("rev-parse", "HEAD")
            for pull in pulls[:-1]:
                pull["base"] = {"ref": "integration", "repo": {"full_name": "owner/repo"}}
            pulls[-1].update(merge_commit_sha=squashed, head={
                "ref": "integration", "sha": second, "repo": {"full_name": "owner/repo"},
            })
            coverage_index.update_history(reports, repository, pulls)
            for target in ("pr/1", "pr/99"):
                metadata = json.loads((reports / target / "coverage-meta.json").read_text())
                self.assertEqual(metadata["history"]["integration_commit"], squashed)
                self.assertEqual(metadata["history"]["position"], 2)
                self.assertEqual(metadata["source"], originals[target]["source"])
            for target in ("pr/77", "pr/88"):
                metadata = json.loads((reports / target / "coverage-meta.json").read_text())
                self.assertIsNone(metadata["history"])

    def test_archive_retains_details_and_each_run_attempt_without_replacing_old_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "pr/868"
            (report / "lcov/source").mkdir(parents=True)
            (report / "lcov/source/file.cc.gcov.html").write_text("original detailed source")
            (report / "index.html").write_text("original report")
            (report / "coverage-summary.json").write_text(json.dumps(_summary(95)))
            metadata = coverage_index.report_metadata(
                _summary(95), "pr/868", "2026-08-22T10:00:00Z", "2026-08-22T10:00:00Z",
                "2026-08-22T10:01:00Z", 100, 1, "original-tested-head",
            )
            (report / "coverage-meta.json").write_text(json.dumps(metadata))
            with mock.patch.object(sys, "argv", ["coverage_index.py", "archive", str(root)]):
                self.assertEqual(coverage_index.main(), 0)
            original = root / "runs/100/1"
            self.assertEqual((original / "lcov/source/file.cc.gcov.html").read_text(), "original detailed source")
            self.assertEqual(json.loads((original / "coverage-meta.json").read_text()), metadata)
            (report / "lcov/source/file.cc.gcov.html").write_text("new detail")
            coverage_index.archive_reports(root)
            self.assertEqual((original / "lcov/source/file.cc.gcov.html").read_text(), "original detailed source")
            metadata["source"]["run_attempt"] = 2
            (report / "coverage-meta.json").write_text(json.dumps(metadata))
            coverage_index.archive_reports(root)
            self.assertEqual((root / "runs/100/2/lcov/source/file.cc.gcov.html").read_text(), "new detail")
            metadata["source"].update(run_id=101, run_attempt=1, head_sha="integration-tested-head")
            (report / "coverage-meta.json").write_text(json.dumps(metadata))
            coverage_index.archive_reports(root)
            rendered = (root / "runs/index.html").read_text()
            paths = ["101/1", "100/2", "100/1"]
            offsets = [rendered.index(f'href="{path}/"') for path in paths]
            self.assertEqual(offsets, sorted(offsets))
            for path in paths:
                self.assertIn(f'href="{path}/coverage-summary.json"', rendered)
            self.assertIn("original-tested-head"[:7], rendered)
            self.assertIn("integration-tested-head"[:7], rendered)
            self.assertIn('href="runs/"', coverage_index.render_site(root))

    def test_archive_retains_late_incoming_run_without_changing_latest_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "site"
            latest = root / "main"
            incoming = Path(directory) / "incoming"
            latest.mkdir(parents=True)
            incoming.mkdir()
            for report, run in ((latest, 200), (incoming, 100)):
                metadata = coverage_index.report_metadata(
                    _summary(95), "main", "2026-08-22T10:00:00Z", "2026-08-22T10:00:00Z",
                    "2026-08-22T10:01:00Z", run, 1, f"head-{run}",
                )
                (report / "coverage-meta.json").write_text(json.dumps(metadata))
                (report / "index.html").write_text(f"report-{run}")
            with mock.patch.object(sys, "argv", [
                "coverage_index.py", "archive", str(root), "--incoming", str(incoming),
            ]):
                self.assertEqual(coverage_index.main(), 0)
            self.assertEqual((latest / "index.html").read_text(), "report-200")
            for run in (100, 200):
                self.assertEqual((root / f"runs/{run}/1/index.html").read_text(), f"report-{run}")

    def test_archive_skips_legacy_and_recovers_after_an_interrupted_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "main"
            report.mkdir()
            metadata = coverage_index.report_metadata(
                _summary(95), "main", "2026-08-22T10:00:00Z", "2026-08-22T10:00:00Z",
                "2026-08-22T10:01:00Z", 0, 0, "legacy",
            )
            (report / "coverage-meta.json").write_text(json.dumps(metadata))
            coverage_index.archive_reports(root)
            self.assertFalse((root / "runs/0").exists())
            self.assertIn("No archived coverage runs", (root / "runs/index.html").read_text())
            metadata["source"].update(run_id=200, run_attempt=1)
            (report / "coverage-meta.json").write_text(json.dumps(metadata))
            with mock.patch.object(coverage_index.shutil, "copytree", side_effect=OSError("copy failed")):
                with self.assertRaisesRegex(OSError, "copy failed"):
                    coverage_index.archive_reports(root)
            self.assertFalse((root / "runs/200/1").exists())
            self.assertEqual(list((root / "runs/200").glob(".archive-*")), [])
            coverage_index.archive_reports(root)
            self.assertTrue((root / "runs/200/1/coverage-meta.json").exists())

    def test_unpositioned_reports_use_run_creation_not_completion_time(self):
        reports = [
            coverage_index.report_metadata(
                _summary(95), target, created, created, completed, index, 1, "sha"
            )
            for index, (target, created, completed) in enumerate((
                ("pr/9", "2026-08-20T10:00:00Z", "2026-08-25T10:00:00Z"),
                ("pr/1", "2026-08-21T10:00:00Z", "2026-08-21T10:01:00Z"),
                ("tag/1.0.0", "2026-08-19T10:00:00Z", "2026-08-25T10:00:00Z"),
            ))
        ]
        self.assertEqual(
            [value["target"] for value in sorted(reports, key=coverage_index._report_order, reverse=True)],
            ["pr/1", "pr/9", "tag/1.0.0"],
        )

    def test_empty_site_says_no_reports_are_available(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertIn("No coverage reports are available", coverage_index.render_site(Path(directory)))

    def test_legacy_report_does_not_link_placeholder_metadata(self):
        metadata = coverage_index.report_metadata(
            _summary(95.0),
            "pr/9",
            "1970-01-01T00:00:00Z",
            "1970-01-01T00:00:00Z",
            "1970-01-01T00:00:00Z",
            0,
            0,
            "legacy",
        )
        row = coverage_index._short_row(metadata)
        self.assertIn('href="https://github.com/mboworks/xff/pull/9">PR #9</a>', row)
        self.assertNotIn("actions/runs/0", row)
        self.assertNotIn("commit/legacy", row)

    def test_newer_report_wins_even_when_an_older_run_finishes_later(self):
        summary = _summary(95.0)
        old = coverage_index.report_metadata(
            summary,
            "pr/42",
            "2026-08-22T10:00:00Z",
            "2026-08-22T10:01:00Z",
            "2026-08-22T10:10:00Z",
            100,
            1,
            "old",
        )
        new = coverage_index.report_metadata(
            summary,
            "pr/42",
            "2026-08-22T10:01:00Z",
            "2026-08-22T10:02:00Z",
            "2026-08-22T10:03:00Z",
            101,
            1,
            "new",
        )
        rerun = coverage_index.report_metadata(
            summary,
            "pr/42",
            "2026-08-22T10:01:00Z",
            "2026-08-22T10:11:00Z",
            "2026-08-22T10:12:00Z",
            101,
            2,
            "new",
        )
        self.assertTrue(coverage_index.is_newer(new, old))
        self.assertFalse(coverage_index.is_newer(old, new))
        self.assertTrue(coverage_index.is_newer(rerun, new))
        self.assertEqual(
            {"pr/42": rerun}, coverage_index.latest_metadata([new, old, rerun, new])
        )
        same_tick_new_run = {
            **old,
            "source": {**old["source"], "run_id": 102, "run_attempt": 1},
        }
        same_tick_old_rerun = {
            **old,
            "source": {**old["source"], "run_id": 100, "run_attempt": 9},
        }
        self.assertTrue(
            coverage_index.is_newer(same_tick_new_run, same_tick_old_rerun)
        )


if __name__ == "__main__":
    unittest.main()
