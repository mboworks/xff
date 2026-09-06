#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Map Bazel external-repository LCOV paths back to checked-in extra modules."""

from __future__ import annotations

import argparse
from collections import defaultdict
import copy
import fnmatch
import json
import re
from pathlib import Path

import extras

SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"})


def declared_extras() -> dict[str, str]:
    """Returns every extra from the repository's single source of truth."""
    root = Path(__file__).resolve().parent.parent
    return extras.extras((root / "bazelmod" / "extras.MODULE.bazel").read_text(encoding="utf-8"))


def resolved_policy(policy: dict, modules: dict[str, str]) -> dict:
    """Adds every registered extra to the coverage scope and module table."""
    result = copy.deepcopy(policy)
    includes = result.setdefault("include", ["xff/**"])
    extensions = result.setdefault("categories", {}).setdefault("extensions", {})
    data_only = result.get("data_only_extras", [])
    for module in modules:
        pattern = f"{module}/**"
        if pattern not in includes:
            includes.append(pattern)
        if module not in data_only and not any(
            pattern in category.get("include", ()) for category in extensions.values()
        ):
            name = module.removeprefix("xff_").replace("_", " ").title()
            extensions[name] = {"include": [pattern]}
    return result


def remap(report: str, modules: dict[str, str]) -> str:
    """Returns an LCOV report whose declared extra sources use workspace paths."""
    for module, path in modules.items():
        pattern = rf"(?m)^SF:external/{re.escape(module)}\+/(.*)$"
        report = re.sub(pattern, rf"SF:{path}/\1", report)
    return report


