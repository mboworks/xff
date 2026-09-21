# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Sampling, compatible main baselines, and CPU-grouped benchmark tables."""

import html
import json
import math
import statistics


def selected_samples(report, entry):
    samples = entry['samples']
    contract = report['contract']
    if len(samples) != contract['repetitions'] or not samples:
        raise ValueError('missing tool comparison samples')
    keep = contract.get('retained', len(samples))
    if not isinstance(keep, int) or not 1 <= keep <= len(samples):
        raise ValueError('invalid retained sample count')
    for sample in samples:
        elapsed = sample['elapsed_seconds']
        if not isinstance(elapsed, (float, int)) or not math.isfinite(elapsed) or elapsed <= 0:
            raise ValueError('elapsed time must be finite and positive')
    return sorted(samples, key=lambda sample: sample['elapsed_seconds'])[:keep]


def elapsed_mean(report, entry):
    values = [sample['elapsed_seconds'] for sample in selected_samples(report, entry)]
    return (statistics.fmean(values) if report['contract'].get('estimator') == 'mean-fastest'
            else statistics.median(values))


def policy(report):
    contract = report['contract']
    if contract.get('estimator') == 'mean-fastest':
        return f"mean of fastest {contract['retained']}/{contract['repetitions']} runs"
    return f"median of {contract['repetitions']} runs"


def task_key(task):
    return f"{task['dataset']}/{task['name']}/{task['cpus']}/{task['files']}"


def compatibility(report):
    contract = report['contract']
    excluded = {'repetitions', 'retained', 'cpu_count', 'affinity_by_cpu_count', 'storage',
                'file_counts', 'cpu_counts', 'depth', 'depth_rule', 'invocation'}
    key = {name: value for name, value in contract.items() if name not in excluded}
    key['storage'] = {name: value for name, value in contract['storage'].items() if name != 'parent'}
    key['affinity_enforced'] = all(value is not None for value in contract['affinity_by_cpu_count'].values())
    return key


def cell_contract(task):
    return (task['fixture_identity'], task['expected_sha256'], task['input_files'],
            task.get('cpus'), task.get('files'))


def participant_contract(report, label, entry):
    identities = tuple((name, tuple(report['tools'].get(name, {}).get(field)
                                   for field in ('status', 'sha256', 'version')))
                       for name in label.split('+') if name != 'xff')
    arguments = tuple(tuple(command[1:]) for command in entry.get('pipeline', []))
    return identities, arguments


def attach_baseline(report, root):
    """Retained reports come from the trusted main publisher; never execute their data."""
    candidates = []
    current_tasks = {task_key(task): task for task in report['tasks']}
    for path in root.glob('runs/*/*/report.json'):
        record = json.loads(path.read_text())
        source = record.get('source', {})
        other = record.get('tool_comparisons')
        if source.get('event') != 'push' or source.get('head_branch') != 'main' or not other:
            continue
        if other['contract'].get('estimator') != 'mean-fastest':
            continue
        if compatibility(other) != compatibility(report):
            continue
        averages = {}
        for task in other['tasks']:
            current = current_tasks.get(task_key(task))
            if current is None or cell_contract(task) != cell_contract(current):
                continue
            shared = {tool: elapsed_mean(other, entry) for tool, entry in task['participants'].items()
                      if tool in current['participants'] and participant_contract(other, tool, entry) ==
                      participant_contract(report, tool, current['participants'][tool])}
            if shared:
                averages[task_key(task)] = shared
        if averages:
            candidates.append((source['created_at'], int(source['id']), int(source['run_attempt']), record, averages))
    if not candidates:
        report['baseline'] = {'status': 'unavailable', 'reason': 'No compatible successful main benchmark is retained.'}
        return
    selected = max(candidates, key=lambda row: row[:3])
    record, averages = selected[3:]
    other = record['tool_comparisons']
    report['baseline'] = {'status': 'available', 'head': record['head'], 'run': record['source']['id'],
                          'attempt': record['source']['run_attempt'], 'policy': policy(other), 'averages': averages}


