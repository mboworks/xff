# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Check the live repository using bazel run's explicit workspace directory."""

import argparse
import os
from pathlib import Path
import re
import shlex
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

    def test_linux_perf_config_is_explicitly_opt_in(self):
        commands = [shlex.split(line, comments=True)
                    for line in (self.workspace / ".bazelrc").read_text().splitlines()]
        commands = [command for command in commands if command]
        settings = [option for command in commands if command[0] == "common:linux_perf"
                    for option in command[1:]]
        self.assertEqual(settings, ["--define=pfm=1",
                                    "--downloader_config=bazelmod/linux_perf_downloader.cfg"])
        for command in commands:
            if command[0] != "common:linux_perf":
                self.assertNotIn("--define=pfm=1", command)
                self.assertNotIn("--config=linux_perf", command)

    def test_libpfm_downloader_rewrites_only_the_retired_archive(self):
        config = self.workspace / "bazelmod/linux_perf_downloader.cfg"
        directives = [line.split() for line in config.read_text().splitlines()
                      if line.strip() and not line.lstrip().startswith("#")]
        self.assertEqual(len(directives), 1)
        directive, pattern, replacement = directives[0]
        self.assertEqual(directive, "rewrite")
        retired = "netcologne.dl.sourceforge.net/project/perfmon2/libpfm4/libpfm-4.11.0.tar.gz"
        canonical = "https://downloads.sourceforge.net/project/perfmon2/libpfm4/libpfm-4.11.0.tar.gz"
        self.assertIsNotNone(re.fullmatch(pattern, retired))
        self.assertEqual(replacement, canonical)
        for unrelated in (retired.replace("4.11.0", "4.13.0"),
                          retired.replace("netcologne", "another-mirror"),
                          retired.replace("libpfm4", "another-project"),
                          retired + "?download=1", canonical.removeprefix("https://")):
            self.assertIsNone(re.fullmatch(pattern, unrelated))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=os.environ.get("BUILD_WORKSPACE_DIRECTORY"))
    args, unittest_args = parser.parse_known_args()
    if args.workspace is None:
        parser.error("run with bazel run, or supply --workspace=PATH")
    RepositoryToolingTest.workspace = args.workspace.resolve(strict=True)
    unittest.main(argv=[__file__, *unittest_args])
