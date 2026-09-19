#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Regression tests for inline range-list policy."""

import unittest

import check_inline_range_lists as checker


class InlineRangeListsTest(unittest.TestCase):
    def test_accepts_short_headers_and_named_or_computed_ranges(self):
        for source in (
            "for (const auto x : {1, 2, 3}) {}",
            'for (std::string_view x : {"yes", "no"}) {}',
            'for (auto x : std::to_array<std::string_view>({"yes", "no"})) {}',
            "for (const auto& x :\n    kMeaningfulValues) {}",
            "for (auto x :\n    CalculateValues()) {}",
            "for (auto x = 0; x < 3; ++x) {}",
        ):
            with self.subTest(source=source):
                self.assertEqual(checker.violations(source), [])

    def test_rejects_wrapped_literal_ranges_with_or_without_typed_wrappers(self):
        for source in (
            "for (auto x : {1,\n 2, 3}) {}",
            "for (auto x :\n {1, 2}) {}",
            "for (auto x : std::to_array<int>({\n 1, 2,\n})) {}",
            "for (auto x : std::array{\n 1, 2,\n}) {}",
            "for (auto x : std::vector<int>{\n 1, 2,\n}) {}",
            "for (auto x : {" + ", ".join(str(x) for x in range(50)) + "}) {}",
        ):
            with self.subTest(source=source):
                self.assertEqual(len(checker.violations(source)), 1)

    def test_comments_literals_and_loop_bodies_are_not_loop_headers(self):
        source = r'''
// for (auto x : {
/* for (auto x : {1,
2}) {} */
const auto example = R"doc(for (auto x : {1,
2}) {})doc";
for (const auto x : {"a)b", "c:d"}) {
  Work(x);
}
'''
        self.assertEqual(checker.violations(source), [])

    def test_reports_source_line(self):
        self.assertEqual(checker.violations("\nfor (auto x : {1,\n2}) {}"), [2])


if __name__ == "__main__":
    unittest.main()
