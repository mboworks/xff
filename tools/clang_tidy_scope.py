#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Select translation units affected by files passed to clang-tidy."""

from __future__ import annotations

import json
import pathlib
import re
import shlex
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


def virtual_include_aliases(
    database: list[dict[str, object]], root: pathlib.Path
) -> dict[str, set[str]]:
    """Resolve Bazel's actual include-prefix headers, without a second alias catalog."""
    directories: set[pathlib.Path] = set()
    for entry in database:
        arguments = entry.get("arguments")
        if not isinstance(arguments, list):
            arguments = shlex.split(str(entry.get("command", "")))
        directory = pathlib.Path(str(entry.get("directory", root)))
        if not directory.is_absolute():
            directory = root / directory
        for index, argument in enumerate(arguments):
            spelling = ""
            for option in ("-isystem", "-iquote", "-I"):
                if argument == option and index + 1 < len(arguments):
                    spelling = str(arguments[index + 1])
                    break
                if argument.startswith(option) and len(argument) > len(option):
                    spelling = argument[len(option):]
                    break
            path = pathlib.Path(spelling)
            if "_virtual_includes" in path.parts:
                directories.add(path if path.is_absolute() else directory / path)
    aliases: dict[str, set[str]] = {}
    workspace = root.resolve()
    for directory in sorted(directories):
        for path in directory.rglob("*"):
            if not path.is_file() or not is_header(path.name):
                continue
            physical = path.resolve()
            if not physical.is_relative_to(workspace):
                continue
            relative = physical.relative_to(workspace)
            if relative.parts[0] in {"bazel-out", "external"}:
                continue
            spelling = path.relative_to(directory).as_posix()
            aliases.setdefault(spelling, set()).add(relative.as_posix())
    return aliases


def quoted_includes(
    path: pathlib.Path, root: pathlib.Path, aliases: dict[str, set[str]]
) -> set[str]:
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
        else:
            includes.update(aliases.get(spelling, set()))
    return includes


def include_graph(
    root: pathlib.Path, aliases: dict[str, set[str]] | None = None
) -> dict[str, set[str]]:
    graph: dict[str, set[str]] = {}
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if path.is_file() and (is_source(relative) or is_header(relative)):
            graph[relative] = quoted_includes(path, root, aliases or {})
    return graph


def select_sources(
    database: list[dict[str, object]],
    changed: list[str],
    graph: dict[str, set[str]],
) -> list[str]:
    sources = database_sources(database)
    if changed == ["--all"]:
        return sorted(sources)
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
        raise SystemExit("usage: clang_tidy_scope.py COMPILE_COMMANDS [--all | CHANGED_FILE ...]")
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
    root = pathlib.Path.cwd()
    graph = {} if changed == ["--all"] else include_graph(root, virtual_include_aliases(database, root))
    for source in select_sources(database, changed, graph):
        print(source)
    return 0


if __name__ == "__main__":
    sys.exit(main())
