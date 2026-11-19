# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Publish shared benchmark reports and immutable SQLite aggregates per dataseries."""

import argparse
from collections import defaultdict
from contextlib import closing
import gzip
import hashlib
import json
import re
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(REPO / "tools"))
import benchmark_history_views as history
import benchmark_landscape as landscape
import benchmark_matrix as matrix
import benchmark_overview as overview
import benchmark_records as records
import site_storage
import benchmark_database_storage as storage


def encoded(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)


def digest(value):
    return hashlib.sha256(value).hexdigest()


def original_bytes(path):
    return path.read_bytes() if path.exists() else gzip.decompress(path.with_suffix(".json.gz").read_bytes())


def identity(wrapper, entry=None, path=None):
    if wrapper.get("kind") == "extended-production":
        if wrapper["origin"] == "ci":
            parts = ["CI", wrapper["platform"], wrapper["machine"]]
        else:
            parts = [
                "Local",
                wrapper["series"],
                wrapper["platform"],
                wrapper["machine"],
                wrapper["contract"]["recipe"],
            ]
    elif entry and entry.get("extended"):
        parts = [
            "Local",
            wrapper["series"],
            wrapper["platform"],
            wrapper["tool_comparisons"]["contract"].get("machine", ""),
            wrapper["tool_comparisons"]["contract"].get("suite", ""),
        ]
    elif (entry and entry.get("local")) or records.is_local(wrapper):
        contract = wrapper.get("tool_comparisons", {}).get("contract", {})
        parts = [
            "Local",
            wrapper.get("series", (entry or {}).get("platform_name", "local")),
            contract.get("machine", ""),
            landscape.layout_details(contract),
        ]
    else:
        contract = wrapper.get("tool_comparisons", {}).get("contract", {})
        platform = (entry or {}).get("platform", "").split(" / ")[0] or records.platform_key(wrapper)
        if not platform and path:
            platform = next((part for part in path.parts if part in ("linux", "macos")), "legacy")
        machine = (
            wrapper.get("machine")
            or contract.get("machine")
            or ("arm64" if platform == "macos" else "x86_64")
        )
        parts = ["CI", platform, machine]
    return digest(encoded(parts).encode())[:20], " / ".join(parts)


