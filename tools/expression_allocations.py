#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Convert the ASan expression-allocation test's measured phases to provenance-bearing JSON."""

import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET


PHASES = ('parse', 'bind', 'prepare', 'worker', 'first', 'steady',
          'worker_teardown', 'plan_teardown', 'source_teardown')
COUNTERS = ('allocations', 'requested_bytes', 'frees')


def report_from_xml(source, revision):
    """Reject failed/skipped/incomplete diagnostics instead of treating them as zero allocation."""
    root = ET.fromstring(source)
    rows = []
    names = set()
    for case in root.iter('testcase'):
        name = case.get('name', '')
        if any(case.find(tag) is not None for tag in ('failure', 'error', 'skipped')):
            raise ValueError(f'allocation diagnostic did not pass: {name}')
        properties = {item.attrib['name']: item.attrib['value'] for item in case.findall('properties/property')}
        if properties.get('allocation_diagnostic') != '1':
            continue
        if not name or name in names:
            raise ValueError(f'duplicate or empty allocation case: {name}')
        names.add(name)
        try:
            phases = {phase: {counter: int(properties[f'{phase}_{counter}']) for counter in COUNTERS}
                      for phase in PHASES}
            predicates = int(properties['predicates'])
            entries = int(properties['steady_entries'])
            role = properties['worker_role']
        except (KeyError, ValueError) as error:
            raise ValueError(f'incomplete allocation counters: {name}') from error
        if (predicates < 1 or entries < 1 or role not in ('coordinator', 'concurrent')
                or any(value < 0 for counters in phases.values() for value in counters.values())):
            raise ValueError(f'invalid allocation counters: {name}')
        rows.append(dict(case=name, predicates=predicates, worker_role=role, steady_entries=entries, phases=phases))
    if not rows:
        raise ValueError('no measured allocation cases; use --config=clang --config=asan')
    return dict(schema=1, revision=revision, runtime='ASan', scope='calling thread',
                bytes='requested allocation bytes, excluding sanitizer redzones and allocator bookkeeping',
                timing=False, production_representative=False,
                limitations='ASan replaces the allocator and can change allocation behavior; diagnostic counts only',
                cases=rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xml', type=Path, help='GoogleTest test.xml from expression_allocations_test')
    parser.add_argument('--revision', required=True, help='Exact tested revision')
    parser.add_argument('--output', required=True, type=Path, help='Output JSON path')
    args = parser.parse_args(argv)
    report = report_from_xml(args.xml.read_text(), args.revision)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
