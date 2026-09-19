#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Require names for literal ranges whose complete for-header does not fit one line."""

from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools/inline_range_lists_baseline.json"
# Preserve offsets/newlines while ignoring examples in comments and quoted literals.
LITERALS = re.compile(
    r'//[^\n]*|/\*.*?\*/|R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|(?<![\w\'])\'(?:\\.|[^\'\\])*\'', re.DOTALL
)
LITERAL_RANGE = re.compile(r"\s*(?:\{|(?:::)?std::(?:to_array|array|initializer_list|vector)\b)")


def violations(source: str) -> list[tuple[int, str]]:
    masked = LITERALS.sub(lambda match: re.sub(r"[^\n]", " ", match.group()), source)
    found = []
    for match in re.finditer(r"\bfor\s*\(", masked):
        depth = 1
        colon = None
        end = match.end()
        while end < len(masked) and depth:
            char = masked[end]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            elif (char == ":" and depth == 1 and masked[end - 1] != ":"
                  and masked[end + 1:end + 2] != ":" and colon is None):
                colon = end
            end += 1
        if depth or colon is None:
            continue
        expression = masked[colon + 1:end - 1]
        if not LITERAL_RANGE.match(expression) or "{" not in expression:
            continue
        header = source[match.start():end]
        line_start = source.rfind("\n", 0, match.start()) + 1
        if "\n" not in header and end - line_start <= 120:
            continue
        fingerprint = hashlib.sha256(header.encode()).hexdigest()
        found.append((source.count("\n", 0, match.start()) + 1, fingerprint))
    return found


def check(path: Path, baseline: dict[str, list[str]]) -> list[str]:
    relative = path.resolve().relative_to(ROOT).as_posix()
    allowed = baseline.get(relative, [])
    return [f"{relative}:{line}: name this multiline/overlong literal range before the loop"
            for line, fingerprint in violations(path.read_text(encoding="utf-8"))
            if fingerprint not in allowed]


def main() -> int:
    baseline = json.loads(BASELINE.read_text(encoding="utf-8")) if BASELINE.exists() else {}
    errors = [error for name in sys.argv[1:] for error in check(Path(name), baseline)]
    for error in errors:
        print(error)
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
