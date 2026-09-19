# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Check the live repository using bazel run's explicit workspace directory."""

import argparse
import os
from pathlib import Path
import unittest

import build_rules
import coverage_sources
import extras
import fuzz_targets


class RepositoryToolingTest(unittest.TestCase):
    workspace: Path

    def test_repository_discovery_parses_the_live_build_graph(self):
        self.assertGreaterEqual(len(fuzz_targets.discover_targets(self.workspace)), 6)

    def test_maps_every_extra_in_the_repository_registry(self):
        registry = self.workspace / "bazelmod/extras.MODULE.bazel"
        modules = extras.extras(registry.read_text())
        self.assertTrue(modules)
        report = "".join(f"SF:external/{module}+/source.cc\n" for module in modules)
        expected = "".join(f"SF:{path}/source.cc\n" for path in modules.values())
        self.assertEqual(expected, coverage_sources.remap(report, modules))

    def test_python_suites_have_bazel_owners(self):
        tools = self.workspace / "tools"
        rules = build_rules.parse_rules((tools / "BUILD.bazel").read_text())
        owners = {rule.name for rule in rules if rule.kind == "py_test"}
        suites = {path.stem for path in tools.glob("*_test.py")
                  if path.name != "repository_tooling_test.py"}
        self.assertEqual(suites, owners)
        self.assertIn(("py_binary", "repository_tooling_test"),
                      {(rule.kind, rule.name) for rule in rules})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=os.environ.get("BUILD_WORKSPACE_DIRECTORY"))
    args, unittest_args = parser.parse_known_args()
    if args.workspace is None:
        parser.error("run with bazel run, or supply --workspace=PATH")
    RepositoryToolingTest.workspace = args.workspace.resolve(strict=True)
    unittest.main(argv=[__file__, *unittest_args])
