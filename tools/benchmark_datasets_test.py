# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Published recipes retain provenance and drive compatible local collection."""

import argparse
import contextlib
import copy
import gzip
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import urllib.error

import benchmark_datasets as datasets
import benchmark_discovery as discovery


def observations(root):
    tools = {'xff': {'path': 'xff'}, 'find': {}, 'rg': {}, 'fzf': {}}
    tasks = {name: sorted(participants) for name, _, participants in
             discovery.batch.compare.scenarios(Path('.'), [], tools)}
    contract = dict(file_counts=[10, 100], cpu_counts=[1, 3], depth=20, repetitions=9, retained=7,
                    estimator='mean-fastest', fixture_version=2, storage={'memory_required': False},
                    machine='arm64', platform='Darwin', cpu_count=18,
                    affinity_by_cpu_count={'1': None, '3': None},
                    warmup_rounds=1, order='rotate starting participant by task and round')
    report = dict(contract=contract, tools={'rg': {'version': '15', 'sha256': 'r' * 64}},
                  tasks=[dict(name=name, dataset=tree, participants={tool: {} for tool in participants})
                         for name, participants in tasks.items() for tree in ('broad', 'deep')])
    for index, sha in enumerate(('a' * 40, 'b' * 40), 1):
        folder = root / 'local/mac/batch' / sha
        folder.mkdir(parents=True)
        record = dict(kind='backfill', purpose='local-addition', platform='macos', series='mac', head=sha,
                      revision={'date': f'2026-10-0{index}T00:00:00Z'}, completed_at='2026-10-03T00:00:00Z',
                      build={'revision': sha, 'configuration': {'llvm': 'build-hash'}}, tool_comparisons=report)
        (folder / 'report.json').write_text(json.dumps(record))
    manifest = dict(contract=dict(machine_id='machine', allocation={'kind': 'workers'},
                                  driver={'benchmark.py': 'driver-hash'},
                                  environment={'machine': 'arm64', 'cpu_count': 18, 'host_id': 'host'}))
    (root / 'local/mac/batch/batch.json').write_text(json.dumps(manifest))
    return tasks


