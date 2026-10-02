# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Publication compaction preserves canonical reports, provenance and deep links."""

import json
from pathlib import Path
import tempfile
import unittest

import compact_coverage
from test_paths import repository_file


class CompactCoverageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = {'run_id': 123, 'run_attempt': 2, 'head_sha': 'abc'}
        self.original = b'<html><head></head><body>' + b'coverage ' * 100 + b'</body></html>'
        self.archive = self.report('runs/123/2')

    def report(self, target):
        directory = self.root / target
        (directory / 'lcov').mkdir(parents=True)
        (directory / 'coverage-meta.json').write_text(json.dumps({'source': self.source}))
        (directory / 'coverage-summary.json').write_text('{"lines": 10}')
        (directory / 'lcov/source.cc.html').write_bytes(self.original)
        return directory

    def test_stable_reports_redirect_without_changing_archive_or_json(self):
        reports = [self.report(name) for name in ('main', 'pr/42', 'tag/1.2.3')]
        before = {p: p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        count, saved = compact_coverage.compact(self.root)
        self.assertEqual(count, 3)
        self.assertGreater(saved, 0)
        for report in reports:
            page = report / 'lcov/source.cc.html'
            content = page.read_text()
            self.assertIn('runs/123/2/lcov/source.cc.html', content)
            self.assertIn('location.search + location.hash', content)
        for path, content in before.items():
            if path.is_relative_to(self.archive) or path.suffix == '.json':
                self.assertEqual(path.read_bytes(), content)
        self.assertEqual(compact_coverage.compact(self.root), (0, 0))

    def test_dry_run_reports_exact_saving_without_writing(self):
        report = self.report('pr/42')
        predicted = compact_coverage.compact(self.root, dry_run=True)
        self.assertEqual((report / 'lcov/source.cc.html').read_bytes(), self.original)
        self.assertEqual(predicted, compact_coverage.compact(self.root))

    def test_different_same_size_or_missing_archived_page_stays(self):
        report = self.report('main')
        (report / 'lcov/source.cc.html').write_bytes(self.original.replace(b'coverage', b'distinct'))
        (report / 'lcov/missing.html').write_bytes(self.original)
        self.assertEqual(compact_coverage.compact(self.root), (0, 0))

    def test_missing_archive_legacy_and_mismatched_provenance_stay(self):
        for source in ({**self.source, 'run_id': 999}, {**self.source, 'run_id': 0},
                       {**self.source, 'run_attempt': 0}, {**self.source, 'head_sha': 'other'},
                       {**self.source, 'run_id': '../outside'}):
            with self.subTest(source=source):
                self.source = source
                report = self.report('pr/' + str(len(list(self.root.glob('pr/*')))))
                self.assertEqual(compact_coverage.compact(self.root), (0, 0))
                self.assertEqual((report / 'lcov/source.cc.html').read_bytes(), self.original)

    def test_redirect_encodes_path_and_does_not_inject_markup(self):
        content = compact_coverage.redirect('../a #?"<script>.html').decode()
        self.assertIn('../a%20%23%3F%22%3Cscript%3E.html', content)
        self.assertNotIn('<script>.html', content)

    def test_absent_coverage_is_a_noop(self):
        self.assertEqual(compact_coverage.compact(self.root / 'missing'), (0, 0))

    def test_complete_site_size_gate(self):
        expected = sum(path.stat().st_size for path in self.root.rglob('*') if path.is_file())
        self.assertEqual(compact_coverage.check_site_size(self.root, expected), expected)
        with self.assertRaisesRegex(ValueError, 'deployment ceiling'):
            compact_coverage.check_site_size(self.root, expected - 1)

    def test_all_publishers_compact_and_check_size_before_artwork_and_upload(self):
        for workflow in ('coverage_pages.yml', 'benchmark_pages.yml', 'pages.yml'):
            with self.subTest(workflow=workflow):
                source = repository_file('.github/workflows/' + workflow).read_text()
                compact = source.index('compact_coverage.py public/coverage --site-root public')
                artwork = source.index('site_artwork.py')
                upload = source.index('uses: actions/upload-pages-artifact@')
                self.assertLess(compact, artwork)
                self.assertLess(artwork, upload)


if __name__ == '__main__':
    unittest.main()
