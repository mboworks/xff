# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Publication waits for benchmarks, rejects stale attempts, and treats artifacts as data."""

import io
import json
from pathlib import Path
import subprocess
import sys
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
    return [{'name': 'Benchmark comparison (Linux and macOS, best 7 of 9)', 'status': 'completed',
             'conclusion': 'success', 'started_at': '2026-10-03T10:00:00Z',
             'completed_at': '2026-10-03T10:02:00Z'}]


def artifacts():
    return [{'id': index, 'name': f'benchmark-pr-{platform}-{arch}', 'expired': False,
             'created_at': '2026-10-03T10:01:00Z', 'size_in_bytes': 100}
            for index, (platform, arch) in enumerate((('linux', 'x86_64'), ('macos', 'arm64')), 1)]


class BenchmarkPreviewCiTest(unittest.TestCase):
    def test_queued_preview_check_rejects_stale_source_without_loading_site_or_artifacts(self):
        cases = (
            ('current', source(), pull(), True),
            ('new-attempt', dict(source(), run_attempt=3), pull(), False),
            ('new-head', source(), dict(pull(), head={'sha': 'd' * 40, 'repo': {'id': 2}}), False),
            ('closed', source(), dict(pull(), state='closed'), False),
            ('wrong-repository', source(), dict(pull(), head={'sha': 'b' * 40, 'repo': {'id': 3}}), False),
        )
        for name, run, pr, available in cases:
            with self.subTest(case=name), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'output'
                api = mock.Mock(repository='owner/repo')
                api.request.side_effect = lambda path: run if path == 'actions/runs/10' else pr
                arguments = ['preview', 'current', '--repository=owner/repo', '--run=10', '--attempt=2',
                             '--output=' + str(output)]
                with mock.patch.object(sys, 'argv', arguments), mock.patch.object(ci, 'GitHub', return_value=api), \
                        mock.patch.object(ci, 'collect') as collect, mock.patch.object(ci, 'wait') as wait:
                    ci.main()
                self.assertEqual(output.read_text(), f'available={str(available).lower()}\n')
                api.pages.assert_not_called()
                collect.assert_not_called()
                wait.assert_not_called()
                self.assertEqual(api.request.call_count, 1 if name == 'new-attempt' else 2)
                self.assertEqual(list(Path(directory).iterdir()), [output])

    def test_queued_preview_api_failure_is_an_error_not_an_obsolete_result(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'output'
            arguments = ['preview', 'current', '--repository=owner/repo', '--run=10', '--attempt=2',
                         '--output=' + str(output)]
            api = mock.Mock()
            failure = subprocess.CalledProcessError(1, ['gh'], stderr=b'gh: HTTP 503\n')
            api.request.side_effect = failure
            with mock.patch.object(sys, 'argv', arguments), mock.patch.object(ci, 'GitHub', return_value=api), \
                    self.assertRaises(subprocess.CalledProcessError):
                ci.main()
            self.assertFalse(output.exists())

    def test_coalesced_deployment_comments_on_every_current_pr_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pulls = [pull(), dict(pull(), number=11)]
            sources = {10: source(), 12: dict(source(), id=12, pull_requests=[{'number': 11}])}
            for number, run in ((9, 10), (11, 12)):
                for platform in ('linux', 'macos'):
                    folder = root / f'previews/{number}/{run}/2/{platform}'
                    folder.mkdir(parents=True)
                    record = {'kind': 'pr-preview', 'pull_number': number, 'platform': platform,
                              'head': 'c' * 40, 'branch_head': 'b' * 40, 'base': 'a' * 40,
                              'source': sources[run],
                              'tool_comparisons': {'tasks': [{'shape': 'legacy'}]}}
                    (folder / 'report.json').write_text(json.dumps(record))
            api = mock.Mock(repository='owner/repo')
            api.pages.side_effect = lambda path: pulls if path == 'pulls?state=open' else []

            def request(path, *args):
                if path.startswith('actions/runs/'):
                    return sources[int(path.rsplit('/', 1)[1])]
                if path.startswith('pulls/'):
                    return next(pr for pr in pulls if pr['number'] == int(path.rsplit('/', 1)[1]))
                return {}

            api.request.side_effect = request
            url = 'https://example.org/project/benchmarks/'
            self.assertEqual(ci.update_retained_comments(api, root, url), 2)
            mutations = [call for call in api.request.call_args_list if len(call.args) > 1]
            self.assertEqual([call.args[0] for call in mutations], ['issues/9/comments', 'issues/11/comments'])
            for call, (number, run) in zip(mutations, ((9, 10), (11, 12))):
                self.assertIn(f'{url}previews/{number}/{run}/2/linux/', call.args[1]['body'])
            # A newer current attempt or a closed PR must never receive this snapshot's comment.
            sources[10]['run_attempt'] = 3
            pulls[1]['state'] = 'closed'
            api.request.reset_mock()
            self.assertEqual(ci.update_retained_comments(api, root, url), 0)
            self.assertFalse(any(len(call.args) > 1 for call in api.request.call_args_list))

    def test_source_operations_still_require_both_run_and_attempt(self):
        for operation in ('wait', 'current', 'collect', 'comment'):
            for arguments in ([], ['--run=10'], ['--attempt=2']):
                with self.subTest(operation=operation, arguments=arguments), \
                        mock.patch.object(sys, 'argv', ['preview', operation, '--repository=owner/repo', *arguments]), \
                        mock.patch.object(sys, 'stderr', io.StringIO()), self.assertRaises(SystemExit) as error:
                    ci.main()
                self.assertEqual(error.exception.code, 2)

    def test_retained_comment_command_uses_snapshot_without_a_triggering_source_run(self):
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(sys, 'argv', ['preview', 'comment-retained', '--repository=owner/repo',
                                               '--root=' + directory, '--site-url=https://example.org/']), \
                mock.patch.object(ci, 'GitHub') as github, \
                mock.patch.object(ci, 'update_retained_comments', return_value=2) as update:
            ci.main()
            update.assert_called_once_with(github.return_value, Path(directory), 'https://example.org/')

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
        api.pages.return_value = [dict(jobs()[0], status='in_progress', conclusion=None)]
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

    def test_artifact_provenance_uses_the_same_unambiguous_comparison_jobs_as_readiness(self):
        aggregate = jobs()[0]
        legacy = [dict(aggregate, name=f'Benchmark comparison ({platform}, best 7 of 9)')
                  for platform in ('linux', 'macos')]
        self.assertEqual(set(ci.matching_artifacts(artifacts(), legacy)), {'linux', 'macos'})
        for candidates in ([dict(aggregate, conclusion='failure')],
                           [dict(aggregate, status='in_progress', conclusion=None)], legacy[:1]):
            with self.subTest(jobs=candidates), self.assertRaisesRegex(ValueError, 'both current-attempt'):
                ci.matching_artifacts(artifacts(), candidates)
        for candidates in ([aggregate, aggregate], [aggregate, legacy[0]]):
            with self.subTest(jobs=candidates), self.assertRaisesRegex(ValueError, 'ambiguous'):
                ci.matching_artifacts(artifacts(), candidates)
        for boundary in ('started_at', 'completed_at'):
            candidates = artifacts()
            candidates[0]['created_at'] = aggregate[boundary]
            with self.subTest(boundary=boundary):
                self.assertEqual(set(ci.matching_artifacts(candidates, jobs())), {'linux', 'macos'})

    def test_transient_read_errors_retry_with_bounded_backoff(self):
        response = subprocess.CompletedProcess([], 0, stdout=b'{"available":true}')
        for failure in (subprocess.CalledProcessError(1, ['gh'], stderr=b'gh: HTTP 500\n'),
                        subprocess.CalledProcessError(1, ['gh'], stderr=b'gh: HTTP 503\n'),
                        subprocess.CalledProcessError(1, ['gh'], stderr=b'gh: Server Error (HTTP 502)\n'),
                        subprocess.TimeoutExpired(['gh'], 120)):
            with self.subTest(failure=failure), \
                    mock.patch.object(ci.subprocess, 'run', side_effect=[failure, response]) as run, \
                    mock.patch.object(ci.time, 'sleep') as sleep:
                self.assertEqual(ci.GitHub('owner/repo').request('actions/runs/10'), {'available': True})
                self.assertEqual(run.call_count, 2)
                sleep.assert_called_once_with(1)
        failure = subprocess.CalledProcessError(1, ['gh'], stderr=b'gh: HTTP 504\n')
        with mock.patch.object(ci.subprocess, 'run', side_effect=failure) as run, \
                mock.patch.object(ci.time, 'sleep') as sleep, self.assertRaises(subprocess.CalledProcessError):
            ci.GitHub('owner/repo').request('actions/runs/10')
        self.assertEqual(run.call_count, 4)
        self.assertEqual(sleep.call_args_list, [mock.call(1), mock.call(2), mock.call(4)])

    def test_permanent_read_errors_and_comment_mutations_are_never_retried(self):
        for body, method, stderr in ((None, None, b'gh: HTTP 404\n'),
                                     (None, None, b'gh: HTTP 403\n'),
                                     ({'body': 'comment'}, None, b'gh: HTTP 503\n'),
                                     ({'body': 'comment'}, 'PATCH', b'gh: HTTP 502\n')):
            failure = subprocess.CalledProcessError(1, ['gh'], stderr=stderr)
            with self.subTest(body=body, method=method), \
                    mock.patch.object(ci.subprocess, 'run', side_effect=failure) as run, \
                    mock.patch.object(ci.time, 'sleep') as sleep, \
                    self.assertRaises(subprocess.CalledProcessError):
                ci.GitHub('owner/repo').request('issues/9/comments', body, method)
            run.assert_called_once()
            sleep.assert_not_called()
        with mock.patch.object(ci.subprocess, 'run', side_effect=subprocess.TimeoutExpired(['gh'], 120)) as run, \
                mock.patch.object(ci.time, 'sleep') as sleep, self.assertRaises(subprocess.TimeoutExpired):
            ci.GitHub('owner/repo').request('issues/9/comments', {'body': 'comment'}, 'PATCH')
        run.assert_called_once()
        sleep.assert_not_called()

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
