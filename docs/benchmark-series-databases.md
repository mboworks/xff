# Benchmark databases and shared viewer

The benchmark publisher builds one lossless SQLite aggregate per dataseries and one shared
viewer. It combines each new publication with the latest retained aggregate. Switching versions,
workloads, order, scale or worker allocation queries the selected database; it does not download
another generated report. Measurement contracts, normalization and the original sample bytes
remain unchanged.

## Retained and deployed files

`benchmarks/database-manifest.json` contains the navigation catalog, dataseries identities,
database hashes and sizes, canonical input paths, report aliases and historical URL mappings.
Content-addressed database names make browser caching independent of publication timing.

- `documents` stores each original JSON document once by its byte SHA-256, compressed with zstd.
- `files` maps canonical input paths to the current document. `paths` retains source/path/document
  associations, including earlier document versions.
- `reports` stores disposable numeric comparison models, catalog metadata and source document IDs.
  Chart coordinates are generated in the browser from these rows using the existing conventions.
- `aliases` preserves report selection when a combined or newer view supersedes a read model.
- `metadata` records storage migrations. It is separate from measurement contracts.

The whole SQLite file is zstd compressed. Source documents remain individually compressed inside
it, so opening a series does not expand every historical raw sample in browser memory. Python
restoration and browser downloads verify the original byte SHA-256. Database opening verifies the
expanded database SHA-256 from the manifest.

Keep the current and one previous generation for each series on `coverage-pages`. Only current
generations go into the public deployment. Previous generations are rollback copies, not another
source of measurements. Git history is unchanged.

Large compressed databases use immutable 48 MiB transport parts on the retained branch. This
stays below GitHub's [100 MiB Git file limit](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github).
Deployment verifies and joins those parts into one `.sqlite.zst` file per series, then excludes
the transport parts and previous generations from the public copy. The browser still fetches one
file for a selected dataseries. No database binaries belong in the source-code PR.

## Publishing and reading

The existing `Publish benchmarks` workflow remains the sole benchmark publisher. It restores
retained inputs into a disposable checkout, reconciles every available valid artifact, regenerates
the existing comparison models and runs `tools/benchmark_database.py`. Packing completes before
the manifest is replaced and before any redundant input files are removed. The existing shared
writer lock and push/reconcile retry remain in force.

`Deploy retained site` runs `benchmark_database_storage.py stage-deployment` on its public copy.
It assembles the current databases and enforces the 1,000,000,000-byte whole-site budget before
uploading Pages. The retained tree may be larger because it includes rollback generations.

The shared viewer uses locally served SQL.js and zstd WebAssembly in a worker. A spinner covers
download, decompression, integrity checking, database opening and chart preparation. Download
progress and measured load timings are visible under **Storage & loading**. The worker keeps at
most two open databases with a combined 384 MiB cache budget; a single larger selected series is
allowed and evicts other open series. Downloads may also use browser Cache Storage. Switching a
selection discards obsolete results. A stale database URL causes a manifest refresh and retry.

The report table supports filtering and exact original JSON downloads, including adjacent proof
sidecars. Historical report, PR and release URLs route through the shared viewer. Local inventory,
upload discovery, baseline selection, recovery and planning restore the canonical JSON needed by
their existing validators. Legacy JSON/gzip sites remain readable.

## Backfill transition

1. Preserve the currently running backfill, its frozen plan and successful platform artifacts.
   Do not cancel jobs or rerun successful measurements for this representation change.
2. Merge the storage PR. The next trusted main-branch publisher migrates the latest retained
   snapshot, including any results published while this PR was under review. No manual seed push
   to `coverage-pages` is needed.
3. Verify successful publication and deployment, the live database manifest and selection of both
   CI platforms. The publisher retains original reports and collection/reconciliation checkpoints.
4. Resume `benchmark_extended_backfill.yml` on `main`, initially with `batch_size=10` and
   `platform=both`. Its planner restores only `extended/` and `reconciliation/` before recovering
   unexpired successful artifacts and freezing missing work. Inspect the plan to ensure already
   successful revision/platform pairs were excluded.
5. Retry only failed work when needed. Original artifact names, proof validation, frozen plans,
   measurement recipes and 30-day artifact retention are unchanged. Old in-flight runs remain
   valid inputs to the new publisher.

The core backfill planner similarly restores `runs/`. These compatibility changes concern input
storage and discovery; they do not append samples, change measured commands or require a new
benchmark population. See [the extended history procedure](benchmark-dense-history-20261115.md).

## Qualification and limits

The [real-data qualification](benchmarks/series-databases-20261119.json) uses retained snapshot
`03bd081a292526df117cfcae8160d833e941d508`, including the first completed extended backfill.
All 3,110 input JSON paths, totaling 8,168,894,339 expanded bytes, were checked after migration.
There were no mismatches; the catalog was preserved byte-for-byte and `datasets.json` was compared
after excluding its new storage descriptor. The resulting viewer has 620 selectable reports in
five current databases totaling 447,536,030 compressed bytes and 17 deployed benchmark files.
The prior benchmark section occupied 700,456,771 bytes, a reduction of about 36%. Rollback copies
are additional retained-branch storage, excluded from that deployed total.

Focused tests cover exact restoration, document deduplication, numeric rows, repeatable imports,
generation rotation, new-upload precedence, integrity failures, safe paths, local download,
CI checkpoint recovery and transport assembly. Browser tests compare all six chart variants with
the Python renderer and exercise loading, caching, original downloads and existing controls.
The two latest real CI reports also loaded 728 comparisons each in Chrome without JavaScript
errors. Comparing all six variants against their retained charts checked 34,944 numeric coordinate
values: the largest difference was 1.12e-16 from the Python/JavaScript logarithm implementations.
Both continuous and PR aggregation jobs fetch database-backed baselines; their workflow guards and
baseline/shard tests pass.

This is not a fully normalized sample schema. Original documents are stored byte-for-byte rather
than replaced by reconstructed JSON. The publisher currently restores the legacy JSON working set
and generates existing read models before packing: this snapshot requires over 8 GB of scratch
space, beyond database and working-copy storage. Direct database readers and column-oriented sample
storage remain possible follow-ups. CI series currently require a 175 MB or 148 MB first download
and roughly 543 MB or 473 MB of SQLite data; the spinner makes that work visible but does not remove
its network or memory cost. Safari and Firefox have not been qualified by the local Chrome tests.
