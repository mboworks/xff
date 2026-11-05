#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

import pathlib
import tempfile
import unittest

from tools import clang_tidy_compdb


def entry(path: str, configuration: str, *extra: str) -> dict[str, object]:
    return {
        "directory": "/checkout",
        "file": path,
        "arguments": [
            "clang++",
            "-std=c++20",
            *extra,
            f"-frandom-seed=bazel-out/{configuration}/bin/object.o",
            "-iquote",
            f"bazel-out/{configuration}/bin",
            "-MF",
            f"bazel-out/{configuration}/bin/object.d",
            "-c",
            path,
            "-o",
            f"bazel-out/{configuration}/bin/object.o",
        ],
    }


class ClangTidyCompdbTest(unittest.TestCase):
    def test_only_owned_system_includes_are_reclassified(self):
        root = pathlib.Path("/workspace parent/xff")
        owned = (
            "xff",
            "xff_extras_api",
            "extra_modules/archive",
            "external/xff_extras_api+",
            "bazel-out/platform/bin/external/xff_archive+/_virtual_includes/api",
        )
        foreign = (
            "external/mboworks_mbo+",
            "external/abseil-cpp+",
            "xff_near_miss",
            "xff_extras_api_near_miss",
            "/Library/Developer/SDK/usr/include",
        )
        for absolute in (False, True):
            for joined in (False, True):
                command = ["clang++", "-std=c++23", "-iquote", "."]
                expected = list(command)
                for name in (*owned, *foreign):
                    path = str(root / name) if absolute else name
                    option = "-I" if name in owned else "-isystem"
                    command.extend(["-isystem" + path] if joined else ["-isystem", path])
                    expected.extend([option + path] if joined else [option, path])
                with self.subTest(absolute=absolute, joined=joined):
                    self.assertEqual(clang_tidy_compdb.normalize_owned_includes(command, root, root),
                                     expected)

    def test_physical_bazel_alias_paths_match_without_matching_foreign_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary) / "xff"
            root.mkdir()
            external = pathlib.Path(temporary) / "bazel-external"
            external.mkdir()
            generated = pathlib.Path(temporary) / "bazel-generated"
            generated.mkdir()
            (root / "external").symlink_to(external, target_is_directory=True)
            (root / "bazel-out").symlink_to(generated, target_is_directory=True)
            paths = [external / "xff_archive+", external / "mboworks_mbo+",
                     generated / "platform/bin/external/xff_extras_api+/_virtual_includes/api",
                     generated / "platform/bin/external/abseil-cpp+"]
            command = [argument for path in paths for argument in ("-isystem", str(path))]
            expected = [argument for index, path in enumerate(paths)
                        for argument in ("-I" if index in (0, 2) else "-isystem", str(path))]
            self.assertEqual(clang_tidy_compdb.normalize_owned_includes(command, root, root), expected)

    def test_incomplete_system_include_option_is_preserved(self):
        root = pathlib.Path("/checkout")
        self.assertEqual(clang_tidy_compdb.normalize_owned_includes(["-isystem"], root, root),
                         ["-isystem"])

    def test_prefers_ordinary_entry_over_equivalent_fuzz_transition(self):
        ordinary = entry("mbo/file/glob.cc", "k8-fastbuild")
        fuzz = entry(
            "mbo/file/glob.cc",
            "k8-fastbuild-ST-deadbeef",
            clang_tidy_compdb.FUZZ_DEFINE,
        )

        filtered, removed = clang_tidy_compdb.deduplicate([ordinary, fuzz])

        self.assertEqual(filtered, [ordinary])
        self.assertEqual(removed, 1)

    def test_deduplicates_meaningfully_different_commands(self):
        ordinary = entry("mbo/file/glob.cc", "k8-fastbuild")
        different = entry(
            "mbo/file/glob.cc",
            "k8-fastbuild-ST-deadbeef",
            clang_tidy_compdb.FUZZ_DEFINE,
            "-DDIFFERENT_SEMANTICS",
        )

        filtered, removed = clang_tidy_compdb.deduplicate([different, ordinary])

        self.assertEqual(filtered, [ordinary])
        self.assertEqual(removed, 1)

    def test_retains_first_entry_when_all_duplicates_are_fuzz_commands(self):
        first = entry(
            "mbo/file/glob.cc",
            "k8-fastbuild-ST-first",
            clang_tidy_compdb.FUZZ_DEFINE,
        )
        second = entry(
            "mbo/file/glob.cc",
            "k8-fastbuild-ST-second",
            clang_tidy_compdb.FUZZ_DEFINE,
            "-DDIFFERENT_SEMANTICS",
        )

        filtered, removed = clang_tidy_compdb.deduplicate([first, second])

        self.assertEqual(filtered, [first])
        self.assertEqual(removed, 1)

    def test_retains_unique_entries(self):
        first = entry("mbo/file/file.cc", "k8-fastbuild")
        second = entry("mbo/log/demangle.cc", "k8-fastbuild")

        filtered, removed = clang_tidy_compdb.deduplicate([first, second])

        self.assertEqual(filtered, [first, second])
        self.assertEqual(removed, 0)


if __name__ == "__main__":
    unittest.main()
