# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Packed data stays lossless and supports repeated publication."""

import gzip
import json
from pathlib import Path
import tempfile
import unittest

import site_storage as storage


class SiteStorageTest(unittest.TestCase):
    def test_shared_json_values_round_trip_without_type_collisions(self):
        row = {'sample': [1, True, 1.0, None, False, 0, -0.0], 'label': '</script>', '__proto__': 'literal'}
        value = {'a': row, 'b': row, 'nested': [[row] * 200]}
        packed = storage.packed(value)
        self.assertEqual(storage.unpacked(packed), value)
        self.assertLess(len(json.dumps(packed)), len(json.dumps(value)) / 10)
        with self.assertRaisesRegex(ValueError, 'reference'):
            storage.unpacked({'xff_storage': 1, 'root': 0, 'nodes': [['l', 0]]})

    def test_raw_observations_retain_exact_bytes_and_chart_has_one_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'benchmarks'
            report = root / 'runs/1/1/linux'
            report.mkdir(parents=True)
            original = b'{ "value": 1.000, "samples": [3, 2, 1] }\n'
            report.joinpath('report.json').write_bytes(original)
            figure = {'a': ['sample', 2]}
            report.joinpath('landscape.json').write_text(json.dumps(figure))
            report.joinpath('index.html').write_text('<html><head></head><body><div id="landscape"></div>'
                '<script>const figures=' + json.dumps(figure) + ';window.chart=figures;</script></body></html>')
            storage.compact_benchmarks(root)
            self.assertEqual(gzip.decompress(report.joinpath('report.json.gz').read_bytes()), original)
            self.assertFalse(report.joinpath('report.json').exists())
            page = gzip.decompress(report.joinpath('page.html.gz').read_bytes()).decode()
            self.assertIn('await window.XffSiteStorage.read("landscape.json")', page)
            self.assertNotIn('const figures={', page)
            self.assertIn('../../../../site-storage.js', page)
            self.assertEqual(storage.unpacked(json.loads(gzip.decompress(report.joinpath('landscape.json.gz').read_bytes()))), figure)
            before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            storage.compact_benchmarks(root)
            self.assertEqual(before, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})
            storage.expand_benchmarks(root)
            self.assertEqual(report.joinpath('report.json').read_bytes(), original)
            self.assertEqual(json.loads(report.joinpath('landscape.json').read_bytes()), figure)
            self.assertEqual(report.joinpath('index.html').read_text(), page)

    def test_chart_disagreement_fails_before_overwriting_measurements(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            root.joinpath('index.html').write_text('<script>const figures={"a":1};run();</script>')
            root.joinpath('landscape.json').write_text('{"a":2}')
            with self.assertRaisesRegex(ValueError, 'differs'):
                storage.compact_benchmarks(root)
            self.assertEqual(json.loads(root.joinpath('landscape.json').read_text()), {'a': 2})


if __name__ == '__main__':
    unittest.main()
