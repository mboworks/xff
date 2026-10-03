# Published site storage

The complete Pages deployment shares one storage budget across coverage, benchmarks, release
sites, and assets. `storage-report.json` reports each section's file count and byte total.
Publication emits a warning at **200 MB**, so we can review growth before it disrupts deployment.
The final packaging guard remains 9 GB. Git history is not part of the deployed site; publishers
restore the site with a shallow checkout and do not rewrite old publication commits. Source
checkouts that need complete commit history use `blob:none` filtering to avoid fetching historical
report contents.

## Retention

- Coverage history, category totals, patch results, policy thresholds, and original run metadata
  are retained indefinitely. The overview still prefers post-merge results when available and
  retains the separate pre/post-merge history.
- Detailed coverage source, function, and branch pages are retained for **seven days**, based on
  the original report completion time. Republishing does not restart that clock. Older reports
  explicitly explain that details expired and still link their aggregate summary.
- Benchmark observations for releases and merges to main are retained indefinitely, including
  original measurements superseded by backfills. There is no report-count eviction. Local data,
  provenance, invocation contracts, and normalization inputs are preserved as well.

## Representation

Coverage details use compressed report manifests and shared source-page bases. Each changed page
stores edits against one base; there are no chains through earlier versions. Numbered source-line
labels are generated in the viewer so inserting a source line does not duplicate the remaining
HTML. Garbage collection removes only detail archives and bases no longer referenced by a retained
seven-day report. The shared viewer loads the selected report and source page on demand.
Historical detail URLs route through this viewer; expired details show an explanation.

Raw benchmark reports and coverage summaries are available as `.json.gz` downloads. Gzip preserves
the original observation bytes. Chart data interns repeated JSON values before compression; the
browser reconstructs the same values without rounding, resampling, or changing normalization.
Standalone reports share their chart payload with the history explorer. Large report tables are
packed HTML loaded at their original URL, preserving navigation and relative assets.

The browser uses its built-in `DecompressionStream` support; no archive library, remote renderer,
or server-side execution is required. Packed report viewing requires JavaScript. Raw compressed
JSON remains downloadable without it. Loading a historical report performs a small decompression
step instead of transferring repeated generated markup.

All publishers compact the retained `coverage-pages` branch **before committing**, then inspect
and deploy the complete site. Benchmark publication expands its stored inputs in the disposable
checkout before regenerating views. CI baseline selection and backfill revision discovery read
both expanded and compressed observations. This keeps storage changes independent of coverage
policy and benchmark compatibility decisions.

## Next review at 200 MB

- Replace packed generated summary/table HTML with one shared renderer driven by compact records.
- Generate chart order, scale, and CPU-pair views from one numeric matrix in the browser instead
  of storing precomputed views, even though repeated values are currently interned.
- Evaluate column-oriented benchmark observations and shared invocation/tool dictionaries. Keep
  original observations and provenance recoverable; never evict release or merged-PR data to
  meet a size target.
- Measure whether coverage file data can replace remaining presentation markup in detail deltas.
- Consolidate duplicate historical release documentation and old renderer assets only after
  checking all retained links.
- Report deployment bytes separately from the Git object database and Actions artifact/cache
  usage. A smaller current tree does not erase historical Git objects; any history rewrite needs
  a separate, explicit decision.
