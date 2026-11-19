// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
import { build } from "esbuild";
import { readFile, copyFile, writeFile } from "node:fs/promises";

// Embed the complete MIT notice in both the shared and standalone bundle.
const license = await readFile(
  new URL("node_modules/three/LICENSE", import.meta.url),
  "utf8",
);
await build({
  entryPoints: [new URL("landscape.js", import.meta.url).pathname],
  outfile: new URL("dist/landscape.js", import.meta.url).pathname,
  bundle: true,
  minify: true,
  format: "iife",
  target: "es2022",
  legalComments: "inline",
  banner: { js: `/*! Three.js\n${license}*/` },
});
await build({
  entryPoints: [new URL("database_worker.js", import.meta.url).pathname],
  outfile: new URL("dist/database-worker.js", import.meta.url).pathname,
  bundle: true,
  minify: true,
  format: "iife",
  platform: "browser",
  target: "es2022",
  define: { "import.meta.url": "self.location.href" },
});
for (const [source, destination] of [
  ["database_app.js", "database-app.js"],
  ["database.css", "database.css"],
  ["node_modules/sql.js/dist/sql-wasm.js", "sql-wasm.js"],
  ["node_modules/sql.js/dist/sql-wasm.wasm", "sql-wasm.wasm"],
  ["node_modules/@bokuweb/zstd-wasm/dist/web/zstd.wasm", "zstd.wasm"],
])
  await copyFile(
    new URL(source, import.meta.url),
    new URL("dist/" + destination, import.meta.url),
  );
const notices = await Promise.all(
  [
    "node_modules/sql.js/LICENSE",
    "vendor-licenses/zstd-wasm.txt",
    "vendor-licenses/zstandard.txt",
  ].map((path) => readFile(new URL(path, import.meta.url), "utf8")),
);
await writeFile(
  new URL("dist/database-NOTICES.txt", import.meta.url),
  notices.join("\n\n"),
);
