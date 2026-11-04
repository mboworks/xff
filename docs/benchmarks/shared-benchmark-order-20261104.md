<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shared benchmark-history task order

Keep one Similarity task order across compatible merged history. In the latest 20 published
macOS reports, the old order changes at every revision boundary (19 changes). A shared order
removes all those positional changes without changing a single measured value. Linux already
keeps the same order throughout its latest 20-report sample. This is a presentation correction,
not a runtime optimization or a reinterpretation of the measured performance differences.

## Method and tradeoffs

For each compatible platform/machine series and workload, select one retained measurement per
revision, preferring replacement backfills and newer attempts as before. Build symmetric
`log10(reference / xff)` task/reference profiles across CPU/file-count/tree cells. Within each
revision, compare pairs using their mean absolute difference over shared cells. Take the median
of those distances across revisions, then choose a deterministic minimum-total-adjacency chain
(exact up to 16 pairs, bounded multistart search above that). Orient its better endpoint toward
the center using median revision performance. Broad and Deep mirror the same order.

Reuse every rank across all revisions, CPU pairs and scales within that publication. Missing
tasks retain empty slots; missing cells remain gaps. Preview-only tasks append alphabetically
and cannot rearrange merged data. New merged data may update the common order at the next full
publication. Average remains specific to the selected report, Alphabetical remains fixed by name,
and standalone unpublished reports retain their own percentage-profile order.

The ordering key ignores only the accounting launcher's executable fingerprint while retaining
its method. Mach-O build fingerprints can change with debug/sandbox paths, which otherwise splits
every macOS revision into its own presentation group. This does not relax strict timing-baseline
or reference-window normalization identity. Raw reports are never rewritten.

## Published-data validation

The validation downloaded the latest 20 retained reports from each CI platform in the public
catalog (40 reports, nine task/reference pairs). It uses their existing exact measured scalars;
no new timing run is claimed. Each compressed source URL and SHA-256, all input profiles, derived
orders and neighbor distances are retained in the companion JSON.gz. The local original gzip
files remain available for reproduction. Computing all models for this sample took about
0.28 seconds on this Mac, excluding downloads and JSON decoding; this is a diagnostic duration,
not a benchmark comparison.

| Platform | Reports | Old order transitions | Shared order transitions | Median old neighbor distance | Median shared neighbor distance |
| :------- | ------: | --------------------: | -----------------------: | ---------------------------: | ------------------------------: |
| Linux    |      20 |                     0 |                        0 |                     0.087229 |                        0.083830 |
| macOS    |      20 |                    19 |                        0 |                     0.069072 |                        0.069335 |

Distances above are mean adjacent absolute log-ratio differences per revision, then their median.
The old percentage-profile optimizer uses a different objective. The common order sacrifices
about 0.4% median local smoothness on this macOS sample; Linux's log-distance median is about 3.9%
smaller. Neither result changes measured time, comparison factors, percentages, CPU allocation or
normalization. A fixed arrangement cannot guarantee every revision is individually smoother.
The purpose is truthful visual continuity when moving the version slider.

## Verification

Committed tests cover majority/outlier revisions, deterministic input reordering, duplicate cells,
empty/single/disjoint/large task sets, missing task slots, changed best/worst rankings, every CPU
pair and scale, exact hover-to-axis mapping, replacement attempt selection, incompatible workloads,
platform separation, preview-only appended tasks and identical embedded/payload coordinates.
A separate regression proves only the ordering key ignores launcher hashes; reference-window
qualification stays strict. Publication preserves every raw report byte. The affected landscape,
history and normalization Bazel suites pass. All three browser integration tests pass, covering
offline rendering, history controls and packed publication. The final PR's current-head CI remains
the merge gate.

`landscape.json` retains the ordering recipe, compatible revision count, series identity and exact
ordered pairs. The renderer consumes the same coordinates for surfaces, ticks and hover data.