class Builder:
    def __init__(self, output, state):
        self.output, self.state = output, state
        output.mkdir(parents=True, exist_ok=True)
        state.mkdir(parents=True, exist_ok=True)
        self.previous = storage.read_manifest(output) or {}
        self.connections, self.labels, self.changed = {}, {}, set()
        self.navigation = {
            (r["platform"], r.get("source", "merged"), r["commit"]): r
            for r in self.previous.get("catalog", [])
        }
        self.source_revisions = []
        self.doc_count = 0
        self.raw_index = defaultdict(list)
        self.owners = {}
        self.legacy = dict(self.previous.get("legacy_paths", {}))
        self.orders = dict(self.previous.get("task_orders", {}))
        self.legacy_raw = []
        for item in self.previous.get("series", []):
            self.db(item["id"], item["label"])
        if self.previous and not self.orders:
            for db in self.connections.values():
                for catalog, model in db.execute("SELECT catalog,model FROM reports"):
                    platform = json.loads(catalog)["platform"]
                    if platform not in self.orders:
                        self.orders[platform] = json.loads(model)["order"]

    def db(self, series, label):
        self.labels[series] = label
        if series not in self.connections:
            path = self.state / (series + ".sqlite")
            old = next((item for item in self.previous.get("series", []) if item["id"] == series), None)
            if not path.exists() and old:
                storage.unpack(self.output, old, path)
            # The working copy is the latest complete aggregate. New imports merge
            # into it transactionally; original documents and old report IDs remain.
            db = sqlite3.connect(path)
            db.executescript("""
                PRAGMA journal_mode=DELETE;
                CREATE TABLE IF NOT EXISTS documents(id TEXT PRIMARY KEY, original_bytes INTEGER, payload BLOB) WITHOUT ROWID;
                CREATE TABLE IF NOT EXISTS paths(source TEXT, path TEXT, document TEXT,
                    PRIMARY KEY(source,path,document)) WITHOUT ROWID;
                CREATE TABLE IF NOT EXISTS reports(id TEXT PRIMARY KEY, catalog TEXT, model TEXT, documents TEXT) WITHOUT ROWID;
                CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT) WITHOUT ROWID;
                CREATE TABLE IF NOT EXISTS aliases(id TEXT PRIMARY KEY,target TEXT) WITHOUT ROWID;
                CREATE TABLE IF NOT EXISTS files(path TEXT PRIMARY KEY,document TEXT) WITHOUT ROWID;
            """)
            if not db.execute("SELECT 1 FROM metadata WHERE key='canonical-documents-v2'").fetchone():
                # Derived comparison wrappers can be regenerated from the source
                # observations; they are not additional measured evidence.
                db.execute("DELETE FROM paths WHERE path LIKE 'extended/views/%' OR path LIKE 'history/ci/%'")
                db.execute("DELETE FROM documents WHERE id NOT IN (SELECT document FROM paths)")
                db.execute("INSERT INTO metadata VALUES('canonical-documents-v2','1')")
                self.changed.add(series)
            if not db.execute("SELECT 1 FROM metadata WHERE key='logical-source-paths-v1'").fetchone():
                db.execute(
                    "INSERT OR IGNORE INTO paths SELECT 'retained-benchmarks',path,document FROM paths"
                )
                db.execute("DELETE FROM paths WHERE source!='retained-benchmarks'")
                db.execute("INSERT INTO metadata VALUES('logical-source-paths-v1','1')")
                self.changed.add(series)
            self.connections[series] = db
        return self.connections[series]

    def document(self, series, label, root, path):
        data = original_bytes(path)
        sha = digest(data)
        db = self.db(series, label)
        if db.execute("SELECT 1 FROM documents WHERE id=?", (sha,)).fetchone() is None:
            # Preserve source bytes, including hashes, sampling and formatting.
            # In-database compression bounds expanded browser memory. The complete
            # SQLite generation is independently zstd compressed for transfer.
            db.execute("INSERT INTO documents VALUES(?,?,?)", (sha, len(data), storage.compress(data)))
            self.changed.add(series)
            self.doc_count += 1
        db.execute(
            "INSERT OR IGNORE INTO paths VALUES(?,?,?)",
            ("retained-benchmarks", path.relative_to(root).as_posix(), sha),
        )
        self.owners[path.relative_to(root).as_posix()] = series
        return sha

    def source(self, root):
        started = time.monotonic()
        qualification = root.parent.parent / "qualification.json"
        try:
            revision = subprocess.check_output(
                ["git", "-C", str(root), "rev-parse", "HEAD"], text=True, stderr=subprocess.DEVNULL
            ).strip()
        except subprocess.CalledProcessError:
            revision = "unversioned input directory"
        if qualification.exists():
            revision = json.loads(qualification.read_text())["publication_base"]
        self.source_revisions.append({"root": str(root), "revision": revision})
        for path in sorted(root.glob("extended/observations/*/report.json*")):
            if path.name not in ("report.json", "report.json.gz"):
                continue
            canonical = path.with_suffix("") if path.suffix == ".gz" else path
            raw = records.read(canonical)
            self.raw_index[raw["revision"]["sha"], raw["platform"], raw["series"]].append((root, canonical))
        catalog = records.read(root / "catalog.json")
        orders = dict(self.orders)
        found_orders = set()
        for entry in reversed(sorted(catalog, key=lambda e: e.get("date", ""))):
            if entry["platform"] in found_orders:
                continue
            chart = root / entry["figures"]
            if records.exists(chart):
                data = site_storage.unpacked(records.read(chart))
                orders[entry["platform"]] = data.get("task_order", {})
                found_orders.add(entry["platform"])
        self.orders.update(orders)
        for index, entry in enumerate(catalog, 1):
            path = root / entry["report"] / "report.json"
            wrapper = records.read(path)
            series, label = identity(wrapper, entry, path)
            self.owners[path.relative_to(root).as_posix()] = series
            documents = (
                []
                if wrapper.get("kind") == "ci-history-view" or entry.get("extended")
                else [self.document(series, label, root, path)]
            )
            norm_path = path.with_name("normalization.json")
            normalization = records.read(norm_path) if records.exists(norm_path) else {}
            if wrapper.get("kind") == "ci-history-view":
                components = []
                for part in wrapper["observations"]:
                    dependency = root / part["report"]
                    sub = records.read(dependency)
                    if not part["report"].startswith("extended/views/"):
                        documents.append(self.document(series, label, root, dependency))
                    components.append(
                        dict(
                            name=part["name"],
                            namespace="extended / " if part["name"] == "Extended workloads" else "",
                            observation=part,
                            report=sub["tool_comparisons"],
                        )
                    )
                    for source_root, raw_path in self.raw_index.get(
                        (entry["commit"], wrapper["platform"], sub.get("series")), []
                    ):
                        documents.append(self.document(series, label, source_root, raw_path))
                counts = sorted({n for p in components for n in p["report"]["contract"]["file_counts"]})
                cpus = sorted({n for p in components for n in p["report"]["contract"]["cpu_counts"]})
                report = history.presentation(components, dict(file_counts=counts, cpu_counts=cpus))
            else:
                report = wrapper["tool_comparisons"]
                for source_root, raw_path in self.raw_index.get(
                    (entry["commit"], wrapper.get("platform"), wrapper.get("series")), []
                ):
                    documents.append(self.document(series, label, source_root, raw_path))
            rows = matrix.relative_results(report)
            if not rows:
                continue
            contract = report["contract"]
            model = dict(
                rows=rows,
                normalization=normalization,
                order=orders.get(entry["platform"], {}),
                overview=overview.summarize(report),
                counts=contract["file_counts"],
                cpus=contract["cpu_counts"],
                allocations={
                    str(cpu): matrix.allocation_label(report, cpu) for cpu in contract["cpu_counts"]
                },
                title=matrix.platform_title(report),
            )
            key = digest(
                encoded(
                    [entry["platform"], entry.get("source", "merged"), entry["commit"], entry["report"]]
                ).encode()
            )
            item = dict(
                entry,
                database=series,
                record_id=key,
                figures="database:" + key,
                report="?series=" + series + "&report=" + key + "#report",
            )
            db = self.db(series, label)
            content = (encoded(item), encoded(model), encoded(list(dict.fromkeys(documents))))
            old = db.execute("SELECT catalog,model,documents FROM reports WHERE id=?", (key,)).fetchone()
            if old != content:
                db.execute("INSERT OR REPLACE INTO reports VALUES(?,?,?,?)", (key, *content))
                self.changed.add(series)
            nav = {
                k: item[k]
                for k in (
                    "platform",
                    "source",
                    "source_label",
                    "commit",
                    "date",
                    "label",
                    "local",
                    "database",
                    "record_id",
                    "figures",
                    "report",
                )
                if k in item
            }
            self.navigation[entry["platform"], entry.get("source", "merged"), entry["commit"]] = nav
            self.legacy[entry["report"]] = key
            if not entry.get("local") and entry.get("source", "merged") == "merged":
                for link in entry.get("links", []):
                    if link.get("label", "").startswith("PR #"):
                        reference = "pr/" + link["label"][4:] + "/"
                        self.legacy.setdefault(reference, key)
                    elif link.get("label", "").startswith("Release "):
                        reference = "tag/" + link["label"][8:].removeprefix("v") + "/"
                        self.legacy.setdefault(reference, key)
            if index % 50 == 0:
                for connection in self.connections.values():
                    connection.commit()
                print(f"{root.name}: imported {index}/{len(catalog)} selectable reports", flush=True)
        # Keep superseded raw observations too, independently of viewer selection.
        archived = 0
        for packed in sorted({*root.rglob("report.json"), *root.rglob("report.json.gz")}):
            relative = packed.relative_to(root)
            if relative.parts[:2] in (("extended", "views"), ("history", "ci")):
                continue
            path = packed.with_suffix("") if packed.suffix == ".gz" else packed
            value = records.read(path)
            series, label = identity(value, path=relative)
            commit = (
                value.get("head")
                or value.get("revision", {}).get("sha")
                or value.get("source", {}).get("head_sha")
            )
            self.legacy_raw.append((path.parent.relative_to(root).as_posix() + "/", series, commit))
            self.document(series, label, root, path)
            archived += 1
        for db in self.connections.values():
            db.commit()
        print(
            f"Imported {len(catalog)} selections and {archived} retained raw reports in {time.monotonic()-started:.1f}s",
            flush=True,
        )

    def enrich_evidence(self):
        for path, series, commit in self.legacy_raw:
            selected = [
                item
                for item in self.navigation.values()
                if item["database"] == series and item["commit"] == commit
            ]
            selected.sort(key=lambda item: item.get("source", "merged") != "merged")
            if selected:
                self.legacy[path] = selected[0]["record_id"]

    def archive_inputs(self, root):
        """Keep all machine-readable inputs, including raw samples and proof sidecars."""
        parents = {}
        for path, series in self.owners.items():
            parent = Path(path).parent
            while parent != Path("."):
                parents.setdefault(parent.as_posix(), series)
                parent = parent.parent
        fallback = next(iter(self.connections))
        for path in sorted(root.rglob("*")):
            if (
                not path.is_file()
                or path.name == storage.MANIFEST
                or "data" == path.relative_to(root).parts[0]
            ):
                continue
            if not (path.name.endswith(".json") or path.name.endswith(".json.gz")) or path.name.startswith(
                "landscape.json"
            ):
                continue
            canonical = path.with_suffix("") if path.suffix == ".gz" else path
            relative = canonical.relative_to(root).as_posix()
            if relative in ("catalog.json", "datasets.json"):
                continue
            parent = canonical.relative_to(root).parent
            series = self.owners.get(relative)
            while series is None and parent != Path("."):
                series = parents.get(parent.as_posix())
                parent = parent.parent
            series = series or fallback
            sha = self.document(series, self.labels[series], root, canonical)
            db = self.connections[series]
            before = db.execute("SELECT document FROM files WHERE path=?", (relative,)).fetchone()
            if before != (sha,):
                db.execute("INSERT OR REPLACE INTO files VALUES(?,?)", (relative, sha))
                self.changed.add(series)
        for db in self.connections.values():
            db.commit()

    def finish(self):
        series_meta = {s["id"]: s for s in self.previous.get("series", [])}
        for series, db in self.connections.items():
            # Comparison models are disposable read models. Retain every original
            # observation, but keep just the selected model for each viewer key.
            for report_id, metadata in db.execute("SELECT id,catalog FROM reports").fetchall():
                item = json.loads(metadata)
                active = self.navigation.get((item["platform"], item.get("source", "merged"), item["commit"]))
                if active and active["record_id"] != report_id:
                    db.execute("INSERT OR REPLACE INTO aliases VALUES(?,?)", (report_id, active["record_id"]))
                    db.execute("DELETE FROM reports WHERE id=?", (report_id,))
                    self.changed.add(series)
            db.commit()
            old = series_meta.get(series)
            if old and series not in self.changed and old["file"].endswith(".sqlite.zst"):
                db.close()
                continue
            document_count, original = db.execute(
                "SELECT count(*),coalesce(sum(original_bytes),0) FROM documents"
            ).fetchone()
            report_count = db.execute("SELECT count(*) FROM reports").fetchone()[0]
            db.execute("VACUUM")
            db.close()
            source = self.state / (series + ".sqlite")
            db_hash = digest(source.read_bytes())
            filename = series + "-" + db_hash[:16] + ".sqlite.zst"
            destination = self.output / "data" / filename
            destination.parent.mkdir(exist_ok=True)
            started = time.monotonic()
            if not destination.exists():
                packed = destination.with_suffix(destination.suffix + ".tmp")
                packed.write_bytes(storage.compress(source.read_bytes()))
                packed.replace(destination)
            generations = (
                (
                    [
                        {
                            k: old[k]
                            for k in (
                                "file",
                                "sha256",
                                "expanded_bytes",
                                "compressed_bytes",
                                "compressed_sha256",
                                "chunks",
                            )
                            if k in old
                        }
                    ]
                    + old.get("generations", [])
                )[:1]
                if old
                else []
            )
            series_meta[series] = dict(
                id=series,
                label=self.labels[series],
                file="data/" + filename,
                sha256=db_hash,
                expanded_bytes=source.stat().st_size,
                compressed_bytes=destination.stat().st_size,
                original_bytes=original,
                documents=document_count,
                reports=report_count,
                generations=generations,
            )
            print(
                f"{self.labels[series]}: {report_count} reports, {document_count} raw documents; "
                f"{source.stat().st_size/1e6:.1f} MB expanded / {destination.stat().st_size/1e6:.1f} MB zstd "
                f"({time.monotonic()-started:.1f}s)",
                flush=True,
            )
        for series in series_meta.values():
            for generation in [series, *series.get("generations", [])]:
                storage.retain_transport(self.output, generation)
        allowed = {
            name
            for s in series_meta.values()
            for g in [s, *s.get("generations", [])]
            for name in storage.transport_files(g)
        }
        for p in (self.output / "data").glob("*.sqlite.*"):
            if (
                re.fullmatch(r"[0-9a-f]{20}-[0-9a-f]{16}\.sqlite\.zst(?:\.part[0-9]{3})?(?:\.tmp)?", p.name)
                and p.relative_to(self.output).as_posix() not in allowed
            ):
                p.unlink()
        aliases = dict(self.previous.get("aliases", {}))
        for series in series_meta:
            with closing(sqlite3.connect(self.state / (series + ".sqlite"))) as db:
                aliases.update(db.execute("SELECT id,target FROM aliases").fetchall())
                series_meta[series]["paths"] = [
                    row[0] for row in db.execute("SELECT path FROM files ORDER BY path")
                ]
        sources = self.source_revisions or self.previous.get("sources", [])
        for source in sources:
            qualification = Path(source["root"]).parent.parent / "qualification.json"
            if qualification.exists():
                source["revision"] = json.loads(qualification.read_text())["publication_base"]
        manifest = dict(
            schema=1,
            format="sqlite-zstd",
            sources=sources,
            aliases=aliases,
            series=list(series_meta.values()),
            catalog=sorted(self.navigation.values(), key=lambda e: e["date"]),
            legacy_paths=self.legacy,
            task_orders=self.orders,
        )
        manifest["storage"] = dict(
            current_database_bytes=sum(s["compressed_bytes"] for s in series_meta.values()),
            previous_database_bytes=sum(
                g["compressed_bytes"] for s in series_meta.values() for g in s.get("generations", [])
            ),
            files=sum(p.is_file() for p in self.output.rglob("*")),
        )
        # Expose the new manifest only after every referenced immutable file exists.
        temporary = self.output / (storage.MANIFEST + ".tmp")
        temporary.write_text(encoded(manifest) + "\n")
        temporary.replace(self.output / storage.MANIFEST)
        panel = landscape.history_panel([], include_overview=True)
        panel = panel[: panel.index('<script src="assets/three-landscape.js">')]
        page = (
            """<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>XFF benchmark history</title><link rel="icon" href="data:,"><link rel="stylesheet" href="assets/database.css"></head><body>
<div id="database-loading" role="status" aria-live="polite"><div class="loading-card"><span class="loading-spinner" aria-hidden="true"></span><div><strong data-loading-stage>Loading benchmark history\u2026</strong><div data-loading-label></div></div></div></div>
<header><div><span class="eyebrow">XFF \u00b7 REAL MEASUREMENTS</span><h1>Benchmark history</h1>
<p>Compare measured XFF performance with traditional tools across the retained history.</p></div>
<details id="storage"><summary>Storage &amp; loading <span id="storage-summary"></span></summary><div id="storage-body"></div></details></header>
<main>"""
            + panel
            + """<section id="report"><h2>Measurement report</h2><p id="report-status">Select a measured version to view its comparisons and original observations.</p>
<div id="report-actions"></div><div id="report-table"></div></section></main>
<script src="assets/three-landscape.js"></script><script type="module" src="assets/database-app.js"></script></body></html>"""
        )
        (self.output / "index.html").write_text(page)
        print(
            f'Current aggregates: {sum(s["compressed_bytes"] for s in series_meta.values())/1e6:.1f} MB; '
            f'{len(series_meta)} database series; {len(manifest["catalog"])} selectable reports',
            flush=True,
        )


