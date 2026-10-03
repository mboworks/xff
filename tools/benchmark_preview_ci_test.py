# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Publication waits for benchmarks, rejects stale attempts, and treats artifacts as data."""

import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile

import benchmark_preview_ci as ci


def source():
    return {'id': 10, 'name': 'Test', 'status': 'in_progress', 'run_attempt': 2,
            'event': 'pull_request', 'head_sha': 'b' * 40, 'head_repository': {'id': 2},
            'head_branch': 'feature', 'created_at': '2026-10-03T09:00:00Z',
            'repository': {'id': 1}, 'pull_requests': [{'number': 9}]}


def pull():
    return {'number': 9, 'state': 'open', 'head': {'sha': 'b' * 40, 'repo': {'id': 2}},
            'base': {'repo': {'id': 1}}}


def jobs():
    return [{'name': f'Benchmark comparison ({platform}, best 7 of 9)', 'status': 'completed',
             'conclusion': 'success', 'started_at': '2026-10-03T10:00:00Z',
             'completed_at': '2026-10-03T10:02:00Z'} for platform in ('linux', 'macos')]


def artifacts():
    return [{'id': index, 'name': f'benchmark-pr-{platform}-{arch}', 'expired': False,
             'created_at': '2026-10-03T10:01:00Z', 'size_in_bytes': 100}
            for index, (platform, arch) in enumerate((('linux', 'x86_64'), ('macos', 'arm64')), 1)]


