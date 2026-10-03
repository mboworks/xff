#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Lossless storage for generated Pages data; measurements keep their original bytes."""

import argparse
import gzip
import json
from pathlib import Path


def packed(value):
    """Intern repeated JSON values once, in children-before-parents order."""
    nodes, identities = [], {}

    def visit(item):
        if isinstance(item, dict):
            node = ['d', *[part for key, child in item.items() for part in (visit(key), visit(child))]]
        elif isinstance(item, list):
            node = ['l', *[visit(child) for child in item]]
        else:
            node = ['v', item]
        key = json.dumps(node, ensure_ascii=False, separators=(',', ':'), allow_nan=False)
        if key not in identities:
            identities[key] = len(nodes)
            nodes.append(node)
        return identities[key]

    root = visit(value)
    return {'xff_storage': 1, 'root': root, 'nodes': nodes}


def unpacked(value):
    if not isinstance(value, dict) or value.get('xff_storage') != 1:
        return value
    nodes = []
    for node in value['nodes']:
        if node[0] == 'v':
            item = node[1]
        else:
            if any(type(index) is not int or index < 0 or index >= len(nodes) for index in node[1:]):
                raise ValueError('invalid packed JSON reference')
            children = [nodes[index] for index in node[1:]]
            if node[0] == 'd' and len(children) % 2 == 0:
                item = dict(zip(children[::2], children[1::2]))
            elif node[0] == 'l':
                item = children
            else:
                raise ValueError('invalid packed JSON node')
        nodes.append(item)
    return nodes[value['root']]


def compact_json(path, intern=False):
    original = path.read_bytes()
    data = (json.dumps(packed(json.loads(original)), ensure_ascii=False, separators=(',', ':'),
                       allow_nan=False).encode() if intern else original)
    destination = path.with_suffix(path.suffix + '.gz')
    destination.write_bytes(gzip.compress(data, mtime=0))
    path.unlink()


def expand_benchmarks(root):
    """Expand only machine-readable inputs needed by trusted publisher programs."""
    for name in ('report.json.gz', 'normalization.json.gz', 'artifact.json.gz', 'landscape.json.gz'):
        for path in root.rglob(name):
            destination = path.with_suffix('')
            if destination.exists():
                continue
            content = gzip.decompress(path.read_bytes())
            if name == 'landscape.json.gz':
                content = json.dumps(unpacked(json.loads(content)), ensure_ascii=False, allow_nan=False).encode()
            destination.write_bytes(content)
    for path in root.rglob('page.html.gz'):
        path.with_name('index.html').write_bytes(gzip.decompress(path.read_bytes()))


def compact_benchmarks(root):
    """Share chart data between the history explorer and each standalone report."""
    decoder = json.JSONDecoder()
    for page in root.rglob('index.html'):
        content = page.read_text()
        if 'data-packed-page=' in content:
            continue
        marker = 'const figures='
        start = content.find(marker)
        if start >= 0 and content[start + len(marker):].startswith('{'):
            figures, length = decoder.raw_decode(content[start + len(marker):])
            payload = page.with_name('landscape.json')
            # Each report's chart and explorer must use the exact same measured values.
            if payload.exists() and json.loads(payload.read_text()) != figures:
                raise ValueError(f'chart payload differs from standalone report: {page}')
            if not payload.exists():
                payload.write_text(json.dumps(figures, ensure_ascii=False, allow_nan=False))
            end = content.index('</script>', start)
            content = (content[:start] + '(async()=>{const figures=await window.XffSiteStorage.read("landscape.json");'
                       + content[start + len(marker) + length + 1:end]
                       + '})().catch(error=>{document.getElementById("landscape").textContent="Chart unavailable: "+error.message;});'
                       + content[end:])
        for name in ('report.json', 'normalization.json', 'artifact.json'):
            content = content.replace(f'href="{name}"', f'href="{name}.gz"')
        script = '/'.join(['..'] * len(page.parent.relative_to(root.parent).parts)) + '/site-storage.js'
        include = f'<script src="{script}"></script>'
        if include not in content:
            # Must run before the shared chart bundle or any inline explorer initialization.
            position = content.find('<script')
            if position < 0:
                position = content.find('</head>')
            if position < 0:
                position = 0
            content = content[:position] + include + content[position:]
        page.write_text(content)
    for name in ('report.json', 'normalization.json', 'artifact.json', 'landscape.json'):
        for path in root.rglob(name):
            compact_json(path, intern=name == 'landscape.json')
    for report in root.rglob('report.json.gz'):
        compact_page(report.with_name('index.html'), root.parent, 'report.json.gz')


def compact_page(page, root, data):
    if not page.exists():
        return
    if 'data-packed-page=' not in page.read_text():
        page.with_name('page.html.gz').write_bytes(gzip.compress(page.read_bytes(), mtime=0))
    prefix = '/'.join(['..'] * len(page.parent.relative_to(root).parts))
    page.write_text('<!doctype html><meta charset="utf-8"><title>Report</title>'
                        '<p id="loading" role="status">Loading report...</p>'
                        f'<script src="{prefix}/site-page.js" data-packed-page="page.html.gz"></script>'
                        f'<noscript>This report requires JavaScript. <a href="{data}">Download data</a>.</noscript>\n')


def compact_summaries(root):
    for summary in root.rglob('coverage-summary.json'):
        compact_json(summary)
    for page in root.rglob('index.html'):
        if page.parent.name == 'lcov':
            continue
        text = page.read_text().replace('coverage-summary.json"', 'coverage-summary.json.gz"')
        page.write_text(text)
        if page.with_name('coverage-meta.json').exists():
            compact_page(page, root.parent, 'coverage-summary.json.gz')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('expand-benchmarks',))
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    expand_benchmarks(args.root)


if __name__ == '__main__':
    main()
