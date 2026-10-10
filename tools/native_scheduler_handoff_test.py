# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Portable controls for the native-only handoff and measurement contracts."""
from collections import Counter
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import native_scheduler_collect as collect
import native_scheduler_prepare as prepare


def valid_sample(payload=b'./a\0'):
    return {'output': payload, 'stderr': '', 'stderr_bytes': 0, 'stdout_bytes': len(payload),
            'exit_codes': [0], 'elapsed_seconds': 0.01, 'first_stdout_seconds': 0.005 if payload else None}


class NativeSchedulerHandoffTest(unittest.TestCase):
    def check(self, sample, expected=None, ordered=False):
        expected = [b'./a'] if expected is None else expected
        return collect.validate(sample, expected, Counter(expected), [['/test/xff']], ordered)

    def test_native_host_rejects_linux(self):
        with mock.patch.object(prepare.sys, 'platform', 'linux'), self.assertRaisesRegex(ValueError, 'native macOS'):
            prepare.native_host()

    def test_native_host_rejects_other_apple_cpu(self):
        with mock.patch.object(prepare.sys, 'platform', 'darwin'), \
                mock.patch.object(prepare.subprocess, 'check_output', return_value='Apple M2 Ultra\n'), \
                self.assertRaisesRegex(ValueError, 'M5 Pro'):
            prepare.native_host()

    def test_native_host_keeps_core_observations_without_affinity_claim(self):
        observed = subprocess.CompletedProcess(['sysctl'], 1, 'partial topology', 'optional key unavailable')
        with mock.patch.object(prepare.sys, 'platform', 'darwin'), \
                mock.patch.object(prepare.subprocess, 'check_output', return_value='Apple M5 Pro\n'), \
                mock.patch.object(prepare.subprocess, 'run', return_value=observed):
            host = prepare.native_host()
        self.assertFalse(host['affinity_verified'])
        self.assertEqual(host['observations']['performance_efficiency_cores']['exit_code'], 1)

    def test_retained_output_valid(self):
        self.assertEqual(self.check(valid_sample()), b'./a\0')

    def test_empty_output_has_no_timestamp(self):
        self.assertEqual(self.check(valid_sample(b''), []), b'')

    def test_empty_output_with_timestamp_rejected(self):
        sample = valid_sample(b'')
        sample['first_stdout_seconds'] = 0
        with self.assertRaisesRegex(ValueError, 'first-output'):
            self.check(sample, [])

    def test_output_mismatch_rejected(self):
        with self.assertRaisesRegex(ValueError, 'oracle'):
            self.check(valid_sample(b'./b\0'))

    def test_duplicate_output_rejected(self):
        with self.assertRaisesRegex(ValueError, 'oracle'):
            self.check(valid_sample(b'./a\0./a\0'))

    def test_unterminated_output_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unterminated'):
            self.check(valid_sample(b'./a'))

    def test_diagnostics_rejected(self):
        sample = valid_sample()
        sample['stderr'] = 'warning'
        with self.assertRaisesRegex(ValueError, 'diagnostics'):
            self.check(sample)

    def test_nonzero_exit_rejected(self):
        sample = valid_sample()
        sample['exit_codes'] = [2]
        with self.assertRaisesRegex(ValueError, 'exit'):
            self.check(sample)

    def test_output_byte_disagreement_rejected(self):
        sample = valid_sample()
        sample['stdout_bytes'] += 1
        with self.assertRaisesRegex(ValueError, 'byte count'):
            self.check(sample)

    def test_nan_and_nonpositive_durations_rejected(self):
        for elapsed in (float('nan'), float('inf'), 0, -1):
            with self.subTest(elapsed=elapsed):
                sample = valid_sample()
                sample['elapsed_seconds'] = elapsed
                with self.assertRaisesRegex(ValueError, 'elapsed'):
                    self.check(sample)

    def test_first_output_after_finish_rejected(self):
        sample = valid_sample()
        sample['first_stdout_seconds'] = 0.02
        with self.assertRaisesRegex(ValueError, 'first-output'):
            self.check(sample)

    def test_tree_order_rejected_even_when_counts_match(self):
        with self.assertRaisesRegex(ValueError, 'tree order'):
            self.check(valid_sample(b'./b\0./a\0'), [b'./a', b'./b'], True)

    def test_existing_artifact_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'state.json'
            prepare.save(path, {'first': True})
            with self.assertRaises(FileExistsError):
                prepare.save(path, {'second': True})
            self.assertIn('first', path.read_text())

    def test_forbidden_profiler_detected_by_basename(self):
        with mock.patch.object(collect.subprocess, 'check_output', return_value='/usr/bin/xctrace\n'), \
                self.assertRaisesRegex(ValueError, 'overlaps'):
            collect.no_builds_or_profiles()

    def test_idle_java_server_not_a_compiler(self):
        with mock.patch.object(collect.subprocess, 'check_output', return_value='/usr/bin/java\n'):
            collect.no_builds_or_profiles()

    def test_gsort_preferred_on_macos(self):
        with mock.patch.object(collect.shutil, 'which', side_effect=lambda name: '/test/' + name), \
                mock.patch.object(collect.compare, 'tool_info', side_effect=lambda name, path: {'status': 'available', 'path': path}), \
                mock.patch.object(collect.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, b'a\0b\0', b'')):
            self.assertEqual(collect.reference_tools()['sort']['path'], '/test/gsort')

    def test_sort_without_nul_support_rejected(self):
        with mock.patch.object(collect.shutil, 'which', side_effect=lambda name: '/test/' + name), \
                mock.patch.object(collect.compare, 'tool_info', side_effect=lambda name, path: {'status': 'available', 'path': path}), \
                mock.patch.object(collect.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, b'', b'unsupported')), \
                self.assertRaisesRegex(ValueError, 'NUL-capable GNU sort'):
            collect.reference_tools()

    def test_complete_grid_and_worker_requests_preserved(self):
        self.assertEqual(collect.GRID, (10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000))
        self.assertEqual(collect.WORKERS, (1, 3, 10))
        self.assertEqual(collect.ORDERS, ('declared', 'reverse', 'seeded-shuffle-20261010'))

    def test_fifteen_scenarios_include_four_scheduler_controls(self):
        tools = {name: {'path': '/test/' + name} for name in ('xff', 'find', 'rg', 'fzf', 'sort')}
        anchor = Path('/fixture')
        rows = [(anchor / 'src/a.txt', b'needle\n'), (anchor / 'src/b.txt', b'')]
        scenarios = list(collect.scenarios(anchor, rows, tools, 3))
        self.assertEqual(len(scenarios), 15)
        selected = {name: (expected, pipeline, ordered, scope) for name, expected, pipeline, ordered, scope in scenarios}
        for name in collect.EXTRA_TASKS:
            self.assertIn('--jobs=3', selected[name][1]['xff'][0])
        self.assertEqual(selected['files-stat-eager'][0], [b'./src/a.txt'])
        self.assertTrue(selected['files-tree'][2])
        self.assertIn('find+sort', selected['files-tree'][1])
        self.assertIn('-collect:audit', selected['files-collect'][1]['xff'][0])

    def test_mac_measurement_rejects_affinity_mask(self):
        with tempfile.TemporaryDirectory() as directory:
            stdin = Path(directory) / 'empty'
            stdin.write_bytes(b'')
            with self.assertRaisesRegex(ValueError, 'must not claim Linux CPU affinity'):
                collect.measure([['/test/xff']], stdin, Path(directory), [0], '/test/launcher', {})


if __name__ == '__main__':
    unittest.main()
