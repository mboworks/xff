#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Run cross-feature smoke checks against a supplied, already staged executable."""

import argparse
import csv
import io
import json
from pathlib import Path
import re
import subprocess
import tempfile

from release_smoke_unicode import unicode_smoke


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def smoke(binary, expected_version):
    binary = str(Path(binary).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="xff-release-smoke-") as directory:
        root = Path(directory)

        def run(*args):
            result = subprocess.run(
                [binary, "--no-config", "--no-pager", "--color=never", *map(str, args)],
                cwd=root,
                capture_output=True,
                text=True,
                timeout=30,
                check=False,
            )
            check(result.returncode == 0, f"{args!r}: exit {result.returncode}\n{result.stderr}")
            return result.stdout

        version = run("--version").strip()
        check(version == f"xff {expected_version}", f"unexpected version: {version!r}")
        check("--help" in run("--help"), "help is missing its usage options")
        left, right = root / "left", root / "right"
        left.mkdir()
        right.mkdir()
        left_files = {"same.txt": "same\n", "changed.txt": "before\n", 'left,\"\n\t\\.total': "left\n"}
        right_files = {"same.txt": "same\n", "changed.txt": "after\n", "right.txt": "right\n"}
        for folder, files in ((left, left_files), (right, right_files)):
            for name, content in files.items():
                (folder / name).write_text(content, encoding="utf-8")

        records = [json.loads(line) for line in run(left, "-type", "f", "--format=jsonl").splitlines()]
        check(
            {row["path"] for row in records} == {str(left / name) for name in left_files},
            "JSONL filename round trip",
        )
        csv_output = run(left, "-type", "f", "--format=csv", "--columns=name,size")
        rows = list(csv.DictReader(io.StringIO(csv_output)))
        check(
            {row["name"]: int(row["size"]) for row in rows}
            == {name: len(content.encode("utf-8")) for name, content in left_files.items()},
            "CSV filename/size round trip",
        )

        paths = {str(left / name) for name in left_files}
        nul_output = run(left, "-type", "f", "--format=nul")
        check(nul_output.endswith("\0"), "missing NUL record terminator")
        check(set(nul_output[:-1].split("\0")) == paths, "NUL filename round trip")

        def escape_path(path):
            return path.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t")

        escaped_paths = {escape_path(path) for path in paths}
        plain = run(left, "-type", "f", "--path-encoding=escape")
        check(set(plain.splitlines()) == escaped_paths, "escaped plain filename boundaries")
        tsv = run(left, "-type", "f", "--format=tsv")
        check(tsv.splitlines()[0] == "path", "TSV header")
        check(set(tsv.splitlines()[1:]) == escaped_paths, "TSV filename encoding")

        profile = root / "smoke.ini"
        profile.write_text('[smoke]\n-type f -name "same.txt" # only the shared file\n', encoding="utf-8")
        configured = run(left, f"--xffrc={profile}", "--config=smoke", "--format=jsonl")
        selected = [json.loads(line) for line in configured.splitlines()]
        check(selected == [{"path": str(left / "same.txt")}], "named INI selection")

        output = run(left, "-type", "f", "--summary=ext", "--summary=ext", "--format=jsonl")
        summaries = [json.loads(line) for line in output.splitlines()]
        for request in (0, 1):
            group = [row for row in summaries if row["request"] == request]
            totals = [row for row in group if row["is_total"]]
            check(len(totals) == 1 and totals[0]["count"] == 3, "repeated summary identity/count")
            check(
                any(row["group"] == "total" and not row["is_total"] for row in group),
                "literal total extension identity",
            )

        comparison = run("--compare=summary", left, right, "-type", "f")
        for category in ("left-only", "right-only", "different", "identical"):
            check(
                re.search(rf"\b{category}\s+1\s+25\.00%", comparison),
                f"comparison category {category}: {comparison}",
            )
        unicode_smoke(run, root, check)
    print(f"Release smoke passed: {binary} ({expected_version})")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--expected-version", required=True)
    args = parser.parse_args()
    smoke(args.binary, args.expected_version)


if __name__ == "__main__":
    main()
