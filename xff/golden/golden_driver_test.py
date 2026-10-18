# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Run the golden driver against directory and manifest-only runfiles."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from python.runfiles import runfiles


class GoldenDriverTest(unittest.TestCase):
    def test_runfiles(self):
        resolver = runfiles.Create()
        driver, bashtest, library = [resolver.Rlocation(key) for key in INPUT_KEYS]
        for manifest_only in (False, True):
            for ordered in (False, True):
                with self.subTest(manifest_only=manifest_only, ordered=ordered):
                    with tempfile.TemporaryDirectory() as temporary:
                        root = Path(temporary)
                        inputs = root / "external inputs with spaces"
                        inputs.mkdir()
                        setup = inputs / "setup.sh"
                        setup.write_text("touch entry\n", encoding="utf-8")
                        golden = inputs / "expected.txt"
                        golden.write_text("<ROOT>/entry\n", encoding="utf-8")
                        executable = inputs / "renamed executable"
                        executable.write_text(
                            '#!/usr/bin/env bash\nset -euo pipefail\n'
                            '[[ -f "$2/entry" ]]\nprintf "%s/entry\\n" "$2"\n',
                            encoding="utf-8",
                        )
                        executable.chmod(0o755)
                        entries = {
                            "bazel_tools/tools/bash/runfiles/runfiles.bash": library,
                            "framework+/bashtest.sh": bashtest,
                            "fixtures+/setup.sh": str(setup),
                            "fixtures+/find.txt": str(golden),
                            "fixtures+/xff.txt": str(golden),
                            "tools+/executable": str(executable),
                        }
                        env = {
                            key: value
                            for key, value in os.environ.items()
                            if not key.startswith("RUNFILES_")
                        }
                        env["TEST_WORKSPACE"] = "unrelated_workspace"
                        env["TEST_TMPDIR"] = str(root / "scratch")
                        tree = root / "runfiles"
                        env["TEST_SRCDIR"] = str(tree)
                        if manifest_only:
                            manifest = root / "MANIFEST"
                            manifest.write_text(
                                "".join(f"{key} {value}\n" for key, value in entries.items()),
                                encoding="utf-8",
                            )
                            env["RUNFILES_MANIFEST_FILE"] = str(manifest)
                            env["RUNFILES_MANIFEST_ONLY"] = "1"
                        else:
                            for key, value in entries.items():
                                target = tree / key
                                target.parent.mkdir(parents=True, exist_ok=True)
                                target.symlink_to(value)
                            env["RUNFILES_DIR"] = str(tree)
                        result = subprocess.run(
                            [
                                "bash", driver, "framework+/bashtest.sh",
                                "fixtures+/setup.sh", "fixtures+/find.txt",
                                "fixtures+/xff.txt", "tools+/executable",
                                "1" if ordered else "0", "--", "<ROOT>",
                            ],
                            cwd=root,
                            env=env,
                            capture_output=True,
                            text=True,
                            check=False,
                            timeout=30,
                        )
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn("[  PASS  ] find_mode", result.stdout + result.stderr)
                        self.assertIn("[  PASS  ] xff_mode", result.stdout + result.stderr)


if __name__ == "__main__":
    INPUT_KEYS = sys.argv[1:4]
    unittest.main(argv=[sys.argv[0]])
