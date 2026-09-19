# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Verify favicons across nested release, reference, and coverage pages."""

from pathlib import Path
import tempfile
import unittest

import site_artwork
from test_paths import repository_file


class SiteArtworkTest(unittest.TestCase):
    def test_nested_pages_and_idempotence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            assets = repository_file("docs/assets")
            documents = {
                "index.html": '<!doctype html><html lang="en"><meta charset="utf-8"><body>Home</body></html>',
                "site/tag/v0.5.0/XFF.html": '<!doctype html><html><head><title>Help</title></head><body>Help</body></html>',
                "coverage/pr/835/index.html": '<HTML><HEAD><TITLE>Coverage</TITLE></HEAD><BODY>Report</BODY></HTML>',
                "fragment.html": '<p>A fragment</p>',
            }
            for name, text in documents.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            site_artwork.decorate(assets, root)
            for name, original in documents.items():
                path = root / name
                decorated = path.read_text()
                if name == "fragment.html":
                    self.assertEqual(decorated, original)
                    continue
                prefix = "." if name == "index.html" else "../../.."
                for icon in site_artwork.ICONS:
                    self.assertIn(f'href="{prefix}/{icon}"', decorated)
                    self.assertEqual((path.parent / prefix / icon).read_bytes(),
                                     (assets / icon).read_bytes())
                self.assertIn('sizes="32x32"', decorated)
                self.assertIn('sizes="180x180"', decorated)
                self.assertEqual(decorated.count(site_artwork.MARKER), 1)
            before = {name: (root / name).read_bytes() for name in documents}
            site_artwork.decorate(assets, root)
            self.assertEqual(before, {name: (root / name).read_bytes() for name in documents})


if __name__ == "__main__":
    unittest.main()
