#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Measure frozen historical executables against a current anchor and reference tools.

Linux qualification only. This separate versioned campaign never appends incompatible
workloads to the published core dataset. Each command uses the existing native resource
launcher; fresh fork workers provide timeout and affinity isolation. Validation and JSON
serialization occur after the measured interval. All raw timing samples are retained.
"""

import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import fcntl
import gzip
import hashlib
import json
import multiprocessing
import os
from pathlib import Path
import platform
import random
import shutil
import signal
import subprocess
import sys
import time

import benchmark_extended as suite
import process_resources


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def freeze(path, value):
    if path.exists() and json.loads(path.read_text()) != value:
        raise ValueError('cannot resume changed contract: ' + str(path))
    write(path, value)


def cpu_allocation(count):
    """Prefer separate physical cores over sibling hardware threads."""
    if sys.platform == 'darwin':
        if count > (os.cpu_count() or 0):
            raise ValueError('worker count exceeds available logical CPUs')
        return []
    cores = {}
    for cpu in sorted(os.sched_getaffinity(0)):
        topology = Path(f'/sys/devices/system/cpu/cpu{cpu}/topology')
        key = ((topology / 'physical_package_id').read_text(), (topology / 'core_id').read_text())
        cores.setdefault(key, cpu)
    result = list(cores.values())[:count]
    if len(result) != count:
        raise ValueError('not enough allowed physical cores')
    return result


def worker(connection, spec, wanted, separator, failure_path):
    """Fresh worker; no Python process is included in command resource accounting."""
    sample = None
    try:
        os.setsid()
        if spec['affinity']:
            os.sched_setaffinity(0, spec['affinity'])
        with open(spec['stdin'], 'rb') as source:
            sample = process_resources.measure_pipeline(
                [spec['command']], source=source, cwd=spec['cwd'], environment=spec['environment'],
                launcher=spec['launcher'], stderr_limit=8192)
        output_hash = suite.validate(sample, wanted, separator)
        sample.pop('output')
        sample.update(stdout_sha256=output_hash, validated_records=len(wanted))
        connection.send({'sample': sample})
    except BaseException as error:
        if sample is not None and 'output' in sample:
            with gzip.open(failure_path, 'wb') as output:
                output.write(sample.pop('output'))
        connection.send({'error': repr(error), 'sample': sample})
    finally:
        connection.close()


def invoke(spec, wanted, separator, failure_path, timeout=120):
    context = multiprocessing.get_context('fork')
    receiver, sender = context.Pipe(duplex=False)
    child = context.Process(target=worker, args=(sender, spec, wanted, separator, failure_path))
    child.start()
    sender.close()
    try:
        if not receiver.poll(timeout):
            # setsid precedes launch; kill only our worker's own process group.
            if child.is_alive() and os.getpgid(child.pid) == child.pid:
                os.killpg(child.pid, signal.SIGKILL)
            else:
                child.kill()
            raise ValueError(f'measurement exceeded {timeout} seconds')
        response = receiver.recv()
        if 'error' in response:
            raise ValueError(str(response))
        return response['sample']
    finally:
        receiver.close()
        child.join(timeout=5)
        if child.is_alive():
            child.kill()
            child.join()


def host_context():
    if sys.platform == 'darwin':
        return {'utc': datetime.now(timezone.utc).isoformat(), 'loadavg': os.getloadavg(),
                'vm_stat': subprocess.check_output(['vm_stat'], text=True),
                'memory_pressure': subprocess.check_output(['sysctl', 'vm.swapusage'], text=True)}
    return {'utc': datetime.now(timezone.utc).isoformat(), 'loadavg': os.getloadavg(),
            'meminfo': Path('/proc/meminfo').read_text(),
            'cpu_stat': Path('/proc/stat').read_text(),
            'pressure': {name: (Path('/proc/pressure') / name).read_text()
                         for name in ('cpu', 'memory', 'io')}}


def round_order(labels, case_index, repetition):
    offset = (case_index + repetition) % len(labels)
    return labels[offset:] + labels[:offset]


def collect_cell(case, revision, anchor, root, fixture, tools, launcher, affinity, index, repetitions):
    directory = root / 'cells' / revision['sha'] / case.key
    directory.mkdir(parents=True, exist_ok=True)
    completed = directory / 'complete.json'
    if completed.exists():
        return json.loads(completed.read_text())
    attempts = directory / 'attempts'
    attempts.mkdir(exist_ok=True)
    attempt = attempts / str(len(list(attempts.iterdir())) + 1)
    attempt.mkdir()
    wanted = suite.expected(case, fixture)
    commands, cwd, separator = suite.commands(case, fixture, revision['binary'], tools)
    anchor_commands, _, _ = suite.commands(case, fixture, anchor['binary'], tools)
    commands['anchor'] = anchor_commands['xff']
    labels = list(commands)
    environment = {'PATH': '/usr/bin:/bin', 'HOME': str(fixture / 'home'), 'LC_ALL': 'C', 'TZ': 'UTC'}
    row = {'case': asdict(case), 'revision': revision, 'anchor': anchor,
           'expected_records': len(wanted), 'expected_sha256': hashlib.sha256(b'\0'.join(sorted(wanted))).hexdigest(),
           'commands': commands, 'cwd': str(cwd), 'affinity': affinity[:case.cpus], 'environment': environment,
           'before': host_context(), 'samples': [], 'attempt': str(attempt)}
    write(attempt / 'start.json', row)
    try:
        with (attempt / 'samples.jsonl').open('x') as raw:
            for repetition in range(repetitions + 1):
                for label in round_order(labels, index, repetition):
                    spec = {'command': commands[label], 'cwd': str(cwd), 'environment': environment,
                            'stdin': str(fixture / 'empty'), 'launcher': launcher,
                            'affinity': affinity[:case.cpus]}
                    record = {'label': label, 'round': repetition,
                              'phase': 'warmup' if repetition == 0 else 'measured',
                              'utc': datetime.now(timezone.utc).isoformat()}
                    record.update(invoke(spec, wanted, separator, attempt / 'failed-output.gz'))
                    row['samples'].append(record)
                    raw.write(json.dumps(record) + '\n')
                    raw.flush()
        row['after'] = host_context()
        write(completed, row)
    except BaseException as error:
        write(attempt / 'failure.json', {'error': repr(error), 'context': host_context()})
        raise
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--revisions', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--resource-launcher', type=Path, required=True)
    parser.add_argument('--cases', type=Path, help='Optional frozen JSON list of Case records for qualification')
    parser.add_argument('--repetitions', type=int, default=9)
    parser.add_argument('--keep', type=int, default=7)
    parser.add_argument('--limit', type=int, help='Stop cleanly after this many new cells; contract is unchanged')
    args = parser.parse_args()
    if sys.platform != 'linux' or not 1 <= args.keep <= args.repetitions:
        parser.error('requires Linux and 1 <= keep <= repetitions')
    root, fixture, builds = args.root.resolve(), args.fixture.resolve(), args.build_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    collection_lock = (root / 'collection.lock').open('a')
    fcntl.flock(collection_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    if (root / 'invalid.json').exists():
        raise ValueError('campaign identity changed previously; inspect invalid.json before starting a new campaign')
    revisions = json.loads(args.revisions.read_text())
    cases = [suite.Case(**case) for case in json.loads(args.cases.read_text())] if args.cases else suite.cases()
    if not revisions or not cases or len({r['sha'] for r in revisions}) != len(revisions) or len({c.key for c in cases}) != len(cases):
        raise ValueError('empty or duplicate revisions/cases')
    affinity = cpu_allocation(max(case.cpus for case in cases))
    # Collection requires already-frozen binaries. Build completion is checked by the operator;
    # no compiler is invoked anywhere in this measurement driver.
    for revision in revisions:
        directory = builds / 'binaries' / revision['sha']
        identity = json.loads((directory / 'build.json').read_text())
        binary = directory / 'xff_full'
        if identity['source']['revision'] != revision['sha'] or digest(binary) != identity['sha256']:
            raise ValueError('binary/source identity mismatch')
        actions = json.loads((directory / 'link-actions.json').read_text())
        links = [action['arguments'] for action in actions['actions']
                 if identity['artifact'] in action.get('arguments', [])]
        if len(links) != 1 or '-flto=thin' not in links[0]:
            raise ValueError('production ThinLTO link evidence is missing: ' + revision['sha'])
        revision.update(binary=str(binary), binary_sha256=identity['sha256'], build_sha256=digest(directory / 'build.json'),
                        link_actions_sha256=digest(directory / 'link-actions.json'))
    anchor = revisions[-1]
    tools = {}
    for name in ('find', 'rg', 'tar'):
        path = str(Path(shutil.which(name)).resolve(strict=True))
        tools[name] = {'path': path, 'sha256': digest(path),
                       'version': subprocess.check_output([path, '--version'], text=True).strip()}
    launcher = process_resources.launcher_identity(args.resource_launcher)
    filesystem = subprocess.check_output(['findmnt', '-n', '-o', 'FSTYPE', '-T', str(fixture.parent)], text=True).strip()
    if filesystem != 'tmpfs':
        raise ValueError('fixtures must reside on tmpfs, found: ' + filesystem)
    contract = {'recipe': suite.RECIPE, 'cases': [asdict(case) for case in cases], 'revisions': revisions,
                'repetitions': args.repetitions, 'keep': args.keep, 'estimator': 'mean-fastest',
                'warmup_rounds': 1, 'anchor': anchor, 'fixture': str(fixture), 'filesystem': filesystem,
                'tools': tools, 'launcher': launcher, 'affinity': affinity, 'platform': platform.platform(),
                'cpuinfo': Path('/proc/cpuinfo').read_text().split('\n\n')[0],
                'python': sys.version, 'drivers': {Path(module).name: digest(module) for module in
                                                 (__file__, suite.__file__, process_resources.__file__)},
                'build_contract_sha256': digest(builds / 'build-contract.json'),
                'order': 'seed 20261107 shuffles revisions and cells; participant start rotates each round',
                'output_retention': 'all outputs checked; successful output hash/count retained; failing output gzip retained'}
    # cpu MHz fluctuates: record it as context, never as a resume identity field.
    contract['cpuinfo'] = '\n'.join(line for line in contract['cpuinfo'].splitlines() if not line.startswith('cpu MHz'))
    freeze(root / 'contract.json', contract)
    if not fixture.exists():
        suite.prepare(fixture, cases)
    manifest = suite.verify(fixture, cases)
    freeze(root / 'fixtures.json', manifest)
    order = list(revisions)
    random.Random(20261107).shuffle(order)
    ordered_cases = list(cases)
    random.Random(20261108).shuffle(ordered_cases)
    progress = {'total': len(order) * len(cases), 'completed': 0, 'new_cells': 0, 'state': 'running'}
    started = time.monotonic()
    for revision in order:
        for case in ordered_cases:
            completed = root / 'cells' / revision['sha'] / case.key / 'complete.json'
            existing = completed.exists()
            try:
                collect_cell(case, revision, anchor, root, fixture, {key: value['path'] for key, value in tools.items()},
                             launcher['path'], affinity, progress['completed'], args.repetitions)
            except BaseException as error:
                progress.update(state='failed', error=repr(error), pr=revision['pr'], case=case.key)
                write(root / 'progress.json', progress)
                raise
            progress['completed'] += 1
            if not existing:
                progress['new_cells'] += 1
            progress.update(utc=datetime.now(timezone.utc).isoformat(), pr=revision['pr'], case=case.key,
                            session_seconds=time.monotonic() - started)
            write(root / 'progress.json', progress)
            print(f'CELL {progress["completed"]}/{progress["total"]} PR #{revision["pr"]} {case.key} '
                  f'{progress["session_seconds"]:.1f}s session', flush=True)
            if args.limit and progress['new_cells'] >= args.limit:
                print('Requested cell limit reached; resume with the same contract.', flush=True)
                return
        for record in (revision, anchor):
            if digest(record['binary']) != record['binary_sha256']:
                write(root / 'invalid.json', {'reason': 'binary changed while collecting', 'revision': record['sha']})
                raise ValueError('binary changed while collecting')
    try:
        suite.verify(fixture, cases)
        for tool in tools.values():
            if digest(tool['path']) != tool['sha256']:
                raise ValueError('reference executable changed while collecting')
        if process_resources.launcher_identity(launcher['path']) != launcher:
            raise ValueError('resource launcher changed while collecting')
    except BaseException as error:
        write(root / 'invalid.json', {'reason': repr(error)})
        raise
    progress.update(state='complete', utc=datetime.now(timezone.utc).isoformat())
    write(root / 'progress.json', progress)
    print('MEASUREMENT PHASE COMPLETE', flush=True)


if __name__ == '__main__':
    main()
