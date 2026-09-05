#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Tests for tools/fuzz_targets.py."""

import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fuzz_targets  # noqa: E402


class FuzzTargetsTest(unittest.TestCase):
    def test_parses_balanced_rules_with_nested_calls_and_parentheses_in_strings(self):
        self.assertEqual(
            fuzz_targets.rule_blocks(
                'cc_fuzz_test(name = "one", corpus = glob(["(*)"]))\ncc_fuzz_test(name = "two")',
                "cc_fuzz_test",
            ),
            ['name = "one", corpus = glob(["(*)"])', 'name = "two"'],
        )

    def test_discovers_core_and_extra_campaign_labels(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "bazelmod").mkdir()
            (root / "bazelmod" / "extras.MODULE.bazel").write_text(
                'local_path_override(module_name = "xff_arc", path = "extra_modules/arc")',
                encoding="utf-8",
            )
            (root / "xff" / "parser").mkdir(parents=True)
            (root / "xff" / "parser" / "BUILD.bazel").write_text(
                'cc_fuzz_test(name = "parser_fuzz_test", corpus = glob(["corpus/**"]))',
                encoding="utf-8",
            )
            (root / "extra_modules" / "arc" / "nested").mkdir(parents=True)
            (root / "extra_modules" / "arc" / "nested" / "BUILD.bazel").write_text(
                'cc_fuzz_test(name = "archive_fuzz_test")', encoding="utf-8"
            )
            self.assertEqual(
                fuzz_targets.discover_targets(root),
                ["//xff/parser:parser_fuzz_test_run", "@xff_arc//nested:archive_fuzz_test_run"],
            )

    def test_repository_discovery_parses_the_live_build_graph(self):
        # A lower bound catches an accidentally narrowed scan without making every newly declared
        # fuzzer require a hand-maintained count here: BUILD files remain the source of truth.
        self.assertGreaterEqual(len(fuzz_targets.discover_targets(fuzz_targets._REPO_ROOT)), 6)

    def test_campaign_duration_tracks_target_count(self):
        self.assertEqual(fuzz_targets.campaign_duration_seconds(["one", "two", "three"], 60, 180), 180)

    def test_campaign_duration_rejects_nonpositive_seconds(self):
        with self.assertRaisesRegex(ValueError, "must be positive"):
            fuzz_targets.campaign_duration_seconds(["one"], 0)

    def test_campaign_duration_rejects_over_budget_target_growth(self):
        with self.assertRaisesRegex(ValueError, "240 seconds, exceeding the 180-second budget"):
            fuzz_targets.campaign_duration_seconds(["one", "two", "three", "four"], 60, 180)

    @mock.patch("fuzz_targets.subprocess.run")
    def test_campaign_passes_the_dedicated_cache_flags_to_bazel(self, run):
        run.return_value.returncode = 0
        self.assertEqual(
            fuzz_targets.run_campaigns(
                ["//xff/parser:parser_fuzz_test_run"],
                30,
                ["--disk_cache=/cache", "--experimental_disk_cache_gc_max_size=600M"],
            ),
            0,
        )
        command = run.call_args.args[0]
        self.assertIn("--disk_cache=/cache", command)
        self.assertIn("--experimental_disk_cache_gc_max_size=600M", command)


if __name__ == "__main__":
    unittest.main()
