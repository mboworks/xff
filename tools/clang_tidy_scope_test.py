#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

import subprocess
import sys
import unittest
from unittest import mock
from pathlib import Path
from tempfile import TemporaryDirectory

from tools import clang_tidy_scope


class ClangTidyScopeTest(unittest.TestCase):
    def test_missing_requested_source_fails_instead_of_reporting_a_clean_run(self):
        with TemporaryDirectory() as directory:
            database = Path(directory) / "compile_commands.json"
            database.write_text('[{"file": "xff/old.cc"}]', encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(Path(clang_tidy_scope.__file__).resolve()),
                 str(database), "xff/new.cc"],
                cwd=directory, capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn("xff/new.cc", result.stderr)
            self.assertIn("./compile_commands-update.sh", result.stderr)

    def test_full_scan_selects_only_indexed_first_party_translation_units(self):
        database = [
            {"file": "xff/z.cc"},
            {"file": "xff/z.cc"},
            {"file": "xff/a.cc"},
            {"file": "xff_extras_api/api.cc"},
            {"file": "external/dependency.cc"},
            {"file": "bazel-out/generated.cc"},
            {"file": "/tmp/absolute.cc"},
            {"file": "xff/header.h"},
        ]
        self.assertEqual(
            clang_tidy_scope.select_sources(database, ["--all"], {}),
            ["xff/a.cc", "xff/z.cc", "xff_extras_api/api.cc"],
        )

    def test_full_scan_cli_does_not_walk_the_checkout(self):
        with TemporaryDirectory() as directory:
            database = Path(directory) / "compile_commands.json"
            database.write_text('[{"file": "xff/z.cc"}, {"file": "xff/a.cc"}]', encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(Path(clang_tidy_scope.__file__).resolve()),
                 str(database), "--all"],
                cwd=directory, capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.splitlines(), ["xff/a.cc", "xff/z.cc"])

    def test_source_changes_are_narrow_and_sorted(self):
        database = [{"file": "xff/z.cc"}, {"file": "xff/a.cc"}, {"file": "external/x.cc"}]
        self.assertEqual(
            clang_tidy_scope.select_sources(database, ["xff/z.cc", "xff/a.cc"], {}),
            ["xff/a.cc", "xff/z.cc"],
        )

    def test_header_changes_select_transitive_dependents(self):
        database = [
            {"file": "xff/z.cc"},
            {"file": "xff/a.cc"},
            {"file": "external/x.cc"},
            {"file": "/tmp/absolute.cc"},
        ]
        self.assertEqual(
            clang_tidy_scope.select_sources(
                database,
                ["xff/a.h"],
                {
                    "xff/a.cc": {"xff/a.h"},
                    "xff/middle.h": {"xff/a.h"},
                    "xff/z.cc": {"xff/middle.h"},
                },
            ),
            ["xff/a.cc", "xff/z.cc"],
        )

    def test_unrelated_header_does_not_select_sources(self):
        database = [{"file": "xff/a.cc"}]
        self.assertEqual(clang_tidy_scope.select_sources(database, ["xff/other.h"], {}), [])

    def test_include_graph_resolves_workspace_and_relative_headers(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "xff" / "example"
            package.mkdir(parents=True)
            (package / "detail.h").write_text("", encoding="utf-8")
            (package / "public.h").write_text('#include "detail.h"\n', encoding="utf-8")
            (package / "use.cc").write_text(
                '#include "xff/example/public.h"\n', encoding="utf-8"
            )

            self.assertEqual(
                clang_tidy_scope.include_graph(root),
                {
                    "xff/example/detail.h": set(),
                    "xff/example/public.h": {"xff/example/detail.h"},
                    "xff/example/use.cc": {"xff/example/public.h"},
                },
            )

    def test_bazel_include_prefix_selects_transitive_owned_consumers(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "extra_modules/archive/archive_fs.h"
            header.parent.mkdir(parents=True)
            header.write_text("", encoding="utf-8")
            middle = root / "xff/middle.h"
            middle.parent.mkdir()
            middle.write_text('#include "xff/archive/archive_fs.h"\n', encoding="utf-8")
            (root / "xff/use.cc").write_text('#include "xff/middle.h"\n', encoding="utf-8")
            include = root / "bazel-out/config/bin/external/archive/_virtual_includes/api"
            virtual = include / "xff/archive/archive_fs.h"
            virtual.parent.mkdir(parents=True)
            virtual.write_text("", encoding="utf-8")
            database = [{"file": "xff/use.cc", "directory": str(root),
                         "arguments": ["clang", "-isystem", str(include)]}]
            resolve = Path.resolve

            def resolve_header(path, *args, **kwargs):
                # Model the compiler-generated mapping without creating repository symlinks.
                return resolve(header if path == virtual else path, *args, **kwargs)

            with mock.patch.object(Path, "resolve", resolve_header):
                aliases = clang_tidy_scope.virtual_include_aliases(database, root)
            self.assertEqual(aliases, {"xff/archive/archive_fs.h": {"extra_modules/archive/archive_fs.h"}})
            graph = clang_tidy_scope.include_graph(root, aliases)
            self.assertEqual(
                clang_tidy_scope.select_sources(database, ["extra_modules/archive/archive_fs.h"], graph),
                ["xff/use.cc"],
            )
            self.assertEqual(clang_tidy_scope.select_sources(database, ["xff/other.h"], graph), [])

    def test_virtual_aliases_accept_command_and_joined_options_and_exclude_dependencies(self):
        with TemporaryDirectory() as directory:
            root = Path(directory) / "workspace"
            root.mkdir()
            own = root / "xff/own.h"
            own.parent.mkdir()
            own.write_text("", encoding="utf-8")
            foreign = Path(directory) / "dependency/other.h"
            foreign.parent.mkdir()
            foreign.write_text("", encoding="utf-8")
            include = root / "bazel-out/_virtual_includes/with space"
            include.mkdir(parents=True)
            owned_alias = include / "owned.h"
            foreign_alias = include / "foreign.h"
            owned_alias.write_text("", encoding="utf-8")
            foreign_alias.write_text("", encoding="utf-8")
            database = [
                {"command": 'clang -I"bazel-out/_virtual_includes/with space"'},
                {"arguments": ["clang", "-iquote" + str(include)]},
                {"arguments": ["clang", "-isystem" + str(include)]},
            ]
            resolve = Path.resolve

            def resolve_header(path, *args, **kwargs):
                target = own if path == owned_alias else foreign if path == foreign_alias else path
                return resolve(target, *args, **kwargs)

            with mock.patch.object(Path, "resolve", resolve_header):
                self.assertEqual(
                    clang_tidy_scope.virtual_include_aliases(database, root),
                    {"owned.h": {"xff/own.h"}},
                )


if __name__ == "__main__":
    unittest.main()
