# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Verify equivalent populations and process accounting without performance thresholds."""

import base64
import copy
import contextlib
import io
import json
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
            self.assertEqual(len(tasks['files'][0]), 12)
            self.assertEqual(tasks['files-safe'][0], tasks['files'][0])
            self.assertIn('--safe', tasks['files-safe'][1]['xff'][0])
            self.assertEqual(len(tasks['name-txt'][0]), 5)
            self.assertEqual(len(tasks['content-needle'][0]), 4)
            self.assertEqual(tasks['content-absent_marker'][0], [])
            self.assertEqual(tasks['fuzzy-discovery-itm'][0], tasks['fuzzy-list-itm'][0])
            self.assertEqual(tasks['fuzzy-list-zzzz'][0], [])
            self.assertNotIn(b'./file-link', tasks['files'][0])

    @unittest.skipUnless(hasattr(os, 'sched_getaffinity'), 'Linux affinity required')
    def test_worker_children_inherit_cpu_affinity(self):
        cpus = sorted(os.sched_getaffinity(0))[:1]
        command = [[sys.executable, '-c',
                    'import os,sys; sys.stdout.buffer.write(str(sorted(os.sched_getaffinity(0))).encode()+b"\\0")']]
        with tempfile.NamedTemporaryFile() as source:
            sample = compare.invoke({'pipeline': command, 'stdin': source.name, 'environment': dict(os.environ),
                                     'cpu_affinity': cpus}, [sys.executable, compare.__file__])
        compare.validate_output(sample, [str(cpus).encode()], command)

    def test_cpu_allocations_and_worker_settings(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'root'
            rows = compare.fixture(root, 4, 2)
            tasks = {name: commands for name, _, commands in
                     compare.scenarios(root, rows, {name: {'path': name} for name in ('xff', 'find', 'rg', 'fzf')}, cpus=4)}
            self.assertIn('--jobs=4', tasks['files']['xff'][0])
            self.assertIn('--threads=4', tasks['files']['rg'][0])
        with mock.patch.object(compare.os, 'sched_getaffinity', return_value={2, 3}, create=True):
            with self.assertRaisesRegex(ValueError, 'only 2 available'):
                compare.collect(Path('/xff'), cpus=4)
        def report(binary, files, depth, repetitions, **kwargs):
            return {"tools": {}, "contract": {"files": files, "cpu_affinity": list(range(kwargs['cpus']))},
                    "tasks": [{"shape": "broad", "name": "files"}]}
        with mock.patch.object(compare, 'collect', side_effect=report):
            result = compare.collect_scales(Path('/xff'), [10])
        self.assertEqual([task['shape'] for task in result['tasks']], ['1cpu/10/broad', '4cpu/10/broad'])
        self.assertEqual(result['contract']['affinity_by_cpu_count'], {'1': [0], '4': [0, 1, 2, 3]})

    def test_memory_storage_requires_verified_tmpfs(self):
        with tempfile.TemporaryDirectory() as temporary:
            with mock.patch.object(compare.sys, 'platform', 'linux'), mock.patch.object(compare.subprocess, 'run') as run:
                run.return_value = subprocess.CompletedProcess([], 0, 'tmpfs\n', '')
                self.assertEqual(compare.fixture_storage(temporary, True)['filesystem'], 'tmpfs')
                run.return_value = subprocess.CompletedProcess([], 0, 'ext4\n', '')
                with self.assertRaisesRegex(ValueError, 'verified Linux tmpfs'):
                    compare.fixture_storage(temporary, True)
                self.assertEqual(compare.fixture_storage(temporary)['filesystem'], 'ext4')
            with mock.patch.object(compare.sys, 'platform', 'darwin'):
                with self.assertRaisesRegex(ValueError, 'verified Linux tmpfs'):
                    compare.fixture_storage(temporary, True)
            with self.assertRaisesRegex(ValueError, 'existing directory'):
                compare.fixture_storage(Path(temporary) / 'missing')

    def test_scales_keep_populations_separate_and_detect_changed_tools(self):
        def report(binary, files, depth, repetitions, **kwargs):
            return {"tools": {"xff": {"sha256": "same"}}, "contract": {"files": files},
                    "tasks": [{"shape": "broad", "name": "files", "input_files": files + 1}]}
        with mock.patch.object(compare, 'collect', side_effect=report):
            result = compare.collect_scales(Path('/xff'), [10, 100], depth=2, cpu_counts=(1,))
        self.assertEqual(result['contract']['file_counts'], [10, 100])
        with mock.patch.object(compare, 'collect', side_effect=report) as collector:
            compare.collect_scales(Path('/xff'), [10, 100], cpu_counts=(1,))
        self.assertEqual([call.args[2] for call in collector.call_args_list], [10, 40])
        self.assertEqual([task['shape'] for task in result['tasks']], ['1cpu/10/broad', '1cpu/100/broad'])
        self.assertEqual([task['input_files'] for task in result['tasks']], [11, 101])
        first, second = report(None, 10, 2, 1), report(None, 100, 2, 1)
        second['tools']['xff']['sha256'] = 'changed'
        with mock.patch.object(compare, 'collect', side_effect=[first, second]):
            with self.assertRaisesRegex(ValueError, 'identity changed'):
                compare.collect_scales(Path('/xff'), [10, 100], depth=2, cpu_counts=(1,))
        for counts in ([], [0], [10, 10]):
            with self.subTest(counts=counts), self.assertRaises(ValueError):
                compare.collect_scales(Path('/xff'), counts, depth=2)

    def test_default_grid_stops_at_ten_thousand(self):
        data = {'tasks': [], 'contract': {}}
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'report.json'
            with mock.patch.object(sys, 'argv', ['compare', '--binary=/xff', f'--report={output}']), \
                    mock.patch.object(compare, 'collect_scales', return_value=data) as collect, \
                    mock.patch.object(compare.benchmark_matrix, 'regressions', return_value=[]), \
                    mock.patch.object(compare.benchmark_matrix, 'relative_results', return_value=[]):
                compare.main()
            self.assertEqual(collect.call_args.args[1], [10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000])

    def test_cli_alarm_records_warning_without_failure(self):
        data = {'contract': {'repetitions': 3, 'retained': 2, 'estimator': 'mean-fastest'},
                'tasks': [{'dataset': 'broad', 'name': 'files', 'files': 10, 'cpus': 1,
                           'participants': {'xff': {'samples': [{'elapsed_seconds': value} for value in (2, 3, 100)]},
                                            'find': {'samples': [{'elapsed_seconds': 1} for _ in range(3)]}}}]}
        def attach(report, root):
            report['baseline'] = {'status': 'available', 'averages': {'broad/files/1/10': {'xff': 1, 'find': 1}}}
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'report.json'
            argv = ['compare', '--binary=/xff', f'--report={output}', f'--baseline-root={temporary}',
                    '--files=10', '--cpus=1', '--repetitions=3', '--keep=2']
            console = io.StringIO()
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(compare, 'collect_scales', return_value=data), \
                    mock.patch.object(compare.benchmark_matrix, 'attach_baseline', side_effect=attach), \
                    contextlib.redirect_stdout(console):
                compare.main()
            record = json.loads(output.read_text())
        self.assertIn('::warning title=Benchmark performance::', console.getvalue())
        self.assertFalse(record['tool_comparisons']['alarm']['blocking'])
        self.assertEqual(record['tool_comparisons']['alarm']['threshold_percent'], 15)
        self.assertEqual(len(record['tool_comparisons']['alarm']['cells']), 1)

    def test_progress_throttles_fast_runs_but_keeps_scale_boundaries(self):
        output = io.StringIO()
        with contextlib.redirect_stderr(output), mock.patch.object(compare.time, 'monotonic',
                                                                  side_effect=[0, 1, 4.9, 5, 5.1]):
            progress = compare.MeasurementProgress()
            progress('Scale 1/10', force=True)
            progress('fast run 1')
            progress('fast run 2')
            progress('Run 9/12')
            progress('Completed scale 1/10', force=True)
        self.assertEqual(output.getvalue().splitlines(), ['Scale 1/10', 'Run 9/12', 'Completed scale 1/10'])

    def test_warmup_is_discarded_and_tools_are_interleaved(self):
        calls = []
        def invoke(spec, worker):
            calls.append(spec['pipeline'][0][0])
            return {'sequence': len(calls)}
        commands = {'xff': [['xff']], 'find': [['find']], 'rg': [['rg']]}
        progress = io.StringIO()
        with mock.patch.object(compare, 'tool_info', side_effect=lambda name, path: {
                'status': 'available', 'path': name, 'sha256': 'fixed'}), \
                mock.patch.object(compare, 'digest', return_value='fixed'), \
                mock.patch.object(compare, 'scenarios', return_value=[('files', set(), commands)]), \
                mock.patch.object(compare, 'invoke', side_effect=invoke), \
                mock.patch.object(compare, 'validate_output') as validate, contextlib.redirect_stderr(progress):
            result = compare.collect(Path('/xff'), files=1, depth=1, repetitions=3, keep=2,
                                     progress=lambda message: print(message, file=sys.stderr))
        self.assertEqual(calls[:12], ['xff', 'find', 'rg', 'find', 'rg', 'xff',
                                     'rg', 'xff', 'find', 'xff', 'find', 'rg'])
        self.assertEqual(calls[12:15], ['find', 'rg', 'xff'])
        self.assertEqual(validate.call_count, 24)
        self.assertIn('Run 1/12: broad/files; 1 files; 1 requested workers; xff; warm-up', progress.getvalue())
        self.assertIn('Run 12/12:', progress.getvalue())
        self.assertIn('Completed 12/12: deep/files', progress.getvalue())
        self.assertEqual(result['contract']['warmup_rounds'], 1)
        first = result['tasks'][0]['participants']
        self.assertEqual([s['sequence'] for s in first['xff']['samples']], [6, 8, 10])
        self.assertTrue(all(len(entry['samples']) == 3 for entry in first.values()))

    def test_custom_manifest_with_binary_and_links_matches_real_xff(self):
        if BINARY is None:
            self.skipTest("Bazel supplies xff executable")
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'fixture.manifest'
            path.write_text('a.txt=:needle beta\nb.bin=b:AG5lZWRsZQD/\nempty-dir/\nlink=l:a.txt\nloop=l:loop\n')
            fixtures = compare.benchmark_fixture.mapping([f'custom:2={path}'])
            with mock.patch.object(compare.shutil, 'which', return_value=None):
                result = compare.collect_scales(BINARY, [2], depth=1, repetitions=1, keep=1,
                                                cpu_counts=(1,), fixtures=fixtures)
        self.assertEqual(len(result['tasks']), 11)
        self.assertTrue(all('binary fixture' in task['skips']['xff'] for task in result['tasks']
                            if task['name'].startswith('content-')))
        self.assertTrue(all(task['input_files'] == 2 for task in result['tasks']))
        self.assertEqual(result['contract']['invocation']['fixtures'][0]['dataset'], 'custom')
        self.assertIn('1cpu/2/custom', result['tasks'][0]['shape'])
        self.assertIn('mean of fastest 1/1', compare.render(result))
        document = compare.render_document(result)
        self.assertIn('<!doctype html>', document)
        self.assertIn('<title>xff benchmarks - ', document)
        self.assertIn('<h1>xff benchmarks - ', document)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'measurements.json'
            output = Path(directory) / 'measurements.html'
            source.write_text(json.dumps({'tool_comparisons': result}))
            original = source.read_bytes()
            with mock.patch.object(sys, 'argv', ['benchmark_compare', '--render-only',
                                                '--report', str(source), '--html', str(output)]), \
                    mock.patch.object(compare, 'collect_scales', side_effect=AssertionError('must not measure')):
                compare.main()
            self.assertEqual(output.read_text(), document)
            self.assertEqual(source.read_bytes(), original)


    def test_actual_xff_matches_independent_oracle(self):
        if BINARY is None:
            self.skipTest("Bazel supplies xff executable")
        # Other tools are host-dependent; always verify xff, and explicitly report their skips.
        with mock.patch.object(compare.shutil, 'which', return_value=None):
            result = compare.collect(BINARY, files=4, depth=2, repetitions=1)
        self.assertEqual(len(result['tasks']), 22)
        self.assertTrue(all(task['skips'] for task in result['tasks'] if task['name'] != 'files-safe'))
        self.assertTrue(all(not task['skips'] for task in result['tasks'] if task['name'] == 'files-safe'))
        page = compare.render(result)
        self.assertIn('Tool comparisons', page)
        self.assertIn('Input files/s', page)
        self.assertIn('Skipped:', page)
        broken = copy.deepcopy(result)
        broken['tasks'][0]['participants']['xff']['samples'] = []
        with self.assertRaisesRegex(ValueError, 'missing'):
            compare.render(broken)


if __name__ == '__main__':
    if len(sys.argv) > 1 and not sys.argv[1].startswith('-'):
        BINARY = Path(sys.argv.pop(1)).resolve()
    unittest.main()
