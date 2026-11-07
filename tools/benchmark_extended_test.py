# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Check fixture semantics and reject incorrect benchmark outputs."""

from pathlib import Path
import re
import tarfile
import tempfile
import unittest

import benchmark_extended as suite


class BenchmarkExtendedTest(unittest.TestCase):
    def test_case_grid_has_unique_cells_and_bounded_archive_sizes(self):
        cases = suite.cases()
        self.assertEqual(len(cases), 60)
        self.assertEqual(len({case.key for case in cases}), len(cases))
        self.assertEqual({c.files for c in cases if c.name == 'archive-tar'}, {100, 1000, 10000})
        self.assertEqual({c.files for c in cases if c.name != 'archive-tar'}, {100, 10000, 100000})
        for name in suite.TASKS:
            self.assertEqual({c.cpus for c in cases if c.name == name}, {1, 3})
        for values in [('unknown', 'broad', 1, 1), ('files-exact', 'wide', 1, 1),
                       ('files-exact', 'deep', 0, 1), ('files-exact', 'deep', 1, 0)]:
            with self.assertRaises(ValueError):
                suite.Case(*values)

    def test_content_fixture_matches_the_regex_and_preserves_nonmatches(self):
        rows = list(suite.entries('broad', 100))
        self.assertEqual(len(rows), 100)
        self.assertEqual({len(data) for _, data, _ in rows}, {256, 8192})
        actual = [name for name, data, _ in rows if re.search(suite.PATTERN.encode(), data)]
        self.assertEqual(actual, [f'item_{i:06}.txt' if i % 2 == 0 else f'item_{i:06}.log'
                                  for i in range(0, 100, 3)])

    def test_oracles_have_explicit_paths_metadata_and_selections(self):
        parent = Path('/fixture')
        expectations = {
            'files-exact': [b'./item_000000.txt', b'./item_000001.log', b'./item_000002.txt'],
            'name-native': [b'/fixture/native/broad/3/item_000000.txt', b'/fixture/native/broad/3/item_000002.txt'],
            'metadata-time': [b'/fixture/native/broad/3/item_000000.txt\t8192\t2020-01-02',
                              b'/fixture/native/broad/3/item_000002.txt\t256\t2020-01-02'],
            'content-regex': [b'/fixture/native/broad/3/item_000000.txt'],
            'archive-tar': [b'fixture.tar/d0000/item_000000.txt', b'fixture.tar/d0000/item_000002.txt'],
        }
        for task, expected in expectations.items():
            with self.subTest(task=task):
                self.assertEqual(suite.expected(suite.Case(task, 'broad', 3, 1), parent), expected)

    def test_materialized_native_and_archive_fixtures_match_recipe(self):
        cases = [suite.Case(task, shape, 6, 1) for task in ('files-exact', 'archive-tar') for shape in ('broad', 'deep')]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'fixture'
            suite.prepare(root, cases)
            self.assertEqual(len(suite.verify(root, cases)['fixtures']), 4)
            for shape in ('broad', 'deep'):
                with tarfile.open(root / 'archive' / shape / '6/fixture.tar') as archive:
                    members = archive.getmembers()
                    self.assertEqual(len(members), 6)
                    for member, (name, data, _) in zip(members, suite.entries(shape, 6, True), strict=True):
                        self.assertEqual(member.name, name)
                        self.assertTrue(member.isfile())
                        self.assertEqual(member.mtime, suite.MTIME)
                        self.assertEqual(archive.extractfile(member).read(), data)
            with self.assertRaises(FileExistsError):
                suite.prepare(root, cases)

    def test_fixture_changes_are_rejected(self):
        case = suite.Case('files-exact', 'broad', 3, 1)
        mutations = [lambda root: (root / 'native/broad/3/item_000000.txt').write_bytes(b'wrong'),
                     lambda root: (root / 'native/broad/3/item_000000.txt').chmod(0o600),
                     lambda root: (root / 'native/broad/3/extra').write_bytes(b'extra')]
        for mutation in mutations:
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / 'fixture'
                suite.prepare(root, [case])
                mutation(root)
                with self.assertRaisesRegex(ValueError, 'changed'):
                    suite.verify(root, [case])

    def test_validation_compares_multisets_and_checks_status_and_bytes(self):
        sample = {'exit_codes': [0], 'stderr_bytes': 0, 'stderr': '', 'output': b'b\0a\0', 'stdout_bytes': 4}
        self.assertEqual(len(suite.validate(sample, [b'a', b'b'], b'\0')), 64)
        for changes in ({'output': b'a\0a\0'}, {'output': b'a\0', 'stdout_bytes': 2},
                        {'output': b'b\0a'}, {'exit_codes': [1]}, {'stderr_bytes': 4, 'stderr': 'oops'},
                        {'stdout_bytes': 5}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                suite.validate(dict(sample, **changes), [b'a', b'b'], b'\0')

    def test_commands_keep_native_case_and_equivalent_archive_paths(self):
        tools = {'find': '/bin/find', 'rg': '/bin/rg', 'tar': '/bin/tar'}
        commands, _, separator = suite.commands(suite.Case('name-native', 'deep', 100, 3),
                                                 Path('/fixture'), '/bin/xff', tools)
        self.assertNotIn('--exact', commands['xff'])
        self.assertFalse(any(arg.startswith('--case=') for arg in commands['xff']))
        self.assertIn('--jobs=3', commands['xff'])
        commands, _, separator = suite.commands(suite.Case('archive-tar', 'deep', 100, 1),
                                                 Path('/fixture'), '/bin/xff', tools)
        self.assertEqual(separator, b'\n')
        self.assertIn('--archive-separator=/', commands['xff'])
        self.assertIn('--transform=s,^,fixture.tar/,', commands['tar'])
        self.assertNotIn('-printf', commands['xff'])


if __name__ == '__main__':
    unittest.main()
