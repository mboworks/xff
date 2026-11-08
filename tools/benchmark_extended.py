#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Versioned production workloads for the extended benchmark qualification campaign."""

from collections import Counter
from dataclasses import dataclass
import hashlib
import io
import json
import os
from pathlib import Path
import tarfile


RECIPE = 'extended-production-v1'
MTIME = 1577923200  # 2020-01-02 00:00:00 UTC.
PATTERN = r'(ERROR|WARN) service=api_[0-9]+ status=5[0-9]{2}'
TASKS = ('files-exact', 'name-native', 'metadata-time', 'content-regex', 'archive-tar')


@dataclass(frozen=True)
class Case:
    name: str
    shape: str
    files: int
    cpus: int

    def __post_init__(self):
        if (self.name not in TASKS or self.shape not in ('broad', 'deep') or
                type(self.files) is not int or self.files < 1 or type(self.cpus) is not int or self.cpus < 1):
            raise ValueError('invalid extended case')

    @property
    def key(self):
        return f'{self.name}/{self.shape}/{self.files}/{self.cpus}'


def cases():
    """Include large traversal cells without making every archive trial a 100k scan."""
    return [Case(name, shape, count, cpus) for name in TASKS
            for shape in ('broad', 'deep')
            for count in ((100, 1000, 10000) if name == 'archive-tar' else (100, 10000, 100000))
            for cpus in (1, 3)]


