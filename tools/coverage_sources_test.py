#!/usr/bin/env python3
"""Tests for tools/coverage_sources.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_sources  # noqa: E402


class CoverageSourcesTest(unittest.TestCase):
    def test_policy_discovers_a_future_extra_without_a_policy_edit(self):
        policy = {"include": ["xff/**"], "categories": {"extensions": {}}}
        actual = coverage_sources.resolved_policy(
            policy, {"xff_future": "extra_modules/future"}
        )
        self.assertIn("xff_future/**", actual["include"])
        self.assertEqual(
            {"include": ["xff_future/**"]},
            actual["categories"]["extensions"]["Future"],
        )
        self.assertNotIn("xff_future/**", policy["include"])

    def test_data_only_extra_stays_in_scope_without_an_empty_metric_row(self):
        policy = {
            "include": ["xff/**"],
            "data_only_extras": ["xff_data"],
            "categories": {"extensions": {}},
        }
        actual = coverage_sources.resolved_policy(
            policy, {"xff_data": "extra_modules/data"}
        )
        self.assertIn("xff_data/**", actual["include"])
        self.assertNotIn("Data", actual["categories"]["extensions"])

    def test_maps_every_extra_in_the_repository_registry(self):
        modules = coverage_sources.declared_extras()
        report = "".join(f"SF:external/{module}+/source.cc\n" for module in modules)
        expected = "".join(f"SF:{path}/source.cc\n" for path in modules.values())
        self.assertEqual(expected, coverage_sources.remap(report, modules))

    def test_maps_a_future_extra_without_a_mapper_change(self):
        report = (
            "SF:xff/cli/main.cc\n"
            "SF:external/xff_future+/future.cc\n"
        )
        self.assertEqual(
            "SF:xff/cli/main.cc\n"
            "SF:extra_modules/future/future.cc\n",
            coverage_sources.remap(
                report,
                {
                    "xff_future": "extra_modules/future",
                },
            ),
        )

    def test_groups_sources_by_the_policy_module(self):
        policy = {
            "include": ["xff/**", "xff_archive/**"],
            "exclude": ["**/*_test.cc"],
            "categories": {
                "program": {"command line": {"include": ["xff/cli/**"]}},
                "extensions": {"archive": {"include": ["xff_archive/**"]}},
            },
        }
        report = (
            "SF:xff/cli/main.cc\nDA:1,1\nend_of_record\n"
            "SF:extra_modules/archive/archive_fs.cc\nDA:1,1\nend_of_record\n"
            "SF:xff/cli/main_test.cc\nDA:1,1\nend_of_record\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            actual = coverage_sources.grouped(
                report, {"xff_archive": "extra_modules/archive"}, policy, root
            )
            self.assertIn(f"SF:{root}/program-command-line/xff/cli/main.cc\n", actual)
            self.assertIn(
                f"SF:{root}/extensions-archive/xff_archive/archive_fs.cc\n", actual
            )
            self.assertNotIn("main_test.cc", actual)
            self.assertTrue((root / "program-command-line/xff/cli/main.cc").is_symlink())

    def test_grouping_ignores_data_only_lcov_records(self):
        policy = {
            "include": ["xff_data/**"],
            "data_only_extras": ["xff_data"],
            "categories": {"extensions": {}},
        }
        report = "SF:extra_modules/data/data.json\nFNF:0\nFNH:0\nLH:0\nLF:0\nend_of_record\n"
        with tempfile.TemporaryDirectory() as directory:
            actual = coverage_sources.grouped(
                report, {"xff_data": "extra_modules/data"}, policy, Path(directory)
            )
        self.assertEqual("", actual)

    def test_rejects_a_source_without_exactly_one_policy_module(self):
        policy = {
            "include": ["xff/**"],
            "categories": {
                "one": {"first": {"include": ["xff/**"]}},
                "two": {"second": {"include": ["xff/cli/**"]}},
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "belongs to 2 policy categories"):
                coverage_sources.grouped(
                    "SF:xff/cli/main.cc\nDA:1,1\nend_of_record\n",
                    {},
                    policy,
                    Path(directory),
                )

    def test_applies_exclusions_and_merges_repeated_template_records(self):
        policy = {
            "include": ["xff/**"],
            "categories": {"program": {"file": {"include": ["xff/file/**"]}}},
        }
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            source = workspace / "xff/file/file.h"
            source.parent.mkdir(parents=True)
            source.write_text(
                "int excluded() {  // LCOV_EXCL_FUNC_LINE, LCOV_EXCL_LINE\n"
                "int shared() {  // LCOV_MERGE_FUNC_LINE\n"
                "return true;  // LCOV_MERGE_BR_LINE 2\n"
            )
            report = (
                "SF:xff/file/file.h\n"
                "FN:1,excluded\nFN:2,shared_int\nFN:2,shared_long\n"
                "FNDA:0,excluded\nFNDA:3,shared_int\nFNDA:0,shared_long\nFNF:3\nFNH:1\n"
                "BRDA:3,0,0,2\nBRDA:3,0,1,0\nBRDA:3,0,2,0\nBRDA:3,0,3,4\nBRF:4\nBRH:2\n"
                "DA:1,0\nDA:2,3\nDA:3,3\nLF:3\nLH:2\nend_of_record\n"
            )

            actual = coverage_sources.grouped(
                report, {}, policy, Path(directory) / "grouped", workspace
            )

            self.assertNotIn("excluded", actual)
            self.assertIn("FN:2,__xff_lcov_merged_function_at_line_2", actual)
            self.assertIn("FNDA:3,__xff_lcov_merged_function_at_line_2", actual)
            self.assertIn("FNF:1\nFNH:1", actual)
            self.assertIn("BRDA:3,0,0,2\nBRDA:3,0,1,4\nBRF:2\nBRH:2", actual)
            self.assertIn("DA:2,3\nDA:3,3\nLF:2\nLH:2", actual)

    def test_rejects_an_invalid_branch_merge_width(self):
        policy = {
            "include": ["xff/**"],
            "categories": {"program": {"file": {"include": ["xff/file/**"]}}},
        }
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            source = workspace / "xff/file/file.h"
            source.parent.mkdir(parents=True)
            source.write_text("return true;  // LCOV_MERGE_BR_LINE 2\n")
            with self.assertRaisesRegex(ValueError, "cannot merge 3 branch records"):
                coverage_sources.grouped(
                    "SF:xff/file/file.h\n"
                    "BRDA:1,0,0,1\nBRDA:1,0,1,0\nBRDA:1,0,2,0\n"
                    "BRF:3\nBRH:1\nend_of_record\n",
                    {},
                    policy,
                    Path(directory) / "grouped",
                    workspace,
                )

    def test_excludes_explicit_source_blocks(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.cc"
            source.write_text(
                "int covered = 1;\n"
                "// LCOV_EXCL_START: requires process termination.\n"
                "int unreachable = 2;\n"
                "// LCOV_EXCL_STOP\n"
                "int covered_too = 3;\n",
                encoding="utf-8",
            )
            actual = coverage_sources.normalize_record(
                "SF:source.cc\nDA:1,1\nDA:2,0\nDA:3,0\nDA:4,0\nDA:5,1\nLF:5\nLH:2\nend_of_record\n",
                source,
            )
            self.assertIn("DA:1,1\nDA:5,1\nLF:2\nLH:2", actual)

    def test_excludes_blocks_without_exposing_lcov_markers_to_genhtml(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.cc"
            source.write_text(
                "int covered = 1;\n"
                "// XFF_UNSTABLE_COVERAGE_START: asynchronous kernel callback.\n"
                "int nondeterministically_covered = 2;\n"
                "// XFF_UNSTABLE_COVERAGE_STOP\n",
                encoding="utf-8",
            )
            actual = coverage_sources.normalize_record(
                "SF:source.cc\n"
                "FN:3,nondeterministic\n"
                "FNDA:1,nondeterministic\n"
                "FNF:1\n"
                "FNH:1\n"
                "DA:1,1\n"
                "DA:2,1\n"
                "DA:3,1\n"
                "DA:4,1\n"
                "LF:4\n"
                "LH:4\n"
                "end_of_record\n",
                source,
            )
            self.assertIn("FNF:0\nFNH:0\nBRF:0\nBRH:0\nDA:1,1\nLF:1\nLH:1", actual)
            self.assertNotIn("nondeterministic", actual)
            self.assertNotIn("LCOV_EXCL", source.read_text(encoding="utf-8"))

    def test_rejects_unbalanced_source_exclusion_blocks(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.cc"
            source.write_text("// LCOV_EXCL_START\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "has no LCOV_EXCL_STOP"):
                coverage_sources.normalize_record("SF:source.cc\nDA:1,0\nend_of_record\n", source)


if __name__ == "__main__":
    unittest.main()
