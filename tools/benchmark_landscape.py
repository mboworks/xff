# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Render benchmark JSON as an offline, interactive Three.js comparison landscape.

Supply the built Three.js renderer with --renderer-js; no network is used by the page.
"""

import argparse
import html
import itertools
import json
import math
import os
from pathlib import Path
import re

import benchmark_datasets
import benchmark_matrix as matrix
import benchmark_normalization
import benchmark_overview
import benchmark_preview
import benchmark_provenance
import benchmark_records


ORDER_LABELS = {
    'similarity': 'Similarity, better toward center',
    'average': 'Average performance, best at center',
    'alphabetical': 'Alphabetical',
}


def platform_details(value):
    """Short display name only; retain the complete platform contract in raw data."""
    match = re.fullmatch(r'(macOS-[^-]+)-(arm64|x86_64)(?:-(?:arm|i386|x86_64))?-64bit(?:-Mach-O)?', value)
    if match:
        return match[1].replace('macOS-', 'macOS ') + ' / ' + match[2]
    return value


def layout_details(contract):
    """Name the workload revision anywhere multiple dataset recipes can coexist."""
    layouts = contract.get('layouts', [])
    if not layouts:
        return 'broad/v1 + deep/v1'
    return ' + '.join(f"{layout['name']}/v{layout['revision']}" for layout in layouts)


def dataset_metadata(record):
    """Separate the measured platform from the workload recipes in a dataset."""
    contract = record.get('tool_comparisons', {}).get('contract', {})
    platform = (record['series'] if benchmark_records.is_local(record) else
                benchmark_records.platform_key(record) or contract.get('platform', 'Unknown platform'))
    machine = contract.get('machine', record.get('contract', {}).get('machine', ''))
    if machine and machine.lower() not in platform.lower():
        platform += ' / ' + machine
    return dict(platform_name=platform, dataset_type=layout_details(contract))


def ordered_pairs(rows, mode):
    """Minimize adjacent mean absolute differences, then orient better inward."""
    if mode not in ORDER_LABELS:
        raise ValueError('unknown task order')
    profiles = {}
    for row in rows:
        pair = (row['task'], row['reference'])
        cell = (row['dataset'], row['cpus'], row['files'])
        profile = profiles.setdefault(pair, {})
        if cell in profile:
            raise ValueError('duplicate comparison cell')
        profile[cell] = 100 * (1 - row['xff_over_reference'])
    pairs = sorted(profiles)
    means = {pair: sum(profile.values()) / len(profile) for pair, profile in profiles.items()}
    if mode == 'alphabetical':
        return pairs
    if mode == 'average':
        return sorted(pairs, key=lambda pair: (-means[pair], pair))
    if len(pairs) < 2:
        return pairs
    distances = {}
    for i, left in enumerate(pairs):
        for j, right in enumerate(pairs):
            common = profiles[left].keys() & profiles[right].keys()
            distances[i, j] = (sum(abs(profiles[left][k] - profiles[right][k]) for k in sorted(common))
                               / len(common) if common else math.inf)
    n = len(pairs)
    if n <= 16:
        # Exact shortest Hamiltonian path; each state holds cost and deterministic tie-break path.
        states = {(1 << i, i): (0.0, (i,)) for i in range(n)}
        for mask in range(1, 1 << n):
            for last in range(n):
                current = states.get((mask, last))
                if current is None:
                    continue
                for nxt in range(n):
                    if mask & (1 << nxt) or not math.isfinite(distances[last, nxt]):
                        continue
                    key = (mask | (1 << nxt), nxt)
                    candidate = (current[0] + distances[last, nxt], current[1] + (nxt,))
                    if key not in states or candidate < states[key]:
                        states[key] = candidate
        candidates = [states[((1 << n) - 1, i)] for i in range(n)
                      if ((1 << n) - 1, i) in states]
    else:
        # Bound work for future larger reports; try every greedy starting point.
        candidates = []
        for start in range(n):
            path = [start]
            unused = set(range(n)) - {start}
            cost = 0.0
            while unused:
                nxt = min(unused, key=lambda i: (distances[path[-1], i], i))
                cost += distances[path[-1], nxt]
                path.append(nxt)
                unused.remove(nxt)
            if math.isfinite(cost):
                candidates.append((cost, tuple(path)))
    if not candidates:
        # No complete chain of overlapping profiles; do not invent comparisons.
        return sorted(pairs, key=lambda pair: (-means[pair], pair))
    result = [pairs[i] for i in min(candidates)[1]]
    if means[result[0]] < means[result[-1]]:
        result.reverse()
    return result


def comparison_colorscale(extent):
    """Keep the near-neutral blue band within +/-10 percentage points."""
    limit = max(20.0, extent)
    stops = [(-limit, '#ff3030'), (-20.0, '#84202a'), (-10.0, '#24477b'),
             (0.0, '#3269b5'), (10.0, '#24477b'), (20.0, '#12572d'), (limit, '#35ef69')]
    colors = {(value + limit) / (2 * limit): color for value, color in stops}
    colors[0.0] = '#ff3030'
    return limit, [[position, color] for position, color in sorted(colors.items())]


def padded_ticks(labels, align='left'):
    """Align tick text with non-breaking spaces retained by the renderer adapter."""
    width = max((len(label) for label in labels), default=0)
    if align == 'right':
        return ['\u00a0' * (width - len(label) + 3) + label for label in labels]
    return [label + '\u00a0' * (width - len(label) + 3) for label in labels]


def allocation_groups(report):
    groups = sorted(report.get('contract', {}).get('cpu_counts', []))
    return groups[:2] if len(groups) >= 2 and len(set(groups)) == len(groups) and groups[0] > 0 else None


def hover_rows(rows):
    return ''.join(f'<tr><th scope="row">{html.escape(label)}</th><td>{html.escape(value)}</td></tr>'
                   for label, value in rows)


def hover_table(rows):
    return '<table class="landscape-hover-table"><tbody>' + hover_rows(rows) + '</tbody></table>'


def normalized_hover(text, value):
    if not text.endswith('</tbody></table>'):
        return text
    if not value or value['status'] != 'available':
        reason = value['reason'] if value else 'No compatible reference history is available.'
        rows = [('Reference window', 'Unavailable: ' + reason)]
    else:
        window = value['window']
        first, last = window[0]['date'][:10], window[-1]['date'][:10]
        rows = [('Reference-normalized XFF', f'{value["xff_seconds"] * 1000:.3f} ms'),
                ('Window reference mean', f'{value["reference_mean_seconds"] * 1000:.3f} ms'),
                ('Correction factor', f'{value["factor"]:.4f}'),
                ('Reference window', f'{len(window)} measurements'),
                ('Window start', first), ('Window end', last)]
    return text.removesuffix('</tbody></table>') + hover_rows(rows) + '</tbody></table>'


def figure(report, order='similarity', metric='percent', groups=None, normalization=None):
    """Keep CPU/tree quadrants disconnected; never fill missing observations."""
    if metric not in ('percent', 'factor'):
        raise ValueError('unknown comparison metric')
    rows = matrix.relative_results(report)
    counts = sorted(report['contract']['file_counts'])
    if not counts or any(n <= 0 for n in counts):
        raise ValueError('file counts must be positive')
    groups = allocation_groups(report) if groups is None else groups
    if groups is None or len(groups) != 2 or groups[0] >= groups[1] or any(
            cpu not in report['contract']['cpu_counts'] for cpu in groups):
        raise ValueError('landscape requires two distinct positive allocation groups')
    rows = [row for row in rows if row['cpus'] in groups]
    pairs = ordered_pairs(rows, order)
    if not pairs:
        raise ValueError('no comparable measurements')
    lookup = {}
    for row in rows:
        key = (row['dataset'], row['cpus'], row['files'], row['task'], row['reference'])
        if key in lookup:
            raise ValueError('duplicate comparison cell')
        lookup[key] = row
    extent = max(1, max(abs(100 * (1 - r['xff_over_reference'])) for r in rows))
    extent, colorscale = comparison_colorscale(extent)
    traces = []
    ticks = []
    labels = []
    for cpu, side in zip(groups, (-1, 1), strict=True):
        ordered = list(reversed(counts)) if side < 0 else counts
        xs = [side * (0.3 + math.log10(n / counts[0])) for n in ordered]
        ticks.extend(xs)
        labels.extend(f'{n:,}' for n in ordered)
        for dataset, direction in (('broad', -1), ('deep', 1)):
            xgrid, ygrid, zgrid, hover, normalized = [], [], [], [], []
            for index, (task, reference) in enumerate(pairs, 1):
                values, texts = [], []
                for count in ordered:
                    row = lookup.get((dataset, cpu, count, task, reference))
                    values.append(None if row is None else 100 * (1 - row['xff_over_reference']))
                    texts.append('Missing measurement' if row is None else hover_table([
                        ('Task', task), ('Tree', dataset.title()),
                        ('Allocation', matrix.allocation_label(report, cpu)), ('File count', f'{count:,}'),
                        ('Reference tool', reference), ('Relative performance', f'{values[-1]:+.2f}%'),
                        ('Performance factor', f'{1 / row["xff_over_reference"]:.2f}x (reference/xff)'),
                        ('xff/reference', f'{row["xff_over_reference"]:.2f}'),
                        ('XFF time', f'{row["xff_seconds"] * 1000:.3f} ms'),
                        ('Reference time', f'{row["reference_seconds"] * 1000:.3f} ms'),
                    ]))
                xgrid.append(xs)
                ygrid.append(values)
                zgrid.append([direction * index] * len(xs))
                hover.append(texts)
                normalized.append([normalized_hover(text, (normalization or {}).get(
                    benchmark_normalization.cell_key({'dataset': dataset, 'name': task, 'cpus': cpu, 'files': count}, reference)))
                    for count, text in zip(ordered, texts)])
            traces.append(dict(type='surface', x=xgrid, y=ygrid, z=zgrid,
                               surfacecolor=ygrid, coloraxis='coloraxis', connectgaps=False,
                               text=hover, normalized_text=normalized, hoverinfo='text',
                               name=f'{dataset.title()} / {matrix.allocation_label(report, cpu)}',
                               lighting=dict(ambient=1, diffuse=0, specular=0),
                               showscale=False))
    task_ticks = [-i for i in range(len(pairs), 0, -1)] + list(range(1, len(pairs) + 1))
    task_labels = [f'{a} / {b}' for a, b in reversed(pairs)] + [f'{a} / {b}' for a, b in pairs]
    labels = padded_ticks(labels, 'right')
    task_labels = padded_ticks(task_labels)
    tick_font = dict(family='DejaVu Sans Mono, Menlo, Consolas, monospace', size=12)
    result = dict(data=traces, layout=dict(
        title=dict(text=html.escape(matrix.platform_title(report))), height=1000,
        margin=dict(l=10, r=10, t=70, b=10),
        coloraxis=dict(cmin=-extent, cmax=extent,
                       colorscale=colorscale,
                       colorbar=dict(title=dict(text='Relative performance %'), ticksuffix='%')),
        scene=dict(xaxis=dict(title=dict(text=f'{matrix.allocation_label(report, groups[0])} \u2190 File count \u2192 {matrix.allocation_label(report, groups[1])}'),
                              ticks='outside', tickfont=tick_font, tickvals=ticks, ticktext=labels),
                   yaxis=dict(title=dict(text='Relative performance (%)'), zeroline=True,
                              zerolinecolor='#3269b5', ticksuffix='%'),
                   zaxis=dict(title=dict(text='Broad FS \u2190 Task \u2192 Deep FS'),
                              ticks='outside', tickfont=tick_font, tickvals=task_ticks, ticktext=task_labels),
                   aspectmode='manual', aspectratio=dict(x=1.4, y=0.8, z=1.5),
                   camera=dict(up=dict(x=0, y=1, z=0), eye=dict(x=1.5, y=1.5, z=1.8))),
        template='plotly_white'))
    if metric == 'factor':
        # Transform heights; keep the percentage-based colors identical in both views.
        maximum = max(0.01, max(abs(math.log10(r['xff_over_reference'])) for r in rows))
        target_step = maximum / 4
        decade = math.floor(math.log10(target_step))
        step = next(multiple * 10 ** decade for multiple in (1, 2, 5, 10)
                    if multiple * 10 ** decade >= target_step)
        count = math.ceil(maximum / step)
        limit = count * step
        powers = [index * step for index in range(-count, count + 1)]
        labels = [f'{value:+g}' if value else '0' for value in powers]
        for surface in traces:
            surface['y'] = [[None if value is None else -math.log10(1 - value / 100)
                             for value in row] for row in surface['y']]
        result['layout']['scene']['yaxis'] = dict(
            title=dict(text='Relative performance (log10 ratio)'), zeroline=True,
            zerolinecolor='#3269b5', tickvals=powers, ticktext=labels, range=[-limit, limit])
        color_ticks = [(100 * (1 - 10 ** -power), label) for power, label in zip(powers, labels)
                       if -extent <= 100 * (1 - 10 ** -power) <= extent]
        result['layout']['coloraxis']['colorbar'] = dict(
            title=dict(text='log10 ratio'), tickvals=[value for value, _ in color_ticks],
            ticktext=[label for _, label in color_ticks])
    return result


def figures(report, normalization=None):
    result = {mode: {metric: figure(report, mode, metric, normalization=normalization) for metric in ('percent', 'factor')}
              for mode in ORDER_LABELS}
    result['overview'] = benchmark_overview.summarize(report)
    if len(report['contract']['cpu_counts']) > 2:
        result['allocation_pairs'] = [
            dict(value=f'{left}/{right}',
                 label=f'{matrix.allocation_label(report, left)} / {matrix.allocation_label(report, right)}',
                 figures={mode: {metric: figure(report, mode, metric, (left, right), normalization)
                                 for metric in ('percent', 'factor')} for mode in ORDER_LABELS})
            for left, right in itertools.combinations(sorted(report['contract']['cpu_counts']), 2)]
    return result


def range_controls(attribute):
    return ('<label>Range: <select ' + attribute + '="range"><option value="auto">Auto</option>'
            + ''.join(f'<option value="{value}"' + (' selected' if value == 1 else '') + f'>+/- {int(value * 100)}%</option>'
                      for value in (0.2, 0.5, 1, 2)) + '</select></label> '
            '<label>Out of range: <select ' + attribute + '="overflow">'
            '<option value="cap">Cap</option><option value="cut">Cut off</option></select></label> ')


def view_controls(attribute):
    return (range_controls(attribute) + '<label>Timings: <select ' + attribute + '="normalization">'
            '<option value="reference">Reference normalized</option><option value="raw">Raw</option></select></label> ')


def history_panel(catalog):
    """A bounded catalog; only the selected report's six views are downloaded."""
    data = json.dumps(catalog, allow_nan=False).replace('<', '\\u003c')
    return ('<section id="benchmark-explorer">'
            '<details id="benchmark-history-chart" open><summary hidden>3D comparison chart</summary>'
            '<details data-help-panel class="landscape-card">'
            '<summary><strong>Help</strong></summary><div class="landscape-help-body">'
            '<p>Select a measured dataset and version. Oldest is left; newest is right. '
            'A dataset groups measurements from the same machine and measurement configuration. '
            'A dataset switch retains the commit when available, otherwise selects its newest report. '
            'Only retained successful reports with comparison data appear. '
            'Each chart compares xff with reference tools measured in that run; '
            'different dates, machines or measurement contracts are not paired performance comparisons. '
            'The default +/-100% range keeps the vertical scale fixed across versions; Auto fits each report.</p>'
            '<p>Drag to rotate; right-drag to pan; scroll to zoom. Focus the chart for arrow-key rotation, '
            '+/- zoom and Home reset. Percentage = 100 &times; (1 - xff/reference time); '
            'logarithmic = log10(reference/xff time). Green is faster, red slower. '
            'Surfaces connect measured neighbors; they are not predictions.</p>'
            '</div></details>'
            '<details data-view-panel class="landscape-card" open>'
            '<summary><strong>View</strong><span data-view-platform>No measured datasets</span></summary>'
            '<div class="landscape-view-controls">'
            '<label>Dataset: <select data-control="platform"></select></label> '
            '<label>Source: <select data-control="source"><option value="merged">Merged history</option></select></label> '
            '<label>Order: <select data-control="order">' +
            ''.join('<option value="' + key + '">' + label + '</option>'
                    for key, label in ORDER_LABELS.items()) + '</select></label> '
            '<label data-allocation-label hidden>Workers: <select data-control="allocations"></select></label> '
            '<label>Timings: <select data-control="normalization">'
            '<option value="reference">Reference normalized</option><option value="raw">Raw</option></select></label> '
            '<button type="button" data-reset>Reset</button></div></details>'
            '<details data-performance-panel class="landscape-card landscape-legend-panel" open>'
            '<summary><strong>Performance</strong></summary><div data-performance-body>'
            '<div data-performance-controls style="display:flex;flex-wrap:wrap;align-items:center;gap:.5rem;margin-bottom:6px">'
            '<label>Scale: <select data-control="metric"><option value="percent">Percentage</option>'
            '<option value="factor">Logarithmic</option></select></label> '
            + range_controls('data-control') + '</div></div></details>'
            '<div data-chart><details data-version-panel class="landscape-card" open>'
            '<summary><strong>Version</strong></summary><div data-version-body>'
            '<table class="landscape-version-table"><tbody>'
            '<tr><th scope="row">Version</th><td data-version-count></td></tr>'
            '<tr><th scope="row">Commit</th><td><a data-report title="Open the full benchmark report"></a></td></tr>'
            '<tr data-links-row><th scope="row">PR / Release</th><td data-version-links>Not available</td></tr>'
            '<tr><th scope="row">Revision date</th><td data-revision-date></td></tr>'
            '<tr data-measured-row><th scope="row">Measured</th><td data-measured>Not recorded</td></tr>'
            '<tr><th scope="row">Platform</th><td data-platform></td></tr>'
            '<tr><th scope="row">Dataset type</th><td data-dataset-type></td></tr>'
            '<tr><th scope="row">Binary</th><td data-binary-state>Not recorded</td></tr>'
            '<tr><th scope="row">SHA-256</th><td><details data-binary-details>'
            '<summary data-binary-hash>Not recorded</summary><code data-binary-sha256></code>'
            '<div data-binary-evidence></div></details></td></tr>'
            '<tr><th scope="row">Details</th><td><div data-platform-details></div></td></tr>'
            '</tbody></table><div role="status" aria-live="polite" hidden></div>'
            '<div class="landscape-version-control">'
            '<button type="button" data-version-previous aria-label="Previous version">-</button>'
            '<input data-control="version" type="range" min="0" max="0" step="1" value="0" '
            'aria-label="Version"><button type="button" data-version-next aria-label="Next version">+</button>'
            '</div></div></details></div></details>'
            '<section data-overview aria-live="polite"><h2>Performance overview</h2>'
            '<p>Select a measurement to compare it with its recorded merged baseline.</p></section>'
            '<script src="assets/three-landscape.js"></script><script>'
            'window.XffBenchmarkHistory(document.getElementById("benchmark-explorer"),' + data + ');'
            '</script></section>')