def entries(shape, count, archive=False):
    if shape not in ('broad', 'deep') or count < 1:
        raise ValueError('invalid fixture dimensions')
    for index in range(count):
        if archive:
            directory = f'd{index // 100:04}/'
            if shape == 'deep':
                directory = 'level/' * (index // 100 % 8 + 1) + directory
        else:
            directory = '' if shape == 'broad' else 'level/' * (index % 40 + 1)
        name = directory + f'item_{index:06}.' + ('txt' if index % 2 == 0 else 'log')
        matched = index % 3 == 0
        size = 8192 if index % 50 == 0 else 256
        tail = (b'ERROR service=api_12 status=503\n' if matched else
                b'ERROR service=api_12 status=200\n')
        data = b'x' * (size - len(tail) - 1) + b'\n' + tail
        yield name, data, matched


def fixture_identity(shape, count, archive=False):
    digest = hashlib.sha256()
    digest.update(f'{RECIPE}/{shape}/{count}/{archive}/{MTIME}'.encode())
    for name, data, _ in entries(shape, count, archive):
        digest.update(name.encode() + b'\0' + data + b'\0')
    return digest.hexdigest()


def prepare(parent, selected_cases=None):
    """Create only inside a new owned directory; callers verify tmpfs before collection."""
    parent = Path(parent)
    parent.mkdir(exist_ok=False)
    (parent / 'empty').write_bytes(b'')
    (parent / 'home').mkdir()
    specifications = sorted({(case.shape, case.files, case.name == 'archive-tar')
                             for case in (cases() if selected_cases is None else selected_cases)})
    records = []
    for shape, count, archive in specifications:
        root = parent / ('archive' if archive else 'native') / shape / str(count)
        root.mkdir(parents=True)
        if archive:
            with tarfile.open(root / 'fixture.tar', 'w', format=tarfile.USTAR_FORMAT) as handle:
                for name, data, _ in entries(shape, count, True):
                    member = tarfile.TarInfo(name)
                    member.size, member.mtime, member.mode = len(data), MTIME, 0o644
                    member.uid = member.gid = 0
                    handle.addfile(member, io.BytesIO(data))
            content_hash = hashlib.sha256((root / 'fixture.tar').read_bytes()).hexdigest()
        else:
            for name, data, _ in entries(shape, count):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                target.chmod(0o644)
                os.utime(target, (MTIME, MTIME))
            content_hash = None
        records.append({'shape': shape, 'files': count, 'archive': archive,
                        'fixture_identity': fixture_identity(shape, count, archive),
                        'archive_sha256': content_hash})
    (parent / 'manifest.json').write_text(json.dumps({'recipe': RECIPE, 'fixtures': records}, indent=2) + '\n')
    return records


def commands(case, parent, binary, tools):
    parent = Path(parent)
    root = parent / ('archive' if case.name == 'archive-tar' else 'native') / case.shape / str(case.files)
    base = [str(binary), '--no-config', '--no-pager', '--color=never', '--sort=none',
            '--gitignore=off', '--hidden', f'--jobs={case.cpus}']
    find = [tools['find'], '-P', str(root), '-type', 'f']
    if case.name == 'files-exact':
        return {'xff': [*base, '--exact', '.', '-type', 'f', '-print0'],
                'find': [tools['find'], '-P', '.', '-type', 'f', '-print0']}, root, b'\0'
    if case.name == 'name-native':
        return {'xff': [*base, str(root), '-type', 'f', '-name', '*.txt', '-print0'],
                'find': [*find, '-name', '*.txt', '-print0']}, root, b'\0'
    if case.name == 'metadata-time':
        template = r'%p\t%s\t%TY-%Tm-%Td\0'
        return {'xff': [*base, '--exact', '--timezone=UTC', str(root), '-type', 'f',
                        '-name', '*.txt', '-size', '+128c', '-printf', template],
                'find': [*find, '-name', '*.txt', '-size', '+128c', '-printf', template]}, root, b'\0'
    if case.name == 'content-regex':
        return {'xff': [*base, '--exact', '--case=sensitive', '--regextype=RE2', str(root),
                        '-type', 'f', '-name', '*.txt', '-rxc', PATTERN, '-print0'],
                'rg': [tools['rg'], '--no-config', '--hidden', '--no-ignore', '--color=never',
                       '--case-sensitive', '--text', f'--threads={case.cpus}', '-g', '*.txt',
                       '--files-with-matches', '--null', '-e', PATTERN, str(root)]}, root, b'\0'
    if case.name == 'archive-tar':
        return {'xff': [*base, '--exact', '--archive=roots', '--archive-separator=/',
                        'fixture.tar', '-type', 'f', '-name', '*.txt', '-print'],
                'tar': [tools['tar'], '-tf', 'fixture.tar', '--wildcards', '*.txt',
                        '--transform=s,^,fixture.tar/,', '--show-transformed-names']}, root, b'\n'
    raise ValueError('unknown extended task: ' + case.name)


def expected(case, parent):
    root = Path(parent) / 'native' / case.shape / str(case.files)
    result = []
    for name, data, matched in entries(case.shape, case.files, case.name == 'archive-tar'):
        if case.name != 'files-exact' and not name.endswith('.txt'):
            continue
        if case.name == 'content-regex' and not matched:
            continue
        if case.name == 'archive-tar':
            value = 'fixture.tar/' + name
        elif case.name == 'files-exact':
            value = './' + name
        elif case.name == 'metadata-time':
            value = f'{root / name}\t{len(data)}\t2020-01-02'
        else:
            value = str(root / name)
        result.append(value.encode())
    return result


def validate(sample, wanted, separator):
    if sample['exit_codes'] != [0] or sample['stderr_bytes'] or sample['stderr']:
        raise ValueError(f'command failed: exit={sample["exit_codes"]}; '
                         f'stderr={sample["stderr"][:2000]!r}')
    output = sample['output']
    if output and not output.endswith(separator):
        raise ValueError('output lacks its final record separator')
    actual = output.removesuffix(separator).split(separator) if output else []
    if Counter(actual) != Counter(wanted):
        missing = list((Counter(wanted) - Counter(actual)).items())[:3]
        extra = list((Counter(actual) - Counter(wanted)).items())[:3]
        raise ValueError(f'output oracle mismatch; missing={missing!r}; extra={extra!r}')
    if sample['stdout_bytes'] != len(output):
        raise ValueError('output byte accounting mismatch')
    return hashlib.sha256(output).hexdigest()


def verify(parent, selected_cases=None):
    """Re-read fixture contents and metadata before accepting or resuming measurements."""
    parent = Path(parent)
    specifications = {(case.shape, case.files, case.name == 'archive-tar')
                      for case in (cases() if selected_cases is None else selected_cases)}
    manifest = json.loads((parent / 'manifest.json').read_text())
    if manifest['recipe'] != RECIPE or len(manifest['fixtures']) != len(specifications):
        raise ValueError('fixture manifest changed')
    for record in manifest['fixtures']:
        shape, count, archive = record['shape'], record['files'], record['archive']
        if (shape, count, archive) not in specifications:
            raise ValueError('unexpected fixture specification')
        root = parent / ('archive' if archive else 'native') / shape / str(count)
        if record['fixture_identity'] != fixture_identity(shape, count, archive):
            raise ValueError('fixture recipe identity changed')
        if archive:
            if hashlib.sha256((root / 'fixture.tar').read_bytes()).hexdigest() != record['archive_sha256']:
                raise ValueError('archive fixture changed')
            continue
        paths = set()
        for name, data, _ in entries(shape, count):
            target = root / name
            stat = target.stat()
            if target.is_symlink() or target.read_bytes() != data or stat.st_mtime != MTIME or stat.st_mode & 0o777 != 0o644:
                raise ValueError('native fixture changed: ' + str(target))
            paths.add(name)
        actual = {str(p.relative_to(root)) for p in root.rglob('*') if not p.is_dir()}
        if actual != paths:
            raise ValueError('native fixture population changed')
    return manifest