def publish(root, renderer):
    required = (
        "landscape.js",
        "database-app.js",
        "database-worker.js",
        "database.css",
        "sql-wasm.js",
        "sql-wasm.wasm",
        "zstd.wasm",
        "database-NOTICES.txt",
    )
    if any(not (renderer / name).is_file() for name in required):
        raise ValueError("build the pinned benchmark renderer before packing databases")
    storage.expand(root)
    with tempfile.TemporaryDirectory(prefix="xff-database-build-") as temporary:
        builder = Builder(root, Path(temporary))
        try:
            builder.source(root)
            builder.archive_inputs(root)
            builder.enrich_evidence()
            builder.finish()
        finally:
            for db in builder.connections.values():
                db.close()
    datasets_path = root / "datasets.json"
    if datasets_path.exists():
        datasets = json.loads(datasets_path.read_text())
        datasets["storage"] = {"format": "sqlite-zstd", "manifest": storage.MANIFEST}
        datasets_path.write_text(encoded(datasets) + "\n")
    # Derived per-report HTML/figures have one shared replacement. JSON inputs
    # have been imported and are recoverable through the verified file index.
    for path in list(root.rglob("*")):
        if (
            not path.is_file()
            or "data" == path.relative_to(root).parts[0]
            or "assets" == path.relative_to(root).parts[0]
        ):
            continue
        if path == root / "index.html" or path.name in (storage.MANIFEST, "catalog.json", "datasets.json"):
            continue
        if path.suffix == ".html" or path.name.endswith((".html.gz", ".json", ".json.gz")):
            path.unlink()
    import shutil

    # Every benchmark page now uses the shared Three.js renderer. The older
    # Plotly bundle has no remaining benchmark HTML consumers.
    (root / "assets/plotly-landscape.js").unlink(missing_ok=True)
    for path in renderer.iterdir():
        if path.is_file():
            destination = (
                root / "assets" / ("three-landscape.js" if path.name == "landscape.js" else path.name)
            )
            destination.parent.mkdir(exist_ok=True)
            shutil.copyfile(path, destination)
    manifest = storage.read_manifest(root)
    manifest["storage"]["files"] = sum(path.is_file() for path in root.rglob("*"))
    (root / storage.MANIFEST).write_text(encoded(manifest) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--renderer-dir", type=Path, default=HERE / "benchmark_web/dist")
    args = parser.parse_args()
    publish(args.root.resolve(), args.renderer_dir.resolve())


if __name__ == "__main__":
    main()