def publish_history(root, catalog):
    page = root / 'index.html'
    if not page.exists():
        return
    text = page.read_text()
    start, end = '<!-- benchmark-explorer:start -->', '<!-- benchmark-explorer:end -->'
    if start in text:
        before, _, rest = text.partition(start)
        _, separator, after = rest.partition(end)
        if not separator:
            raise ValueError('incomplete benchmark explorer section')
        text = before + after
    toggle_start, toggle_end = '<!-- benchmark-chart-toggle:start -->', '<!-- benchmark-chart-toggle:end -->'
    if toggle_start in text:
        before, _, rest = text.partition(toggle_start)
        _, separator, after = rest.partition(toggle_end)
        if not separator:
            raise ValueError('incomplete benchmark chart toggle')
        text = before + after
    selected = {}
    for item in catalog:
        record_path = root / item['report'] / 'report.json'
        if benchmark_records.exists(record_path):
            item['binary'] = binary_evidence(record_path, benchmark_records.read(record_path))
        key = (item.get('source', 'merged'), item['platform'], item['commit'])
        rank = (item.get('backfill', False), item.get('measured', ''), item.get('run', 0), item.get('attempt', 0))
        if key not in selected or rank > selected[key][0]:
            selected[key] = (rank, item)
    entries = sorted((item for _, item in selected.values()),
                     key=lambda item: (item['date'], item.get('run', 0), item.get('attempt', 0)))
    for index, item in enumerate(entries):
        if 'platform_name' not in item or 'dataset_type' not in item:
            report_path = root / item['report'] / 'report.json'
            if benchmark_records.exists(report_path):
                entries[index] = dict(item, **dataset_metadata(benchmark_records.read(report_path)))
    heading_end = text.index('</h1>')
    toggle = (toggle_start + '<button type="button" data-history-chart-toggle '
              'aria-controls="benchmark-history-chart" aria-expanded="true" '
              'style="float:right;font-size:1rem">Hide chart</button>' + toggle_end)
    text = text[:heading_end] + toggle + text[heading_end:]
    insertion = text.index('</h1>') + len('</h1>')
    page.write_text(text[:insertion] + start + history_panel(entries) + end + text[insertion:], encoding='utf-8')


