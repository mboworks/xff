# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Exercise the complete collector contract with explicitly synthetic samples."""

from contextlib import contextmanager
import hashlib
from pathlib import Path
import unittest
from unittest import mock

import benchmark_extended_ci as ci
from benchmark_extended_test_support import ExtendedFixture
from test_paths import repository_file


class BenchmarkExtendedCiTest(unittest.TestCase):
    def setUp(self):
        self.fixture = ExtendedFixture()
        self.addCleanup(self.fixture.close)
        self.root = self.fixture.root / 'collection'
        ci.history.write(self.fixture.root / 'revisions.json', self.fixture.revisions)
        ci.history.write(self.fixture.root / 'build-contract.json', {'synthetic-only': True})
        self.tools = {}
        for name in ('find', 'rg', 'tar'):
            path = self.fixture.root / name
            path.write_bytes(('synthetic ' + name).encode())
            self.tools[name] = str(path)
        self.launcher = self.fixture.root / 'launcher'
        self.launcher.write_bytes(b'synthetic native launcher')
        self.calls = 0
        self.fail_at = None
        self.mutate_at = None

    @contextmanager
    def memory(self, root, existing=None):
        root.mkdir(parents=True, exist_ok=True)
        try:
            yield root / 'fixture', {'storage': 'memory', 'filesystem': 'tmpfs'}
        finally:
            ci.history.write(root / 'storage-release.json', {'synthetic-only': True, 'released_at': '2026-11-08T12:00:00Z'})

    def invoke(self, spec, wanted, separator, failure):
        self.calls += 1
        if self.fail_at == self.calls:
            raise ValueError('synthetic failed output')
        if self.mutate_at == self.calls:
            Path(self.tools['find']).write_bytes(b'changed reference executable')
        output = separator.join(wanted) + separator
        return {'elapsed_seconds': 1, 'user_cpu_seconds': .5, 'system_cpu_seconds': .2,
                'first_stdout_seconds': .1, 'peak_child_rss_bytes': 1024, 'exit_codes': [0],
                'stderr_bytes': 0, 'stderr': '', 'stdout_bytes': len(output),
                'stdout_sha256': hashlib.sha256(output).hexdigest(), 'validated_records': len(wanted)}

    def collect(self, collection=None, existing=None):
        with mock.patch.object(ci.sys, 'platform', 'linux'), \
                mock.patch.object(ci.platform, 'platform', return_value='Linux-synthetic-x86_64'), \
                mock.patch.object(ci.platform, 'machine', return_value='x86_64'), \
                mock.patch.object(ci.os, 'sched_getaffinity', return_value={0, 1, 2}, create=True), \
                mock.patch.object(ci.shutil, 'which', side_effect=self.tools.get), \
                mock.patch.object(ci.subprocess, 'check_output', return_value='synthetic tool'), \
                mock.patch.object(ci.history, 'cpu_allocation', return_value=[0, 1, 2]), \
                mock.patch.object(ci.history, 'host_context', return_value={'utc': '2026-11-08T12:00:00Z'}), \
                mock.patch.object(ci.history, 'invoke', side_effect=self.invoke), \
                mock.patch.object(ci.storage, 'fixture', side_effect=self.memory), \
                mock.patch.object(ci.process_resources, 'launcher_identity', return_value={
                    'path': str(self.launcher), 'sha256': ci.build.digest(self.launcher),
                    'wall_method': 'synthetic wall', 'memory_method': 'synthetic RSS'}):
            return ci.collect(self.fixture.root, self.root, self.launcher, collection, 'synthetic-only', existing)

    def test_complete_head_and_identical_parent_controls_are_retained_after_final_checks(self):
        value = self.collect()
        self.assertEqual(value['origin'], 'local')
        self.assertEqual(value['comparison'], 'parent')
        self.assertEqual(len(value['rows']), len(self.fixture.cases))
        self.assertEqual(len(value['controls']), len(self.fixture.cases))
        self.assertEqual(self.calls, len(self.fixture.cases) * 30 * 2)
        self.assertEqual(value['controls'][0]['revision'], value['contract']['anchor'])
        self.assertTrue((self.root / 'observation.json').exists())
        self.assertEqual(ci.records.read(self.root / 'measurements/progress.json')['state'], 'complete')

    def test_control_failure_retains_complete_head_and_failed_attempt_diagnostics(self):
        self.fail_at = len(self.fixture.cases) * 30 + 1
        with self.assertRaisesRegex(ValueError, 'head measurements retained'):
            self.collect()
        value = ci.records.read(self.root / 'observation.json')
        ci.records.validate(value)
        self.assertEqual(value['controls'], [])
        self.assertIn('synthetic failed output', value['control_error'])
        self.assertTrue(list((self.root / 'measurements/cells').glob('**/failure.json')))
        self.assertEqual(ci.records.read(self.root / 'measurements/progress.json')['state'], 'head-complete-controls-failed')

    def test_head_failure_never_creates_publishable_observation(self):
        self.fail_at = 1
        with self.assertRaisesRegex(ValueError, 'synthetic failed output'):
            self.collect()
        self.assertFalse((self.root / 'observation.json').exists())
        self.assertTrue((self.root / 'storage-release.json').exists())

    def test_ci_control_failure_retains_head_with_explicit_error_and_valid_workflow_binding(self):
        self.fail_at = len(self.fixture.cases) * 30 + 1
        collection = self.fixture.record(origin='ci', comparison='parent')['collection']
        with self.assertRaisesRegex(ValueError, 'head measurements retained'):
            self.collect(collection)
        value = ci.records.read(self.root / 'observation.json')
        self.assertEqual(value['origin'], 'ci')
        self.assertEqual(value['controls'], [])
        self.assertIn('synthetic failed output', value['control_error'])
        ci.records.validate(value, self.fixture.source())

    def test_changed_reference_invalidates_whole_collection_after_successful_samples(self):
        self.mutate_at = len(self.fixture.cases) * 30 * 2
        with self.assertRaisesRegex(ValueError, 'reference executable changed'):
            self.collect()
        self.assertFalse((self.root / 'observation.json').exists())
        self.assertTrue((self.root / 'measurements/invalid.json').exists())

    def test_ci_cannot_reuse_a_local_fixture_or_measure_a_different_head(self):
        collection = self.fixture.record(origin='ci', comparison='parent')['collection']
        with self.assertRaisesRegex(ValueError, 'own isolated fixture'):
            self.collect(collection, Path('/shared-fixture'))
        with self.assertRaisesRegex(ValueError, 'workflow head/build mismatch'):
            self.collect(dict(collection, head_sha='f' * 40))
        self.assertEqual(self.calls, 0)

    def test_ci_accepts_allowed_logical_cpus_without_requiring_three_distinct_cores(self):
        with mock.patch.object(ci.sys, 'platform', 'linux'), \
                mock.patch.object(ci.os, 'sched_getaffinity', return_value={0, 1, 2, 3}, create=True), \
                mock.patch.object(ci.history, 'cpu_allocation', side_effect=ValueError('not enough distinct cores')) as physical:
            self.assertEqual(ci.cpu_allocation(3, ci=True), [0, 1, 2])
            physical.assert_not_called()
            with self.assertRaisesRegex(ValueError, 'not enough distinct cores'):
                ci.cpu_allocation(3, ci=False)
        with mock.patch.object(ci.sys, 'platform', 'linux'), \
                mock.patch.object(ci.os, 'sched_getaffinity', return_value={0, 1}, create=True), \
                self.assertRaisesRegex(ValueError, 'not enough allowed logical CPUs'):
            ci.cpu_allocation(3, ci=True)

    def test_workflow_keeps_collection_independent_and_measures_downloaded_production_bytes(self):
        workflow = repository_file('.github/workflows/benchmarks.yml').read_text()
        extended = workflow.split('  extended:\n', 1)[1].split('\n  build:', 1)[0]
        self.assertNotIn('needs:', extended)
        self.assertIn('fail-fast: false', extended)
        helper = extended.index('cp "${launcher}" "${RUNNER_TEMP}/extended-resource-launcher"')
        self.assertLess(extended.index('Check extended runner capacity before building'), helper)
        head = extended.index('--role=head')
        parent = extended.index('--role=base')
        upload = extended.index('name: benchmark-extended-build-${{ matrix.platform }}')
        download = extended.index('actions/download-artifact@')
        restore = extended.index('tar -xf "${RUNNER_TEMP}/extended-retention/benchmark-extended-build.tar"')
        measure = extended.index('benchmark_extended_ci.py collect')
        self.assertEqual(sorted((helper, head, parent, upload, download, restore, measure)),
                         [helper, head, parent, upload, download, restore, measure])
        self.assertIn('--build-root="${RUNNER_TEMP}/extended-retained-build"', extended)
        diagnostics = extended.split('name: Retain extended build diagnostics', 1)[1]
        self.assertIn('if: always()', diagnostics)
        self.assertIn('/extended-build/binaries/*/build.log', diagnostics)


if __name__ == '__main__':
    unittest.main()
