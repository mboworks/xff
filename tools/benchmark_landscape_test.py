# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Check landscape coordinates and comparison semantics without a browser dependency."""

import itertools
import json
import math
from pathlib import Path
import tempfile
import unittest

import benchmark_landscape as landscape


def report():
    return {'contract': {'file_counts': [10, 100, 1000], 'cpu_counts': [1, 4],
                         'repetitions': 1, 'retained': 1, 'estimator': 'mean-fastest'},
            'tasks': [dict(dataset=tree, name=task, cpus=cpu, files=count, skips={},
                           participants={name: {'samples': [{'elapsed_seconds': elapsed}]}
                                         for name, elapsed in [('xff', 1), ('rg', reference)]})
                      for tree in ('broad', 'deep') for cpu in (1, 4)
                      for count in (10, 100, 1000)
                      for task, reference in [('fast', 2), ('slow', 0.5)]]}


class BenchmarkLandscapeTest(unittest.TestCase):
    def test_percent_ticks_and_colorbar_show_units(self):
        layout = landscape.figure(report())['layout']
        self.assertEqual(layout['scene']['yaxis']['ticksuffix'], '%')
        self.assertEqual(layout['coloraxis']['colorbar']['ticksuffix'], '%')

    def test_factor_view_preserves_order_colors_and_hover(self):
        for order in landscape.ORDER_LABELS:
            percent = landscape.figure(report(), order)
            factor = landscape.figure(report(), order, 'factor')
            for original, converted in zip(percent['data'], factor['data']):
                for key in ('x', 'z', 'text', 'surfacecolor'):
                    self.assertEqual(original[key], converted[key])
                for old, new in zip(original['y'], converted['y']):
                    for value, height in zip(old, new):
                        self.assertAlmostEqual(height, math.log10(2 if value > 0 else 0.5))
            axis = factor['layout']['scene']['yaxis']
            expected = [index * 0.1 for index in range(-4, 5)]
            self.assertEqual(axis['tickvals'], expected)
            self.assertEqual(axis['ticktext'], ['-0.4', '-0.3', '-0.2', '-0.1', '0',
                                                '+0.1', '+0.2', '+0.3', '+0.4'])
            self.assertEqual(axis['range'], [-0.4, 0.4])
            for left, right in zip(axis['tickvals'], axis['tickvals'][1:]):
                self.assertAlmostEqual(right - left, 0.1)
            bar = factor['layout']['coloraxis']['colorbar']
            for value, label in zip(bar['tickvals'], bar['ticktext']):
                self.assertAlmostEqual(value, 100 * (1 - 10 ** -float(label)))

    def test_factor_missing_measurements_stay_missing(self):
        data = report()
        data['tasks'].pop(0)
        surface = landscape.figure(data, metric='factor')['data'][0]
        self.assertIsNone(surface['y'][0][-1])
        self.assertIsNone(surface['surfacecolor'][0][-1])

    def test_rejects_unknown_metric(self):
        with self.assertRaisesRegex(ValueError, 'unknown comparison metric'):
            landscape.figure(report(), metric='invalid')

    def test_mirrored_axes_sign_and_neutral_color(self):
        chart = landscape.figure(report())
        self.assertEqual(len(chart['data']), 4)
        left, deep, right, _ = chart['data']
        self.assertEqual(left['x'][0], [-2.3, -1.3, -0.3])
        self.assertEqual(right['x'][0], [0.3, 1.3, 2.3])
        self.assertEqual(left['z'][0], [-1, -1, -1])
        self.assertEqual(deep['z'][0], [1, 1, 1])
        self.assertEqual(left['y'], [[50.0] * 3, [-100.0] * 3])
        self.assertEqual(left['surfacecolor'], left['y'])
        self.assertEqual(chart['layout']['coloraxis']['cmin'], -100)
        self.assertEqual(chart['layout']['coloraxis']['cmax'], 100)
        self.assertIn([0.5, '#3269b5'], chart['layout']['coloraxis']['colorscale'])

    def test_similarity_minimizes_adjacent_profile_difference(self):
        profiles = {'a': (70, -40), 'b': (60, 80), 'c': (-80, -70),
                    'd': (50, -30), 'e': (10, 20)}
        rows = [dict(task=name, reference='rg', dataset='broad', cpus=1, files=count,
                     xff_over_reference=1 - value / 100)
                for name, values in profiles.items() for count, value in zip((10, 100), values)]
        def cost(order):
            return sum(sum(abs(x - y) for x, y in zip(profiles[a], profiles[b])) / 2
                       for a, b in zip(order, order[1:]))
        actual = [pair[0] for pair in landscape.ordered_pairs(rows, 'similarity')]
        optimum = min(cost(order) for order in itertools.permutations(profiles))
        self.assertAlmostEqual(cost(actual), optimum)
        self.assertGreaterEqual(sum(profiles[actual[0]]), sum(profiles[actual[-1]]))
        self.assertEqual(landscape.ordered_pairs(rows, 'similarity'),
                         landscape.ordered_pairs(list(reversed(rows)), 'similarity'))
        average = [pair[0] for pair in landscape.ordered_pairs(rows, 'average')]
        self.assertEqual(average, sorted(profiles, key=lambda key: -sum(profiles[key])))

    def test_disjoint_profiles_fall_back_to_average(self):
        rows = [dict(task=name, reference='rg', dataset='broad', cpus=1, files=count,
                     xff_over_reference=ratio)
                for name, count, ratio in [('a', 10, 2), ('b', 100, 0.5)]]
        self.assertEqual(landscape.ordered_pairs(rows, 'similarity'), [('b', 'rg'), ('a', 'rg')])

    def test_colors_keep_blue_band_at_ten_percentage_points(self):
        extent, stops = landscape.comparison_colorscale(100)
        self.assertEqual(extent, 100)
        values = {round(position * 2 * extent - extent): color for position, color in stops}
        self.assertEqual(values[-10], '#24477b')
        self.assertEqual(values[10], '#24477b')
        self.assertEqual(values[-20], '#84202a')
        self.assertEqual(values[20], '#12572d')
        self.assertEqual(values[-100], '#ff3030')
        self.assertEqual(values[100], '#35ef69')
        limit, small = landscape.comparison_colorscale(3)
        self.assertEqual(limit, 20)
        self.assertEqual(len({position for position, _ in small}), len(small))
        self.assertEqual(small[0], [0.0, '#ff3030'])
        self.assertEqual(small[-1], [1.0, '#35ef69'])

    def test_axis_ticks_do_not_repeat_group_names(self):
        chart = landscape.figure(report())
        scene = chart['layout']['scene']
        self.assertEqual([label.strip() for label in scene['xaxis']['ticktext']],
                         ['1,000', '100', '10', '10', '100', '1,000'])
        self.assertEqual(scene['xaxis']['title']['text'], '1 worker \u2190 File count \u2192 4 workers')
        self.assertEqual(scene['zaxis']['title']['text'], 'Broad FS \u2190 Task \u2192 Deep FS')
        self.assertEqual(scene['xaxis']['ticks'], 'outside')
        self.assertEqual(scene['zaxis']['ticks'], 'outside')
        self.assertNotIn('tickangle', scene['xaxis'])
        self.assertNotIn('tickangle', scene['zaxis'])
        self.assertTrue(all('FS' not in label and 'Broad:' not in label and 'Deep:' not in label
                            for label in scene['zaxis']['ticktext']))

    def test_padding_preserves_automatic_tick_angles(self):
        scene = landscape.figure(report())['layout']['scene']
        for axis in ('xaxis', 'zaxis'):
            self.assertNotIn('tickangle', scene[axis])
            self.assertIn('monospace', scene[axis]['tickfont']['family'])
            self.assertEqual(len({len(text) for text in scene[axis]['ticktext']}), 1)
            self.assertTrue(all('.' not in text and '\u2800' not in text
                                for text in scene[axis]['ticktext']))
        self.assertTrue(all(text.startswith('\u00a0' * 3) for text in scene['xaxis']['ticktext']))
        self.assertTrue(all(text.endswith('\u00a0' * 3) for text in scene['zaxis']['ticktext']))
        self.assertNotIn('annotations', scene)

    def test_publication_precedes_tables_preserves_data_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = root / 'runs/123/1/linux'
            folder.mkdir(parents=True)
            data = folder / 'report.json'
            data.write_text(json.dumps({'tool_comparisons': report()}))
            original = data.read_bytes()
            page = folder / 'index.html'
            page.write_text('<html><head></head><body><h1>Benchmark</h1><table>Original</table></body></html>')
            legacy = root / 'runs/122/1'
            legacy.mkdir(parents=True)
            (legacy / 'report.json').write_text('{}')
            (legacy / 'index.html').write_text('Legacy untouched')
            self.assertEqual(landscape.publish(root, '/* plotting asset */'), 1)
            first = page.read_text()
            self.assertLess(first.index('Comparison landscape'), first.index('<table>'))
            self.assertIn('src="../../../../assets/three-landscape.js"', first)
            self.assertIn('<table>Original</table>', first)
            self.assertIn('<details id="landscape-panel" open>', first)
            self.assertLess(first.index('</details>'), first.index('<table>'))
            self.assertEqual(landscape.publish(root, '/* plotting asset */'), 1)
            self.assertEqual(first, page.read_text())
            self.assertEqual(data.read_bytes(), original)
            self.assertEqual((legacy / 'index.html').read_text(), 'Legacy untouched')
            self.assertEqual((root / 'assets/three-landscape.js').read_text(), '/* plotting asset */')

    def test_hover_cells_match_ticks_after_every_reordering(self):
        for mode in landscape.ORDER_LABELS:
            with self.subTest(order=mode):
                chart = landscape.figure(report(), mode)
                scene = chart['layout']['scene']
                tasks = dict(zip(scene['zaxis']['tickvals'], scene['zaxis']['ticktext']))
                files = dict(zip(scene['xaxis']['tickvals'], scene['xaxis']['ticktext']))
                for surface in chart['data']:
                    # Plotly's surface pick uses [column,row]; template text lookup transposes it.
                    self.assertEqual(surface['hoverinfo'], 'text')
                    self.assertNotIn('hovertemplate', surface)
                    for row, values in enumerate(surface['text']):
                        for column, text in enumerate(values):
                            z = surface['z'][row][column]
                            task, reference = tasks[z].strip().rsplit(' / ', 1)
                            self.assertIn(f'{task} / xff vs {reference}', text)
                            count = files[surface['x'][row][column]].strip()
                            self.assertIn(f'{count} files', text)
                            self.assertIn(f'Relative performance: {surface["y"][row][column]:+.2f}%', text)
                            self.assertTrue(text.startswith('Deep' if z > 0 else 'Broad'))

    def test_missing_observations_remain_holes(self):
        data = report()
        data['tasks'].pop(0)
        surface = landscape.figure(data)['data'][0]
        self.assertIsNone(surface['y'][0][-1])
        self.assertFalse(surface['connectgaps'])

    def test_rejects_duplicate_cells(self):
        data = report()
        data['tasks'].append(data['tasks'][0])
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            landscape.figure(data)

    def test_absolute_table_is_collapsed_and_comparison_is_visible(self):
        text = landscape.render(report(), '// offline plotting bundle')
        start = text.index('<details><summary>Absolute measurements')
        end = text.index('</details>', start)
        self.assertGreater(text.index('<h3>xff/reference ratios'), end)
        self.assertIn('<details id="landscape-panel" open>', text)
        self.assertIn('</details><h2>Measurements</h2>', text)
        self.assertIn('chart.resize', text)
        self.assertIn('Scale: <select', text)
        self.assertIn('>Percentage</option>', text)
        self.assertIn('>Logarithmic</option>', text)
        self.assertNotIn('Time saved', text)
        self.assertIn('100 &times; (1 - xff time / reference time)', text)


if __name__ == '__main__':
    unittest.main()