class BenchmarkPreviewCiTest(unittest.TestCase):
    def test_collect_validates_tested_merge_and_retains_exact_json_from_each_platform(self):
        api = mock.Mock(repository='owner/repo')
        sample = dict.fromkeys(ci.benchmark_preview.benchmark_compare.METRICS, 1)
        sample.update(stderr='', exit_codes=[0])
        raw = json.dumps({'head': 'c' * 40, 'tool_comparisons': {
            'schema': 1, 'tools': {}, 'contract': {'repetitions': 1},
            'tasks': [{'shape': 'broad', 'name': 'files', 'input_files': 1, 'expected_count': 1, 'skips': {},
                       'participants': {'xff': {'pipeline': [['xff']], 'samples': [sample]}}}]}}).encode()
        downloads = {}
        for artifact in artifacts():
            data = io.BytesIO()
            with zipfile.ZipFile(data, 'w') as archive:
                archive.writestr(artifact['name'] + '.json', raw)
            downloads[f'actions/artifacts/{artifact["id"]}/zip'] = data.getvalue()
        commit = {'sha': 'c' * 40, 'parents': [{'sha': 'a' * 40}, {'sha': 'b' * 40}],
                  'committer': {'date': '2026-10-03T09:59:00Z'}}
        responses = {'actions/runs/10': source(), 'pulls/9': pull(), 'git/commits/' + 'c' * 40: commit, **downloads}
        api.request.side_effect = lambda path, **kwargs: responses[path]
        api.pages.side_effect = lambda path, key: artifacts() if key == 'artifacts' else jobs()
        with tempfile.TemporaryDirectory() as directory:
            paths = ci.collect(api, 10, 2, Path(directory))
            self.assertEqual(len(paths), 2)
            for path in paths:
                self.assertEqual((path / 'artifact.json').read_bytes(), raw)
                record = json.loads((path / 'report.json').read_text())
                self.assertEqual(record['branch_head'], 'b' * 40)
                self.assertEqual(record['artifact']['id'], 1 if path.name == 'linux' else 2)
            self.assertEqual(ci.collect(api, 10, 2, Path(directory)), paths)
        commit['parents'][1]['sha'] = 'f' * 40
        with tempfile.TemporaryDirectory() as directory, self.assertRaisesRegex(ValueError, 'workflow branch head'):
            ci.collect(api, 10, 2, Path(directory))

    def test_comment_updates_only_its_bot_message_for_the_current_head(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for platform in ('linux', 'macos'):
                folder = root / 'previews/9/10/2' / platform
                folder.mkdir(parents=True)
                record = {'kind': 'pr-preview', 'pull_number': 9, 'platform': platform,
                          'head': 'c' * 40, 'branch_head': 'b' * 40, 'base': 'a' * 40,
                          'source': dict(source(), created_at='2026-10-03T10:00:00Z'),
                          'tool_comparisons': {'tasks': [{'shape': 'legacy'}]}}
                (folder / 'report.json').write_text(json.dumps(record))
            api = mock.Mock(repository='owner/repo')
            api.request.side_effect = lambda path, *args: source() if path.startswith('actions/') else pull()
            api.pages.return_value = [
                {'id': 1, 'user': {'login': 'person', 'type': 'User'}, 'body': ci.COMMENT_MARKER},
                {'id': 2, 'user': {'login': 'another-bot', 'type': 'Bot'}, 'body': ci.COMMENT_MARKER}]
            url = 'https://example.org/project/benchmarks/'
            self.assertTrue(ci.update_comment(api, 10, 2, root, url))
            mutations = [call for call in api.request.call_args_list if len(call.args) > 1]
            self.assertEqual(len(mutations), 1)
            path, data = mutations[0].args
            self.assertEqual(path, 'issues/9/comments')
            self.assertIn('regression status is unknown', data['body'])
            self.assertIn(url + 'previews/9/10/2/linux/', data['body'])
            own = {'id': 3, 'user': {'login': 'github-actions[bot]', 'type': 'Bot'}, 'body': data['body']}
            api.pages.return_value.append(own)
            api.request.reset_mock()
            self.assertTrue(ci.update_comment(api, 10, 2, root, url))
            self.assertFalse(any(len(call.args) > 1 for call in api.request.call_args_list))
            own['body'] = ci.COMMENT_MARKER + '\nold result'
            self.assertTrue(ci.update_comment(api, 10, 2, root, url))
            api.request.assert_called_with('issues/comments/3', data, 'PATCH')
            api.request.reset_mock()
            api.request.side_effect = lambda path, *args: source() if path.startswith('actions/') else dict(pull(), state='closed')
            self.assertFalse(ci.update_comment(api, 10, 2, root, url))
            self.assertFalse(any(len(call.args) > 1 for call in api.request.call_args_list))

    def test_benchmark_completion_does_not_wait_for_other_test_jobs(self):
        api = mock.Mock()
        api.request.side_effect = lambda path: source() if path.startswith('actions/') else pull()
        api.pages.return_value = jobs() + [{'name': 'test (macos-latest)', 'status': 'in_progress'}]
        state, result = ci.ready(api, 10, 2)
        self.assertEqual(state, 'ready')
        self.assertEqual(result[1]['number'], 9)
        api.pages.assert_called_once_with('actions/runs/10/attempts/2/jobs', 'jobs')
        api.pages.return_value = jobs()[:1]
        self.assertEqual(ci.ready(api, 10, 2), ('waiting', None))
        api.request.side_effect = lambda path: dict(source(), status='completed') if path.startswith('actions/') else pull()
        self.assertEqual(ci.ready(api, 10, 2), ('finished', None))

    def test_stale_run_attempt_or_head_never_publishes(self):
        for run, pr in ((dict(source(), run_attempt=3), pull()),
                        (dict(source(), event='push'), pull()),
                        (dict(source(), name='Other workflow'), pull()),
                        (dict(source(), pull_requests=[]), pull()),
                        (source(), dict(pull(), state='closed'))):
            api = mock.Mock()
            api.request.side_effect = lambda path: run if path.startswith('actions/') else pr
            with self.subTest(run=run, pr=pr):
                self.assertEqual(ci.ready(api, 10, 2), ('obsolete', None))
                api.pages.assert_not_called()

    def test_only_artifacts_created_by_this_attempt_comparison_jobs_are_selected(self):
        self.assertEqual(set(ci.matching_artifacts(artifacts(), jobs())), {'linux', 'macos'})
        for field, value in (('created_at', '2026-10-02T10:01:00Z'),
                             ('created_at', '2026-10-03T10:03:00Z'), ('expired', True)):
            candidates = artifacts()
            candidates[0][field] = value
            with self.subTest(field=field, value=value), self.assertRaisesRegex(ValueError, 'both current-attempt'):
                ci.matching_artifacts(candidates, jobs())
        with self.assertRaisesRegex(ValueError, 'multiple artifacts'):
            ci.matching_artifacts([*artifacts(), artifacts()[0]], jobs())

    def test_zip_reads_only_expected_json_without_extracting_uploaded_paths_or_html(self):
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w') as archive:
            archive.writestr('benchmark-pr-linux-x86_64.json', b'{"head":"data"}')
            archive.writestr('../../escaped-file', b'never extracted')
            archive.writestr('benchmark-pr-linux-x86_64.html', b'<script>untrusted()</script>')
        self.assertEqual(ci.artifact_json(data.getvalue(), 'benchmark-pr-linux-x86_64'), b'{"head":"data"}')
        with self.assertRaisesRegex(ValueError, 'one bounded JSON'):
            ci.artifact_json(data.getvalue(), 'benchmark-pr-macos-arm64')
        link = zipfile.ZipInfo('benchmark-pr-linux-x86_64.json')
        link.external_attr = 0o120777 << 16
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w') as archive:
            archive.writestr(link, b'target')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            ci.artifact_json(data.getvalue(), 'benchmark-pr-linux-x86_64')

    def test_wait_finishes_on_failed_or_superseded_run(self):
        for terminal, expected in (('ready', True), ('finished', False), ('obsolete', False)):
            with mock.patch.object(ci, 'ready', side_effect=[('waiting', None), (terminal, None)]), \
                    mock.patch.object(ci.time, 'sleep') as sleep:
                self.assertEqual(ci.wait(mock.Mock(), 10, 2), expected)
                sleep.assert_called_once_with(30)


if __name__ == '__main__':
    unittest.main()
