# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Self-contained benchmark trees, validated before materialization."""

import base64
from collections import deque
from dataclasses import dataclass
import hashlib
import io
from pathlib import Path, PurePosixPath
import tarfile


@dataclass(frozen=True)
class Entry:
    name: str
    kind: str
    value: bytes | str = b''


def path_name(name):
    if not name or '\0' in name or name.startswith('/') or any(part in ('', '.', '..') for part in name.split('/')):
        raise ValueError(f'invalid fixture path: {name!r}')
    return name


def validate(entries):
    nodes = {}
    for entry in entries:
        path_name(entry.name)
        if entry.kind not in ('file', 'dir', 'link') or entry.name in nodes:
            raise ValueError(f'duplicate or invalid fixture entry: {entry.name}')
        nodes[entry.name] = entry
    for entry in entries:
        for parent in PurePosixPath(entry.name).parents:
            name = str(parent)
            if name != '.' and name in nodes and nodes[name].kind != 'dir':
                raise ValueError(f'non-directory fixture parent: {name}')
    links = {entry.name: entry.value for entry in entries if entry.kind == 'link'}
    for name, target in links.items():
        if not target or target.startswith('/') or '\0' in target:
            raise ValueError(f'invalid symlink target: {name}')
        parts = list(PurePosixPath(name).parts[:-1])
        pending = deque(target.split('/'))
        seen = set()
        expansions = 0
        while pending:
            state = (tuple(parts), tuple(pending))
            if state in seen:  # An internal cycle is a valid test fixture.
                break
            seen.add(state)
            part = pending.popleft()
            if part in ('', '.'):
                continue
            if part == '..':
                if not parts:
                    raise ValueError(f'symlink escapes fixture: {name}')
                parts.pop()
                continue
            parts.append(part)
            link = '/'.join(parts)
            if link in links:
                parts.pop()
                pending.extendleft(reversed(links[link].split('/')))
                expansions += 1
                if expansions > 256:
                    raise ValueError(f'symlink expansion too complex: {name}')
    return entries


def manifest(data):
    entries = []
    for number, line in enumerate(data.decode('utf-8').split('\n'), 1):
        if line.endswith('\r'):
            line = line[:-1]
        if not line:
            continue
        name, sep, value = line.partition('=')
        if not sep:
            entries.append(Entry(name.rstrip('/') if name.endswith('/') else name,
                                 'dir' if name.endswith('/') else 'file'))
        elif value.startswith((':', 't:')):
            entries.append(Entry(name, 'file', value[1 if value.startswith(':') else 2:].encode('utf-8')))
        elif value.startswith('b:'):
            entries.append(Entry(name, 'file', base64.b64decode(value[2:], validate=True)))
        elif value.startswith('l:'):
            entries.append(Entry(name, 'link', value[2:]))
        else:
            raise ValueError(f'line {number}: expected =:, =t:, =b:, or =l:')
    return validate(entries)


def archive(data):
    entries = []
    total = 0
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:*') as source:
        for member in source:
            name = member.name.removeprefix('./').rstrip('/')
            if name in ('', '.') and member.isdir():
                continue
            if member.isfile():
                total += member.size
                if total > 1024 ** 3:
                    raise ValueError('fixture contents exceed 1 GiB')
                with source.extractfile(member) as content:
                    entries.append(Entry(name, 'file', content.read()))
            elif member.isdir():
                entries.append(Entry(name, 'dir'))
            elif member.issym():
                entries.append(Entry(name, 'link', member.linkname))
            else:
                raise ValueError(f'unsupported archive entry: {member.name}')
            if len(entries) > 200000:
                raise ValueError('fixture exceeds 200000 entries')
    return validate(entries)


def load(path):
    path = Path(path)
    data = path.read_bytes()
    entries = archive(data) if tarfile.is_tarfile(io.BytesIO(data)) else manifest(data)
    return entries, hashlib.sha256(data).hexdigest()


def identity(entries):
    digest = hashlib.sha256()
    for entry in sorted(entries, key=lambda item: item.name):
        for value in (entry.name.encode(), entry.kind.encode(),
                      entry.value.encode() if isinstance(entry.value, str) else entry.value):
            digest.update(len(value).to_bytes(8, 'big'))
            digest.update(value)
    return digest.hexdigest()


def materialize(root, entries):
    validate(entries)
    # The caller owns a private temporary parent; never populate an existing tree.
    root.mkdir()
    rows = []
    for entry in entries:
        path = root / entry.name
        path.parent.mkdir(parents=True, exist_ok=True)
        if entry.kind == 'dir':
            path.mkdir(exist_ok=True)
        elif entry.kind == 'file':
            with path.open('xb') as output:
                output.write(entry.value)
            rows.append((path, entry.value))
    for entry in entries:
        if entry.kind == 'link':
            (root / entry.name).symlink_to(entry.value)
    return rows


def mapping(values):
    datasets = {}
    for value in values:
        key, sep, source = value.partition('=')
        shape, colon, count_text = key.partition(':')
        if not sep or not colon or not shape or not shape.replace('-', '').replace('_', '').isalnum():
            raise ValueError('fixture mapping must be SHAPE:COUNT=PATH')
        count = int(count_text)
        if count < 1 or (shape, count) in datasets:
            raise ValueError('fixture counts must be positive and mappings unique')
        entries, source_hash = load(source)
        if sum(entry.kind == 'file' for entry in entries) != count:
            raise ValueError(f'fixture does not contain {count} regular files: {source}')
        datasets[shape, count] = entries, source_hash
    return datasets
