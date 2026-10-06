# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Read published dataset metadata and select a local collection recipe."""

import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import subprocess
import urllib.error
import urllib.parse
import urllib.request

import benchmark_backfill as batch
import benchmark_datasets as datasets


CATALOG_URL = 'https://mboworks.github.io/xff/benchmarks/datasets.json'


def read_url(url, limit=64 * 1024 * 1024):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != 'https' or not parsed.netloc:
        raise ValueError('benchmark metadata requires an HTTPS URL')
    with urllib.request.urlopen(url, timeout=30) as response:
        if urllib.parse.urlsplit(response.geturl()).scheme != 'https':
            raise ValueError('benchmark download redirected away from HTTPS')
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError('benchmark download exceeds size limit')
    if url.endswith('.gz'):
        with gzip.GzipFile(fileobj=io.BytesIO(data)) as stream:
            data = stream.read(limit + 1)
        if len(data) > limit:
            raise ValueError('expanded benchmark report exceeds size limit')
    return json.loads(data)


def catalog(args):
    if args.history_root:
        path = args.history_root / 'datasets.json'
        value = json.loads(path.read_text()) if path.exists() else None
    elif args.no_fetch:
        result = subprocess.run(['git', '-C', str(args.repo), 'show', args.history_ref + ':benchmarks/datasets.json'],
                                text=True, capture_output=True, check=False)
        value = json.loads(result.stdout) if result.returncode == 0 else None
    else:
        try:
            value = read_url(args.catalog_url, 16 * 1024 * 1024)
        except urllib.error.HTTPError as error:
            if error.code != 404:
                raise
            print('Dataset metadata is not published yet; using batch discovery.')
            value = None
    if value is not None:
        datasets.validate(value)
    return value


def same_machine(dataset, machine_id):
    if dataset['kind'] != 'local':
        return False
    stable = {machine['machine_id'] for machine in dataset['machines'] if machine.get('machine_id')}
    if stable:
        return stable == {machine_id}
    host = hashlib.sha256(platform.node().encode()).hexdigest()
    return any((machine.get('host_id'), machine.get('architecture'), machine.get('cpu_count'))
               == (host, platform.machine(), os.cpu_count()) for machine in dataset['machines'])


def choices(catalog, machine_id):
    system, architecture = batch.host_platform(), platform.machine().lower()
    capacity = (len(batch.physical_cpus(os.sched_getaffinity(0))) if system == 'linux' else os.cpu_count() or 1)
    tools = {'xff': {'path': 'xff'}, 'find': {}, 'rg': {}, 'fzf': {}}
    tasks = {name: sorted(participants) for name, _, participants in batch.compare.scenarios(Path('.'), [], tools)}
    result = []
    for dataset in catalog['datasets']:
        reasons = datasets.compatibility(dataset, system, architecture, capacity, tasks)
        result.append(dict(dataset=dataset, same_machine=same_machine(dataset, machine_id), reasons=reasons))
    return sorted(result, key=lambda item: (bool(item['reasons']), not item['same_machine'],
                                            item['dataset']['series'], item['dataset']['id']))


def show_choices(options, revisions):
    commits = {revision['sha'] for revision in revisions}
    print('Published measurement datasets:')
    index_width = len(str(len(options)))
    series_width = max((len(option['dataset']['series']) for option in options), default=0)
    for index, option in enumerate(options, 1):
        dataset, recipe = option['dataset'], option['dataset']['recipe']
        observed = {observation['commit'] for observation in dataset['observations']}
        covered = len(observed & commits) if option['same_machine'] else 0
        status = ('incompatible: ' + '; '.join(option['reasons']) if option['reasons'] else
                  'continue this machine' if option['same_machine'] else 'runnable as a new local series')
        print(f"  {index:>{index_width}}. {dataset['id'][:12]}  {dataset['series']:<{series_width}}  "
              f"{dataset['platform']}/{dataset['architecture']}")
        print(f"     CPUs {recipe.get('cpus')}; files {recipe.get('files')}; "
              f"fastest {recipe.get('keep')}/{recipe.get('repetitions')}; {status}")
        layouts = recipe.get('layouts', [])
        labels = ', '.join(f"{layout['name']}/v{layout['revision']}" for layout in layouts)
        print('     Layouts: ' + (labels or 'broad/v1, deep/v1 (legacy)'))
        print(f'     Published: {len(observed)} revisions; this machine: {covered} available, {len(commits) - covered} missing')


