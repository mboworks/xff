# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Compact, advisory comparisons of equivalent PR and merged benchmark cells."""

import html
import math
import statistics

import align_markdown_tables
import benchmark_matrix as matrix


def population(rows, threshold):
    """Give every compatible comparison equal weight, without averaging percentages."""
    factors = [row['xff_over_reference'] / row['main_ratio'] for row in rows]
    if any(not math.isfinite(value) or value <= 0 for value in factors):
        raise ValueError('comparison factors must be finite and positive')
    faster = sum(row['normalized_change_percent'] < -threshold for row in rows)
    slower = sum(row['normalized_change_percent'] > threshold for row in rows)
    return {'comparisons': len(rows), 'faster': faster, 'slower': slower,
            'within_threshold': len(rows) - faster - slower,
            'change_percent': 100 * math.expm1(statistics.fmean(math.log(value) for value in factors)) if factors else None}


def summarize(report, threshold=None, minimum_files=1):
    """Normalize each XFF/main change by the corresponding reference-tool change."""
    if threshold is None:
        threshold = report.get('alarm', {}).get('threshold_percent', 15)
    if not math.isfinite(threshold) or threshold <= 0 or minimum_files < 1:
        raise ValueError('positive threshold and minimum file count required')
    rows = matrix.relative_results(report) if all('dataset' in task for task in report['tasks']) else []
    matched = [row for row in rows if row['normalized_change_percent'] is not None]
    eligible = [row for row in matched if row['files'] >= minimum_files]
    warnings = [row for row in eligible if row['normalized_change_percent'] > threshold]
    worst = sorted((row for row in matched if row['normalized_change_percent'] > 0),
                   key=lambda row: (-row['normalized_change_percent'], row['task'], row['dataset'],
                                    row['cpus'], row['files'], row['reference']))[:5]
    return {'status': 'warning' if warnings else 'available' if matched else 'unavailable',
            'threshold_percent': threshold, 'minimum_files': minimum_files, 'blocking': False,
            'total_comparisons': len(rows), 'unmatched_comparisons': len(rows) - len(matched),
            'all': population(matched, threshold), 'warning_population': population(eligible, threshold),
            'warning_count': len(warnings), 'largest_regressions': worst,
            'baseline': {key: value for key, value in report.get('baseline', {}).items() if key != 'averages'}}


def signed_percent(value):
    return 'n/a' if value is None else f'{value:+.1f}%'


def explanation(summary):
    scope = f" at {summary['minimum_files']:,} or more files" if summary['minimum_files'] > 1 else ''
    if summary['status'] == 'unavailable':
        return 'No compatible merged baseline comparisons are available; regression status is unknown.'
    if summary['warning_count']:
        return (f"WARNING: {summary['warning_count']} comparison(s) exceed the "
                f"{summary['threshold_percent']:g}% slowdown threshold{scope}.")
    if not summary['warning_population']['comparisons']:
        return 'No warning-eligible comparisons are available; only smaller workloads could be compared.'
    return (f"No compatible comparison{scope} exceeds the "
            f"{summary['threshold_percent']:g}% slowdown threshold.")


def table_rows(summary):
    rows = [('All matched cases', summary['all'])]
    if summary['minimum_files'] > 1:
        rows.append((f"Warning set (>= {summary['minimum_files']:,} files)", summary['warning_population']))
    return rows


def baseline_text(summary):
    baseline = summary['baseline']
    if not baseline.get('head'):
        return 'Merged baseline: unavailable.'
    return (f"Merged baseline: {baseline['head'][:10]}; run {baseline.get('run', 'unrecorded')}, "
            f"attempt {baseline.get('attempt', 'unrecorded')}; {baseline.get('policy', 'sampling policy unrecorded')}.")


