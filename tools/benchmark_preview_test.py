# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""PR preview provenance, immutable observations, and obsolete-head rejection."""

import copy
import json
from pathlib import Path
import tempfile
import unittest

import benchmark_preview as preview


def source():
    return {'id': 10, 'run_attempt': 1, 'head_sha': 'b' * 40, 'event': 'pull_request',
            'head_branch': 'feature', 'created_at': '2026-10-03T10:00:00Z',
            'repository': {'id': 1}, 'head_repository': {'id': 2}}


def pull():
    return {'number': 9, 'state': 'open', 'head': {'sha': 'b' * 40, 'repo': {'id': 2}},
            'base': {'repo': {'id': 1}}}


def tested_commit():
    return {'sha': 'c' * 40, 'parents': [{'sha': 'a' * 40}, {'sha': 'b' * 40}],
            'committer': {'date': '2026-10-03T09:59:00Z'}}


def report():
    sample = {metric: 1 for metric in preview.benchmark_compare.METRICS}
    sample.update(stderr='', exit_codes=[0])
    return {'head': 'c' * 40, 'tool_comparisons': {
        'schema': 1, 'tools': {'xff': {'sha256': 'binary'}}, 'contract': {'repetitions': 1},
        'tasks': [{'shape': 'broad', 'name': 'files', 'input_files': 1, 'expected_count': 1, 'skips': {},
                   'participants': {'xff': {'pipeline': [['xff']], 'samples': [sample]}}}]}}


class BenchmarkPreviewTest(unittest.TestCase):
    def retain(self, root, data=None, run=None, pr=None, commit=None):
        return preview.retain(root, json.dumps(data or report()).encode(), run or source(), pr or pull(),
                              commit or tested_commit(), 'linux', '2026-10-03T10:04:00Z')

    def test_preserves_branch_tested_merge_and_raw_observations(self):
        raw = json.dumps(report()).encode()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = self.retain(root)
            self.assertEqual(folder.relative_to(root).as_posix(), 'previews/9/10/1/linux')
            self.assertEqual((folder / 'artifact.json').read_bytes(), raw)
            record = json.loads((folder / 'report.json').read_text())
            self.assertEqual(record['branch_head'], 'b' * 40)
            self.assertEqual(record['head'], 'c' * 40)
            self.assertEqual(record['base'], 'a' * 40)
            self.assertEqual(record['source']['pull_requests'], [{'number': 9}])
            self.assertEqual(record['tool_comparisons'], report()['tool_comparisons'])
            self.assertEqual(self.retain(root), folder)
            changed = report()
            changed['tool_comparisons']['tasks'][0]['name'] = 'different'
            with self.assertRaisesRegex(ValueError, 'cannot change'):
                self.retain(root, data=changed)
            text = preview.render_report(record, 'owner/repo', '../../../../../')
            self.assertIn('PR preview', text)
            self.assertIn('owner/repo/pull/9', text)
            self.assertIn('tested merge', text)

    def test_only_current_matching_repository_and_branch_head_are_eligible(self):
        for change in (
            lambda value: value.update(state='closed'),
            lambda value: value['head'].update(sha='d' * 40),
            lambda value: value['head'].update(repo=None),
            lambda value: value['head']['repo'].update(id=3),
            lambda value: value['base']['repo'].update(id=3),
        ):
            pr = pull()
            change(pr)
            with self.subTest(pr=pr):
                self.assertFalse(preview.current_pull(source(), pr))
                with tempfile.TemporaryDirectory() as directory, self.assertRaisesRegex(ValueError, 'PR is closed'):
                    self.retain(Path(directory), pr=pr)
        self.assertFalse(preview.current_pull({'event': 'pull_request'}, {'state': 'open'}))

    def test_rejects_unrelated_tested_commit_and_failed_measurements(self):
        for change in (
            lambda value: value.update(sha='d' * 40),
            lambda value: value['parents'][1].update(sha='d' * 40),
            lambda value: value.update(parents=[]),
        ):
            commit = tested_commit()
            change(commit)
            with self.subTest(commit=commit), tempfile.TemporaryDirectory() as directory, self.assertRaises(ValueError):
                self.retain(Path(directory), commit=commit)
        failed = report()
        failed['tool_comparisons']['tasks'][0]['participants']['xff']['samples'][0]['exit_codes'] = [2]
        with tempfile.TemporaryDirectory() as directory, self.assertRaisesRegex(ValueError, 'failed comparison'):
            self.retain(Path(directory), data=failed)

    def test_latest_attempt_selected_only_while_its_head_is_current(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.retain(root)
            rerun = dict(source(), run_attempt=2)
            latest = self.retain(root, run=rerun)
            self.assertEqual(preview.selected(root, [pull()]), [latest / 'report.json'])
            closed = dict(pull(), state='closed')
            self.assertEqual(preview.selected(root, [closed]), [])
            newer = pull()
            newer['head']['sha'] = 'd' * 40
            self.assertEqual(preview.selected(root, [newer]), [])
            self.assertTrue((latest / 'artifact.json').exists())

    def test_waits_for_both_platform_comparisons_not_other_ci_jobs(self):
        jobs = [{'name': f'Benchmark comparison ({platform}, best 7 of 9)', 'status': 'completed',
                 'conclusion': 'success', 'completed_at': '2026-10-03T10:04:00Z'} for platform in ('linux', 'macos')]
        self.assertIsNone(preview.completed_comparisons(jobs[:1]))
        self.assertEqual(set(preview.completed_comparisons(jobs)), {'linux', 'macos'})
        pending_test = {'name': 'test (macos-latest)', 'status': 'in_progress', 'conclusion': None}
        self.assertEqual(preview.completed_comparisons(jobs + [pending_test]), preview.completed_comparisons(jobs))
        failed = copy.deepcopy(jobs)
        failed[1]['conclusion'] = 'failure'
        self.assertIsNone(preview.completed_comparisons(failed))
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            preview.completed_comparisons(jobs + jobs)

    def test_artifact_names_and_output_identifiers_are_constrained(self):
        for name, expected in (('benchmark-pr-linux-x86_64', 'linux'), ('benchmark-pr-macos-arm64', 'macos')):
            self.assertEqual(preview.artifact_platform(name), expected)
        for name in ('../../linux', 'benchmark-pr-windows-x86_64', 'benchmark-pr-linux-../../x'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                preview.artifact_platform(name)
        for value in (-1, 0, True, '../2'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                preview.positive_id(value)


if __name__ == '__main__':
    unittest.main()