def binary_evidence(path, record):
    proof = path.with_name('binary-verification.json')
    if benchmark_records.exists(proof):
        try:
            retained = benchmark_records.read(proof)
        except (ValueError, OSError):
            retained = {}
        return benchmark_provenance.describe(record, retained)
    return benchmark_provenance.describe(record)


def render(report, javascript, normalization=None):
    plots = figures(report, normalization)
    groups = allocation_groups(report)
    plot = json.dumps(plots, allow_nan=False, ensure_ascii=False).replace('<', '\\u003c')
    return ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>' + html.escape(matrix.platform_title(report)) + ' - landscape</title>'
            '<style>body{font:16px system-ui;margin:1.5rem}table{border-collapse:collapse}'
            'td,th{padding:.35rem;border:1px solid #ccd}details{margin:1rem 0;overflow:auto}'
            'summary{cursor:pointer;font-weight:bold}</style></head><body>'
            '<h1>Benchmark comparison landscape</h1>'
            '<details id="landscape-panel" open><summary>3D comparison chart (show/hide)</summary>'
            '<p>Drag to rotate; scroll to zoom; right-drag to pan. Focus the chart for arrow-key rotation, +/- zoom, and Home reset. Y is vertical. '
            + (f'Left: {matrix.allocation_label(report, groups[0])}, large to small; '
               f'right: {matrix.allocation_label(report, groups[1])}, small to large. '
               if len(report['contract']['cpu_counts']) == 2 else
               'Choose the worker pair below. Left runs large to small; right runs small to large. ')
            + 'The mirrored X axis uses log10 spacing. '
            'Broad and Deep tasks extend in opposite Z directions.</p>'
            '<p>Relative performance (%) = 100 &times; (1 - xff time / reference time). Green is faster, '
            'blue is equal, red is slower; this reverses the sign of the table difference. '
            'Blue spans +/-10 percentage points; dark red/green at -/+20 points brightens toward the extremes. Hover for timings and ratios. '
            'Surfaces interpolate neighboring measured points, including between categorical tasks; '
            'they are visual guides, not predictions. Gaps between CPU/tree groups are intentional.</p>'
            '<p>Better profiles face the center on both tree halves. Similarity order minimizes '
            'neighbor differences; it is not necessarily monotonic. Average order descends by mean '
            'relative performance. All file counts, CPU groups and tree shapes receive equal weight.</p>'
            '<p>Performance factor = reference time / xff time: 2x is twice as fast; 0.5x is half as fast. '
            'Factor heights and tick labels use log10: 0 is parity, +1 means 10x, -1 means 0.1x. Colors and task order keep the same meaning in both views.</p>'
            '<p>Range limits affect only the view. Cap shows outliers at the boundary in bright green/red; '
            'Cut off hides outliers and surface cells that touch them. Hover cards retain the original values. '
            'Reference-normalized timings scale each XFF time by its five-measurement reference window mean '
            'divided by its own reference time. Ratios and the raw tables are unchanged; unavailable windows are labeled.</p>'
            '<label>Scale: <select id="metric"><option value="percent">Percentage</option>'
            '<option value="factor">Logarithmic</option></select></label> '
            + view_controls('id') +
            '<label>Task order: <select id="task-order">' +
            ''.join('<option value="' + key + '">' + label + '</option>'
                    for key, label in ORDER_LABELS.items()) + '</select></label>'
            '<label id="allocation-label" hidden>Workers: <select id="allocations"></select></label>'
            '<button id="reset-landscape" type="button">Reset view</button><div id="landscape"></div><script>' + javascript + '</script><script>'
            'const figures=' + plot + ';'
            'const chart=window.XffLandscape(document.getElementById("landscape"),figures);'
            'document.getElementById("landscape-panel").addEventListener("toggle",event=>{'
            'if(event.target.open)requestAnimationFrame(()=>chart.resize());});'
            'const allocations=document.getElementById("allocations");'
            'for(const pair of figures.allocation_pairs||[]){allocations.add(new Option(pair.label,pair.value));}'
            'document.getElementById("allocation-label").hidden=!figures.allocation_pairs;'
            'function updateLandscape(){const selected=figures.allocation_pairs?.find(pair=>pair.value===allocations.value)?.figures||figures;'
            'const metric=document.getElementById("metric").value;'
            'chart.update(document.getElementById("task-order").value,metric,selected,'
            'window.XffLandscapeView(metric,document.getElementById("range"),document.getElementById("overflow"),document.getElementById("normalization")));}'
            'for(const id of ["task-order","metric","allocations","range","overflow","normalization"])document.getElementById(id).addEventListener("change",updateLandscape);'
            'updateLandscape();'
            'document.getElementById("reset-landscape").addEventListener("click",()=>chart.reset());</script></details>'
            '<h2>Measurements</h2>' + matrix.render_html(report) + '</body></html>')


