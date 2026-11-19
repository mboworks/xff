# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Verify lossless database migration and repeatable retained-history imports."""

import json
from contextlib import closing
import argparse
import sqlite3
import unittest
from unittest import mock

import benchmark_database as database
import benchmark_database_storage as storage
import benchmark_extended_records as records
import benchmark_extended_site as site
import benchmark_extended as suite
import benchmark_extended_campaign as campaign
import benchmark_discovery as discovery
import io
import shutil
from benchmark_extended_test_support import ExtendedFixture


class BenchmarkDatabaseTest(unittest.TestCase):
    def setUp(self):
        self.fixture = ExtendedFixture()
        self.addCleanup(self.fixture.close)
        self.root = self.fixture.root / "site"
        self.root.mkdir()
        (self.root / "index.html").write_text("<h1>Benchmark history</h1>")
        (self.root / "catalog.json").write_text("[]")
        self.original = records.retain(self.root, self.fixture.record())
        site.publish(self.root)
        self.renderer = self.fixture.root / "renderer"
        self.renderer.mkdir()
        for name in (
            "landscape.js",
            "database-app.js",
            "database-worker.js",
            "database.css",
            "sql-wasm.js",
            "sql-wasm.wasm",
            "zstd.wasm",
            "database-NOTICES.txt",
        ):
            (self.renderer / name).write_text("synthetic renderer fixture")

    def test_import_retains_exact_originals_sidecars_and_single_renderer(self):
        (self.root / "assets").mkdir(exist_ok=True)
        (self.root / "assets/plotly-landscape.js").write_text("obsolete benchmark renderer")
        original = self.original.read_bytes()
        sidecar = self.original.with_name("artifact.json")
        proof = b'{ "proof": "original retention bytes", "hash": "abc" }\n'
        sidecar.write_bytes(proof)
        manifest = database.publish(self.root, self.renderer)
        self.assertEqual(len(manifest["series"]), 1)
        self.assertEqual(len(manifest["catalog"]), 2)
        self.assertEqual(list(self.root.rglob("report.json*")), [])
        self.assertEqual(list(self.root.rglob("page.html.gz")), [])
        self.assertEqual(list(self.root.rglob("landscape.json*")), [])
        self.assertEqual(list(self.root.rglob("index.html")), [self.root / "index.html"])
        self.assertIn("database-app.js", (self.root / "index.html").read_text())
        self.assertFalse((self.root / "assets/plotly-landscape.js").exists())
        storage.expand(self.root)
        self.assertEqual(self.original.read_bytes(), original)
        self.assertEqual(sidecar.read_bytes(), proof)

    def test_duplicate_views_share_canonical_document_and_query_rows(self):
        manifest = database.publish(self.root, self.renderer)
        destination = self.fixture.root / "read.sqlite"
        storage.unpack(self.root, manifest["series"][0], destination)
        with closing(sqlite3.connect(destination)) as db:
            documents = [json.loads(row[0]) for row in db.execute("SELECT documents FROM reports")]
            canonical = storage.digest(
                storage.decompress(
                    db.execute("SELECT payload FROM documents WHERE id=?", (documents[0][0],)).fetchone()[0]
                )
            )
            self.assertEqual(documents, [[canonical], [canonical]])
            self.assertEqual(
                db.execute("SELECT count(*) FROM documents WHERE id=?", (canonical,)).fetchone(), (1,)
            )
            models = [json.loads(row[0]) for row in db.execute("SELECT model FROM reports")]
        self.assertEqual({len(model["rows"]) for model in models}, {len(self.fixture.cases)})
        self.assertEqual({row["files"] for model in models for row in model["rows"]}, {3, 6})
        self.assertEqual({row["xff_over_reference"] for model in models for row in model["rows"]}, {0.5, 2})

    def test_repeated_import_is_stable_and_next_generation_keeps_only_one_previous(self):
        first = database.publish(self.root, self.renderer)
        second = database.publish(self.root, self.renderer)
        self.assertEqual(first["series"][0]["sha256"], second["series"][0]["sha256"])
        for number in range(3):
            storage.expand(self.root)
            sidecar = self.original.with_name("artifact.json")
            sidecar.write_text(json.dumps({"generation": number}))
            latest = database.publish(self.root, self.renderer)
        self.assertEqual(len(latest["series"][0]["generations"]), 1)
        self.assertEqual(len(list((self.root / "data").glob("*.sqlite.zst"))), 2)
        storage.expand(self.root)
        self.assertEqual(json.loads(sidecar.read_text()), {"generation": 2})

    def test_failed_integrity_check_preserves_retained_files(self):
        manifest = database.publish(self.root, self.renderer)
        path = self.root / manifest["series"][0]["file"]
        path.write_bytes(storage.compress(b"damaged SQLite bytes"))
        with self.assertRaisesRegex(ValueError, "manifest"):
            storage.expand(self.root)
        self.assertTrue(path.exists())
        self.assertFalse(self.original.exists())

    def test_new_upload_takes_precedence_over_retained_database(self):
        database.publish(self.root, self.renderer)
        self.original.parent.mkdir(parents=True, exist_ok=True)
        self.original.write_text('{"new":"upload"}')
        storage.expand(self.root)
        self.assertEqual(self.original.read_text(), '{"new":"upload"}')

    def test_unsafe_manifest_paths_are_rejected(self):
        manifest = database.publish(self.root, self.renderer)
        manifest["series"][0]["file"] = "../outside.sqlite.zst"
        (self.root / storage.MANIFEST).write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "unsafe"):
            storage.expand(self.root)

    def test_selected_local_dataset_download_restores_and_checks_database_json(self):
        manifest = database.publish(self.root, self.renderer)
        catalog = json.loads((self.root / "datasets.json").read_text())
        args = argparse.Namespace(
            catalog_url="https://example.invalid/benchmarks/datasets.json",
            dataset_catalog=catalog,
            dataset_selection={"same_machine": True, "dataset": catalog["datasets"][0]},
        )

        def download(url, timeout):
            response = mock.MagicMock()
            response.__enter__.return_value = response
            response.geturl.return_value = url
            response.read.side_effect = io.BytesIO(
                (self.root / manifest["series"][0]["file"]).read_bytes()
            ).read
            return response

        with mock.patch.object(discovery, "read_url", return_value=manifest), mock.patch.object(
            discovery.urllib.request, "urlopen", side_effect=download
        ):
            discovery.download_observations(args, self.fixture.root / "download")
        for observation in catalog["datasets"][0]["observations"]:
            restored = json.loads((self.fixture.root / "download" / observation["report"]).read_text())
            self.assertEqual(discovery.datasets.identity(restored), observation["report_identity"])

    def test_complete_ci_checkpoint_survives_database_publication(self):
        self.fixture.contract["recipe"] = suite.DENSE_RECIPE
        record = self.fixture.record(origin="ci", comparison="parent")
        path = records.retain(self.root, record, self.fixture.source())
        site.publish(self.root)
        expected = campaign.checkpoints(self.root, records)
        self.assertEqual(expected, {("linux", self.fixture.revisions[0]["sha"])})
        database.publish(self.root, self.renderer)
        self.assertFalse(path.exists())
        self.assertEqual(campaign.checkpoints(self.root, records), expected)

    def test_deployment_omits_previous_generations_without_losing_retained_rollback(self):
        database.publish(self.root, self.renderer)
        storage.expand(self.root)
        self.original.with_name("artifact.json").write_text('{"new":"generation"}')
        manifest = database.publish(self.root, self.renderer)
        previous = manifest["series"][0]["generations"][0]["file"]
        public = self.fixture.root / "public"
        shutil.copytree(self.root, public)
        storage.stage_deployment(public)
        self.assertTrue((self.root / previous).is_file())
        self.assertFalse((public / previous).exists())
        for series in manifest["series"]:
            self.assertTrue((public / series["file"]).is_file())
        storage.expand(public)
        self.assertEqual(
            json.loads((public / self.original.relative_to(self.root)).read_text())["kind"],
            "extended-production",
        )

    def test_large_database_transport_parts_reassemble_to_one_verified_public_file(self):
        with mock.patch.object(storage, "PART_BYTES", 1024):
            manifest = database.publish(self.root, self.renderer)
        series = manifest["series"][0]
        self.assertGreater(len(series["chunks"]), 1)
        self.assertTrue(all(chunk["bytes"] <= 1024 for chunk in series["chunks"]))
        self.assertFalse((self.root / series["file"]).exists())
        storage.expand(self.root)
        self.assertEqual(json.loads(self.original.read_text())["kind"], "extended-production")
        public = self.fixture.root / "transport-public"
        shutil.copytree(self.root, public)
        storage.stage_deployment(public)
        self.assertEqual(storage.digest((public / series["file"]).read_bytes()), series["compressed_sha256"])
        self.assertFalse(any((public / chunk["file"]).exists() for chunk in series["chunks"]))


if __name__ == "__main__":
    unittest.main()
