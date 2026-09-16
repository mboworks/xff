#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Regression checks for disk eviction and immutable GitHub cache lifecycle."""

import os
from pathlib import Path
import tempfile
import unittest

import bazel_cache


class BazelCacheTest(unittest.TestCase):
    def test_preserves_small_outputs_over_new_large_executable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            files = [("cas/object", 6, 1), ("ac/object", 2, 2),
                     ("cas/executable", 20, 3), ("ac/executable", 1, 4)]
            for name, size, age in files:
                path = root / name
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(b"x" * size)
                os.utime(path, (age, age))
            self.assertEqual(bazel_cache.trim(root, 9), (29, 9, 1))
            self.assertEqual((root / "cas/object").read_bytes(), b"xxxxxx")
            self.assertEqual((root / "ac/object").read_bytes(), b"xx")
            self.assertFalse((root / "cas/executable").exists())
            self.assertEqual(bazel_cache.trim(root, 9), (9, 9, 0))

    def test_equal_sizes_evict_oldest_first(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "cas").mkdir()
            for name, age in (("old", 1), ("new", 2)):
                path = root / "cas" / name
                path.write_bytes(b"1234")
                os.utime(path, (age, age))
            self.assertEqual(bazel_cache.trim(root, 4), (8, 4, 1))
            self.assertEqual((root / "cas/new").read_bytes(), b"1234")

    def test_missing_cache_is_harmless(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.assertEqual(bazel_cache.trim(Path(temporary) / "missing"), (0, 0, 0))

    def test_does_not_follow_symlinks_or_touch_other_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "cas").mkdir()
            (root / "keep").write_bytes(b"untouched")
            (root / "cas/link").symlink_to(root / "keep")
            self.assertEqual(bazel_cache.trim(root, 0), (0, 0, 0))
            self.assertEqual((root / "keep").read_bytes(), b"untouched")

    def test_workflows_refresh_only_main_and_release_only_restores(self):
        root = Path(__file__).resolve().parent.parent
        workflow = (root / ".github/workflows/main.yml").read_text()
        restores = workflow.count("uses: ./.github/actions/bazel-cache-restore")
        saves = workflow.count("uses: ./.github/actions/bazel-cache-save")
        self.assertEqual(restores, 9)
        self.assertEqual(saves, restores)
        self.assertEqual(workflow.count("!cancelled() && github.ref == 'refs/heads/main'"), saves)
        release = (root / ".github/workflows/release.yml").read_text()
        self.assertEqual(release.count("uses: ./.github/actions/bazel-cache-restore"), 2)
        self.assertNotIn("actions/cache/save", release)
        self.assertNotIn("actions/bazel-cache-save", release)
        self.assertNotIn("actions/cache@", release)
        deep_fuzz = (root / ".github/workflows/fuzz.yml").read_text()
        self.assertIn("uses: ./.github/actions/bazel-cache-restore", deep_fuzz)
        self.assertIn("namespace: fuzz", deep_fuzz)
        self.assertIn("uses: ./.github/actions/bazel-cache-save", deep_fuzz)
        self.assertIn("!cancelled() && github.ref == 'refs/heads/main'", deep_fuzz)
        self.assertNotIn("actions/cache@", deep_fuzz)
        self.assertNotIn("--disk-cache-max-size", deep_fuzz)
        self.assertIn('workflows: [Test, Release, Deep fuzz]',
                      (root / ".github/workflows/cache_size.yml").read_text())
        reference_build = release[release.index("bazel build"):release.index("cp bazel-bin/xff/cli/XFF.md.actual")]
        self.assertIn("--disk_cache=", reference_build)
        restore = (root / ".github/actions/bazel-cache-restore/action.yml").read_text()
        self.assertIn("github.run_id", restore)
        self.assertIn("github.run_attempt", restore)
        self.assertIn("runner.arch", restore)
        self.assertIn("actions/cache/restore@", restore)
        save = (root / ".github/actions/bazel-cache-save/action.yml").read_text()
        self.assertLess(save.index("bazel shutdown"), save.index("tools/bazel_cache.py"))
        self.assertLess(save.index("tools/bazel_cache.py"), save.index("actions/cache/save@"))


if __name__ == "__main__":
    unittest.main()