def _matches(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def _function_markers(source_lines: list[str]) -> tuple[set[int], dict[int, int]]:
    excluded: set[int] = set()
    merged: dict[int, int] = {}
    for index, line in enumerate(source_lines):
        exclude = "LCOV_EXCL_FUNC_LINE" in line
        merge = "LCOV_MERGE_FUNC_LINE" in line
        if not exclude and not merge:
            continue
        for continuation in range(index, len(source_lines)):
            if exclude:
                excluded.add(continuation + 1)
            if merge:
                merged[continuation + 1] = index + 1
            if "{" in source_lines[continuation]:
                break
    return excluded, merged


def _excluded_blocks(source_lines: list[str]) -> set[int]:
    """Returns lines inside explicit LCOV exclusion blocks, including markers."""
    excluded: set[int] = set()
    start: int | None = None
    start_marker = ""
    for number, line in enumerate(source_lines, start=1):
        marker = next(
            (candidate for candidate in ("LCOV_EXCL_START", "XFF_UNSTABLE_COVERAGE_START") if candidate in line),
            "",
        )
        if marker:
            if start is not None:
                raise ValueError(f"nested {marker} at line {number}")
            start = number
            start_marker = marker
        if start is not None:
            excluded.add(number)
        stop_marker = next(
            (candidate for candidate in ("LCOV_EXCL_STOP", "XFF_UNSTABLE_COVERAGE_STOP") if candidate in line),
            "",
        )
        if stop_marker:
            if start is None:
                raise ValueError(f"{stop_marker} without an exclusion start at line {number}")
            start = None
    if start is not None:
        raise ValueError(f"{start_marker} at line {start} has no {start_marker.replace('START', 'STOP')}")
    return excluded


def normalize_record(record: str, source: Path) -> str:
    """Applies source coverage directives to one raw LCOV record."""
    source_lines = source.read_text(encoding="utf-8").splitlines()
    excluded_lines = _excluded_blocks(source_lines) | {
        number
        for number, line in enumerate(source_lines, start=1)
        if "LCOV_EXCL_LINE" in line
    }
    excluded_branches = excluded_lines | {
        number
        for number, line in enumerate(source_lines, start=1)
        if "LCOV_EXCL_BR_LINE" in line
    }
    excluded_functions, merged_functions = _function_markers(source_lines)
    merged_branches = {
        number: int(match.group(1))
        for number, line in enumerate(source_lines, start=1)
        if (match := re.search(r"LCOV_MERGE_BR_LINE\s+(\d+)", line))
    }

    raw_lines = record.splitlines()
    headers = [
        line
        for line in raw_lines
        if not re.match(r"^(?:FN|FNDA|FNF|FNH|DA|LF|LH|BRDA|BRF|BRH):", line)
    ]
    definitions: list[tuple[int, str]] = []
    hits_by_name: dict[str, int] = defaultdict(int)
    data_lines: list[str] = []
    branches: list[tuple[int, str, str, str]] = []
    for line in raw_lines:
        if line.startswith("FN:"):
            definition = line[3:].split(",")
            definitions.append((int(definition[0]), definition[-1]))
        elif line.startswith("FNDA:"):
            hits, name = line[5:].split(",", 1)
            hits_by_name[name] += int(hits)
        elif line.startswith("DA:"):
            number = int(line[3:].split(",", 1)[0])
            if number not in excluded_lines:
                data_lines.append(line)
        elif line.startswith("BRDA:"):
            number, block, branch, taken = line[5:].split(",")
            if int(number) not in excluded_branches:
                branches.append((int(number), block, branch, taken))

    functions: list[tuple[int, str, int]] = []
    function_groups: dict[int, list[tuple[int, str, int]]] = defaultdict(list)
    for line, name in definitions:
        if line in excluded_lines or line in excluded_functions:
            continue
        value = (line, name, hits_by_name.get(name, 0))
        if line in merged_functions:
            function_groups[merged_functions[line]].append(value)
        else:
            functions.append(value)
    for group, values in sorted(function_groups.items()):
        functions.append(
            (
                min(line for line, _, _ in values),
                f"__xff_lcov_merged_function_at_line_{group}",
                max(hits for _, _, hits in values),
            )
        )

    ordinary_branches: list[tuple[int, str, str, str]] = []
    branch_groups: dict[int, list[str]] = defaultdict(list)
    for line, block, branch, taken in branches:
        if line in merged_branches:
            branch_groups[line].append(taken)
        else:
            ordinary_branches.append((line, block, branch, taken))
    for line, taken_values in sorted(branch_groups.items()):
        width = merged_branches[line]
        if width <= 0 or len(taken_values) % width:
            raise ValueError(
                f"{source}:{line}: LCOV_MERGE_BR_LINE {width} cannot merge "
                f"{len(taken_values)} branch records"
            )
        for index in range(width):
            values = [value for value in taken_values[index::width] if value != "-"]
            taken = str(sum(int(value) for value in values)) if values else "-"
            ordinary_branches.append((line, "0", str(index), taken))

    function_hits = sum(hits > 0 for _, _, hits in functions)
    branch_hits = sum(taken not in ("-", "0") for _, _, _, taken in ordinary_branches)
    line_hits = sum(int(line.split(",")[1]) > 0 for line in data_lines)
    result = headers
    result.extend(f"FN:{line},{name}" for line, name, _ in functions)
    result.extend(f"FNDA:{hits},{name}" for _, name, hits in functions)
    result.extend((f"FNF:{len(functions)}", f"FNH:{function_hits}"))
    result.extend(f"BRDA:{line},{block},{branch},{taken}" for line, block, branch, taken in ordinary_branches)
    result.extend((f"BRF:{len(ordinary_branches)}", f"BRH:{branch_hits}"))
    result.extend(data_lines)
    result.extend((f"LF:{len(data_lines)}", f"LH:{line_hits}"))
    return "\n".join(result) + "\n"


def is_source(path: Path) -> bool:
    """Returns whether source directives can occur in this UTF-8 C/C++ file."""
    return path.suffix.lower() in SOURCE_SUFFIXES


def _slug(group: str, name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", f"{group}-{name}".lower()).strip("-")


def grouped(
    report: str,
    modules: dict[str, str],
    policy: dict,
    source_root: Path,
    workspace: Path = Path.cwd(),
) -> str:
    """Groups first-party records by policy category and links their source files."""
    categories = [
        (_slug(group, name), category["include"])
        for group, entries in policy.get("categories", {}).items()
        for name, category in entries.items()
    ]
    includes = policy.get("include", ["xff/**"])
    excludes = policy.get("exclude", [])
    physical_to_logical = tuple((path.rstrip("/") + "/", module + "/") for module, path in modules.items())
    result = []
    for record in report.split("end_of_record\n"):
        match = re.search(r"(?m)^SF:(.+)$", record)
        if not match:
            continue
        physical = match.group(1)
        # Bazel may emit empty records for data and licence files owned by an
        # extra module. They have no coverable data and therefore belong in
        # neither a policy category nor genhtml's source tree.
        if not re.search(r"(?m)^(?:DA|FNDA|BRDA):", record):
            continue
        logical = physical
        for prefix, replacement in physical_to_logical:
            if physical.startswith(prefix):
                logical = replacement + physical.removeprefix(prefix)
                break
        if not _matches(logical, includes) or _matches(logical, excludes):
            continue
        matches = [slug for slug, patterns in categories if _matches(logical, patterns)]
        if len(matches) != 1:
            raise ValueError(f"coverage source {logical!r} belongs to {len(matches)} policy categories")
        source = workspace / physical
        normalized = normalize_record(record, source) if is_source(source) else record
        linked = source_root.resolve() / matches[0] / logical
        linked.parent.mkdir(parents=True, exist_ok=True)
        if not linked.exists() and not linked.is_symlink():
            linked.symlink_to(source.resolve())
        result.append(normalized.replace(f"SF:{physical}", f"SF:{linked}", 1) + "end_of_record\n")
    return "".join(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--policy", type=Path)
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    modules = declared_extras()
    policy = None
    if args.policy:
        policy = resolved_policy(json.loads(args.policy.read_text(encoding="utf-8")), modules)
    report = remap(args.input.read_text(encoding="utf-8"), modules)
    if args.policy or args.source_root:
        if not args.policy or not args.source_root:
            parser.error("--policy and --source-root must be specified together")
        report = grouped(
            report,
            modules,
            policy,
            args.source_root,
        )
    args.output.write_text(report, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