def matrices(report):
    if not all('dataset' in task for task in report['tasks']):
        return []  # Legacy reports retain their original per-task table.
    cpus = report['contract']['cpu_counts']
    counts = report['contract']['file_counts']
    columns = [(cpu, count) for cpu in cpus for count in counts]
    groups = {}
    for task in report['tasks']:
        rows = groups.setdefault(task['dataset'], {})
        for tool in sorted(set(task['participants']) | set(task['skips'])):
            cells = rows.setdefault((task['name'], tool), {})
            if tool in task['skips']:
                cells[task['cpus'], task['files']] = 'Skipped: ' + task['skips'][tool]
                continue
            mean = elapsed_mean(report, task['participants'][tool])
            value = f'{mean * 1000:.2f}'
            baseline = report.get('baseline', {})
            previous = baseline.get('averages', {}).get(task_key(task), {}).get(tool)
            if previous is not None:
                if not isinstance(previous, (int, float)) or not math.isfinite(previous) or previous <= 0:
                    raise ValueError('invalid baseline average')
                value += f' / {previous * 1000:.2f} ({(mean / previous - 1) * 100:+.1f}%)'
            elif baseline.get('status') == 'available':
                value += ' / n/a'
            cells[task['cpus'], task['files']] = value
    return [(group, columns, sorted(rows.items())) for group, rows in sorted(groups.items())]


def relative_results(report):
    """Compare only participants performing the same task on the same tree."""
    rows = []
    for task in report['tasks']:
        participants = task['participants']
        if 'xff' not in participants:
            continue
        current = elapsed_mean(report, participants['xff'])
        baseline = report.get('baseline', {}).get('averages', {}).get(task_key(task), {})
        for tool, entry in participants.items():
            if tool == 'xff':
                continue
            reference = elapsed_mean(report, entry)
            ratio = current / reference
            previous = (baseline['xff'] / baseline[tool]
                        if baseline.get('xff', 0) > 0 and baseline.get(tool, 0) > 0 else None)
            rows.append({'dataset': task['dataset'], 'task': task['name'], 'files': task['files'],
                         'cpus': task['cpus'], 'reference': tool, 'xff_seconds': current,
                         'reference_seconds': reference, 'xff_over_reference': ratio,
                         'difference_seconds': current - reference, 'difference_percent': (ratio - 1) * 100,
                         'main_ratio': previous,
                         'normalized_change_percent': (ratio / previous - 1) * 100 if previous else None})
    return rows


def regressions(report, threshold, minimum_files=1000):
    if not math.isfinite(threshold) or threshold <= 0 or minimum_files < 1:
        raise ValueError('regression threshold and minimum files must be positive')
    return [row for row in relative_results(report) if row['files'] >= minimum_files
            and row['normalized_change_percent'] is not None and row['normalized_change_percent'] > threshold]


def relative_matrices(report):
    columns = [(cpu, count) for cpu in report['contract']['cpu_counts'] for count in report['contract']['file_counts']]
    groups = {}
    for row in relative_results(report):
        values = groups.setdefault(row['dataset'] + ' - xff/reference ratios', {}).setdefault(
            (row['task'], 'xff / ' + row['reference']), {})
        value = f"{row['xff_over_reference']:.2f}x ({row['difference_percent']:+.1f}%)"
        if row['normalized_change_percent'] is not None:
            value += f"; vs main {row['normalized_change_percent']:+.1f}%"
        values[row['cpus'], row['files']] = value
    return [(group, columns, sorted(rows.items())) for group, rows in sorted(groups.items())]


def alarm_text(report):
    alarm = report.get('alarm')
    if not alarm:
        return ''
    cells = alarm['cells']
    status = f"{len(cells)} cell(s) exceed" if cells else 'No comparable cells exceed'
    return f"{status} the {alarm['threshold_percent']:g}% normalized slowdown alarm. Advisory only; does not block merging."


def baseline_text(report):
    baseline = report.get('baseline')
    if not baseline:
        return ''
    if baseline['status'] != 'available':
        return baseline['reason']
    return (f"Main baseline: {baseline['head']}, run {baseline['run']}, attempt {baseline['attempt']}; "
            f"{baseline['policy']}. Positive changes are slower. "
            'n/a means no compatible baseline cell. Different sampling policies are labelled; '
            'comparisons are informational, not regression gates.')


def ratio_html(value, ratio):
    text, separator, baseline = value.partition('; vs main ')
    if ratio is not None and ratio != 1:
        color = '#a12622' if ratio > 1 else '#176b36'
        text = f'<span style="color:{color}">' + html.escape(text) + '</span>'
    else:
        text = html.escape(text)
    return text + html.escape(separator + baseline)


