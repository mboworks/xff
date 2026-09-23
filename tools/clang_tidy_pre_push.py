#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Run the full pre-commit suite once on the combined pushed-file list."""

import subprocess
import sys
from pathlib import Path


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], text=True)


def pushed_files(updates: str) -> list[str]:
    files: set[str] = set()
    for line in updates.splitlines():
        _, local, _, remote = line.split()
        if set(local) == {"0"}:
            continue  # Deleting a remote branch has no source to lint.
        if set(remote) == {"0"}:
            commits = git("rev-list", "--reverse", "--topo-order", local, "--not", "--remotes").splitlines()
            if not commits:
                continue
            parents = git("rev-list", "--parents", "-n1", commits[0]).split()[1:]
            if not parents:
                files.update(git("ls-tree", "-r", "--name-only", "-z", local).split("\0"))
                continue
            remote = parents[0]
        files.update(git("diff", "--name-only", "--diff-filter=ACMR", "-z", remote, local).split("\0"))
    return sorted(files - {""})


def main() -> int:
    files = pushed_files(Path(sys.argv[1]).read_text(encoding="utf-8"))
    if not files:
        return 0
    return subprocess.call(["pre-commit", "run", "--hook-stage", "pre-push", "--files", *files])


if __name__ == "__main__":
    sys.exit(main())
