# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Guard interleaving, frozen contracts, resume, and failed-output retention."""

import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import benchmark_extended as suite
import benchmark_extended_history as history


class BenchmarkExtendedHistoryTest(unittest.TestCase):
    def test_each_participant_gets_each_order_position(self):
        labels = ['xff', 'find', 'anchor']
        for case in range(3):
            orders = [history.round_order(labels, case, repetition) for repetition in range(1, 10)]
            for position in range(3):
                self.assertEqual([order[position] for order in orders].count('xff'), 3)
                self.assertEqual([order[position] for order in orders].count('find'), 3)
                self.assertEqual([order[position] for order in orders].count('anchor'), 3)

    def test_resume_rejects_changed_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'contract.json'
            history.freeze(path, {'sha': 'old', 'keep': 7})
            history.freeze(path, {'sha': 'old', 'keep': 7})
            with self.assertRaisesRegex(ValueError, 'changed contract'):
                history.freeze(path, {'sha': 'new', 'keep': 7})
            self.assertEqual(json.loads(path.read_text()), {'sha': 'old', 'keep': 7})

    def test_warmups_and_all_samples_are_retained_and_completed_cells_resume(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = suite.Case('files-exact', 'broad', 3, 1)
            revision = {'sha': 'old', 'binary': '/old/xff'}
            anchor = {'sha': 'new', 'binary': '/new/xff'}
            sample = {'elapsed_seconds': .1, 'stdout_sha256': 'hash', 'validated_records': 3}
            with mock.patch.object(history, 'invoke', return_value=sample) as invoke, \
                    mock.patch.object(history, 'host_context', return_value={'utc': 'fixed'}):
                args = (case, revision, anchor, root, root / 'fixture', {'find': '/bin/find'}, '/launcher', [0, 1, 2], 0, 3)
                row = history.collect_cell(*args)
                self.assertEqual(invoke.call_count, 12)
                self.assertEqual(sum(s['phase'] == 'warmup' for s in row['samples']), 3)
                self.assertEqual(sum(s['phase'] == 'measured' for s in row['samples']), 9)
                self.assertEqual(row['commands']['xff'][0], '/old/xff')
                self.assertEqual(row['commands']['anchor'][0], '/new/xff')
                self.assertEqual(history.collect_cell(*args), row)
                self.assertEqual(invoke.call_count, 12)

    def test_failed_attempt_never_becomes_a_completed_cell(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = suite.Case('files-exact', 'broad', 3, 1)
            args = (case, {'sha': 'old', 'binary': '/old'}, {'sha': 'new', 'binary': '/new'},
                    root, root / 'fixture', {'find': '/bin/find'}, '/launcher', [0], 0, 1)
            with mock.patch.object(history, 'invoke', side_effect=ValueError('incorrect output')), \
                    mock.patch.object(history, 'host_context', return_value={}):
                for _ in range(2):
                    with self.assertRaisesRegex(ValueError, 'incorrect output'):
                        history.collect_cell(*args)
            directory = root / 'cells/old' / case.key
            self.assertFalse((directory / 'complete.json').exists())
            self.assertTrue((directory / 'attempts/1/failure.json').is_file())
            self.assertTrue((directory / 'attempts/2/failure.json').is_file())


if __name__ == '__main__':
    unittest.main()
