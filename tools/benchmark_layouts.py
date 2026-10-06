# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Registered, versioned generated layouts for local benchmark collection."""

from dataclasses import asdict, dataclass
import hashlib
import json

import benchmark_fixture


@dataclass(frozen=True)
class Layout:
    name: str
    revision: int
    fixture_revision: int
    topology: str
    files_per_directory: int
    anchor_prefix: str = "anchor-"

    @property
    def label(self):
        return f"{self.name}/v{self.revision}"

    def identity(self, counts):
        value = asdict(self)
        value.update(counts=list(counts), anchor_policy="nested ascending exact-count roots",
                     special_entries="one .gitignore and two symlinks in the smallest anchor")
        return value


LAYOUTS = {
    "broad/v2": Layout("broad", 2, 3, "shallow fan-out", 64),
    "deep/v2": Layout("deep", 2, 3, "narrow chained groups", 512),
}

NEEDLE_CONTENT = b"needle beta\n" * 96
PLAIN_CONTENT = b"plain alpha\n" * 96


@dataclass(frozen=True)
class PreparedLayout:
    layout: Layout
    counts: tuple[int, ...]
    entries: tuple[benchmark_fixture.Entry, ...]
    anchors: dict[int, str]
    anchor_hashes: dict[int, str]

    @property
    def identity(self):
        return self.layout.identity(self.counts)


def registered(labels=("broad/v2", "deep/v2")):
    try:
        return tuple(LAYOUTS[label] for label in labels)
    except KeyError as error:
        raise ValueError(f"unknown benchmark layout: {error.args[0]}") from None


def _content(index):
    return NEEDLE_CONTENT if index % 3 == 0 else PLAIN_CONTENT


def _file_name(index):
    prefix = ".hidden_" if index % 11 == 0 else "item_"
    return prefix + f"{index:06}." + ("txt" if index % 2 else "log")


def _relative_entries(entries, anchor):
    prefix = anchor + "/"
    result = []
    for entry in entries:
        if entry.name == anchor or not entry.name.startswith(prefix):
            continue
        result.append(benchmark_fixture.Entry(entry.name[len(prefix):], entry.kind, entry.value))
    return result


def _anchor_hash(entries, anchor):
    return benchmark_fixture.identity(_relative_entries(entries, anchor))


def _broad_files(parent, start, count, capacity):
    result = []
    for offset in range(count):
        index = start + offset
        group = offset // capacity
        result.append(benchmark_fixture.Entry(f"{parent}/groups/g{group:04}/{_file_name(index)}",
                                              "file", _content(index)))
    return result


def _deep_files(parent, start, count, capacity):
    result = []
    directory = parent
    for offset in range(count):
        if offset and offset % capacity == 0:
            directory += f"/next-{offset // capacity:04}"
        index = start + offset
        result.append(benchmark_fixture.Entry(f"{directory}/{_file_name(index)}", "file", _content(index)))
    return result


def prepare(label, counts):
    """Return one immutable maximum tree and exact nested roots for every count."""
    layout, = registered((label,))
    counts = tuple(counts)
    if not counts or any(type(count) is not int or count < 1 for count in counts):
        raise ValueError("layout counts must be positive integers")
    if tuple(sorted(set(counts))) != counts:
        raise ValueError("layout counts must be unique and increasing")

    anchors = {}
    parent = ""
    for count in reversed(counts):
        parent = f"{parent}/{layout.anchor_prefix}{count}".lstrip("/")
        anchors[count] = parent

    entries = []
    previous = 0
    for count in counts:
        anchor = anchors[count]
        entries.append(benchmark_fixture.Entry(anchor, "dir"))
        added = count - previous
        generator = _broad_files if layout.topology == "shallow fan-out" else _deep_files
        entries.extend(generator(anchor, previous, added, layout.files_per_directory))
        previous = count
    smallest = anchors[counts[0]]
    # .gitignore remains one of the advertised regular files, matching legacy fixtures.
    first_file = next(entry for entry in entries if entry.kind == "file" and entry.name.startswith(smallest + "/"))
    entries.remove(first_file)
    link_target = next(entry for entry in entries if entry.kind == "file" and entry.name.startswith(smallest + "/"))
    entries.extend((benchmark_fixture.Entry(f"{smallest}/.gitignore", "file", b"*.txt\n"),
                    benchmark_fixture.Entry(f"{smallest}/file-link", "link",
                                            link_target.name[len(smallest) + 1:]),
                    benchmark_fixture.Entry(f"{smallest}/dir-link", "link", ".")))
    entries = tuple(benchmark_fixture.validate(entries))
    hashes = {count: _anchor_hash(entries, anchor) for count, anchor in anchors.items()}
    return PreparedLayout(layout, counts, entries, anchors, hashes)


def generator_identity(prepared):
    value = json.dumps({'recipe': prepared.identity, 'tree_sha256': benchmark_fixture.identity(prepared.entries)},
                       sort_keys=True, separators=(',', ':')).encode()
    return hashlib.sha256(value).hexdigest()
