#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Historical recovery must not authorize other release failures or altered artifacts."""

import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile

import release_backfill as backfill


class ReleaseBackfillTest(unittest.TestCase):
    def setUp(self):
        self.run = {
            "id": backfill.SOURCE_RUN, "head_sha": backfill.SOURCE_SHA,
            "head_branch": backfill.TAG, "event": "push",
            "path": ".github/workflows/release.yml", "status": "completed",
            "conclusion": "failure",
        }
        self.jobs = [{"name": name, "conclusion": conclusion} for name, conclusion in (
            ("validate", "success"), ("build linux", "success"), ("build macos", "success"),
            ("coverage", "failure"), ("release", "skipped"),
        )]
        self.artifacts = [
            {"id": index, "name": f"xff-{platform}", "expired": False,
             "size_in_bytes": 100, "digest": "sha256:" + "a" * 64,
             "workflow_run": {"id": backfill.SOURCE_RUN, "head_sha": backfill.SOURCE_SHA}}
            for index, platform in enumerate(("linux", "macos"), start=1)
        ]
        self.log = (
            "Golden:  'xff 0.0.0'\nResult:  'xff 0.9.0'\n"
            "[  FAIL  ] rg_invocation_selects_short_options_help_and_errors\n"
            "Executed 84 out of 298 tests: 297 tests pass and 1 fails locally.\n"
        )

    def validate(self, tag=backfill.TAG, sha=backfill.SOURCE_SHA):
        return backfill.validate(tag, sha, self.run, self.jobs, self.artifacts, self.log)

    def test_approved_recovery_records_source_and_exception(self):
        metadata = self.validate()
        self.assertEqual(metadata["source_sha"], backfill.SOURCE_SHA)
        self.assertEqual(metadata["source_run"], backfill.SOURCE_RUN)
        self.assertEqual(metadata["exception"], backfill.REASON)
        self.assertEqual(metadata["artifacts"]["macos"]["id"], 2)

    def test_different_tags_and_moved_tag_are_rejected(self):
        for tag, sha in (("v0.9.1", backfill.SOURCE_SHA), (backfill.TAG, "b" * 40)):
            with self.subTest(tag=tag, sha=sha), self.assertRaises(ValueError):
                self.validate(tag, sha)

    def test_different_source_run_identity_is_rejected(self):
        original = copy.deepcopy(self.run)
        for key in original:
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.run = {**original, key: "different"}
                self.validate()

    def test_failed_missing_extra_and_duplicate_jobs_are_rejected(self):
        original = copy.deepcopy(self.jobs)
        variants = [original[:-1], original + [original[0]],
                    original + [{"name": "another gate", "conclusion": "failure"}]]
        for index in (0, 1, 2):
            changed = copy.deepcopy(original)
            changed[index]["conclusion"] = "failure"
            variants.append(changed)
        for jobs in variants:
            with self.subTest(jobs=jobs), self.assertRaises(ValueError):
                self.jobs = jobs
                self.validate()

    def test_log_must_confirm_the_exact_assertion_and_single_failed_target(self):
        original = self.log
        for line in original.splitlines():
            with self.subTest(line=line), self.assertRaises(ValueError):
                self.log = original.replace(line, "different failure")
                self.validate()

    def test_invalid_artifacts_are_rejected(self):
        original = copy.deepcopy(self.artifacts)
        for key, value in (("expired", True), ("digest", None), ("size_in_bytes", 0),
                           ("workflow_run", {"id": backfill.SOURCE_RUN, "head_sha": "b" * 40})):
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.artifacts = copy.deepcopy(original)
                self.artifacts[0][key] = value
                self.validate()
        for artifacts in (original[:1], original + [original[0]]):
            with self.subTest(artifacts=artifacts), self.assertRaises(ValueError):
                self.artifacts = artifacts
                self.validate()

    def test_extract_requires_original_digest_and_exact_filenames(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "artifact.zip"
            names = [f"{binary}-linux-x86_64{suffix}"
                     for binary in ("xff", "xff_full") for suffix in ("", ".tar.zst")]
            for extra in (None, "../escape", "unexpected", names[0]):
                with self.subTest(extra=extra):
                    with zipfile.ZipFile(archive, "w") as zipped:
                        for name in names:
                            zipped.writestr(name, name.encode())
                        if extra:
                            # Exercise duplicate ZIP entries without suppressing warnings.
                            if extra == names[0]:
                                with self.assertWarns(UserWarning):
                                    zipped.writestr(extra, b"extra")
                            else:
                                zipped.writestr(extra, b"extra")
                    metadata = self.validate()
                    with self.assertRaisesRegex(ValueError, "digest"):
                        backfill.extract(archive, root / "output", "linux", metadata)
                    metadata["artifacts"]["linux"]["digest"] = (
                        "sha256:" + hashlib.sha256(archive.read_bytes()).hexdigest())
                    if extra:
                        with self.assertRaisesRegex(ValueError, "exactly"):
                            backfill.extract(archive, root / "output", "linux", metadata)
                    else:
                        backfill.extract(archive, root / "output", "linux", metadata)
                        self.assertEqual((root / "output" / names[0]).read_bytes(), names[0].encode())
            self.assertFalse((root / "escape").exists())


if __name__ == "__main__":
    unittest.main()