def render_html(report):
    result = ['<p>' + html.escape(policy(report)) + '; absolute tables: elapsed milliseconds; delta T is current minus main, also in milliseconds. '
              'Ratio tables: xff/reference, greater than 1 means xff is slower. '
              'Dark red ratios mean xff is slower; dark green means faster; equal ratios stay neutral. '
              'Vs main is the change in that ratio, not the raw elapsed-time change.</p>']
    if alarm_text(report):
        result.append('<p>' + html.escape(alarm_text(report)) + '</p>')
    if baseline_text(report):
        result.append('<p>' + html.escape(baseline_text(report)) + '</p>')
    absolute = matrices(report)
    ratios = {(row['dataset'] + ' - xff/reference ratios', row['task'], 'xff / ' + row['reference'],
               (row['cpus'], row['files'])): row['xff_over_reference'] for row in (relative_results(report) if absolute else [])}
    for sections, relative in ((absolute, False), (relative_matrices(report) if absolute else [], True)):
        if not sections:
            continue
        columns = sections[0][1]
        title = 'xff/reference ratios' if relative else 'Elapsed time'
        labels = ('Ratio', '\u0394 %') if relative else ('T [ms]', '\u0394T')
        result.append('<h3>' + title + '</h3><table><thead><tr>'
                      '<th rowspan="3" style="text-align:left">Task / tool</th>')
        for cpu in report['contract']['cpu_counts']:
            result.append(f'<th colspan="{2 * len(report["contract"]["file_counts"])}">{cpu} CPU{"s" if cpu != 1 else ""}</th>')
        result.append('</tr><tr>' + ''.join(f'<th colspan="2">{count:,}</th>' for _, count in columns) + '</tr><tr>')
        result.append(''.join('<th style="text-align:right">' + label + '</th>' for _ in columns for label in labels))
        result.append('</tr></thead><tbody>')
        for group, _, rows in sections:
            result.append(f'<tr><th colspan="{1 + 2 * len(columns)}" style="text-align:center">' +
                          html.escape(group) + '</th></tr>')
            for (name, tool), values in rows:
                result.append('<tr><th scope="row" style="text-align:left">' + html.escape(name + ' / ' + tool) + '</th>')
                for column in columns:
                    value = values.get(column, 'n/a')
                    ratio = ratios.get((group, name, tool, column))
                    if relative and ratio is not None:
                        current, _, remainder = value.partition(' (')
                        difference, _, baseline = remainder.partition(')')
                        first = ratio_html(current.removesuffix('x'), ratio)
                        second = ratio_html(difference, ratio)
                        if baseline:
                            second += '<br><small>' + html.escape(baseline.removeprefix('; ')) + '</small>'
                    else:
                        current, separator, baseline = value.partition(' / ')
                        first = html.escape(current)
                        previous, change_separator, change = baseline.partition(' (')
                        second = 'n/a'
                        if separator and change_separator:
                            second = f'{float(current) - float(previous):+.2f}'
                            second += '<br><small>' + html.escape(change.removesuffix(')')) + '</small>'
                    result.append('<td style="text-align:right">' + first + '</td>'
                                  '<td style="text-align:right">' + second + '</td>')
                result.append('</tr>')
        result.append('</tbody></table>')
    return ''.join(result)


def render_markdown(report):
    output = ['# Tool comparisons', '', policy(report) + '; absolute tables: elapsed milliseconds.', '',
              'Ratio tables: xff/reference; greater than 1 means xff is slower. Vs main is the change in that ratio.', '']
    if alarm_text(report):
        output.extend([alarm_text(report), ''])
    if baseline_text(report):
        output.extend([baseline_text(report), ''])
    def escape(value):
        return value.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('|', '&#124;').replace('\n', ' ')
    for group, columns, rows in matrices(report) + (relative_matrices(report) if matrices(report) else []):
        output.extend(['## ' + escape(group), ''])
        table = [['Task / tool', *[f'{cpu} CPU / {count:,} files' for cpu, count in columns]]]
        for (name, tool), values in rows:
            table.append([escape(name + ' / ' + tool), *[escape(values.get(column, 'n/a')) for column in columns]])
        widths = [max(3, *(len(row[index]) for row in table)) for index in range(len(table[0]))]
        def line(row):
            return '| ' + ' | '.join(value.ljust(widths[index]) if index == 0 else value.rjust(widths[index])
                                      for index, value in enumerate(row)) + ' |'
        output.extend([line(table[0]), '| ' + ' | '.join('-' * width if index == 0 else '-' * (width - 1) + ':'
                                                      for index, width in enumerate(widths)) + ' |'])
        output.extend(line(row) for row in table[1:])
        output.append('')
    return '\n'.join(output)
