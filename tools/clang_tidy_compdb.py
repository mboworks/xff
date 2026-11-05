#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Create a one-command-per-source clang-tidy database."""

import argparse
import json
import pathlib
import re
import shlex
import sys
from collections import defaultdict
from typing import Optional, Sequence


FUZZ_DEFINE = "-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION"


def arguments(entry: dict[str, object]) -> list[str]:
    raw_arguments = entry.get("arguments")
    if isinstance(raw_arguments, list):
        return [str(argument) for argument in raw_arguments]
    command = entry.get("command")
    if isinstance(command, str):
        return shlex.split(command)
    raise ValueError(f"compile command has neither arguments nor command: {entry!r}")


def owned_header_filter(repository: pathlib.Path) -> str:
    """Match XFF source/generated headers through relative and Bazel alias paths."""
    source = r"(xff/|xff_extras_api/|extra_modules/|external/xff_[^/]+/)"
    generated = r"[^/]+/bin/(xff/|xff_extras_api/|external/xff_[^/]+/)"
    owned = rf"({source}|bazel-out/{generated})"
    root = repository.absolute()
    patterns = [rf"(\./)?{owned}"]
    for spelling in sorted({root, root.resolve()}):
        patterns.append(re.escape(str(spelling)) + "/" + owned)
    for alias, suffix in ((root / "external", r"xff_[^/]+/"),
                          (root / "bazel-out", generated)):
        spellings = {alias.absolute(), alias.resolve()}
        if alias.is_symlink():
            target = alias.readlink()
            spellings.add(target.absolute() if target.is_absolute()
                          else (alias.parent / target).absolute())
        patterns.extend(re.escape(str(spelling)) + "/" + suffix for spelling in sorted(spellings))
    return "^(" + "|".join(patterns) + ")"


def normalize_owned_includes(
    command: Sequence[str], repository: pathlib.Path, directory: pathlib.Path
) -> list[str]:
    """Make first-party Bazel system includes visible only for this lint invocation."""
    pattern = re.compile(owned_header_filter(repository))
    result = []
    index = 0
    while index < len(command):
        argument = command[index]
        if argument == "-isystem" and index + 1 < len(command):
            include = command[index + 1]
            path = pathlib.Path(include)
            absolute = path if path.is_absolute() else directory / path
            result.extend(("-I" if pattern.match(str(absolute) + "/") else argument, include))
            index += 2
        elif argument.startswith("-isystem") and len(argument) > len("-isystem"):
            include = argument[len("-isystem"):]
            path = pathlib.Path(include)
            absolute = path if path.is_absolute() else directory / path
            result.append(("-I" if pattern.match(str(absolute) + "/") else "-isystem") + include)
            index += 1
        else:
            result.append(argument)
            index += 1
    return result


def is_fuzz_entry(entry: dict[str, object]) -> bool:
    return FUZZ_DEFINE in arguments(entry)


def deduplicate(database: list[dict[str, object]]) -> tuple[list[dict[str, object]], int]:
    grouped: dict[str, list[dict[str, object]]] = defaultdict(list)
    for entry in database:
        path = entry.get("file")
        if not isinstance(path, str) or not path:
            raise ValueError(f"compile command has no file: {entry!r}")
        grouped[path].append(entry)

    retained: list[dict[str, object]] = []
    removed = 0
    for path, entries in grouped.items():
        if len(entries) == 1:
            retained.append(entries[0])
            continue

        ordinary = [entry for entry in entries if not is_fuzz_entry(entry)]
        retained.append(ordinary[0] if ordinary else entries[0])
        removed += len(entries) - 1
    return retained, removed


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    with open(args.input, encoding="utf-8") as stream:
        database = json.load(stream)
    filtered, removed = deduplicate(database)
    repository = pathlib.Path.cwd()
    for record in filtered:
        record["arguments"] = normalize_owned_includes(
            arguments(record), repository, pathlib.Path(str(record.get("directory", repository)))
        )
        record.pop("command", None)
    with open(args.output, "w", encoding="utf-8") as stream:
        json.dump(filtered, stream)
    print(
        f"clang-tidy compile database: {len(database)} -> {len(filtered)} entries "
        f"({removed} duplicate entries removed)",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
