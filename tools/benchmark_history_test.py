# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Test benchmark compatibility, source association, rendering, and retention."""

import argparse
import gzip
import json
from pathlib import Path
import re
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


def comparison(elapsed=2):
    observed = {metric: elapsed for metric in history.benchmark_compare.METRICS}
    observed.update(stderr='', exit_codes=[0])
    return {'schema': 1, 'contract': {
                'repetitions': 1, 'retained': 1, 'estimator': 'mean-fastest',
                'file_counts': [10], 'cpu_counts': [1], 'build_identity': 'build',
                'storage': {'filesystem': 'tmpfs', 'memory_required': True},
                'affinity_by_cpu_count': {'1': [0]}},
            'tools': {'xff': {'sha256': 'binary'}, 'find': {'sha256': 'find', 'status': 'available'}},
            'tasks': [{'dataset': 'broad', 'shape': 'broad-10', 'name': 'files', 'cpus': 1, 'files': 10,
                       'fixture_identity': 'fixture', 'expected_sha256': 'output', 'input_files': 10,
                       'expected_count': 10, 'skips': {},
                       'participants': {tool: {'samples': [observed], 'pipeline': [[tool, '.']]}
                                        for tool in ('xff', 'find')}}]}


class BenchmarkPresentationBaselineTest(unittest.TestCase):
    def retain(self, root, run, head, elapsed=2, event='push', branch='main', baseline=None):
        value = record()
        value.update(head=head, tool_comparisons=comparison(elapsed))
        if baseline is not None:
            value['tool_comparisons']['baseline'] = baseline
        provenance = source(run, event=event, sha=head)
        provenance['head_branch'] = branch
        history.retain(root, value, provenance)
        return root / 'runs' / str(run) / '1' / 'report.json'

    def reconstruct(self, root, revisions):
        with mock.patch.object(history.subprocess, 'check_output', return_value='\n'.join(revisions)) as git:
            result = history.presentation_baselines(root, root)
        git.assert_called_once_with(['git', '-C', str(root), 'rev-list', '--first-parent', 'HEAD'], text=True)
        return result

    def test_uses_nearest_ancestor_even_if_older_revision_was_measured_later(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            current = self.retain(root, 2, HEAD)
            self.retain(root, 1, BASE, 3)
            self.retain(root, 4, 'c' * 40, 4)  # Older revision, newer measurement date.
            self.retain(root, 5, 'd' * 40, 5)  # Future revision.
            self.retain(root, 6, 'e' * 40, 6, event='pull_request')
            before = {path: path.read_bytes() for path in root.rglob('report.json')}
            result = self.reconstruct(root, ['d' * 40, HEAD, 'e' * 40, BASE, 'c' * 40])
            self.assertEqual(result[current]['head'], BASE)
            self.assertEqual(result[current]['run'], 1)
            self.assertEqual(result[current]['averages']['broad/files/1/10']['xff'], 3)
            self.assertEqual(before, {path: path.read_bytes() for path in root.rglob('report.json')})

    def test_existing_baseline_and_unrelated_revision_are_not_rewritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            recorded = self.retain(root, 1, HEAD, baseline={
                'status': 'available', 'head': BASE, 'run': 7, 'attempt': 1,
                'policy': 'mean of fastest 1/1 runs', 'averages': {}})
            unrelated = self.retain(root, 2, 'c' * 40)
            self.retain(root, 3, BASE)
            result = self.reconstruct(root, [HEAD, BASE])
            self.assertNotIn(recorded, result)
            self.assertNotIn(unrelated, result)
            self.assertEqual(result, {})  # The oldest known commit cannot compare with itself.

    def test_incompatible_nearest_ancestor_falls_back_to_compatible_older_one(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            current = self.retain(root, 3, HEAD)
            changed = self.retain(root, 2, BASE)
            value = json.loads(changed.read_text())
            value['tool_comparisons']['contract']['build_identity'] = 'different-build'
            changed.write_text(json.dumps(value))
            self.retain(root, 1, 'c' * 40)
            result = self.reconstruct(root, [HEAD, BASE, 'c' * 40])
            self.assertEqual(result[current]['head'], 'c' * 40)

    def test_packed_reports_are_read_without_modifying_the_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            current = self.retain(root, 2, HEAD)
            previous = self.retain(root, 1, BASE)
            packed = previous.with_suffix('.json.gz')
            original = gzip.compress(previous.read_bytes())
            packed.write_bytes(original)
            previous.unlink()
            self.assertEqual(self.reconstruct(root, [HEAD, BASE])[current]['head'], BASE)
            self.assertEqual(packed.read_bytes(), original)
            self.assertFalse(previous.exists())

    def test_refreshed_pages_render_reconstructed_baseline_but_raw_json_stays_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            current = self.retain(root, 2, HEAD)
            self.retain(root, 1, BASE)
            original = current.read_bytes()
            baselines = self.reconstruct(root, [HEAD, BASE])
            for render in (lambda: history.render_site(root, [], 'owner/repo', baselines),
                           lambda: history.reference_pages(root, [], root, 'owner/repo', baselines)):
                with self.subTest(render=render), mock.patch.object(history.subprocess, 'check_output', return_value=''):
                    render()
                    document = current.with_name('index.html').read_text()
                    self.assertIn('baseline was reconstructed', document)
                    self.assertNotIn('No compatible merged baseline', document)
                    self.assertIn(BASE[:10], document)
                    self.assertEqual(current.read_bytes(), original)

    def test_no_main_results_needs_no_git_history(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.retain(root, 1, HEAD, event='pull_request')
            with mock.patch.object(history.subprocess, 'check_output') as git:
                self.assertEqual(history.presentation_baselines(root, root), {})
            git.assert_not_called()

    def test_refresh_command_connects_reconstruction_to_published_pages(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            current = self.retain(root, 2, HEAD)
            self.retain(root, 1, BASE)
            pulls = root / 'pulls.json'
            pulls.write_text('[]')
            arguments = ['benchmark_history.py', 'refresh', '--root', str(root), '--pulls', str(pulls),
                         '--checkout', str(root), '--repository', 'owner/repo']
            def git(command, text):
                self.assertTrue(text)
                return HEAD + '\n' + BASE if 'rev-list' in command else ''
            with mock.patch('sys.argv', arguments), mock.patch.object(history.subprocess, 'check_output', side_effect=git):
                self.assertEqual(history.main(), 0)
            self.assertIn('baseline was reconstructed', current.with_name('index.html').read_text())
            self.assertNotIn('baseline', json.loads(current.read_text())['tool_comparisons'])


class BenchmarkHistoryTest(unittest.TestCase):
    def test_comparisons_are_rendered_and_bound_to_head_binary(self):
        value = record()
        comparison = {"schema": 1, "tools": {"xff": {"sha256": "binary"}},
                      "contract": {"repetitions": 1},
                      "tasks": [{"shape": "broad", "name": "files", "expected_count": 0,
                                 "participants": {}, "skips": {"<fzf>": "not installed"}}]}
        value["tool_comparisons"] = comparison
        self.assertIn("Tool comparisons", history.render_report(value))
        self.assertIn("&lt;fzf&gt;", history.render_report(value))
        with tempfile.TemporaryDirectory() as directory:
            history.retain(Path(directory), value, source())
            comparison["tools"]["xff"]["sha256"] = "different"
            with self.assertRaisesRegex(ValueError, "head binary"):
                history.retain(Path(directory), value, source(2))

    def test_workflow_keeps_measurement_unprivileged_and_publication_serialized(self):
        measure = repository_file(".github/workflows/benchmarks.yml").read_text()
        shard_action = repository_file(".github/actions/benchmark-measure/action.yml").read_text()
        publish = repository_file(".github/workflows/benchmark_pages.yml").read_text()
        self.assertIn("permissions:\n  contents: read", measure)
        self.assertIn("persist-credentials: false", measure)
        self.assertNotIn("pull_request:", measure)
        self.assertNotIn("tags:", measure)
        self.assertIn("branches: [main]", measure)
        self.assertIn("workflow_dispatch: {}", measure)
        self.assertIn('base_sha="$(git rev-parse HEAD^1)"', measure)
        self.assertIn("github.event.workflow_run.event == 'push'", publish)
        self.assertIn("github.event.workflow_run.head_branch == 'main'", publish)
        self.assertIn("--config=clang_release", measure)
        self.assertIn("target.files_to_run.executable.path", measure)
        self.assertIn("namespace: default", measure)
        self.assertNotIn("bazel-cache-save", measure)
        waiter, publisher = publish.split("  publish:", 1)
        self.assertNotIn("group: coverage-pages", waiter)
        self.assertIn("group: coverage-pages\n      queue: max\n      cancel-in-progress: false", publisher)
        self.assertIn("ref: main\n          persist-credentials: false", waiter)
        self.assertNotIn(": write", waiter)
        self.assertIn("ref: main\n          path: source", publish)
        self.assertIn("Verify live benchmark publication", publish)
        self.assertNotIn("--keep=100", publish)
        self.assertIn("retention-days: 30", shard_action)
        self.assertIn("shard: [0, 1, 2]", measure)
        self.assertIn("fail-fast: false", measure)
        self.assertIn("--adaptive --count=5", measure)
        self.assertIn("--partition=samples --repetitions=15 --keep=15", measure)
        self.assertIn("--shard-plan=benchmark-plan.json", shard_action)
        self.assertIn("--repetitions=3 --keep=3", shard_action)
        self.assertIn("--merge-adaptive-directory=candidate-shards", measure)
        self.assertNotIn('shards/*/*.json', measure)
        self.assertIn("git -C site add --all", publish)

    def test_pr_and_main_measurements_use_the_same_runner_identity(self):
        main = repository_file(".github/workflows/benchmarks.yml").read_text()
        pr = repository_file(".github/workflows/main.yml").read_text()
        shard_action = repository_file(".github/actions/benchmark-measure/action.yml").read_text()
        self.assertIn('--runner-class="github-hosted ${BENCHMARK_RUNNER}"', shard_action)
        self.assertIn("runner: ${{ matrix.os }}", main)
        self.assertIn("runner: ${{ matrix.os }}", pr)
        self.assertIn("platform: linux\n            os: ubuntu-latest", main)
        self.assertIn("platform: macos\n            os: macos-latest", main)

    def test_ci_shards_receive_the_prebuilt_native_resource_launcher(self):
        for workflow, job in (('.github/workflows/main.yml', 'benchmark-build'),
                              ('.github/workflows/benchmarks.yml', 'build')):
            with self.subTest(workflow=workflow):
                source = repository_file(workflow).read_text().split('\n  ' + job + ':', 1)[1]
                source = re.split(r'\n  [a-z][a-z-]*:', source, maxsplit=1)[0]
                self.assertIn("bazel cquery 'config(//tools:process_resources_native, target)' "
                              "--config=clang_release", source)
                self.assertIn('cp "${launcher}" "${RUNNER_TEMP}/benchmark-resource-launcher"', source)
                self.assertIn('tar -cf benchmark-build.tar -C "${RUNNER_TEMP}" '
                              'benchmark-head benchmark-resource-launcher', source)
                # A Python driver's data dependency can use a transitioned configuration.
                # Build the exact target configuration queried and copied below.
                head_build = source.split('bazel build ', 1)[1].split('\n', 1)[0].split()
                self.assertIn('//tools:process_resources_native', head_build)
                self.assertIn('--config=clang_release', head_build)
                self.assertLess(source.index('bazel build '),
                                source.index('bazel cquery '))
        action = repository_file('.github/actions/benchmark-measure/action.yml').read_text()
        self.assertIn('test -x "${RUNNER_TEMP}/benchmark-resource-launcher"', action)
        self.assertIn('--resource-launcher="${RUNNER_TEMP}/benchmark-resource-launcher"', action)
        self.assertNotIn('bazel build', action)
        self.assertNotIn('bazel run', action)

    def test_native_launcher_queries_select_only_the_built_target_configuration(self):
        for workflow in ('.github/workflows/main.yml', '.github/workflows/benchmarks.yml',
                         '.github/workflows/benchmark_backfill.yml'):
            with self.subTest(workflow=workflow):
                source = repository_file(workflow).read_text()
                query = source.split('launcher="$(bazel cquery ', 1)[1].split('cp "${launcher}"', 1)[0]
                self.assertIn("'config(//tools:process_resources_native, target)' --config=clang_release", query)

    def test_ci_backfill_builds_native_launcher_before_freezing_and_measuring(self):
        source = repository_file('.github/workflows/benchmark_backfill.yml').read_text()
        build = source.index('bazel build //tools:process_resources_native --config=clang_release')
        query = source.index("bazel cquery 'config(//tools:process_resources_native, target)' --config=clang_release")
        copy = source.index('cp "${launcher}" "${RUNNER_TEMP}/benchmark-resource-launcher"')
        run = source.index('python3 tools/benchmark_campaign.py run')
        self.assertLess(build, query)
        self.assertLess(query, copy)
        self.assertLess(copy, run)
        self.assertIn('--resource-launcher="${RUNNER_TEMP}/benchmark-resource-launcher"', source[run:])

    def test_pr_and_main_aggregation_attach_only_earlier_main_baselines(self):
        for workflow, job in (('.github/workflows/benchmarks.yml', 'aggregate'),
                              ('.github/workflows/main.yml', 'benchmark-aggregate')):
            with self.subTest(workflow=workflow):
                source = repository_file(workflow).read_text().split('\n  ' + job + ':', 1)[1]
                source = re.split(r'\n  [a-z][a-z-]*:', source, maxsplit=1)[0]
                self.assertIn('fetch-depth: 0', source)
                self.assertIn('ref: coverage-pages', source)
                self.assertIn('sparse-checkout: benchmarks/runs', source)
                self.assertIn('git rev-list --first-parent HEAD^1 > benchmark-baseline-revisions.txt', source)
                self.assertIn('--baseline-root=benchmark-baseline/benchmarks', source)
                self.assertIn('--baseline-revisions=benchmark-baseline-revisions.txt', source)

    def test_tag_on_a_merge_commit_remains_a_release_and_uses_commit_time(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            event = source(event="push")
            event.update(head_branch="v1.0.0", reference_time="2026-09-18T10:00:00Z")
            history.retain(root, record(), event)
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

    def test_retention_keeps_all_history_and_preserves_run_attempt_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            history.retain(root, record(), source(2))
            history.render_site(root, [], "owner/repo")
            history.retain(root, record(), source(1))
            stale_payload = root / "runs/1/1/landscape.json"
            stale_payload.write_text("{}")
            history.retain(root, record(), source(3))
            self.assertTrue(stale_payload.exists())
            self.assertTrue((root / "runs/1").exists())
            self.assertTrue((root / "runs/2/1/report.json").exists())
            history.retain(root, record(), source(3))
            packed = root / "runs/3/1/report.json"
            packed.with_suffix('.json.gz').write_bytes(gzip.compress(packed.read_bytes()))
            packed.unlink()
            history.retain(root, record(), source(3))
            changed = record()
            changed["samples"]["head"][0]["measurements"][0]["elapsed_seconds"] = 10
            with self.assertRaisesRegex(ValueError, "cannot change"):
                history.retain(root, changed, source(3))
            history.retain(root, record(), source(3, 2))
            self.assertTrue((root / "runs/2").exists())
            self.assertTrue((root / "runs/3/2/report.json").exists())

    def test_source_commit_must_match_and_paths_are_not_supplied_by_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "does not match"):
                history.retain(root, record(), source(sha=BASE))
            for sha in ("short", "../../outside", "<script>"):
                value = record()
                value["base"] = sha
                with self.assertRaises(ValueError):
                    history.retain(root, value, source())
            with self.assertRaises(ValueError):
                history.retain(root, record(), source(run=0))
            with self.assertRaises(ValueError):
                history.render_site(root, [], "../../outside")

    def test_phases_reruns_and_merge_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pulls = [{"number": 9, "merged_at": "2026-09-19T12:00:00Z", "merge_commit_sha": BASE, "state": "closed"}]
            history.retain(root, record(), source(5))
            history.retain(root, record(), source(6, 2))
            post = record()
            post["head"], post["base"] = BASE, HEAD
            history.retain(root, post, source(3, event="push", sha=BASE))
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

    def test_reference_pages_use_exact_commits_and_survive_expired_measurements(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pulls = [{"number": 9, "merge_commit_sha": HEAD, "merged_at": "2026-09-19T12:00:00Z"}]
            history.retain(root, record(), source(1))
            history.retain(root, record(), source(2, event="push"))
            history.retain(root, record(), source(3))  # Later pre-merge cannot win.

            def git(command, text):
                self.assertTrue(text)
                if command[-3:] == ["tag", "--list", "v*"]:
                    return "v1.0.0\nv2.0.0\nv-invalid\n"
                self.assertEqual(command[-2], "rev-parse")
                return (HEAD if command[-1] == "v1.0.0^{commit}" else BASE) + "\n"

            with mock.patch.object(history.subprocess, "check_output", side_effect=git):
                history.reference_pages(root, pulls, root)
            links = json.loads((root / 'version-links.json').read_text())
            self.assertIn({'label': 'Release v1.0.0', 'href': 'https://github.com/mboworks/xff/releases/tag/v1.0.0'}, links[HEAD])
            self.assertIn({'label': 'PR #9', 'href': 'https://github.com/mboworks/xff/pull/9'}, links[HEAD])
            for reference in ("tag/1.0.0", "pr/9"):
                html = (root / reference / "index.html").read_text()
                self.assertIn('url=../../runs/2/1/', html)
                self.assertTrue((root / "runs/2/1/index.html").is_file())
            self.assertIn("No retained benchmark", (root / "tag/2.0.0/index.html").read_text())
            self.assertFalse((root / "tag/-invalid").exists())
            # New measurements never expire release or merged-PR measurements.
            changed = record()
            changed["head"] = BASE
            history.retain(root, changed, source(4, event="push", sha=BASE))
            with mock.patch.object(history.subprocess, "check_output", side_effect=git):
                history.reference_pages(root, pulls, root)
            self.assertIn('url=../../runs/2/1/', (root / "tag/1.0.0/index.html").read_text())
            self.assertIn('url=../../runs/2/1/', (root / "pr/9/index.html").read_text())
            self.assertIn('url=../../runs/4/1/', (root / "tag/2.0.0/index.html").read_text())
            with mock.patch.object(history.subprocess, "check_output", return_value=""):
                history.reference_pages(root, [], root)
            self.assertNotIn("http-equiv", (root / "tag/2.0.0/index.html").read_text())

    def test_legacy_platform_comes_from_recorded_contract(self):
        self.assertEqual(history.recorded_platform({'contract': {'platform': 'Linux-6.8-x86_64'}}), 'linux')
        self.assertEqual(history.recorded_platform({'contract': {'platform': 'macOS-26.6-arm64'}}), 'macos')
        self.assertEqual(history.recorded_platform({'contract': {}}), '')

    def test_platform_reports_coexist_and_release_lists_both(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for platform_key in ('linux', 'macos'):
                value = dict(record(), platform=platform_key)
                history.retain(root, value, source(2, event='push'))
                self.assertTrue((root / 'runs/2/1' / platform_key / 'report.json').is_file())
            rendered = history.render_site(root, [], 'owner/repo')
            self.assertIn('runs/2/1/linux/', rendered)
            self.assertIn('runs/2/1/macos/', rendered)
            with mock.patch.object(history.subprocess, 'check_output', side_effect=['v1.0.0\n', HEAD + '\n']):
                history.reference_pages(root, [], root)
            page = (root / 'tag/1.0.0/index.html').read_text()
            self.assertIn('../../runs/2/1/linux/', page)
            self.assertIn('../../runs/2/1/macos/', page)
            self.assertNotIn('http-equiv', page)
            self.assertIn('href="../../../../"', (root / 'runs/2/1/macos/index.html').read_text())
            with self.assertRaisesRegex(ValueError, 'platform'):
                history.retain(root, dict(record(), platform='../bad'), source(3))

    def test_release_refresh_does_not_download_measurement_artifacts(self):
        publish = repository_file(".github/workflows/benchmark_pages.yml").read_text()
        self.assertIn("workflows: [Test, Benchmarks, Release, Benchmark backfill]", publish)
        self.assertIn("workflow_dispatch: {}", publish)
        self.assertIn("fetch-depth: 0", publish)
        self.assertIn("uses: actions/download-artifact@v8\n        if: github.event.workflow_run.name == 'Benchmarks'", publish)
        refresh = publish.split("      - name: Refresh stable benchmark references and deploy", 1)[1].split(
            "      - uses: actions/upload-pages-artifact", 1)[0]
        self.assertNotIn("SOURCE_RUN", refresh)
        self.assertNotIn("download-artifact", refresh)
        self.assertIn("benchmark_history.py refresh", refresh)

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
