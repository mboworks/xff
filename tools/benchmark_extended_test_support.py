# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Small synthetic observations for extended-pipeline tests; never performance data."""

import copy
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import tempfile
from unittest import mock

import benchmark_extended as suite
import benchmark_extended_records as records


class ExtendedFixture:
    def __init__(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.cases = [suite.Case('files-exact', shape, files, cpus)
                      for shape in ('broad', 'deep') for files in (3, 6) for cpus in (1, 3)]
        self.patch = mock.patch.object(suite, 'cases', return_value=self.cases)
        self.patch.start()
        self.revisions, self.builds = [], {}
        for index, sha in enumerate(('a' * 40, 'b' * 40), 1):
            directory = self.root / 'binaries' / sha
            directory.mkdir(parents=True)
            binary = directory / 'xff_full'
            binary.write_bytes(('synthetic executable ' + sha).encode())
            artifact = 'bazel-out/test/bin/xff/cli/xff_full'
            manifest = {'source': {'revision': sha, 'tree': sha, 'clean': True}, 'sha256': records.digest(binary),
                        'bytes': binary.stat().st_size, 'artifact': artifact,
                        'command': ['bazel', 'build', '//xff/cli:xff_full', '--config=clang_release', '--config=xff_full'],
                        'configuration': {'.bazelrc': 'c' * 64}}
            (directory / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
            (directory / 'link-actions.json').write_text(json.dumps({'actions': [
                {'arguments': ['clang++', '-flto=thin', '-o', artifact]}]}) + '\n')
            revision = {'sha': sha, 'pr': index, 'date': f'2026-11-0{index}T00:00:00Z',
                        'subject': 'Synthetic fixture', 'binary': str(binary), 'binary_sha256': manifest['sha256'],
                        'build_sha256': records.digest(directory / 'build.json'),
                        'link_actions_sha256': records.digest(directory / 'link-actions.json')}
            self.revisions.append(revision)
            self.builds[sha] = records.build_proof(directory, revision)
        self.contract = {'recipe': suite.RECIPE, 'cases': [asdict(case) for case in self.cases],
                         'revisions': self.revisions, 'anchor': self.revisions[-1], 'repetitions': 9, 'keep': 7,
                         'estimator': 'mean-fastest', 'warmup_rounds': 1, 'fixture': '/synthetic-fixture',
                         'filesystem': 'tmpfs', 'platform': 'Linux-test-x86_64', 'affinity': [0, 1, 2],
                         'tools': {name: {'path': '/bin/' + name, 'sha256': 'd' * 64, 'version': 'synthetic ' + name}
                                   for name in ('find', 'rg', 'tar')},
                         'launcher': {'path': '/synthetic-launcher', 'sha256': 'e' * 64,
                                      'wall_method': 'synthetic wall', 'memory_method': 'synthetic RSS'}}

    def close(self):
        self.patch.stop()
        self.temporary.cleanup()

    def rows(self, revision=None):
        revision = revision or self.revisions[0]
        rows = []
        for case in self.cases:
            commands, cwd, separator = suite.commands(case, Path(self.contract['fixture']), revision['binary'],
                                                     {k: v['path'] for k, v in self.contract['tools'].items()})
            anchor, _, _ = suite.commands(case, Path(self.contract['fixture']), self.revisions[-1]['binary'],
                                          {k: v['path'] for k, v in self.contract['tools'].items()})
            commands['anchor'] = anchor['xff']
            wanted = suite.expected(case, Path(self.contract['fixture']))
            output = separator.join(wanted) + separator
            samples = []
            for round_number in range(10):
                for label in commands:
                    seconds = 100 if round_number == 0 else {'xff': 2, 'anchor': 1, 'find': 4}[label]
                    samples.append({'label': label, 'round': round_number,
                                    'phase': 'warmup' if round_number == 0 else 'measured',
                                    'elapsed_seconds': seconds, 'user_cpu_seconds': seconds / 2,
                                    'system_cpu_seconds': seconds / 4, 'peak_child_rss_bytes': 1024,
                                    'first_stdout_seconds': .01, 'exit_codes': [0], 'stderr_bytes': 0, 'stderr': '',
                                    'stdout_bytes': len(output), 'stdout_sha256': hashlib.sha256(output).hexdigest(),
                                    'validated_records': len(wanted)})
            rows.append({'case': asdict(case), 'revision': revision, 'anchor': self.revisions[-1],
                         'commands': commands, 'cwd': str(cwd), 'affinity': self.contract['affinity'][:case.cpus],
                         'environment': {'PATH': '/usr/bin:/bin', 'HOME': '/synthetic-fixture/home', 'LC_ALL': 'C', 'TZ': 'UTC'},
                         'expected_records': len(wanted), 'expected_sha256': hashlib.sha256(b'\0'.join(sorted(wanted))).hexdigest(),
                         'samples': samples, 'after': {'utc': '2026-11-08T12:00:00Z'}})
        return rows

    def record(self, origin='local', comparison='fixed-anchor'):
        collection = {'run_id': 12, 'run_attempt': 1, 'repository': 'owner/repo',
                      'head_sha': self.revisions[0]['sha'], 'base_sha': self.revisions[-1]['sha']} if origin == 'ci' else None
        return records.make_record(copy.deepcopy(self.contract), self.revisions[0], self.rows(), self.builds,
                                   'linux', 'x86_64', 'synthetic-only', comparison, origin, collection)

    def source(self):
        return {'id': 12, 'run_attempt': 1, 'head_sha': self.revisions[0]['sha'],
                'base_sha': self.revisions[-1]['sha'], 'name': 'Benchmarks',
                'path': '.github/workflows/benchmarks.yml', 'event': 'push', 'head_branch': 'main',
                'created_at': '2026-11-01T00:00:00Z', 'status': 'completed', 'conclusion': 'failure',
                'repository': {'full_name': 'owner/repo'}}
