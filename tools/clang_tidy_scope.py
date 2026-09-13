#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Select translation units affected by files passed to clang-tidy."""

import json
import pathlib
import re
import sys

SOURCE_SUFFIXES = (".cc", ".cpp", ".cxx")
HEADER_SUFFIXES = (".h", ".h.in", ".hh", ".hpp", ".hxx", ".inc", ".ipp")
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)


def is_source(path: str) -> bool:
    return path.endswith(SOURCE_SUFFIXES)


def is_header(path: str) -> bool:
    return path.endswith(HEADER_SUFFIXES)


def database_sources(database: list[dict[str, object]]) -> set[str]:
    sources: set[str] = set()
    for entry in database:
        path = str(entry.get("file", ""))
        candidate = pathlib.PurePath(path)
        if candidate.is_absolute() or not is_source(path):
            continue
        if candidate.parts and candidate.parts[0] in {"bazel-out", "external"}:
            continue
        sources.add(path)
    return sources


def quoted_includes(path: pathlib.Path, root: pathlib.Path) -> set[str]:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return set()
    includes: set[str] = set()
    for spelling in INCLUDE_RE.findall(text):
        workspace_path = root / spelling
        relative_path = path.parent / spelling
        if workspace_path.is_file():
            includes.add(workspace_path.relative_to(root).as_posix())
        elif relative_path.is_file():
            includes.add(relative_path.relative_to(root).as_posix())
    return includes


def include_graph(root: pathlib.Path) -> dict[str, set[str]]:
    graph: dict[str, set[str]] = {}
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if path.is_file() and (is_source(relative) or is_header(relative)):
            graph[relative] = quoted_includes(path, root)
    return graph


def select_sources(
    database: list[dict[str, object]],
    changed: list[str],
    graph: dict[str, set[str]],
) -> list[str]:
    sources = database_sources(database)
    selected = {path for path in changed if path in sources}
    affected = {path for path in changed if is_header(path)}
    visited = set(affected)
    while affected:
        dependents = {
            path for path, includes in graph.items() if path not in visited and includes & affected
        }
        visited.update(dependents)
        selected.update(path for path in dependents if path in sources)
        affected = {path for path in dependents if is_header(path)}
    return sorted(selected)


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: clang_tidy_scope.py COMPILE_COMMANDS [CHANGED_FILE ...]")
    with open(sys.argv[1], encoding="utf-8") as stream:
        database = json.load(stream)
    changed = sys.argv[2:]
    indexed = database_sources(database)
    missing = sorted({path for path in changed if is_source(path) and path not in indexed})
    if missing:
        print(
            "clang-tidy: requested sources missing from compile_commands.json: "
            + ", ".join(missing)
            + "; refresh it with ./compile_commands-update.sh",
            file=sys.stderr,
        )
        return 1
    for source in select_sources(database, changed, include_graph(pathlib.Path.cwd())):
        print(source)
    return 0


if __name__ == "__main__":
    sys.exit(main())
