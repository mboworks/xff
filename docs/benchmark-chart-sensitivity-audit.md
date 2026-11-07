# Benchmark chart sensitivity audit, 2026-11-07

The [extended history campaign](benchmark-extended-history-campaign.md) implements additional
workloads and qualifies them against 130 historical production builds on drogon.

The standard landscape does not provide a sufficient view of XFF's optimization progress.
Its arithmetic reproduces the retained measurements, and its browser renderer responds to
changed timings. However, its workload and executable coverage exclude several large measured
improvements, and it does not show a controlled XFF-before/after comparison. A similar-looking
surface cannot establish that the optimizations were ineffective.

## Evidence and boundaries

Source: `ae8c5af3eb11dc8571f11db9b307b0db948f78c6` (#1130).
Pages: `153d58627f2132b838a091ffcfdb30dd49571b8b`.
The live `/xff/benchmarks/catalog.json` exactly matched this snapshot, including its SHA-256.
The catalog contains 528 entries; the browser's duplicate selection leaves 490 reports across
five datasets. See the [machine-readable results](benchmarks/chart-sensitivity-audit-20261107.json).

The current-performance assessment excludes local Linux v2 at the user's direction because it
has not been measured recently. Its retained arithmetic checks remain historical evidence only;
its age and clipping statistics are not explanations for the current CI charts.

An independent calculation selects each participant's recorded fastest samples and recomputes
`100 * (1 - XFF/reference)` and `-log10(XFF/reference)`. It checks the decoded packed figures
against raw records by task, tree, worker count, file count and reference tool, across all three
orders, both metrics and every available worker pair. All 3,178,656 plotted values match.
Catalog revisions and available catalog binary hashes also match their underlying reports.

A headless Chromium control serves the retained site and its published JavaScript locally.
Selecting the previous and latest Linux versions loads different commits and values. Picked
surface values match the selected figures. A separately generated synthetic report halves every
XFF elapsed sample while preserving reference samples. All 468 default figure values change by
the expected formula `new_height = 50 + old_height / 2`, and the rendered canvas changes.
The synthetic data is an explicitly labelled sensitivity control; it is never published or
substituted for a historical measurement.

This does not independently verify every historical executable, its compiler, its fixture oracle,
or the cause of historical timing variation. The claimed optimization results below come from
their existing qualification reports; those experiments were not rerun in this audit.

## Published coverage is incomplete for the intended question

The actively updated CI datasets contain 138 selected revisions per platform through #1122,
November 6. They do not retain every merge. The workload coverage and hosted-run comparisons
below assess these CI datasets; the older local datasets remain in the historical inventory.

The chart measures the lean `//xff/cli:xff` production executable. It does not measure the
extended `xff_full` executable or exercise its archive features. Its command policy fixes
`--no-config --no-pager --color=never --sort=none --exact --case=sensitive --gitignore=off
--hidden`, uses the relative root `.`, and emits NUL-delimited paths with `-print0`.

There are seven XFF tasks with reference comparisons: file discovery, `*.txt` names, two exact
content searches, and three ordered-subsequence fuzzy path filters. These yield nine task/reference
pairs. `files-safe` has XFF measurements but no reference, so `relative_results()` omits it from
the landscape. The three precomputed fuzzy-list tasks measure fzf alone. Fuzzy path filtering
does not exercise content-similarity scoring.

Specific mismatches between demonstrated gains and the chart:

| Change                           | Existing qualification evidence                                                                                                   | Why the standard chart misses the gain                                                                                   |
| :------------------------------- | :-------------------------------------------------------------------------------------------------------------------------------- | :----------------------------------------------------------------------------------------------------------------------- |
| #1105 archive directory ranges   | Complete 100k-member TAR traversals fall from about 9-14 seconds to 94-187 milliseconds across both hosts                         | No archives; lean binary                                                                                                 |
| #1116 empty CCTZ fragments       | macOS timestamp-bearing metadata commands improve about 92%                                                                       | No formatted timestamps; output is `-print0`                                                                             |
| #1091 uniform native case        | Linux broad 100k-file name search falls from about 129 ms to 44 ms with one worker                                                | Every chart command passes `--exact`, bypassing native-case lookup                                                       |
| #1090 names-only listing paths   | Broad absolute-root listings improve about 9-14%; short relative-root controls are mostly neutral on Mac and 1-2% slower on Linux | Chart always uses the short relative root `.`                                                                            |
| #1094 compact traversal metadata | Broad native listings improve about 5-6% on Mac and 8-9% on Linux; memory improves too                                            | This path is exercised, but the chart shows only competitor-relative elapsed time, with no uncertainty or memory history |

Sources: [archive ranges](benchmarks/archive-directory-range-20261105.md),
[CCTZ](benchmarks/cctz-empty-format-20261105.md),
[native case](benchmarks/uniform-native-case-20261104.md),
[listing paths](benchmarks/native-name-paths-20261104.md), and
[compact metadata](benchmarks/compact-traversal-metadata-20261104.md).
These are workload-specific results, not additive percentages or universal binary speedups.

## Movement exists, but the chart is a poor progress measure

For Linux broad/100k/one-worker `files` versus rg, the published #1093 report records XFF at
63.655 ms and rg at 47.570 ms; #1094 records 59.888 ms and 48.282 ms. The chart value moves
from -33.814% to -24.037%, exactly as the raw data requires. This is an example of actual chart
movement, not a paired causal estimate of #1094: these are different hosted runs.

Six reports (#1102, #1103, #1104, #1105, #1117 and #1107) record the same Linux executable
SHA-256 `09c997ab01d2d7dfe916bf1cced7b54757c6b6b25f8d2a9743812e9db23efb51` and the same
OS release. Yet that same listing cell ranges from 57.341 to 64.831 ms, and its XFF/rg ratio
from 1.2186 to 1.2930. This is observed variation across hosted runs of an unchanged binary;
it cannot be explained by an XFF code improvement between those measurements. It does not
identify the cause of that variation or estimate uncertainty for every workload.

The surface shows XFF/reference in each run, not XFF/new versus XFF/old. Reference normalization
adds a corrected time to the hover details; it intentionally does not change the ratio or surface.
Thus changing the Timings selector does not change geometry. Hosted sample aggregation pools
three hosts' samples and selects fastest samples separately per participant; the selected XFF
and reference host contributions can differ. Neither that estimator nor a reference ratio is a
substitute for an interleaved old/new experiment with uncertainty.

The latest CI default views have no clipped points, so clipping does not explain their apparent
lack of movement. As a general display constraint, the default percentage range is +/-100 with
capping: any XFF/reference ratio >=2 reaches the same bottom boundary, and percentage gains
approach +100 asymptotically. Logarithmic scale can expose differences if its selected range
includes them. Auto range rescales each report and is unsuitable for judging uniform changes by
shape alone. These general constraints are not evidence of clipping in the current CI views.

Recent published Similarity order is shared within compatible history after #1093; Average order
still changes with each report. A shared task order helps track cells but does not supply missing
workloads, new measurements or a historical baseline.

## Changes needed before treating this as an optimization dashboard

1. Show each dataset's latest measured source and its distance behind current main prominently.
   Give the standard landscape an explicit workload scope and identify the measured binary.
2. Retain a permanent, versioned suite of production commands covering ordinary native-case
   and exact-case discovery, relative and absolute roots, metadata/time output, summaries,
   content search, similarity, comparisons, and full-binary archive traversal. Keep the existing
   competitor suite as one view; do not silently change its historical workload definition.
3. Measure selected old/new production binaries interleaved on the same host and fixture, with
   an identical-binary control. Publish absolute elapsed time, first-result latency, CPU and memory,
   paired changes and uncertainty. Preserve worker budgets, raw samples and executable identities.
4. Display XFF-only workloads even when no competing tool implements the same operation. Add an
   XFF-before/after view with fixed task positions and scales; show unavailable cells and clipping
   counts. Keep competitor-relative values and reference corrections explicitly labelled.
5. Link each claimed speedup to its exercised workload and add that workload to continuing
   regression coverage. Qualify a small representative panel before expanding historical runs.

No new benchmark campaign is needed to establish the coverage and presentation defects above.
Repeating the current matrix alone would continue to miss archive, timestamp and native-case gains.

## Retained audit artifacts

The local evidence directory is `/private/tmp/xff-chart-evidence-20261107/`:
the extracted Pages snapshot, live catalog, selected records, independent `audit.py`, complete
`audit-summary.json`, per-cell `cells.csv`, `browser-check.mjs`, browser results and original versus
synthetic screenshots. Their hashes and representative raw-derived rows are in the linked JSON.
Browser controls used the existing installed Playwright/Chromium; no production files or published
measurements were changed. The temporary server and browser were closed after verification.
