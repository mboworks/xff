#!/usr/bin/env python3
"""Rejects unannotated host-file I/O in xff production C++ sources."""

from __future__ import annotations

import pathlib
import re
import sys


MARKER = "XFF_HOST_IO:"
PATTERNS = (
    re.compile(r"\bstd::(?:i|o|io)fstream\b"),
    re.compile(r"\bmbo::file::Artefact::Read(?:MaxLines)?\s*\("),
    re.compile(r"\b(?:fopen|freopen|fclose|fread|fwrite)\s*\("),
    re.compile(
        r"\b(?:std::filesystem|stdfs)::(?:create_directory|create_directories|remove|remove_all|rename|status)\s*\("
    ),
    re.compile(r"::(?:access|lstat|stat)\s*\("),
)


# Mutating host calls must remain in the authoritative adapter. An annotation alone
# cannot authorize a new write path. libarchive may write only an already authorized fd.
MUTATIONS = (
    re.compile(r"\b(?:std::)?(?:ofstream|fstream)\b"),
    re.compile(r"\b(?:std::filesystem|stdfs|fs)::(?:create_directory|create_directories|remove|remove_all|rename|permissions|resize_file|copy|copy_file|create_symlink|create_hard_link)\s*\("),
    re.compile(r"(?<![\w.>])(?:::)?(?:fopen|freopen|fwrite|creat|unlink|unlinkat|rename|renameat|link|linkat|mkdir|mkdirat|rmdir|truncate|ftruncate|chmod|fchmod|mkstemp|mkdtemp|pwrite)\s*\("),
    re.compile(r"\bO_(?:WRONLY|RDWR|CREAT|TRUNC|APPEND)\b"),
    re.compile(r"\barchive_write_open_filename\w*\s*\("),
    re.compile(r"::write\s*\("),
)
ROOT = pathlib.Path(__file__).resolve().parent.parent
MUTATION_ADAPTER = ROOT / "xff_extras_api/mutations.cc"
PIPE_ADAPTER = ROOT / "xff/cli/pager.cc"


def check(path: pathlib.Path) -> list[str]:
    if path.name.endswith("_test.cc") or path.name.endswith("_test.h"):
        return []
    lines = path.read_text(encoding="utf-8").splitlines()
    errors: list[str] = []
    for number, line in enumerate(lines, start=1):
        if line.lstrip().startswith(("//", "#")):
            continue
        mutation = any(pattern.search(line) for pattern in MUTATIONS)
        pipe_write = path.resolve() == PIPE_ADAPTER and "::write(write_fd," in line
        if mutation and path.resolve() != MUTATION_ADAPTER and not pipe_write:
            errors.append(f"{path}:{number}: host mutation must route through xff/vfs/mutations.h")
            continue
        if not any(pattern.search(line) for pattern in PATTERNS):
            continue
        previous = lines[number - 2] if number > 1 else ""
        if MARKER not in previous and MARKER not in line:
            errors.append(f"{path}:{number}: unannotated host file I/O (add {MARKER} <reason>)")
    return errors


def main(argv: list[str]) -> int:
    paths = [pathlib.Path(value) for value in argv]
    errors = [error for path in paths for error in check(path)]
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
