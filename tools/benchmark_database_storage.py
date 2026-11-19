# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Lossless, verified SQLite transport for retained benchmark JSON and evidence."""

import hashlib
from contextlib import closing
import json
from pathlib import Path, PurePosixPath
import sqlite3
import tempfile
import re
import argparse

MANIFEST = "database-manifest.json"
MAX_DATABASE = 2 * 1024 * 1024 * 1024
PART_BYTES = 48 * 1024 * 1024
_expanded = set()


def compress(data):
    import zstandard

    return zstandard.ZstdCompressor(level=9, write_checksum=True).compress(data)


def decompress(data, limit=MAX_DATABASE):
    import zstandard

    size = zstandard.frame_content_size(data)
    if size < 0 or size > limit:
        raise ValueError("benchmark database expanded size exceeds its bound")
    result = zstandard.ZstdDecompressor().decompress(data, max_output_size=limit)
    if len(result) > limit:
        raise ValueError("expanded benchmark data exceeds its bound")
    return result


def digest(data):
    return hashlib.sha256(data).hexdigest()


def relative(value):
    path = PurePosixPath(value)
    if (
        not value
        or path.is_absolute()
        or str(path) != value
        or any(part in (".", "..") for part in path.parts)
        or any(char in value for char in ("\\", ":", "?", "#", "%"))
    ):
        raise ValueError("unsafe benchmark database path: " + value)
    return path


def read_manifest(root):
    path = root / MANIFEST
    if not path.exists():
        return None
    value = json.loads(path.read_text())
    if value.get("schema") != 1 or value.get("format") != "sqlite-zstd":
        raise ValueError("unsupported benchmark database manifest")
    seen = set()
    for series in value["series"]:
        relative(series["file"])
        if (
            not re.fullmatch("[0-9a-f]{20}", series["id"])
            or series["id"] in seen
            or not 0 < series["expanded_bytes"] <= MAX_DATABASE
        ):
            raise ValueError("invalid or duplicate benchmark database series")
        if (
            len(series["sha256"]) != 64
            or any(c not in "0123456789abcdef" for c in series["sha256"])
            or not 0 < series["compressed_bytes"] <= MAX_DATABASE
        ):
            raise ValueError("invalid benchmark database identity")
        seen.add(series["id"])
        chunks = series.get("chunks", [])
        if chunks:
            if len(chunks) > 1024 or sum(chunk["bytes"] for chunk in chunks) != series["compressed_bytes"]:
                raise ValueError("invalid benchmark database transport parts")
            for chunk in chunks:
                relative(chunk["file"])
                if not 0 < chunk["bytes"] <= PART_BYTES or not re.fullmatch("[0-9a-f]{64}", chunk["sha256"]):
                    raise ValueError("benchmark transport part exceeds Git storage bound")
    return value


def unpack(root, series, destination):
    path = root.joinpath(*relative(series["file"]).parts)
    packed = (
        path.read_bytes()
        if path.exists()
        else b"".join(
            root.joinpath(*relative(chunk["file"]).parts).read_bytes() for chunk in series.get("chunks", [])
        )
    )
    if len(packed) != series["compressed_bytes"]:
        raise ValueError("benchmark transport size differs from manifest")
    data = decompress(packed, series["expanded_bytes"])
    if len(data) != series["expanded_bytes"] or digest(data) != series["sha256"]:
        raise ValueError("benchmark database does not match manifest")
    destination.write_bytes(data)


def transport_files(series):
    return [chunk["file"] for chunk in series.get("chunks", [])] or (
        [series["file"]] if "file" in series else []
    )


def retain_transport(root, series):
    """Keep Git blobs below its hard file bound without splitting the logical DB."""
    if series.get("chunks") or "file" not in series:
        return
    path = root.joinpath(*relative(series["file"]).parts)
    data = path.read_bytes()
    series["compressed_sha256"] = digest(data)
    if len(data) <= PART_BYTES:
        return
    chunks = []
    for offset in range(0, len(data), PART_BYTES):
        part = data[offset : offset + PART_BYTES]
        target = path.with_name(path.name + f".part{len(chunks):03d}")
        temporary = target.with_suffix(target.suffix + ".tmp")
        temporary.write_bytes(part)
        temporary.replace(target)
        chunks.append(
            {"file": target.relative_to(root).as_posix(), "bytes": len(part), "sha256": digest(part)}
        )
    series["chunks"] = chunks
    path.unlink()


