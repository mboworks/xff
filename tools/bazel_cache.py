#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Bound a stopped Bazel disk cache before uploading it to GitHub Actions."""

import argparse
from pathlib import Path


MAX_BYTES = 600_000_000


def trim(root: Path, max_bytes: int = MAX_BYTES) -> tuple[int, int, int]:
    """Evict oldest cache files; missing blobs are ordinary Bazel cache misses."""
    files = sorted((path.stat().st_mtime_ns, path.as_posix(), path.stat().st_size, path)
                   for part in ("ac", "cas") for path in (root / part).rglob("*")
                   if path.is_file() and not path.is_symlink())
    before = sum(size for _, _, size, _ in files)
    remaining = before
    removed = 0
    for _, _, size, path in files:
        if remaining <= max_bytes:
            break
        path.unlink()
        remaining -= size
        removed += 1
    return before, remaining, removed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache", type=Path)
    args = parser.parse_args()
    before, after, removed = trim(args.cache)
    print(f"Bazel disk cache: {before:,} -> {after:,} bytes; evicted {removed} files")


if __name__ == "__main__":
    main()
