// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
import { build } from "esbuild";
import { readFile } from "node:fs/promises";

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
