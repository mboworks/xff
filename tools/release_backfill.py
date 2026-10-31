#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Validate the explicitly approved historical release recovery and its artifacts."""

import argparse
import hashlib
import json
from pathlib import Path
import zipfile


TAG = "v0.9.0"
SOURCE_RUN = 40432376834
SOURCE_SHA = "8e95f5546187b2a6f4ec165af9cf794af18ac4a1"
REASON = (
    "The release-stamped rg test expected xff 0.0.0 instead of xff 0.9.0. "
    "Both original platform builds and their staged binary smoke tests passed; "
    "297 of 298 coverage test targets passed. This assertion does not affect the binaries."
)


def validate(tag, sha, run, jobs, artifacts, failure_log):
    """Reject any different source, failure, job state, or unavailable build artifact."""
    if (tag, sha) != (TAG, SOURCE_SHA):
        raise ValueError("only the approved, unchanged v0.9.0 tag can be recovered")
    expected = {
        "id": SOURCE_RUN, "head_sha": SOURCE_SHA, "head_branch": TAG,
        "event": "push", "path": ".github/workflows/release.yml",
        "status": "completed", "conclusion": "failure",
    }
    if any(run.get(key) != value for key, value in expected.items()):
        raise ValueError("source run does not match the approved release failure")
    conclusions = {job["name"]: job.get("conclusion") for job in jobs}
    if len(conclusions) != len(jobs) or conclusions != {
        "validate": "success", "build linux": "success", "build macos": "success",
        "coverage": "failure", "release": "skipped",
    }:
        raise ValueError("source run has unexpected job results")
    required = (
        "Golden:  'xff 0.0.0'", "Result:  'xff 0.9.0'",
        "[  FAIL  ] rg_invocation_selects_short_options_help_and_errors",
        "297 tests pass and 1 fails locally.",
    )
    if any(text not in failure_log for text in required):
        raise ValueError("source log does not confirm the approved version assertion")
    result = {"tag": TAG, "source_sha": SOURCE_SHA, "source_run": SOURCE_RUN,
              "exception": REASON, "artifacts": {}}
    for platform in ("linux", "macos"):
        matches = [artifact for artifact in artifacts if artifact["name"] == f"xff-{platform}"]
        if len(matches) != 1:
            raise ValueError(f"expected one original {platform} artifact")
        artifact = matches[0]
        if (artifact.get("expired") is not False or artifact.get("size_in_bytes", 0) <= 0
                or artifact.get("workflow_run", {}).get("id") != SOURCE_RUN
                or artifact.get("workflow_run", {}).get("head_sha") != SOURCE_SHA
                or not isinstance(artifact.get("digest"), str)
                or not artifact["digest"].startswith("sha256:")):
            raise ValueError(f"invalid original {platform} artifact identity")
        result["artifacts"][platform] = {
            "id": artifact["id"], "digest": artifact["digest"],
        }
    return result


def extract(archive, destination, platform, metadata):
    """Check the original compressed artifact digest and extract only its four known files."""
    digest = "sha256:" + hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != metadata["artifacts"][platform]["digest"]:
        raise ValueError("downloaded artifact digest differs from the original upload")
    arch = {"linux": "x86_64", "macos": "arm64"}[platform]
    expected = {f"{binary}-{platform}-{arch}{suffix}"
                for binary in ("xff", "xff_full") for suffix in ("", ".tar.zst")}
    with zipfile.ZipFile(archive) as zipped:
        if len(zipped.namelist()) != 4 or set(zipped.namelist()) != expected:
            raise ValueError("artifact must contain exactly the four expected release files")
        destination.mkdir(parents=True, exist_ok=True)
        for name in sorted(expected):
            (destination / name).write_bytes(zipped.read(name))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    validate_parser = commands.add_parser("validate")
    validate_parser.add_argument("--tag", required=True)
    validate_parser.add_argument("--sha", required=True)
    validate_parser.add_argument("--inputs", type=Path, required=True)
    validate_parser.add_argument("--output", type=Path, required=True)
    extract_parser = commands.add_parser("extract")
    extract_parser.add_argument("--archive", type=Path, required=True)
    extract_parser.add_argument("--destination", type=Path, required=True)
    extract_parser.add_argument("--platform", choices=("linux", "macos"), required=True)
    extract_parser.add_argument("--metadata", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "validate":
        inputs = args.inputs
        result = validate(
            args.tag, args.sha, json.loads((inputs / "run.json").read_text()),
            json.loads((inputs / "jobs.json").read_text())["jobs"],
            json.loads((inputs / "artifacts.json").read_text())["artifacts"],
            (inputs / "failure.log").read_text(),
        )
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    else:
        extract(args.archive, args.destination, args.platform,
                json.loads(args.metadata.read_text()))


if __name__ == "__main__":
    main()
