#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Describe binary evidence and verify an original retained benchmark executable."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import tarfile

import benchmark_records


def valid_hash(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def measurement_identity(record):
    """Bind evidence to measured data; publication may add workflow metadata."""
    value = {key: record[key] for key in ('head', 'base', 'samples', 'tool_comparisons', 'build') if key in record}
    value['head'] = record.get('head') or record.get('source', {}).get('head_sha')
    return hashlib.sha256(json.dumps(value, sort_keys=True, allow_nan=False).encode()).hexdigest()


def describe(record, retention=None):
    """Check recorded XFF identities without claiming that stored hashes are bytes."""
    report = record.get('tool_comparisons', {})
    tool = report.get('tools', {}).get('xff', {})
    hashes = []
    if tool.get('sha256') is not None:
        hashes.append(('measured executable', tool['sha256']))
    for index, sample in enumerate(record.get('samples', {}).get('head', []), 1):
        if sample.get('binary_sha256') is not None:
            hashes.append((f'head sample {index}', sample['binary_sha256']))
    measured = list(hashes)
    build = record.get('build', {})
    if build.get('sha256') is not None:
        hashes.append(('build manifest', build['sha256']))
    result = {'state': 'Not recorded', 'sha256': None,
              'reason': 'No measured executable SHA-256 was recorded.'}
    if not hashes:
        return result
    result.update(state='Not verified', sha256=next((value for _, value in hashes if valid_hash(value)), None))
    for label, value in hashes:
        if not valid_hash(value):
            result['reason'] = f'Invalid SHA-256 in {label}.'
            return result
    if len({value for _, value in hashes}) != 1:
        result['reason'] = 'Recorded executable SHA-256 values disagree.'
        return result
    if not measured:
        result.update(state='Not recorded', reason='A build hash exists, but the measured executable hash is missing.')
        return result
    head = record.get('head') or record.get('source', {}).get('head_sha')
    for label, revision in (('build manifest', build.get('revision')),
                            ('source tree', build.get('source', {}).get('revision')),
                            ('revision metadata', record.get('revision', {}).get('sha'))):
        if revision is not None and revision != head:
            result['reason'] = f'The {label} revision does not match the measured commit.'
            return result
    source = record.get('source', {})
    expected = record.get('branch_head') if benchmark_records.is_preview(record) else head
    if expected and source.get('head_sha') and source['head_sha'] != expected:
        result['reason'] = 'Workflow provenance does not match the recorded revision.'
        return result
    if build.get('source', {}).get('clean') is False:
        result['reason'] = 'The build recorded changed source files.'
        return result
    for task in report.get('tasks', []):
        pipeline = task.get('participants', {}).get('xff', {}).get('pipeline', [])
        if pipeline and tool.get('path') and pipeline[0][0] != tool['path']:
            result['reason'] = 'An XFF measurement command names a different executable.'
            return result
    proof = retention if retention is not None else record.get('binary_verification')
    if proof is not None:
        if (not isinstance(proof, dict) or proof.get('schema') != 1 or proof.get('method') != 'sha256-retained-executable'
                or proof.get('sha256') != result['sha256']
                or proof.get('measurement_identity') != measurement_identity(record)
                or not isinstance(proof.get('retention_reference'), str)
                or not proof['retention_reference'].strip()):
            result['reason'] = 'Retention verification does not match this measurement.'
            return result
        try:
            verified = datetime.fromisoformat(proof['verified_at'])
            if verified.tzinfo is None:
                raise ValueError('verification time has no timezone')
        except (KeyError, TypeError, ValueError):
            result['reason'] = 'Retention verification has an invalid timestamp.'
            return result
        result.update(state='Verified against Retention',
                      reason=f'SHA-256 checked against {proof["retention_reference"]} at {proof["verified_at"]}.',
                      retention_reference=proof['retention_reference'], verified_at=proof['verified_at'])
    elif len(hashes) > 1:
        result.update(state='Consistent', reason='All available executable hashes, revision identities and commands agree. '
                      'Original retained executable bytes have not been independently checked.')
    else:
        result['reason'] = 'The measured SHA-256 has no corroborating executable identity or retention verification.'
    return result


def verify_retention(record, executable, reference):
    """Read the retained executable, then bind that check to this measurement."""
    checked = describe(record)
    if not checked['sha256'] or checked['state'] == 'Not recorded':
        raise ValueError('cannot verify retention without a recorded measured binary hash')
    actual = hashlib.file_digest(executable, 'sha256').hexdigest()
    if actual != checked['sha256']:
        raise ValueError('retained executable does not match measured SHA-256')
    if not reference or not reference.strip():
        raise ValueError('retention reference required')
    proof = {'schema': 1, 'method': 'sha256-retained-executable', 'sha256': actual,
            'measurement_identity': measurement_identity(record), 'retention_reference': reference,
            'verified_at': datetime.now(timezone.utc).isoformat()}
    result = describe(record, proof)
    if result['state'] != 'Verified against Retention':
        raise ValueError(result['reason'])
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--binary', type=Path)
    source.add_argument('--archive', type=Path, help='Original CI executable archive; never extracted')
    parser.add_argument('--member', default='benchmark-head', help='Exact executable member in the CI archive')
    parser.add_argument('--retention-reference', required=True, help='Original retained artifact/run identity')
    parser.add_argument('--embed', action='store_true', help='Attach evidence to a new, unpublished report')
    args = parser.parse_args()
    try:
        record = benchmark_records.read(args.report)
        if args.archive:
            with tarfile.open(args.archive) as archive:
                matches = [member for member in archive.getmembers() if member.name == args.member]
                if len(matches) != 1 or not matches[0].isfile():
                    raise ValueError('expected one regular retained executable archive member')
                with archive.extractfile(matches[0]) as executable:
                    proof = verify_retention(record, executable, args.retention_reference)
        else:
            with args.binary.open('rb') as executable:
                proof = verify_retention(record, executable, args.retention_reference)
        if args.embed:
            record['binary_verification'] = proof
            destination, value = args.report, record
        else:
            destination, value = args.report.with_name('binary-verification.json'), proof
        temporary = destination.with_suffix(destination.suffix + '.tmp')
        temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')
        temporary.replace(destination)
        print(describe(record, proof)['state'] + ': ' + proof['sha256'])
    except (OSError, ValueError, tarfile.TarError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
