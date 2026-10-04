# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Allocation reports must distinguish measured zeros from failed or unavailable diagnostics."""

import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

import expression_allocations as allocations


def fixture():
    root = ET.Element('testsuites')
    suite = ET.SubElement(root, 'testsuite')
    case = ET.SubElement(suite, 'testcase', name='TypeProductionCoordinator16')
    properties = ET.SubElement(case, 'properties')
    values = dict(allocation_diagnostic='1', predicates='16', steady_entries='128', worker_role='coordinator')
    values.update({f'{phase}_{counter}': '0' for phase in allocations.PHASES for counter in allocations.COUNTERS})
    values.update(prepare_allocations='5', prepare_requested_bytes='2112', plan_teardown_frees='5')
    for name, value in values.items():
        ET.SubElement(properties, 'property', name=name, value=value)
    return root, case, properties


class ExpressionAllocationsTest(unittest.TestCase):
    def test_records_exact_counts_metadata_and_measured_zero(self):
        root, _, _ = fixture()
        report = allocations.report_from_xml(ET.tostring(root), 'revision')
        self.assertEqual(report['revision'], 'revision')
        self.assertEqual(report['runtime'], 'ASan')
        self.assertFalse(report['timing'])
        self.assertEqual(report['cases'][0]['phases']['prepare'],
                         dict(allocations=5, requested_bytes=2112, frees=0))
        self.assertEqual(report['cases'][0]['phases']['steady']['allocations'], 0)
        self.assertEqual(report['cases'][0]['steady_entries'], 128)

    def test_failed_and_skipped_cases_are_not_zero_measurements(self):
        for tag in ('failure', 'error', 'skipped'):
            with self.subTest(tag=tag):
                root, case, _ = fixture()
                ET.SubElement(case, tag)
                with self.assertRaisesRegex(ValueError, 'did not pass'):
                    allocations.report_from_xml(ET.tostring(root), 'revision')

    def test_empty_and_unmarked_reports_are_rejected(self):
        for xml in ('<testsuites/>', '<testsuites><testcase name="calibration"/></testsuites>'):
            with self.subTest(xml=xml), self.assertRaisesRegex(ValueError, 'no measured allocation'):
                allocations.report_from_xml(xml, 'revision')

    def test_incomplete_or_invalid_counters_are_rejected(self):
        for key, value in (('parse_allocations', None), ('first_allocations', 'invalid'),
                           ('worker_frees', '-1'), ('predicates', '0'), ('steady_entries', '0'),
                           ('worker_role', 'unknown')):
            with self.subTest(key=key):
                root, _, properties = fixture()
                prop = next(item for item in properties if item.attrib['name'] == key)
                if value is None:
                    properties.remove(prop)
                else:
                    prop.attrib['value'] = value
                with self.assertRaisesRegex(ValueError, '(incomplete|invalid) allocation counters'):
                    allocations.report_from_xml(ET.tostring(root), 'revision')

    def test_duplicate_and_empty_names_are_rejected(self):
        root, case, _ = fixture()
        root.append(ET.fromstring(ET.tostring(case)))
        with self.assertRaisesRegex(ValueError, 'duplicate or empty'):
            allocations.report_from_xml(ET.tostring(root), 'revision')
        case.set('name', '')
        with self.assertRaisesRegex(ValueError, 'duplicate or empty'):
            allocations.report_from_xml(ET.tostring(root), 'revision')

    def test_command_writes_machine_readable_report(self):
        root, _, _ = fixture()
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / 'test.xml', Path(directory) / 'allocations.json'
            source.write_bytes(ET.tostring(root))
            self.assertEqual(allocations.main([str(source), '--revision', 'tested-head', '--output', str(output)]), 0)
            self.assertEqual(json.loads(output.read_text())['revision'], 'tested-head')


if __name__ == '__main__':
    unittest.main()
