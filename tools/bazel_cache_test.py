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
    def test_preserves_recent_files_and_bounds_upload(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            files = [("cas/old", 6, 1), ("ac/old", 2, 2), ("cas/new", 7, 3), ("ac/new", 1, 4)]
            for name, size, age in files:
                path = root / name
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(b"x" * size)
                os.utime(path, (age, age))
            self.assertEqual(bazel_cache.trim(root, 8), (16, 8, 2))
            self.assertEqual((root / "cas/new").read_bytes(), b"xxxxxxx")
            self.assertEqual((root / "ac/new").read_bytes(), b"x")
            self.assertEqual(bazel_cache.trim(root, 8), (8, 8, 0))

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
