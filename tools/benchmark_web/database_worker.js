// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
import { init as initZstd, decompress } from "@bokuweb/zstd-wasm";
import { figures } from "./database_figures.js";

importScripts("sql-wasm.js");
let SQL, zstdReady;
const databases = new Map();
const cancelled = new Set(),
  jobs = new Set();
const memoryBudget = 384 * 1024 * 1024;
function evict() {
  const [id, value] = databases.entries().next().value;
  value.db.close();
  databases.delete(id);
}
const tell = (value) => postMessage({ progress: true, ...value });
async function zstd() {
  zstdReady ||= initZstd(new URL("zstd.wasm", self.location.href).href);
  await zstdReady;
}
const query = (db, sql, params = []) => {
  const statement = db.prepare(sql);
  try {
    statement.bind(params);
    const rows = [];
    while (statement.step()) rows.push(statement.getAsObject());
    return rows;
  } finally {
    statement.free();
  }
};
async function database(series) {
  if (
    databases.has(series.id) &&
    databases.get(series.id).sha !== series.sha256
  ) {
    databases.get(series.id).db.close();
    databases.delete(series.id);
  }
  if (databases.has(series.id)) {
    const value = databases.get(series.id);
    databases.delete(series.id);
    databases.set(series.id, value);
    tell({ ...value.metrics, stage: "Memory cache", cached: true });
    return value;
  }
  tell({
    series: series.id,
    label: series.label,
    stage: "Downloading series database",
    compressed: series.compressed_bytes,
    expanded: series.expanded_bytes,
  });
  while (
    databases.size &&
    [...databases.values()].reduce(
      (sum, value) => sum + value.metrics.expanded,
      series.expanded_bytes,
    ) > memoryBudget
  )
    evict();
  const start = performance.now();
  const url = new URL("../" + series.file, self.location.href).href;
  let response,
    cache,
    cached = false;
  try {
    cache = await caches.open("xff-series-databases-v1");
    response = await cache.match(url);
    cached = !!response;
  } catch {}
  if (!response) {
    response = await fetch(url);
    if (!response.ok)
      throw new Error("Database download: HTTP " + response.status);
    if (cache) cache.put(url, response.clone()).catch(() => {});
  }
  let received = 0,
    lastProgress = 0;
  const stream = response.body.pipeThrough(
    new TransformStream({
      transform(chunk, controller) {
        received += chunk.byteLength;
        controller.enqueue(chunk);
        if (performance.now() - lastProgress > 200) {
          lastProgress = performance.now();
          tell({
            series: series.id,
            stage: `${cached ? "Reading cached series" : "Downloading series"} \u00b7 ${Math.min(100, Math.round((received / series.compressed_bytes) * 100))}%`,
          });
        }
      },
    }),
  );
  const packed = new Uint8Array(await new Response(stream).arrayBuffer());
  const downloaded = performance.now();
  tell({ series: series.id, stage: "Decompressing zstd database\u2026" });
  await zstd();
  const decodeStart = performance.now();
  const bytes = decompress(packed);
  const decoded = performance.now();
  tell({ series: series.id, stage: "Checking database integrity\u2026" });
  const hash = Array.from(
    new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)),
    (b) => b.toString(16).padStart(2, "0"),
  ).join("");
  if (hash !== series.sha256)
    throw new Error("Expanded database SHA-256 differs from manifest");
  tell({ series: series.id, stage: "Opening SQLite database\u2026" });
  SQL ||= await initSqlJs({
    locateFile: () => new URL("sql-wasm.wasm", self.location.href).href,
  });
  const db = new SQL.Database(bytes);
  db.run("PRAGMA query_only=ON");
  db.run("PRAGMA trusted_schema=OFF");
  const metrics = {
    series: series.id,
    label: series.label,
    stage: "Ready",
    cached,
    compressed: received,
    expanded: bytes.byteLength,
    downloadMs: downloaded - start,
    decodeMs: decoded - decodeStart,
    openMs: performance.now() - decoded,
  };
  const value = { db, metrics, sha: series.sha256 };
  databases.set(series.id, value);
  while (databases.size > 2) evict();
  tell({ ...metrics, resident: databases.size });
  return value;
}

async function handle({ id, action, series, record, document }) {
  try {
    if (cancelled.has(id)) return;
    const { db } = await database(series);
    if (cancelled.has(id)) return;
    if (action === "document") {
      const row = query(
        db,
        "SELECT payload,original_bytes FROM documents WHERE id=?",
        [document],
      )[0];
      if (!row)
        throw new Error("Original document is not retained in this database");
      await zstd();
      const data = decompress(row.payload);
      const sha = Array.from(
        new Uint8Array(await crypto.subtle.digest("SHA-256", data)),
        (b) => b.toString(16).padStart(2, "0"),
      ).join("");
      if (sha !== document)
        throw new Error("Original observation SHA-256 mismatch");
      postMessage({ id, value: { data, sha } }, [data.buffer]);
      return;
    }
    const started = performance.now();
    tell({ series: series.id, stage: "Preparing chart and report\u2026" });
    const row = query(
      db,
      "SELECT catalog,model,documents FROM reports WHERE id=?",
      [record],
    )[0];
    if (!row) throw new Error("Report is absent from series database");
    const model = JSON.parse(row.model),
      catalog = JSON.parse(row.catalog);
    const primary = JSON.parse(row.documents),
      related = [...primary],
      directories = new Set();
    for (const sha of primary)
      for (const { path } of query(
        db,
        "SELECT path FROM files WHERE document=?",
        [sha],
      ))
        directories.add(path.slice(0, path.lastIndexOf("/") + 1));
    for (const directory of directories)
      related.push(
        ...query(
          db,
          "SELECT document FROM files WHERE substr(path,1,?)=? ORDER BY path",
          [directory.length, directory],
        ).map((row) => row.document),
      );
    const documents = [...new Set(related)].map((sha) => ({
      sha,
      paths: query(
        db,
        "SELECT path FROM files WHERE document=? ORDER BY path",
        [sha],
      ).map((row) => row.path),
      ...query(db, "SELECT original_bytes FROM documents WHERE id=?", [sha])[0],
    }));
    const result = { catalog, model, documents, figures: figures(model) };
    tell({
      series: series.id,
      stage: "Ready",
      queryMs: performance.now() - started,
      comparisons: model.rows.length,
      resident: databases.size,
    });
    postMessage({ id, value: result });
  } catch (error) {
    if (!cancelled.has(id))
      postMessage({ id, error: String(error.stack || error) });
  } finally {
    cancelled.delete(id);
    jobs.delete(id);
  }
}
let queue = Promise.resolve();
onmessage = (event) => {
  if (event.data.action === "cancel") {
    if (jobs.has(event.data.id)) cancelled.add(event.data.id);
    return;
  }
  jobs.add(event.data.id);
  queue = queue.then(() => handle(event.data));
};
