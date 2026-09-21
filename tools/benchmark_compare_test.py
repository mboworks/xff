# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Verify equivalent populations and process accounting without performance thresholds."""

import base64
import copy
import os
from pathlib import Path
import sys
import signal
import subprocess
import tempfile
import unittest
from unittest import mock

import benchmark_compare as compare

BINARY = None


class BenchmarkCompareTest(unittest.TestCase):
    def invoke(self, commands, content=b''):
        with tempfile.NamedTemporaryFile() as source:
            source.write(content)
            source.flush()
            return compare.invoke({'pipeline': commands, 'stdin': source.name, 'environment': dict(os.environ)},
                                  [sys.executable, compare.__file__])

    def test_pipeline_accounts_for_both_children(self):
        commands = [[sys.executable, '-c', "import sys; a=bytearray(4000000); sys.stdout.buffer.write(b'item\\0')"],
                    [sys.executable, '-c', 'import sys; a=bytearray(8000000); sys.stdout.buffer.write(sys.stdin.buffer.read())']]
        sample = self.invoke(commands)
        compare.validate_output(sample, [b'item'], commands)
        self.assertEqual(sample['exit_codes'], [0, 0])
        self.assertIsNone(sample['peak_child_rss_bytes'])
        self.assertGreater(sample['sum_child_peak_rss_bytes'], 0)
        self.assertGreater(sample['user_cpu_seconds'], 0)
        self.assertLessEqual(sample['first_stdout_seconds'], sample['elapsed_seconds'])

    def test_single_process_stdin_and_no_output_latency(self):
        command = [[sys.executable, '-c', 'import sys; sys.stdout.buffer.write(sys.stdin.buffer.read())']]
        sample = self.invoke(command, b'path with space\nnewline\0')
        compare.validate_output(sample, [b'path with space\nnewline'], command)
        self.assertEqual(sample['peak_child_rss_bytes'], sample['sum_child_peak_rss_bytes'])
        sample = self.invoke(command)
        compare.validate_output(sample, [], command)
        self.assertIsNone(sample['first_stdout_seconds'])

    def test_oracle_rejects_duplicates_missing_rows_and_bad_delimiters(self):
        for output in (b'a\0a\0', b'b\0', b'a'):
            with self.subTest(output=output), self.assertRaises(ValueError):
                compare.validate_output({'output': base64.b64encode(output).decode(), 'exit_codes': [0], 'stderr': ''},
                                        [b'a'], [['xff']])

    def test_no_match_status_is_accepted_only_for_search_tools(self):
        for tool in ('rg', 'fzf'):
            compare.validate_output({'output': '', 'exit_codes': [1], 'stderr': ''}, [], [[tool]])
        for tool, code, stderr in (('find', 1, ''), ('rg', 2, ''), ('fzf', 0, 'warning')):
            with self.subTest(tool=tool), self.assertRaises(ValueError):
                compare.validate_output({'output': '', 'exit_codes': [code], 'stderr': stderr}, [], [[tool]])

    def test_missing_tools_are_reported_or_required(self):
        self.assertEqual(compare.tool_info('fzf', None)['status'], 'unavailable')
        with mock.patch.object(compare, 'tool_info', return_value={'status': 'unavailable'}):
            with self.assertRaisesRegex(ValueError, 'required'):
                compare.collect(Path('/unused'), require_tools=True)

    def test_pipeline_failure_is_not_a_successful_empty_measurement(self):
        command = [[sys.executable, '-c', 'raise SystemExit(7)'],
                   [sys.executable, '-c', 'import sys; sys.stdout.buffer.write(sys.stdin.buffer.read())']]
        with self.assertRaisesRegex(ValueError, 'failed comparison'):
            compare.validate_output(self.invoke(command), [], command)

    def test_timeout_terminates_the_whole_worker_group(self):
        child = mock.Mock(pid=12345)
        child.communicate.side_effect = [subprocess.TimeoutExpired("worker", 60), (b"", b"")]
        with mock.patch.object(compare.subprocess, "Popen", return_value=child), mock.patch.object(compare.os, "killpg") as kill:
            with self.assertRaisesRegex(ValueError, "60 seconds"):
                compare.invoke({}, ["worker"])
            kill.assert_called_once_with(12345, signal.SIGKILL)

    def test_manifest_and_task_populations(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'root'
            rows = compare.fixture(root, 12, 3)
            tasks = {name: (expected, commands) for name, expected, commands in
                     compare.scenarios(root, rows, {name: {'path': name} for name in ('xff', 'find', 'rg', 'fzf')})}
            self.assertEqual(len(tasks['files'][0]), 13)
            self.assertEqual(len(tasks['name-txt'][0]), 6)
            self.assertEqual(len(tasks['content-needle'][0]), 4)
            self.assertEqual(tasks['content-absent_marker'][0], [])
            self.assertEqual(tasks['fuzzy-discovery-itm'][0], tasks['fuzzy-list-itm'][0])
            self.assertEqual(tasks['fuzzy-list-zzzz'][0], [])
            self.assertNotIn(b'./file-link', tasks['files'][0])

    def test_actual_xff_matches_independent_oracle(self):
        if BINARY is None:
            self.skipTest("Bazel supplies xff executable")
        # Other tools are host-dependent; always verify xff, and explicitly report their skips.
        with mock.patch.object(compare.shutil, 'which', return_value=None):
            result = compare.collect(BINARY, files=4, depth=2, repetitions=1)
        self.assertEqual(len(result['tasks']), 20)
        self.assertTrue(all(task['skips'] for task in result['tasks']))
        page = compare.render(result)
        self.assertIn('Tool comparisons', page)
        self.assertIn('Skipped:', page)
        broken = copy.deepcopy(result)
        broken['tasks'][0]['participants']['xff']['samples'] = []
        with self.assertRaisesRegex(ValueError, 'missing'):
            compare.render(broken)


if __name__ == '__main__':
    if len(sys.argv) > 1 and not sys.argv[1].startswith('-'):
        BINARY = Path(sys.argv.pop(1)).resolve()
    unittest.main()
