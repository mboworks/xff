# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Import measured extended history and expose it in the existing benchmark viewer."""

import argparse
from datetime import datetime, timezone
import html
import json
import os
from pathlib import Path

import benchmark_datasets
import benchmark_extended as suite
import benchmark_extended_records as records
import benchmark_landscape as landscape
import benchmark_matrix as matrix
import benchmark_normalization


def import_campaign(campaign, builds_root, root, series, platform, machine):
    records.require(not (campaign / 'invalid.json').exists(), 'campaign integrity check failed')
    progress = records.read(campaign / 'progress.json')
    records.require(progress['state'] == 'complete' and progress['completed'] == progress['total'],
                    'campaign is incomplete')
    contract = records.read(campaign / 'contract.json')
    records.require(progress['total'] == len(contract['cases']) * len(contract['revisions']), 'campaign grid changed')
    anchor = contract['anchor']
    anchor_build = records.build_proof(builds_root / 'binaries' / anchor['sha'], anchor)
    result = []
    for revision in contract['revisions']:
        rows = [records.read(campaign / 'cells' / revision['sha'] / suite.Case(**case).key / 'complete.json')
                for case in contract['cases']]
        builds = {anchor['sha']: anchor_build,
                  revision['sha']: records.build_proof(builds_root / 'binaries' / revision['sha'], revision)}
        record = records.make_record(contract, revision, rows, builds, platform, machine, series, 'fixed-anchor',
                                     retention_reference=str(builds_root / 'binaries' / revision['sha'] / 'xff_full')
                                     + ' (original retained production executable)')
        path = records.retain(root, record)
        result.append(path)
        print(f'IMPORTED {len(result)}/{len(contract["revisions"])} {revision["sha"]} {path}', flush=True)
    return result


def descriptor(record, view):
    return {'series': record['series'], 'origin': record['origin'], 'platform': record['platform'],
            'machine': record['machine'], 'recipe': record['contract']['recipe'], 'view': view,
            'comparison': record['comparison'],
            'fixed_anchor': record['contract']['anchor']['sha'] if record['comparison'] == 'fixed-anchor' else None,
            'target': '//xff/cli:xff_full', 'repetitions': 9, 'keep': 7,
            'storage': 'tmpfs' if record['platform'] == 'linux' else 'case-sensitive HFS+ RAM',
            'accounting': {key: record['contract']['launcher'][key] for key in ('wall_method', 'memory_method')}}


def retained(root):
    paths = {*root.glob('extended/observations/*/report.json'),
             *(path.with_suffix('') for path in root.glob('extended/observations/*/report.json.gz'))}
    selected = {}
    for path in sorted(paths):
        record = records.read(path)
        records.require(record['origin'] != 'ci' or record.get('publication_source'),
                        'CI publication requires trusted workflow metadata')
        records.validate(record, record.get('publication_source'))
        for view in ('traditional', 'anchor'):
            description = descriptor(record, view)
            series_id = records.identity(description)
            key = series_id, record['revision']['sha']
            collection = record.get('collection') or {}
            rank = (record['completed_at'], int(collection.get('run_id', 0)), int(collection.get('run_attempt', 0)), record['identity'])
            if key not in selected or rank > selected[key][0]:
                selected[key] = (rank, path, record, view, description)
    return sorted(selected.values(), key=lambda item: (item[2]['revision']['date'], item[2]['revision']['sha'], item[3]))