class BenchmarkDatasetsTest(unittest.TestCase):
    def test_publish_groups_workloads_preserves_methods_and_survives_compaction(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tasks = observations(root)
            (root / 'index.html').write_text('<html><body><h1>Benchmarks</h1></body></html>')
            catalog = datasets.publish(root)
            self.assertEqual(len(catalog['datasets']), 1)
            item = catalog['datasets'][0]
            self.assertEqual(item['recipe']['tasks'], tasks)
            self.assertEqual(item['recipe']['depth'], 20)
            self.assertEqual(len(item['methods']), 2)
            self.assertEqual({observation['commit'] for observation in item['observations']}, {'a' * 40, 'b' * 40})
            self.assertEqual(item['machines'][0]['machine_id'], 'machine')
            page = (root / 'index.html').read_text()
            self.assertIn('Measurement datasets and methods', page)
            self.assertIn('bazel run //tools:benchmark -- list', page)
            for path in root.rglob('report.json'):
                path.with_suffix('.json.gz').write_bytes(gzip.compress(path.read_bytes()))
                path.unlink()
            self.assertEqual(datasets.publish(root), catalog)
            self.assertEqual((root / 'index.html').read_text(), page)

    def test_different_grid_creates_another_dataset_and_previews_stay_out(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            observations(root)
            path = root / ('local/mac/batch/' + 'b' * 40 + '/report.json')
            record = json.loads(path.read_text())
            record['tool_comparisons']['contract']['cpu_counts'] = [1, 3, 10]
            path.write_text(json.dumps(record))
            self.assertEqual(len(datasets.build_catalog(root)['datasets']), 2)
            record.update(kind='pr-preview', source={'event': 'pull_request'})
            path.write_text(json.dumps(record))
            catalog = datasets.build_catalog(root)
            self.assertEqual(len(catalog['datasets']), 1)
            self.assertEqual(len(catalog['datasets'][0]['observations']), 1)

    def test_compatibility_distinguishes_machine_capacity_and_workload(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tasks = observations(root)
            item = datasets.build_catalog(root)['datasets'][0]
            self.assertEqual(datasets.compatibility(item, 'macos', 'arm64', 18, tasks), [])
            self.assertIn('different OS or architecture', datasets.compatibility(item, 'linux', 'x86_64', 18, tasks))
            self.assertIn('requires 3 CPUs; 2 available', datasets.compatibility(item, 'macos', 'arm64', 2, tasks))
            for key, value in (('fixture_version', None), ('custom_fixtures', True), ('tasks', {}), ('warmup_rounds', 2),
                               ('order', 'unknown'), ('keep', 10), ('files', [False]), ('cpus', [1, 1])):
                with self.subTest(key=key):
                    modified = copy.deepcopy(item)
                    modified['recipe'][key] = value
                    self.assertTrue(datasets.compatibility(modified, 'macos', 'arm64', 18, tasks))

    def test_metadata_validation_rejects_changed_id_and_external_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            observations(root)
            catalog = datasets.build_catalog(root)
            for path in ('../../file', '/file', 'https://elsewhere/report.json', 'local/%2e%2e/file', 'local/../file'):
                with self.subTest(path=path), self.assertRaisesRegex(ValueError, 'invalid dataset data path'):
                    datasets.relative_path(path)
            catalog['datasets'][0]['recipe']['depth'] += 1
            with self.assertRaisesRegex(ValueError, 'dataset identity'):
                datasets.validate(catalog)
            with self.assertRaisesRegex(ValueError, 'unsupported'):
                datasets.validate({'schema': 2, 'datasets': []})
            for malformed in ([], {'schema': 1, 'datasets': [{}]}):
                with self.subTest(malformed=malformed), self.assertRaisesRegex(ValueError, 'malformed'):
                    datasets.validate(malformed)
            catalog = datasets.build_catalog(root)
            next(iter(catalog['datasets'][0]['methods'].values()))['driver'] = 'changed'
            with self.assertRaisesRegex(ValueError, 'method identity'):
                datasets.validate(catalog)

    def test_known_machine_defaults_are_derived_and_another_host_gets_no_existing_coverage(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            observations(root)
            catalog = datasets.build_catalog(root)
            with mock.patch.object(discovery.batch, 'host_platform', return_value='macos'), \
                    mock.patch.object(discovery.platform, 'machine', return_value='arm64'), \
                    mock.patch.object(discovery.os, 'cpu_count', return_value=18):
                options = discovery.choices(catalog, 'machine')
                self.assertTrue(options[0]['same_machine'])
                self.assertEqual(options[0]['reasons'], [])
                other = discovery.choices(catalog, 'another')
                self.assertFalse(other[0]['same_machine'])
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    discovery.show_choices(other, [{'sha': 'a' * 40}])
                self.assertIn('this machine: 0 available, 1 missing', output.getvalue())
            args = argparse.Namespace(dataset=None, series=None, command='backfill', yes=False,
                                      files=None, cpus=None, depth=None, repetitions=None, keep=None)
            selected = discovery.choose(options, args)
            with contextlib.redirect_stdout(io.StringIO()):
                discovery.apply_recipe(args, selected, [])
            self.assertEqual((args.series, args.files, args.cpus, args.depth, args.repetitions, args.keep),
                             ('mac', [10, 100], [1, 3], 20, 9, 7))
            args.cpus = [1]
            with contextlib.redirect_stdout(io.StringIO()):
                discovery.apply_recipe(args, selected, [])
            self.assertEqual(args.cpus, [1])

    def test_ambiguous_selection_prompts_but_yes_requires_an_explicit_dataset(self):
        args = argparse.Namespace(dataset=None, series=None, command='backfill', yes=False)
        options = [dict(dataset={'id': key * 64, 'series': 'series'}, same_machine=True, reasons=[])
                   for key in ('a', 'b')]
        with mock.patch('builtins.input', return_value='2'):
            self.assertEqual(discovery.choose(options, args), options[1])
        for answer in ('', 'bad', '3'):
            with mock.patch('builtins.input', return_value=answer), self.assertRaises(ValueError):
                discovery.choose(options, args)
        args.yes = True
        with self.assertRaisesRegex(ValueError, 'select --dataset'):
            discovery.choose(options, args)
        args.dataset = 'aaa'
        self.assertEqual(discovery.choose(options, args), options[0])
        options[0]['reasons'] = ['wrong host']
        with self.assertRaisesRegex(ValueError, 'incompatible'):
            discovery.choose(options, args)

    def test_http_catalog_download_and_legacy_404_fallback(self):
        args = argparse.Namespace(history_root=None, no_fetch=False, catalog_url=discovery.CATALOG_URL)
        with mock.patch.object(discovery, 'read_url', return_value={'schema': 1, 'datasets': []}) as read:
            self.assertEqual(discovery.catalog(args)['datasets'], [])
            read.assert_called_once()
        error = urllib.error.HTTPError(discovery.CATALOG_URL, 404, 'not published', {}, None)
        with mock.patch.object(discovery, 'read_url', side_effect=error):
            self.assertIsNone(discovery.catalog(args))
        error.code = 503
        with mock.patch.object(discovery, 'read_url', side_effect=error), self.assertRaises(urllib.error.HTTPError):
            discovery.catalog(args)

    def test_selected_data_download_checks_identity_and_supports_gzip_fallback(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            observations(root)
            item = datasets.build_catalog(root)['datasets'][0]
            args = argparse.Namespace(catalog_url=discovery.CATALOG_URL,
                                      dataset_selection={'dataset': item, 'same_machine': True})
            calls = []
            def download(url):
                calls.append(url)
                path = root / url.split('/benchmarks/', 1)[1]
                if path.name == 'report.json':
                    raise urllib.error.HTTPError(url, 404, 'packed', {}, None)
                return json.loads((path.with_suffix('') if path.suffix == '.gz' else path).read_text())
            with mock.patch.object(discovery, 'read_url', side_effect=download):
                discovery.download_observations(args, root / 'downloaded')
            self.assertEqual(len(list((root / 'downloaded').rglob('report.json'))), 2)
            self.assertEqual(sum(url.endswith('/batch.json') for url in calls), 1)
            with mock.patch.object(discovery, 'read_url', return_value={}):
                with self.assertRaisesRegex(ValueError, 'does not match'):
                    discovery.download_observations(args, root / 'changed')
            args.dataset_selection['same_machine'] = False
            with mock.patch.object(discovery, 'read_url') as read:
                discovery.download_observations(args, root / 'other-host')
                read.assert_not_called()

    def test_https_and_bounded_gzip_decoding(self):
        value = {'schema': 1, 'datasets': []}
        with mock.patch.object(discovery.urllib.request, 'urlopen') as open_url:
            response = open_url.return_value.__enter__.return_value
            response.read.return_value = gzip.compress(json.dumps(value).encode())
            response.geturl.return_value = 'https://example.invalid/report.json.gz'
            self.assertEqual(discovery.read_url(response.geturl.return_value), value)
            response.geturl.return_value = 'http://example.invalid/report.json.gz'
            with self.assertRaisesRegex(ValueError, 'redirected'):
                discovery.read_url('https://example.invalid/report.json.gz')
            response.geturl.return_value = 'https://example.invalid/report.json.gz'
            response.read.return_value = gzip.compress(b' ' * 1000)
            with self.assertRaisesRegex(ValueError, 'expanded'):
                discovery.read_url(response.geturl.return_value, limit=100)
        with self.assertRaisesRegex(ValueError, 'HTTPS'):
            discovery.read_url('file:///etc/passwd')


if __name__ == '__main__':
    unittest.main()