def expand(root, prefixes=()):
    """Hydrate trusted tooling scratch inputs without executing retained site code.

    Existing expanded or gzip JSON takes precedence, preserving new uploads which
    have not reached the aggregate yet. Symlink destinations are rejected.
    """
    root = root.resolve()
    manifest = read_manifest(root)
    if manifest is None:
        return 0
    key = (str(root.resolve()), (root / MANIFEST).stat().st_mtime_ns)
    if (*key, ()) in _expanded or (*key, prefixes) in _expanded:
        return 0
    count = 0
    for series in manifest["series"]:
        if prefixes and not any(path.startswith(prefixes) for path in series.get("paths", [])):
            continue
        with tempfile.TemporaryDirectory(prefix="xff-database-read-") as temporary:
            database = Path(temporary) / "series.sqlite"
            unpack(root, series, database)
            with closing(sqlite3.connect(f"file:{database}?mode=ro", uri=True)) as db:
                db.execute("PRAGMA query_only=ON")
                db.execute("PRAGMA trusted_schema=OFF")
                for path, sha, size, payload in db.execute(
                    "SELECT files.path,documents.id,original_bytes,payload FROM files "
                    "JOIN documents ON documents.id=files.document ORDER BY files.path"
                ):
                    if prefixes and not path.startswith(prefixes):
                        continue
                    target = root.joinpath(*relative(path).parts)
                    if any(part.is_symlink() for part in (target, *target.parents)):
                        raise ValueError("benchmark database destination contains a symlink")
                    if target.exists() or target.with_suffix(target.suffix + ".gz").exists():
                        continue
                    data = decompress(payload, size)
                    if len(data) != size or digest(data) != sha:
                        raise ValueError("retained benchmark JSON identity changed")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                    count += 1
    _expanded.add((*key, prefixes))
    return count


def stage_deployment(root):
    """Keep rollback generations on the retained branch, serve current aggregates."""
    manifest = read_manifest(root)
    if manifest is None:
        return
    current = {series["file"] for series in manifest["series"]}
    for series in manifest["series"]:
        if series.get("chunks"):
            target = root.joinpath(*relative(series["file"]).parts)
            temporary = target.with_suffix(target.suffix + ".tmp")
            checksum = hashlib.sha256()
            total = 0
            with temporary.open("wb") as output:
                for chunk in series["chunks"]:
                    part = root.joinpath(*relative(chunk["file"]).parts).read_bytes()
                    if len(part) != chunk["bytes"] or digest(part) != chunk["sha256"]:
                        raise ValueError("benchmark transport part identity changed")
                    output.write(part)
                    checksum.update(part)
                    total += len(part)
            if total != series["compressed_bytes"] or checksum.hexdigest() != series["compressed_sha256"]:
                raise ValueError("assembled benchmark database identity changed")
            temporary.replace(target)
            series["retained_chunks"] = series.pop("chunks")
            for chunk in series["retained_chunks"]:
                root.joinpath(*relative(chunk["file"]).parts).unlink()
        for previous in series.get("generations", []):
            path = previous.get("file")
            if path and path not in current:
                for name in transport_files(previous):
                    root.joinpath(*relative(name).parts).unlink(missing_ok=True)
                previous["retained_file"] = previous.pop("file")
                previous["retained_in"] = "coverage-pages"
    manifest["storage"]["files"] = sum(path.is_file() for path in root.rglob("*"))
    (root / MANIFEST).write_text(json.dumps(manifest, separators=(",", ":"), allow_nan=False) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("stage-deployment", "expand"))
    parser.add_argument("root", type=Path)
    parser.add_argument("--prefix", action="append", default=[])
    args = parser.parse_args()
    if args.operation == "expand":
        expand(args.root, tuple(args.prefix))
    else:
        stage_deployment(args.root)
