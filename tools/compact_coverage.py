#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Redirect duplicate stable coverage pages to their unchanged archived originals."""

import argparse
import html
import json
import os
from pathlib import Path
from urllib.parse import quote


def redirect(target: str) -> bytes:
    """Keep deep links, query parameters and source-line fragments usable."""
    url = quote(target, safe='/')
    return (f'<!doctype html><html><head><meta charset="utf-8">'
            f'<meta http-equiv="refresh" content="0; url={html.escape(url, quote=True)}">'
            f'<title>Coverage report</title></head><body>'
            f'<a href="{html.escape(url, quote=True)}">Open archived coverage report</a>'
            f'<script>location.replace({json.dumps(url)} + location.search + location.hash);</script>'
            '</body></html>\n').encode('utf-8')


def compact(root: Path, dry_run: bool = False) -> tuple[int, int]:
    """Only replace byte-identical HTML with matching authentic run provenance."""
    metadata = [root / 'main/coverage-meta.json',
                *sorted(root.glob('pr/*/coverage-meta.json')),
                *sorted(root.glob('tag/*/coverage-meta.json'))]
    count, saved = 0, 0
    for path in metadata:
        if not path.is_file():
            continue
        source = json.loads(path.read_text(encoding='utf-8'))['source']
        run, attempt = source['run_id'], source['run_attempt']
        if type(run) is not int or type(attempt) is not int or run <= 0 or attempt <= 0:
            continue
        archive = root / 'runs' / str(run) / str(attempt)
        archived_metadata = archive / 'coverage-meta.json'
        if not archived_metadata.is_file():
            continue
        original = json.loads(archived_metadata.read_text(encoding='utf-8'))['source']
        if any(source.get(key) != original.get(key) for key in ('run_id', 'run_attempt', 'head_sha')):
            continue
        for page in sorted(path.parent.rglob('*.html')):
            canonical = archive / page.relative_to(path.parent)
            if page.is_symlink() or canonical.is_symlink() or not canonical.is_file():
                continue
            size = page.stat().st_size
            if size != canonical.stat().st_size:
                continue
            replacement = redirect(Path(os.path.relpath(canonical, page.parent)).as_posix())
            if len(replacement) >= size or page.read_bytes() != canonical.read_bytes():
                continue
            if not dry_run:
                page.write_bytes(replacement)
            count += 1
            saved += size - len(replacement)
    return count, saved


def check_site_size(root: Path, limit: int = 9_000_000_000) -> int:
    """Leave one GB of headroom below Pages' hard TAR limit for packaging overhead."""
    size = sum(path.stat().st_size for path in root.rglob('*') if path.is_file())
    print(f'Pages file payload: {size:,} bytes; deployment ceiling: {limit:,} bytes')
    if size > limit:
        raise ValueError('Pages file payload exceeds the deployment ceiling; '
                         'review retention or hosting before uploading')
    return size


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path, help='Staged coverage directory')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--site-root', type=Path, help='Also check the complete staged site size')
    args = parser.parse_args()
    count, saved = compact(args.root, args.dry_run)
    print(f'Coverage duplicate pages: {count}; bytes saved: {saved:,}')
    if args.site_root is not None:
        check_site_size(args.site_root)


if __name__ == '__main__':
    main()
