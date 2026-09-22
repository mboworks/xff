# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Fixture content, isolation, and archive equivalence."""

import io
from pathlib import Path
import tarfile
import tempfile
import unittest

import benchmark_fixture as fixture


class BenchmarkFixtureTest(unittest.TestCase):
    def test_inline_content_and_tree(self):
        entries = fixture.manifest(b'empty\nempty-dir/\nsrc/a=: a=b \\n\nsrc/b=t:literal\nbinary=b:AAEC/w==\nlink=l:src/a\ndangling=l:missing\nloop=l:loop\n')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'tree'
            rows = fixture.materialize(root, entries)
            self.assertEqual((root / 'src/a').read_bytes(), b' a=b \\n')
            self.assertEqual((root / 'binary').read_bytes(), b'\0\1\2\xff')
            self.assertEqual((root / 'empty').read_bytes(), b'')
            self.assertTrue((root / 'empty-dir').is_dir())
            self.assertEqual((root / 'link').readlink(), Path('src/a'))
            self.assertTrue((root / 'dangling').is_symlink())
            self.assertTrue((root / 'loop').is_symlink())
            self.assertEqual(len(rows), 4)
            with self.assertRaises(FileExistsError):
                fixture.materialize(root, entries)

    def test_invalid_manifests_rejected_before_creation(self):
        for data in (b'../bad', b'/bad', b'a\na', b'a\na/b', b'a=l:x\na/b', b'a=f:sidecar',
                     b'a=b:@@', b'a=l:/outside', b'a=l:../outside', b'a//b', b'a\0b',
                     b'd/a=l:..\nb=l:d/a/..', b'a=t:x\na/', b'a=q:unknown'):
            with self.subTest(data=data), self.assertRaises(ValueError):
                fixture.manifest(data)

    def test_relative_links_and_mutual_cycles(self):
        entries = fixture.manifest(b'd/a=l:../file\nfile=:text\na=l:b\nb=l:a\n')
        self.assertEqual(len(entries), 4)
        self.assertEqual(fixture.identity(entries), fixture.identity(list(reversed(entries))))
        self.assertNotEqual(fixture.identity(entries), fixture.identity(fixture.manifest(b'file=:other')))

    def write_archive(self, path, members):
        with tarfile.open(path, 'w:gz') as archive:
            for name, kind, content in members:
                member = tarfile.TarInfo(name)
                member.type = kind
                if kind == tarfile.REGTYPE:
                    member.size = len(content)
                    archive.addfile(member, io.BytesIO(content))
                else:
                    if kind == tarfile.SYMTYPE:
                        member.linkname = content
                    archive.addfile(member)

    def test_archive_and_manifest_produce_same_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'tree.tgz'
            self.write_archive(path, [('a', tarfile.REGTYPE, b'hello'), ('dir/', tarfile.DIRTYPE, b''),
                                     ('link', tarfile.SYMTYPE, 'a')])
            entries, source_hash = fixture.load(path)
            expected = fixture.manifest(b'a=:hello\ndir/\nlink=l:a\n')
            self.assertEqual(fixture.identity(entries), fixture.identity(expected))
            self.assertEqual(len(source_hash), 64)
            self.assertEqual(fixture.materialize(Path(temporary) / 'tree', entries)[0][1], b'hello')

    def test_archive_escape_special_entries_and_duplicates_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'tree.tgz'
            for members in ([('../escape', tarfile.REGTYPE, b'x')], [('fifo', tarfile.FIFOTYPE, b'')],
                            [('link', tarfile.SYMTYPE, '/outside')], [('hard', tarfile.LNKTYPE, b'')],
                            [('same', tarfile.REGTYPE, b'x'), ('same', tarfile.REGTYPE, b'y')]):
                with self.subTest(members=members):
                    self.write_archive(path, members)
                    with self.assertRaises(ValueError):
                        fixture.load(path)

    def test_mapping_requires_exact_counts_and_no_duplicate_keys(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'fixture'
            path.write_text('a=:hello\nb=b:AA==\ndir/\nlink=l:a\n')
            mapping = fixture.mapping([f'broad:2={path}'])
            self.assertEqual(len(mapping['broad', 2][0]), 4)
            for values in ([f'broad:1={path}'], [f'broad:2={path}', f'broad:2={path}'],
                           [f'../bad:2={path}'], [f'bad={path}']):
                with self.subTest(values=values), self.assertRaises(ValueError):
                    fixture.mapping(values)


if __name__ == '__main__':
    unittest.main()
