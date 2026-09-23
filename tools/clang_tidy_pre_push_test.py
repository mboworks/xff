#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

import unittest
from unittest.mock import patch

from tools import clang_tidy_pre_push


class ClangTidyPrePushTest(unittest.TestCase):
    def test_push_runs_entire_suite_once_and_propagates_failure(self):
        with patch.object(clang_tidy_pre_push.sys, "argv", ["hook", "updates"]), \
                patch.object(clang_tidy_pre_push.Path, "read_text", return_value="updates"), \
                patch.object(clang_tidy_pre_push, "pushed_files", return_value=["a.cc", "docs.md"]), \
                patch.object(clang_tidy_pre_push.subprocess, "call", return_value=1) as call:
            self.assertEqual(clang_tidy_pre_push.main(), 1)
            call.assert_called_once_with(
                ["pre-commit", "run", "--hook-stage", "pre-push", "--files", "a.cc", "docs.md"])

    def test_empty_push_does_not_run_hooks(self):
        with patch.object(clang_tidy_pre_push.sys, "argv", ["hook", "updates"]), \
                patch.object(clang_tidy_pre_push.Path, "read_text", return_value="updates"), \
                patch.object(clang_tidy_pre_push, "pushed_files", return_value=[]), \
                patch.object(clang_tidy_pre_push.subprocess, "call") as call:
            self.assertEqual(clang_tidy_pre_push.main(), 0)
            call.assert_not_called()

    def test_multiple_refs_are_combined_once_and_deleted_branches_are_ignored(self):
        updates = "refs/heads/a new-a refs/heads/a old-a\nrefs/heads/b new-b refs/heads/b old-b\n"
        updates += f"refs/heads/gone {'0' * 40} refs/heads/gone old-c\n"
        with patch.object(clang_tidy_pre_push, "git", side_effect=["xff/a.cc\0xff/shared.h\0", "xff/b.cc\0xff/shared.h\0"]) as git:
            self.assertEqual(clang_tidy_pre_push.pushed_files(updates), ["xff/a.cc", "xff/b.cc", "xff/shared.h"])
            self.assertEqual(git.call_count, 2)
            git.assert_any_call("diff", "--name-only", "--diff-filter=ACMR", "-z", "old-a", "new-a")

    def test_new_branch_only_selects_changes_since_unpublished_history_started(self):
        updates = f"refs/heads/new tip refs/heads/new {'0' * 40}\n"
        with patch.object(clang_tidy_pre_push, "git", side_effect=["first\ntip\n", "first base\n", "xff/new.cc\0"]) as git:
            self.assertEqual(clang_tidy_pre_push.pushed_files(updates), ["xff/new.cc"])
            git.assert_called_with("diff", "--name-only", "--diff-filter=ACMR", "-z", "base", "tip")

    def test_already_published_new_branch_has_nothing_to_lint(self):
        updates = f"refs/heads/new tip refs/heads/new {'0' * 40}\n"
        with patch.object(clang_tidy_pre_push, "git", return_value=""):
            self.assertEqual(clang_tidy_pre_push.pushed_files(updates), [])

    def test_initial_repository_push_selects_its_files(self):
        updates = f"refs/heads/main tip refs/heads/main {'0' * 40}\n"
        with patch.object(clang_tidy_pre_push, "git", side_effect=["root\ntip\n", "root\n", "xff/new.cc\0"]) as git:
            self.assertEqual(clang_tidy_pre_push.pushed_files(updates), ["xff/new.cc"])
            git.assert_called_with("ls-tree", "-r", "--name-only", "-z", "tip")


if __name__ == "__main__":
    unittest.main()