def choose(options, args):
    candidates = [item for item in options if not item['reasons']]
    if args.dataset:
        matches = [item for item in options if item['dataset']['id'].startswith(args.dataset)]
        if len(matches) != 1:
            raise ValueError('dataset ID must identify exactly one published dataset')
        if matches[0]['reasons']:
            raise ValueError('dataset is incompatible: ' + '; '.join(matches[0]['reasons']))
        return matches[0]
    if args.series:
        candidates = [item for item in candidates if item['dataset']['series'] == args.series]
    matching = [item for item in candidates if item['same_machine']]
    candidates = matching or candidates
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        return None
    if args.command == 'list':
        return None
    if args.yes:
        raise ValueError('multiple compatible datasets; select --dataset=ID with --yes')
    try:
        answer = input('Select a compatible dataset number (empty cancels): ').strip()
    except EOFError:
        answer = ''
    if not answer:
        raise ValueError('dataset selection cancelled; no builds or measurements started')
    if not answer.isdecimal() or not 1 <= int(answer) <= len(options):
        raise ValueError('invalid dataset number')
    selected = options[int(answer) - 1]
    if selected not in candidates:
        raise ValueError('select one of the compatible datasets for this machine/series')
    return selected


def apply_recipe(args, selection, local_batches):
    selected = selection['dataset'] if selection else None
    if selected and selection['same_machine']:
        if args.series and args.series != selected['series']:
            raise ValueError('selected dataset belongs to a different series')
        args.series = selected['series']
    recipe = selected['recipe'] if selected else {}
    if not selected and args.series:
        matches = [value for _, value in local_batches if value['contract']['series'] == args.series]
        if matches:
            newest = max(matches, key=lambda value: value.get('created_at', ''))['contract']
            recipe = {**newest, 'keep': newest['retained']}
    if getattr(args, 'build_library_path', None) is None and args.series:
        paths = [(value.get('created_at', ''), value['contract'].get('build', {}).get('library_path'))
                 for _, value in local_batches if value['contract']['series'] == args.series]
        inherited = [item for item in paths if item[1]]
        if inherited:
            args.build_library_path = Path(max(inherited)[1])
            print('Reusing historical build runtime libraries: ' + str(args.build_library_path))
    for key in ('files', 'cpus', 'depth', 'repetitions', 'keep'):
        if getattr(args, key) is None and key in recipe:
            setattr(args, key, recipe[key])
    if getattr(args, 'layouts', None) is None and selected:
        args.layouts = ([f"{layout['name']}/v{layout['revision']}" for layout in recipe.get('layouts', [])]
                        if recipe.get('fixture_version') == 3 else [])
    elif getattr(args, 'layouts', None) is None and recipe:
        args.layouts = list(recipe.get('layouts', []))
    if selected:
        print('Selected dataset: ' + selected['id'][:12])
        print('Historical tool/build identities remain recorded. New observations use the current driver and '
              'installed reference tools; each XFF revision uses its own release build configuration.')


def download_observations(args, root):
    """Retrieve only the selected local series; validate canonical JSON identities after decompression."""
    selection = args.dataset_selection
    if selection is None or not selection['same_machine']:
        return
    fetched = set()
    for observation in selection['dataset']['observations']:
        for field in ('batch', 'report'):
            path = observation.get(field)
            if not path or path in fetched:
                continue
            datasets.relative_path(path)
            url = urllib.parse.urljoin(args.catalog_url, path)
            try:
                value = read_url(url)
            except urllib.error.HTTPError as error:
                if error.code != 404:
                    raise
                value = read_url(url + '.gz')
            if datasets.identity(value) != observation[field + '_identity']:
                raise ValueError('published ' + field + ' does not match dataset metadata: ' + path)
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps(value))
            fetched.add(path)