def render_html(report):
    summary = summarize(report)
    color = '#a12622' if summary['status'] == 'warning' else '#333'
    rows = []
    for label, values in table_rows(summary):
        cells = [label, str(values['comparisons']), signed_percent(values['change_percent']),
                 str(values['faster']), str(values['within_threshold']), str(values['slower'])]
        rows.append('<tr>' + ''.join('<td>' + html.escape(cell) + '</td>' for cell in cells) + '</tr>')
    worst = []
    for row in summary['largest_regressions']:
        cells = [row['task'], row['dataset'], str(row['cpus']), f"{row['files']:,}", row['reference'],
                 signed_percent(row['normalized_change_percent'])]
        worst.append('<tr>' + ''.join('<td>' + html.escape(cell) + '</td>' for cell in cells) + '</tr>')
    threshold = summary['threshold_percent']
    return ('<section class="benchmark-overview"><h2>Performance overview</h2>'
            f'<p style="color:{color}"><strong>{html.escape(explanation(summary))}</strong> Advisory only.</p>'
            '<p>' + html.escape(baseline_text(summary)) + '</p>'
            '<p>Typical change is the equal-weight geometric mean of compatible comparison factors. '
            'Each XFF/main timing ratio is divided by the corresponding reference-tool timing ratio. '
            'Negative is faster; positive is slower. Correlated workloads are not independent statistical evidence.</p>'
            f"<p>{summary['all']['comparisons']} of {summary['total_comparisons']} comparisons have a compatible baseline; "
            f"{summary['unmatched_comparisons']} are unavailable. No missing result is treated as zero.</p>"
            '<table><tr><th>Population</th><th>Comparisons</th><th>Typical change</th>'
            f'<th>Faster (&gt;{threshold:g}%)</th><th>Within {threshold:g}%</th><th>Slower (&gt;{threshold:g}%)</th></tr>'
            + ''.join(rows) + '</table>'
            + ('<h3>Largest regressions</h3><table><tr><th>Task</th><th>Tree</th><th>Workers</th>'
               '<th>Files</th><th>Reference</th><th>Change</th></tr>' + ''.join(worst) + '</table>' if worst else '')
            + '</section>')


def render_markdown(report):
    summary = summarize(report)
    lines = ['### Performance overview', '', '**' + explanation(summary) + '** Advisory only.', '',
             markdown_cell(baseline_text(summary)), '',
             f"Compatible comparisons: {summary['all']['comparisons']}/{summary['total_comparisons']}; "
             f"unavailable: {summary['unmatched_comparisons']}.", '',
             '| Population | Comparisons | Typical change | Faster | Within threshold | Slower |',
             '| :-- | --: | --: | --: | --: | --: |']
    for label, values in table_rows(summary):
        lines.append(f"| {label} | {values['comparisons']} | {signed_percent(values['change_percent'])} | "
                     f"{values['faster']} | {values['within_threshold']} | {values['slower']} |")
    if summary['largest_regressions']:
        lines.extend(['', 'Largest regressions:', '', '| Task | Tree | Workers | Files | Reference | Change |',
                      '| :-- | :-- | --: | --: | :-- | --: |'])
        for row in summary['largest_regressions']:
            cells = [row['task'], row['dataset'], str(row['cpus']), f"{row['files']:,}", row['reference'],
                     signed_percent(row['normalized_change_percent'])]
            lines.append('| ' + ' | '.join(markdown_cell(cell) for cell in cells) + ' |')
    lines.extend(['', 'Typical change is an equal-weight geometric mean of reference-adjusted XFF/main factors. '
                  'Negative is faster; positive is slower. These correlated cases are not a significance test.'])
    return align_markdown_tables.align_text('\n'.join(lines) + '\n')


def markdown_cell(value):
    value = str(value).replace('\n', ' ').replace('\r', ' ')
    for character in ('\\', '`', '|', '[', ']', '*', '_'):
        value = value.replace(character, '\\' + character)
    return html.escape(value, quote=False).replace('@', '&#64;')
