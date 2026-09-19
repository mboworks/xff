# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Checkout integration check; CI runs this against the complete source tree."""

import unittest
import fuzz_targets
import coverage_sources
import build_rules
from pathlib import Path


class RepositoryToolingTest(unittest.TestCase):
    def test_repository_discovery_parses_the_live_build_graph(self):
        # A lower bound catches an accidentally narrowed scan without making every newly declared
        # fuzzer require a hand-maintained count here: BUILD files remain the source of truth.
        self.assertGreaterEqual(len(fuzz_targets.discover_targets(fuzz_targets._REPO_ROOT)), 6)


    def test_maps_every_extra_in_the_repository_registry(self):
        modules = coverage_sources.declared_extras()
        report = "".join(f"SF:external/{module}+/source.cc\n" for module in modules)
        expected = "".join(f"SF:{path}/source.cc\n" for path in modules.values())
        self.assertEqual(expected, coverage_sources.remap(report, modules))


    def test_python_suites_have_bazel_owners(self):
        tools = Path(__file__).resolve().parent
        owners = {rule.name for rule in build_rules.parse_rules((tools / "BUILD.bazel").read_text())
                  if rule.kind == "py_test"}
        # This file deliberately checks the live checkout and runs in the CI tooling job.
        suites = {path.stem for path in tools.glob("*_test.py")
                  if path.name != "repository_tooling_test.py"}
        self.assertEqual(suites, owners)


if __name__ == "__main__":
    unittest.main()
