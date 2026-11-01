# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Publication races preserve both writers and rebuild indexes before retrying."""

import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import publish_site


class PublishSiteTest(unittest.TestCase):
    def git(self, repo, *arguments):
        return subprocess.run(['git', '-C', str(repo), *arguments], check=True,
                              text=True, capture_output=True).stdout.strip()

    def configure(self, repo):
        for key, value in (('user.name', 'Publisher'), ('user.email', 'publisher@example.invalid'),
                           ('commit.gpgsign', 'false'), ('core.hooksPath', '/dev/null')):
            self.git(repo, 'config', key, value)

    def regenerate(self, repo):
        files = sorted(path.name for path in (repo / 'measurements').glob('*.json'))
        (repo / 'index.json').write_text(json.dumps(files))
        self.git(repo, 'add', '--all')
        if self.git(repo, 'diff', '--cached', '--name-only'):
            self.git(repo, 'commit', '-m', 'Regenerate index')

    def repositories(self, root):
        remote = root / 'remote.git'
        publisher = root / 'publisher'
        uploader = root / 'uploader'
        remote.mkdir()
        publisher.mkdir()
        self.git(remote, 'init', '--bare', '--initial-branch=coverage-pages')
        self.git(publisher, 'init', '--initial-branch=coverage-pages')
        self.configure(publisher)
        (publisher / 'measurements').mkdir()
        (publisher / 'measurements/seed.json').write_text('{"seed": true}')
        self.regenerate(publisher)
        self.git(publisher, 'remote', 'add', 'origin', str(remote))
        self.git(publisher, 'push', '-u', 'origin', 'coverage-pages')
        self.git(root, 'clone', str(remote), str(uploader))
        self.configure(uploader)
        (publisher / 'measurements/paired.json').write_text('{"paired": true}')
        self.regenerate(publisher)
        return publisher, uploader, remote

    def upload(self, uploader, name):
        (uploader / 'measurements' / name).write_text('{"local": true}')
        self.git(uploader, 'add', '--all')
        self.git(uploader, 'commit', '-m', 'Upload local batch')
        self.git(uploader, 'push')
        return self.git(uploader, 'rev-parse', 'HEAD')

    def test_concurrent_upload_is_preserved_and_enters_rebuilt_index(self):
        with tempfile.TemporaryDirectory() as temporary:
            publisher, uploader, remote = self.repositories(Path(temporary))
            uploaded = self.upload(uploader, 'local.json')
            rebuild = mock.Mock(side_effect=lambda: self.regenerate(publisher))
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                publish_site.publish(publisher, rebuild)
            rebuild.assert_called_once()
            self.assertEqual(json.loads(self.git(remote, 'show', 'coverage-pages:index.json')),
                             ['local.json', 'paired.json', 'seed.json'])
            self.git(remote, 'merge-base', '--is-ancestor', uploaded, 'coverage-pages')
            self.assertEqual(self.git(publisher, 'status', '--porcelain'), '')

    def test_retries_are_bounded_when_uploads_keep_advancing_the_branch(self):
        with tempfile.TemporaryDirectory() as temporary:
            publisher, uploader, remote = self.repositories(Path(temporary))
            self.upload(uploader, 'local-0.json')
            retries = []
            def regenerate():
                self.regenerate(publisher)
                retries.append(len(retries) + 1)
                self.upload(uploader, f'local-{len(retries)}.json')
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(subprocess.CalledProcessError):
                publish_site.publish(publisher, regenerate, attempts=3)
            self.assertEqual(retries, [1, 2])
            self.assertNotIn('paired.json', self.git(remote, 'ls-tree', '-r', '--name-only', 'coverage-pages'))

    def test_push_failure_without_branch_advancement_is_not_retried(self):
        with tempfile.TemporaryDirectory() as temporary:
            publisher, _, remote = self.repositories(Path(temporary))
            self.git(remote, 'config', 'core.hooksPath', str(remote / 'hooks'))
            hook = remote / 'hooks/pre-receive'
            hook.write_text('#!/bin/sh\nexit 1\n')
            hook.chmod(0o755)
            rebuild = mock.Mock()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(subprocess.CalledProcessError):
                publish_site.publish(publisher, rebuild)
            rebuild.assert_not_called()

    def test_conflicting_measurement_changes_stop_and_abort_the_rebase(self):
        with tempfile.TemporaryDirectory() as temporary:
            publisher, uploader, remote = self.repositories(Path(temporary))
            (publisher / 'measurements/seed.json').write_text('{"publisher": true}')
            self.git(publisher, 'add', '--all')
            self.git(publisher, 'commit', '-m', 'Change local measurement')
            original = self.git(publisher, 'rev-parse', 'HEAD')
            uploaded = self.upload(uploader, 'seed.json')
            rebuild = mock.Mock()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(subprocess.CalledProcessError):
                publish_site.publish(publisher, rebuild)
            rebuild.assert_not_called()
            self.assertEqual(self.git(publisher, 'rev-parse', 'HEAD'), original)
            self.assertEqual(self.git(publisher, 'status', '--porcelain'), '')
            self.assertEqual(self.git(remote, 'rev-parse', 'coverage-pages'), uploaded)


if __name__ == '__main__':
    unittest.main()
