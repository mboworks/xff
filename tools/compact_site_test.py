# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""All publishers enforce the complete-site storage budget before committing."""

import datetime
from pathlib import Path
import tempfile
import unittest

import compact_site
from test_paths import repository_file


class CompactSiteTest(unittest.TestCase):
    def test_counts_entire_payload_excluding_git_database(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            root.joinpath('.git').mkdir()
            root.joinpath('.git/objects').write_bytes(b'not deployed')
            root.joinpath('site').mkdir()
            root.joinpath('site/docs.html').write_bytes(b'actual documentation')
            self.assertEqual(compact_site.sizes(root), {'site': {'files': 1, 'bytes': 20}})
            report = compact_site.compact(root, datetime.datetime.now(datetime.timezone.utc))
            self.assertEqual(report['sections']['site']['bytes'], 20)
            self.assertTrue(root.joinpath('site-storage.js').exists())

    def test_each_publisher_compacts_retained_branch_and_complete_deployment(self):
        for name in ('coverage_pages.yml', 'benchmark_pages.yml', 'pages.yml'):
            with self.subTest(workflow=name):
                source = repository_file('.github/workflows/' + name).read_text()
                self.assertLess(source.index('compact_site.py site'), source.index('git -C site commit'))
                self.assertLess(source.index('site_artwork.py'), source.index('compact_site.py public'))
                self.assertLess(source.index('compact_site.py public'), source.index('uses: actions/upload-pages-artifact@'))
                self.assertIn('git -C site add --all', source)
                if name == 'pages.yml':
                    self.assertIn('git fetch --no-tags --depth=1 origin', source)
                    self.assertNotIn('git fetch origin\n', source)
                else:
                    self.assertIn('fetch-depth: 0\n          filter: blob:none', source)


if __name__ == '__main__':
    unittest.main()
