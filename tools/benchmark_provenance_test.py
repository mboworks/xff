# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Binary states require consistent identities and actual retained executable bytes."""

import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

import benchmark_provenance as provenance


class BenchmarkProvenanceTest(unittest.TestCase):
    def record(self, build=True):
        sha = hashlib.sha256(b'original executable').hexdigest()
        result = {'head': 'a' * 40, 'tool_comparisons': {'tools': {'xff': {'sha256': sha, 'path': '/saved/xff'}},
                  'tasks': [{'participants': {'xff': {'pipeline': [['/saved/xff', '--jobs=1']],
                                                     'samples': [{'elapsed_seconds': 1}]}}}]}}
        if build:
            result['build'] = {'revision': result['head'], 'sha256': sha}
        return result

    def proof(self, record):
        return provenance.verify_retention(record, io.BytesIO(b'original executable'), 'original retained artifact')

    def test_four_states(self):
        self.assertEqual(provenance.describe({})['state'], 'Not recorded')
        self.assertEqual(provenance.describe(self.record(False))['state'], 'Not verified')
        record = self.record()
        self.assertEqual(provenance.describe(record)['state'], 'Consistent')
        record['binary_verification'] = self.proof(record)
        checked = provenance.describe(record)
        self.assertEqual(checked['state'], 'Verified against Retention')
        self.assertEqual(checked['sha256'], record['build']['sha256'])
        self.assertIn('original retained artifact', checked['reason'])

    def test_one_recorded_hash_can_be_verified_against_bytes(self):
        record = self.record(False)
        self.assertEqual(provenance.describe(record, self.proof(record))['state'], 'Verified against Retention')

    def test_mismatches_never_count_as_consistent_or_verified(self):
        mutations = [
            lambda r: r['build'].update(sha256='b' * 64),
            lambda r: r['build'].update(sha256='invalid'),
            lambda r: r['build'].update(revision='b' * 40),
            lambda r: r.update(revision={'sha': 'b' * 40}),
            lambda r: r.update(source={'head_sha': 'b' * 40}),
            lambda r: r['build'].update(source={'revision': r['head'], 'clean': False}),
            lambda r: r['tool_comparisons']['tasks'][0]['participants']['xff'].update(pipeline=[['/other/xff']]),
        ]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                record = self.record()
                mutate(record)
                self.assertEqual(provenance.describe(record)['state'], 'Not verified')
                with self.assertRaises(ValueError):
                    self.proof(record)

    def test_unchanged_head_samples_corroborate_but_base_is_a_different_executable(self):
        record = self.record(False)
        sha = record['tool_comparisons']['tools']['xff']['sha256']
        record['samples'] = {'head': [{'binary_sha256': sha}], 'base': [{'binary_sha256': 'b' * 64}]}
        self.assertEqual(provenance.describe(record)['state'], 'Consistent')
        record['samples']['head'].append({'binary_sha256': 'b' * 64})
        self.assertEqual(provenance.describe(record)['state'], 'Not verified')

    def test_no_measured_hash_cannot_be_retrofitted_from_build_or_rebuild(self):
        record = self.record()
        del record['tool_comparisons']['tools']['xff']['sha256']
        self.assertEqual(provenance.describe(record)['state'], 'Not recorded')
        with self.assertRaisesRegex(ValueError, 'recorded measured binary hash'):
            self.proof(record)

    def test_retained_bytes_must_match(self):
        with self.assertRaisesRegex(ValueError, 'does not match'):
            provenance.verify_retention(self.record(), io.BytesIO(b'wrong executable'), 'artifact')

    def test_report_or_proof_changes_invalidate_verification(self):
        record = self.record()
        proof = self.proof(record)
        changed = copy.deepcopy(record)
        changed['tool_comparisons']['tasks'][0]['participants']['xff']['samples'][0]['elapsed_seconds'] = 2
        self.assertEqual(provenance.describe(changed, proof)['state'], 'Not verified')
        for field, value in [('sha256', 'b' * 64), ('measurement_identity', 'b' * 64),
                             ('method', 'hash-only'), ('verified_at', '2026-10-09'), ('retention_reference', '')]:
            with self.subTest(field=field):
                self.assertEqual(provenance.describe(record, dict(proof, **{field: value}))['state'], 'Not verified')
        self.assertEqual(provenance.describe(record, [])['state'], 'Not verified')

    def test_publication_metadata_does_not_invalidate_original_evidence(self):
        record = self.record()
        proof = self.proof(record)
        record.update(source={'head_sha': record['head'], 'id': 42}, summary={'derived': True})
        self.assertEqual(provenance.describe(record, proof)['state'], 'Verified against Retention')

    def test_preview_workflow_head_is_branch_not_tested_merge_commit(self):
        record = self.record()
        record.update(kind='pr-preview', branch_head='b' * 40, source={'head_sha': 'b' * 40})
        self.assertEqual(provenance.describe(record)['state'], 'Consistent')
        record['source']['head_sha'] = 'c' * 40
        self.assertEqual(provenance.describe(record)['state'], 'Not verified')

    def test_cli_verifies_tar_without_extracting_and_preserves_raw_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'report.json'
            path.write_text(json.dumps(self.record()))
            original = path.read_bytes()
            archive = root / 'build.tar'
            with tarfile.open(archive, 'w') as output:
                for name in ('benchmark-head', '../unwanted'):
                    member = tarfile.TarInfo(name)
                    member.size = len(b'original executable')
                    output.addfile(member, io.BytesIO(b'original executable'))
            with mock.patch.object(sys, 'argv', ['provenance', '--report', str(path), '--archive', str(archive),
                                                 '--retention-reference', 'CI run 42 build artifact']):
                provenance.main()
            proof = json.loads((root / 'binary-verification.json').read_text())
            self.assertEqual(provenance.describe(self.record(), proof)['state'], 'Verified against Retention')
            self.assertEqual(path.read_bytes(), original)
            self.assertFalse((root / 'benchmark-head').exists())
            self.assertFalse((root.parent / 'unwanted').exists())