def publish(root, repository='mboworks/xff'):
    selected = retained(root)
    catalog_path = root / 'catalog.json'
    catalog = [item for item in records.read(catalog_path) if not item.get('extended')] if catalog_path.exists() else []
    links_path = root / 'version-links.json'
    version_links = records.read(links_path) if links_path.exists() else {}
    groups = {}
    for _, path, record, view, description in selected:
        series_id = records.identity(description)
        wrapper = records.view_report(record, view)
        groups.setdefault(series_id, []).append((path, record, view, description, wrapper))
    for series_id, group in groups.items():
        order = landscape.common_task_order([matrix.relative_results(wrapper['tool_comparisons']) for *_, wrapper in group])
        windows = benchmark_normalization.reference_windows([(record['identity'], wrapper) for _, record, _, _, wrapper in group])
        for path, record, view, description, wrapper in group:
            revision = record['revision']
            anchor = record['contract']['anchor']
            directory = root / 'extended' / 'views' / series_id / revision['sha']
            directory.mkdir(parents=True, exist_ok=True)
            (directory / 'report.json').write_text(json.dumps(wrapper, allow_nan=False) + '\n')
            report = wrapper['tool_comparisons']
            normalization = windows.get(record['identity'], {})
            data = landscape.figures(report, normalization, order)
            (directory / 'landscape.json').write_text(json.dumps(data, allow_nan=False) + '\n')
            (directory / 'normalization.json').write_text(json.dumps(normalization, allow_nan=False) + '\n')
            document = landscape.render(report, '', normalization, order)
            asset = html.escape(os.path.relpath(root / 'assets/three-landscape.js', directory), quote=True)
            document = document.replace('<script></script>', f'<script src="{asset}"></script>')
            raw = html.escape(os.path.relpath(path, directory), quote=True)
            title = ('Traditional tools' if view == 'traditional' else 'Parent XFF' if record['comparison'] == 'parent'
                     else 'Fixed XFF ' + anchor['sha'][:12])
            introduction = ('<p>Extended production suite: <code>' + suite.RECIPE + '</code>; full production CLI. '
                            + html.escape(title) + '. <a href="' + raw + '">Original observations and provenance</a>.</p>')
            document = document.replace('</h1>', '</h1>' + introduction, 1)
            (directory / 'index.html').write_text(document)
            local = record['origin'] == 'local'
            experiment = local and revision['pr'] <= 0
            label = ('Experiment ' + revision['sha'][:12] if experiment else
                     ('PR #' + str(revision['pr']) if revision['pr'] > 0 else 'main') + ' / ' + revision['sha'][:10])
            links = version_links.get(revision['sha'], [])
            if not links and revision['pr'] > 0:
                links = [{'label': 'PR #' + str(revision['pr']), 'href': f'https://github.com/{repository}/pull/{revision["pr"]}'}]
            links = [*links, {'label': ('Parent ' if record['comparison'] == 'parent' else 'Fixed reference ') + anchor['sha'][:12],
                              'href': f'https://github.com/{repository}/commit/{anchor["sha"]}'}]
            origin_label = 'Local / ' + record['series'] if local else 'CI / ' + record['platform']
            collection = record.get('collection') or {}
            catalog.append({'platform': origin_label + ' / ' + suite.RECIPE + ' / ' + title,
                            'platform_name': record['platform'] + ' / ' + record['machine'],
                            'dataset_type': suite.RECIPE + ' / ' + title, 'commit': revision['sha'],
                            'label': label, 'date': datetime.fromisoformat(revision['date']).astimezone(timezone.utc).isoformat(),
                            'measurement_date': record['completed_at'],
                            'source': 'experiment' if experiment else 'merged',
                            'source_label': 'Unmerged experiments' if experiment else 'Merged history',
                            'comparison_role': ('Parent ' if record['comparison'] == 'parent' else 'Fixed reference ') + anchor['sha'][:12],
                            'local': local, 'extended': True, 'series_id': series_id,
                            'measured': record['completed_at'], 'run': collection.get('run_id', 0),
                            'attempt': collection.get('run_attempt', 0), 'links': links,
                            'binary': landscape.binary_evidence(directory / 'report.json', wrapper),
                            'report': directory.relative_to(root).as_posix() + '/',
                            'figures': (directory / 'landscape.json').relative_to(root).as_posix(),
                            'identity': record['contract']['platform'] + ' / ' + suite.RECIPE,
                            'platform_details': record['contract']['platform']})
    catalog_path.write_text(json.dumps(catalog, indent=2, allow_nan=False) + '\n')
    landscape.publish_history(root, catalog)
    benchmark_datasets.publish(root)
    return len(selected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    importer = modes.add_parser('import')
    for name in ('campaign', 'build-root', 'root'):
        importer.add_argument('--' + name, type=Path, required=True)
    for name in ('series', 'platform', 'machine'):
        importer.add_argument('--' + name, required=True)
    renderer = modes.add_parser('publish')
    renderer.add_argument('--root', type=Path, required=True)
    renderer.add_argument('--repository', default='mboworks/xff')
    args = parser.parse_args()
    if args.mode == 'import':
        import_campaign(args.campaign, args.build_root, args.root, args.series, args.platform, args.machine)
    else:
        print(f'Published {publish(args.root, args.repository)} extended views')


if __name__ == '__main__':
    main()