def publish(root, javascript, previews=(), incremental=False, repository='mboworks/xff'):
    """Decorate retained report pages, sharing one plotting bundle across all reports."""
    asset = root / 'assets' / 'three-landscape.js'
    asset.parent.mkdir(parents=True, exist_ok=True)
    asset.write_text(javascript, encoding='utf-8')
    start_marker = '<!-- benchmark-landscape:start -->'
    end_marker = '<!-- benchmark-landscape:end -->'
    count = 0
    catalog_path = root / 'catalog.json'
    incremental = incremental and catalog_path.exists()
    catalog = ([item for item in json.loads(catalog_path.read_text()) if item.get('source', 'merged') == 'merged']
               if incremental else [])
    links_path = root / 'version-links.json'
    version_links = json.loads(links_path.read_text()) if links_path.exists() else {}
    records = [(path, json.loads(path.read_text())) for path in benchmark_records.paths(root)]
    preview_records = [(path, json.loads(path.read_text())) for path in previews]
    windows = benchmark_normalization.reference_windows([(path.relative_to(root).as_posix(), record)
                                                        for path, record in [*records, *preview_records]])
    for path, record in preview_records:
        relative = '../' * len(path.parent.relative_to(root).parts)
        path.with_name('index.html').write_text(benchmark_preview.render_report(record, repository, relative))
    for path, record in ([*preview_records] if incremental else [*records, *preview_records]):
        report = record.get('tool_comparisons')
        if not report or allocation_groups(report) is None:
            continue
        if not matrix.relative_results(report):
            continue
        source = record.get('source')
        local = benchmark_records.is_local(record)
        preview = benchmark_records.is_preview(record)
        normalization = windows.get(path.relative_to(root).as_posix(), {})
        path.with_name('normalization.json').write_text(json.dumps(normalization, allow_nan=False), encoding='utf-8')
        if (source or local) and (not preview or path in previews):
            payload = path.with_name('landscape.json')
            payload.write_text(json.dumps(figures(report, normalization), allow_nan=False), encoding='utf-8')
            contract = report.get('contract', {})
            platform = benchmark_records.platform_key(record) or contract.get('platform', 'Unknown platform')
            machine = contract.get('machine', record.get('contract', {}).get('machine', ''))
            if machine and machine.lower() not in platform.lower():
                platform += ' / ' + machine
            if local:
                platform = 'Local / ' + record['series'] + ' / ' + machine + ' / ' + layout_details(contract)
            commit = record.get('head') or source['head_sha']
            label = ('Local' if local else source['head_branch']) + ' / ' + commit[:10]
            if source and source.get('pull_requests'):
                label = 'PR ' + str(source['pull_requests'][0]['number']) + ' / ' + commit[:10]
            if benchmark_records.is_backfill(record):
                label = 'CI backfill / ' + commit[:10]
            links = version_links.get(commit, [])
            if preview:
                number = record.get('pull_number') or source['pull_requests'][0]['number']
                links = [{'label': f'PR #{number} preview', 'href': f'https://github.com/{repository}/pull/{number}'}]
            catalog.append(dict(platform=platform, commit=commit, label=label,
                                **dataset_metadata(record),
                                source=f'pr-{number}' if preview else 'merged',
                                source_label=f'PR #{number} preview' if preview else 'Merged history',
                                links=links,
                                binary=binary_evidence(path, record),
                                date=benchmark_records.reference_time(record),
                                measurement_date=record.get('completed_at'),
                                backfill=benchmark_records.is_backfill(record),
                                **(dict(measured=record['completed_at'], local=True) if local else
                                   dict(run=int(source['id']), attempt=int(source['run_attempt']))),
                                report=path.parent.relative_to(root).as_posix() + '/',
                                figures=payload.relative_to(root).as_posix(), identity=matrix.platform_title(report),
                                platform_details=platform_details(contract.get('platform', 'Platform details not recorded'))))
        page = path.with_name('index.html')
        text = page.read_text()
        if start_marker in text:
            before, _, rest = text.partition(start_marker)
            _, separator, after = rest.partition(end_marker)
            if not separator:
                raise ValueError('incomplete landscape section')
            text = before + after
        # Keep existing provenance, baseline tables and all detailed metrics intact.
        document = render(report, '', normalization)
        fragment = document.split('<h1>Benchmark comparison landscape</h1>', 1)[1].split('<h2>Measurements</h2>', 1)[0]
        script = html.escape(Path(os.path.relpath(asset, page.parent)).as_posix(), quote=True)
        fragment = fragment.replace('<script></script>', f'<script src="{script}"></script>')
        insertion = text.index('</h1>') + len('</h1>')
        section = start_marker + '<h2>Comparison landscape</h2>' + fragment + end_marker
        page.write_text(text[:insertion] + section + text[insertion:], encoding='utf-8')
        count += 1
    catalog_path.write_text(json.dumps(catalog, indent=2, allow_nan=False) + '\n')
    publish_history(root, catalog)
    benchmark_datasets.publish(root)
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path, nargs='?')
    parser.add_argument('--site-root', type=Path)
    parser.add_argument('--renderer-js', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--pulls', type=Path, help='Trusted PR API metadata, for current preview selection')
    parser.add_argument('--repository', default='mboworks/xff')
    parser.add_argument('--incremental-previews', action='store_true')
    args = parser.parse_args()
    if args.site_root:
        if args.report or args.output:
            parser.error('--site-root cannot be combined with report or --output')
        pulls = json.loads(args.pulls.read_text()) if args.pulls else []
        if pulls and isinstance(pulls[0], list):
            pulls = [pull for page in pulls for pull in page]
        previews = benchmark_preview.selected(args.site_root, pulls)
        print(f'Decorated {publish(args.site_root, args.renderer_js.read_text(), previews, args.incremental_previews, args.repository)} benchmark pages')
        return
    if args.report is None or args.output is None:
        parser.error('report and --output are required without --site-root')
    data = json.loads(args.report.read_text())
    report = data.get('tool_comparisons', data)
    args.output.write_text(render(report, args.renderer_js.read_text()), encoding='utf-8')
    print(args.output)


if __name__ == '__main__':
    main()
