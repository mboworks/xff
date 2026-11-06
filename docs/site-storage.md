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

## 2026-11-05 review

The 200 MB warning fired in the successful publication of PR #1102's exact post-merge coverage.
The publisher reported **669,113,099 bytes**, about 669 MB. This is a review point, not a failed
publication or permission to delete retained observations. The 9 GB packaging guard is unchanged.

The retained snapshot is coverage-pages commit
[`5df110799d3a50df1194e515dece5711eed93d8b`](https://github.com/mboworks/xff/commit/5df110799d3a50df1194e515dece5711eed93d8b),
with its original [storage report](https://github.com/mboworks/xff/blob/5df110799d3a50df1194e515dece5711eed93d8b/storage-report.json).
The recursive GitHub tree response was not truncated. Its blob sizes reproduce the reported total
plus the 978-byte storage report itself; Git history and Actions caches/artifacts are excluded.

| Section                              |  Files |       Bytes |
| :----------------------------------- | -----: | ----------: |
| Benchmarks                           |  4,466 | 479,375,677 |
| Coverage                             |  9,658 | 162,259,518 |
| Release sites                        |    442 |  16,899,029 |
| Legacy release reference pages       |     29 |   6,916,561 |
| Other site files                     |    444 |   3,662,314 |
| Total before the accounting document | 15,039 | 669,113,099 |

Benchmark storage is primarily distinct compressed report data and generated presentation, rather
than repeated JavaScript assets:

| Benchmark representation              | Files |       Bytes |
| :------------------------------------ | ----: | ----------: |
| Raw `report.json.gz`                  |   633 | 288,380,459 |
| Packed `page.html.gz`                 |   633 |  94,611,160 |
| Preview `artifact.json.gz`            |   114 |  39,213,605 |
| Interned `landscape.json.gz`          |   614 |  34,950,127 |
| `normalization.json.gz`               |   614 |  12,367,197 |
| Other metadata, navigation and assets | 1,858 |   9,853,129 |
| All benchmarks                        | 4,466 | 479,375,677 |

Grouped by provenance, local measurements use 182,764,050 bytes, main/run reports 168,455,434,
previews 97,005,338 and backfills 21,780,648. These groups overlap the representation table;
they are a different view of the same files and must not be added to it.

Exact Git blob identity found only **2,250,560 redundant benchmark bytes** across identical files.
The two shared renderer scripts together use 5,403,829 bytes. Asset deduplication alone therefore
cannot return this site to 200 MB. Nor does the 97 MB preview total establish that those reports
can be deleted: the existing retention policy and provenance still apply.

A representative current Linux report is 493,485 compressed bytes and 8,628,704 expanded bytes.
It retains raw samples, tool comparisons, measurement-shard provenance and derived summaries.
Dropping that report or rounding its numeric observations would change the historical record.
The next reduction should target redundant _representation_ while preserving lossless recovery.
No report, measurement, coverage aggregate or retained source detail was deleted by this review.

## Priorities after the 200 MB review

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
