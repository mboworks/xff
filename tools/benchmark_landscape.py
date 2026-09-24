# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Render benchmark JSON as an offline, interactive Three.js comparison landscape.

Supply the built Three.js renderer with --renderer-js; no network is used by the page.
"""

import argparse
import html
import json
import math
import os
from pathlib import Path

import benchmark_matrix as matrix


ORDER_LABELS = {
    'similarity': 'Similarity, better toward center',
    'average': 'Average performance, best at center',
    'alphabetical': 'Alphabetical',
}


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


def figure(report, order='similarity', metric='percent'):
    """Keep CPU/tree quadrants disconnected; never fill missing observations."""
    if metric not in ('percent', 'factor'):
        raise ValueError('unknown comparison metric')
    rows = matrix.relative_results(report)
    counts = sorted(report['contract']['file_counts'])
    if not counts or any(n <= 0 for n in counts):
        raise ValueError('file counts must be positive')
    if sorted(report['contract']['cpu_counts']) != [1, 4]:
        raise ValueError('landscape requires the 1 and 4 allocation groups')
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
    for cpu, side in ((1, -1), (4, 1)):
        ordered = list(reversed(counts)) if cpu == 1 else counts
        xs = [side * (0.3 + math.log10(n / counts[0])) for n in ordered]
        ticks.extend(xs)
        labels.extend(f'{n:,}' for n in ordered)
        for dataset, direction in (('broad', -1), ('deep', 1)):
            xgrid, ygrid, zgrid, hover = [], [], [], []
            for index, (task, reference) in enumerate(pairs, 1):
                values, texts = [], []
                for count in ordered:
                    row = lookup.get((dataset, cpu, count, task, reference))
                    values.append(None if row is None else 100 * (1 - row['xff_over_reference']))
                    texts.append('Missing measurement' if row is None else
                                 f'{dataset.title()} / {html.escape(task)} / xff vs {html.escape(reference)}'
                                 f'<br>{matrix.allocation_label(report, cpu)} / {count:,} files'
                                 f'<br>Relative performance: {values[-1]:+.2f}%'
                                 f'<br>Performance factor: {1 / row["xff_over_reference"]:.2f}x (reference/xff)'
                                 f'<br>xff/reference: {row["xff_over_reference"]:.2f}'
                                 f'<br>xff: {row["xff_seconds"] * 1000:.3f} ms'
                                 f'<br>reference: {row["reference_seconds"] * 1000:.3f} ms')
                xgrid.append(xs)
                ygrid.append(values)
                zgrid.append([direction * index] * len(xs))
                hover.append(texts)
            traces.append(dict(type='surface', x=xgrid, y=ygrid, z=zgrid,
                               surfacecolor=ygrid, coloraxis='coloraxis', connectgaps=False,
                               text=hover, hoverinfo='text',
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
        scene=dict(xaxis=dict(title=dict(text=f'{matrix.allocation_label(report, 1)} \u2190 File count \u2192 {matrix.allocation_label(report, 4)}'),
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


def render(report, javascript):
    plots = {mode: {metric: figure(report, mode, metric) for metric in ('percent', 'factor')}
             for mode in ORDER_LABELS}
    plot = json.dumps(plots, allow_nan=False, ensure_ascii=False).replace('<', '\\u003c')
    return ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>' + html.escape(matrix.platform_title(report)) + ' - landscape</title>'
            '<style>body{font:16px system-ui;margin:1.5rem}table{border-collapse:collapse}'
            'td,th{padding:.35rem;border:1px solid #ccd}details{margin:1rem 0;overflow:auto}'
            'summary{cursor:pointer;font-weight:bold}</style></head><body>'
            '<h1>Benchmark comparison landscape</h1>'
            '<details id="landscape-panel" open><summary>3D comparison chart (show/hide)</summary>'
            '<p>Drag to rotate; scroll to zoom; right-drag to pan. Focus the chart for arrow-key rotation, +/- zoom, and Home reset. Y is vertical. Left: 1 allocation, large to small; '
            'right: 4 allocations, small to large. The mirrored X axis uses log10 spacing. '
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
            '<label>Scale: <select id="metric"><option value="percent">Percentage</option>'
            '<option value="factor">Logarithmic</option></select></label> '
            '<label>Task order: <select id="task-order">' +
            ''.join('<option value="' + key + '">' + label + '</option>'
                    for key, label in ORDER_LABELS.items()) + '</select></label>'
            '<button id="reset-landscape" type="button">Reset view</button><div id="landscape"></div><script>' + javascript + '</script><script>'
            'const figures=' + plot + ';'
            'const chart=window.XffLandscape(document.getElementById("landscape"),figures);'
            'document.getElementById("landscape-panel").addEventListener("toggle",event=>{'
            'if(event.target.open)requestAnimationFrame(()=>chart.resize());});'
            'function updateLandscape(){chart.update('
            'document.getElementById("task-order").value,document.getElementById("metric").value);}'
            'for(const id of ["task-order","metric"])document.getElementById(id).addEventListener("change",updateLandscape);'
            'document.getElementById("reset-landscape").addEventListener("click",()=>chart.reset());</script></details>'
            '<h2>Measurements</h2>' + matrix.render_html(report) + '</body></html>')


def publish(root, javascript):
    """Decorate retained report pages, sharing one plotting bundle across all reports."""
    asset = root / 'assets' / 'three-landscape.js'
    asset.parent.mkdir(parents=True, exist_ok=True)
    asset.write_text(javascript, encoding='utf-8')
    start_marker = '<!-- benchmark-landscape:start -->'
    end_marker = '<!-- benchmark-landscape:end -->'
    count = 0
    for path in sorted(root.glob('runs/*/*/**/report.json')):
        record = json.loads(path.read_text())
        report = record.get('tool_comparisons')
        if not report or sorted(report.get('contract', {}).get('cpu_counts', [])) != [1, 4]:
            continue
        if not matrix.relative_results(report):
            continue
        page = path.with_name('index.html')
        text = page.read_text()
        if start_marker in text:
            before, _, rest = text.partition(start_marker)
            _, separator, after = rest.partition(end_marker)
            if not separator:
                raise ValueError('incomplete landscape section')
            text = before + after
        # Keep existing provenance, baseline tables and all detailed metrics intact.
        document = render(report, '')
        fragment = document.split('<h1>Benchmark comparison landscape</h1>', 1)[1].split('<h2>Measurements</h2>', 1)[0]
        script = html.escape(Path(os.path.relpath(asset, page.parent)).as_posix(), quote=True)
        fragment = fragment.replace('<script></script>', f'<script src="{script}"></script>')
        insertion = text.index('</h1>') + len('</h1>')
        section = start_marker + '<h2>Comparison landscape</h2>' + fragment + end_marker
        page.write_text(text[:insertion] + section + text[insertion:], encoding='utf-8')
        count += 1
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path, nargs='?')
    parser.add_argument('--site-root', type=Path)
    parser.add_argument('--renderer-js', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.site_root:
        if args.report or args.output:
            parser.error('--site-root cannot be combined with report or --output')
        print(f'Decorated {publish(args.site_root, args.renderer_js.read_text())} benchmark pages')
        return
    if args.report is None or args.output is None:
        parser.error('report and --output are required without --site-root')
    data = json.loads(args.report.read_text())
    report = data.get('tool_comparisons', data)
    args.output.write_text(render(report, args.renderer_js.read_text()), encoding='utf-8')
    print(args.output)


if __name__ == '__main__':
    main()
