# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Independent fixture oracle and equivalent engine-invocation contracts."""
import re
import unittest

import benchmark_regex as regex


class BenchmarkRegexTest(unittest.TestCase):
    def test_fixture_labels_match_independent_regex_oracle(self):
        pattern = re.compile(regex.PATTERN.encode())
        for population in ('mixed', 'absent'):
            for index in range(12):
                for size in (128, 16384):
                    with self.subTest(population=population, index=index, size=size):
                        data, expected = regex.payload(index, size, population)
                        self.assertEqual(len(data), size)
                        self.assertEqual(pattern.search(data) is not None, expected)
                        self.assertEqual(any(pattern.search(line) is not None for line in data.splitlines()), expected)
                        self.assertEqual(expected, population == 'mixed' and index % 3 == 0)

    def test_engine_commands_share_pattern_and_worker_count(self):
        commands = regex.commands('/xff', '/rg', 4)
        for name, command in commands.items():
            self.assertIn(regex.PATTERN, command)
            self.assertIn('--no-config', command)
            self.assertIn('--jobs=4' if name.startswith('xff') else '--threads=4', command)
        self.assertIn('--regextype=RE2', commands['xff-re2'])
        self.assertIn('--regextype=PCRE2', commands['xff-pcre2'])
        self.assertIn('--pcre2', commands['rg-pcre2'])

    def test_invalid_measurement_contracts_fail_before_invocation(self):
        for counts, workers, size, reps, keep in (([], [1], 128, 3, 2), ([1], [0], 128, 3, 2),
                                                  ([1], [1], 10, 3, 2), ([1], [1], 128, 3, 4)):
            with self.assertRaises(ValueError):
                regex.collect('/unused', '/unused', counts, workers, size, reps, keep)


if __name__ == '__main__':
    unittest.main()
