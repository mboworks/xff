# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Check repository tooling against explicit Bazel runfiles or the direct checkout."""

from pathlib import Path
import shutil
import tempfile
import unittest

import build_rules
import coverage_sources
import extras
import fuzz_targets
from test_paths import repository_file


class RepositoryToolingTest(unittest.TestCase):
    def test_repository_discovery_parses_core_build_files(self):
        # Use actual core BUILD files while keeping the scan inside a declared fixture tree.
        packages = (
            "xff/config", "xff/engine", "xff/fuzzy", "xff/matching/similarity",
            "xff/parser", "xff/presentation/fields",
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = [f"{package}/BUILD.bazel" for package in packages]
            inputs.append("bazelmod/extras.MODULE.bazel")
            for relative in inputs:
                destination = root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(repository_file(relative), destination)
            self.assertGreaterEqual(len(fuzz_targets.discover_targets(root)), 6)

    def test_maps_every_extra_in_the_repository_registry(self):
        modules = extras.extras(repository_file("bazelmod/extras.MODULE.bazel").read_text())
        self.assertTrue(modules)
        report = "".join(f"SF:external/{module}+/source.cc\n" for module in modules)
        expected = "".join(f"SF:{path}/source.cc\n" for path in modules.values())
        self.assertEqual(expected, coverage_sources.remap(report, modules))

    def test_python_suites_have_bazel_owners(self):
        tools = repository_file("tools")
        owners = {rule.name for rule in build_rules.parse_rules((tools / "BUILD.bazel").read_text())
                  if rule.kind == "py_test"}
        suites = {path.stem for path in tools.glob("*_test.py")}
        self.assertEqual(suites, owners)


if __name__ == "__main__":
    unittest.main()
