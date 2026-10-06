# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Versioned generated benchmark layout contracts."""

from pathlib import Path
import tempfile
import unittest

import benchmark_fixture
import benchmark_layouts


class BenchmarkLayoutsTest(unittest.TestCase):
    def test_registered_layouts_have_nested_exact_anchors(self):
        for label in ("broad/v2", "deep/v2"):
            with self.subTest(label=label):
                prepared = benchmark_layouts.prepare(label, (10, 20, 50, 100, 1000))
                names = {entry.name for entry in prepared.entries if entry.kind == "file"}
                for count, anchor in prepared.anchors.items():
                    prefix = anchor + "/"
                    self.assertEqual(sum(name.startswith(prefix) for name in names), count)
                    self.assertEqual(len(prepared.anchor_hashes[count]), 64)
                for smaller, larger in zip(prepared.counts, prepared.counts[1:], strict=False):
                    self.assertTrue(prepared.anchors[smaller].startswith(prepared.anchors[larger] + "/"))

    def test_topologies_have_distinct_registered_bounds(self):
        broad = benchmark_layouts.prepare("broad/v2", (10, 10000))
        deep = benchmark_layouts.prepare("deep/v2", (10, 10000))
        broad_paths = [entry.name for entry in broad.entries if entry.kind == "file"]
        deep_paths = [entry.name for entry in deep.entries if entry.kind == "file"]
        self.assertLess(max(path.count("/") for path in broad_paths), 6)
        self.assertGreater(max(path.count("/") for path in deep_paths),
                           max(path.count("/") for path in broad_paths))
        self.assertNotEqual(benchmark_fixture.identity(broad.entries), benchmark_fixture.identity(deep.entries))

    def test_materialized_anchor_hashes_are_deterministic(self):
        first = benchmark_layouts.prepare("broad/v2", (10, 20, 100))
        second = benchmark_layouts.prepare("broad/v2", (10, 20, 100))
        self.assertEqual(first, second)
        self.assertEqual(benchmark_layouts.generator_identity(first), benchmark_layouts.generator_identity(second))
        with tempfile.TemporaryDirectory() as temporary:
            rows = benchmark_fixture.materialize(Path(temporary) / "broad", first.entries)
            self.assertEqual(len(rows), 100)

    def test_invalid_layout_or_counts_are_rejected(self):
        for label, counts in (("flat/v1", (10,)), ("broad/v2", ()), ("broad/v2", (20, 10)),
                              ("broad/v2", (10, 10)), ("broad/v2", (0,))):
            with self.subTest(label=label, counts=counts), self.assertRaises(ValueError):
                benchmark_layouts.prepare(label, counts)


if __name__ == "__main__":
    unittest.main()
