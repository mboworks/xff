<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Reusable column-row storage

Keep one coordinator-owned vector for rendered cells and reuse the existing bounded encoded-record
buffer for CSV/TSV rows. Select the combination after measuring the vector and encoder separately
on both hosts. All output bytes, buffered ownership and the 1 MiB retained-capacity limit remain unchanged.

## Native decision

Ratios below are candidate elapsed time divided by its paired baseline. Values below 1 improve.
For each case take the median of three session ratios; each session uses the mean fastest seven
of nine measured rounds. Cohort values are medians across cases, without pooling hosts.

| Host  | Variant         | Column cases, >=1,000 files | Column cases, 10 files | Raw/NUL/template/action controls | Empty roots |
| :---- | :-------------- | --------------------------: | ---------------------: | -------------------------------: | ----------: |
| macos | vector_only     |                      0.9964 |                 1.0054 |                           1.0039 |      1.0039 |
| macos | row_only        |                      0.9777 |                 1.0048 |                           1.0044 |      1.0031 |
| macos | combined        |                      0.9775 |                 0.9987 |                           1.0092 |      0.9928 |
| macos | baseline_repeat |                      0.9994 |                 1.0000 |                           1.0038 |      1.0014 |
| linux | vector_only     |                      0.9978 |                 0.9982 |                           0.9998 |      1.0034 |
| linux | row_only        |                      0.9825 |                 1.0039 |                           1.0009 |      0.9969 |
| linux | combined        |                      0.9818 |                 0.9951 |                           0.9947 |      0.9860 |
| linux | baseline_repeat |                      1.0021 |                 0.9993 |                           0.9966 |      0.9922 |

The 48 larger column cases improve by about 2.3% at the median on macOS and 1.8% on Linux
with the combined implementation; the best per-case session medians improve about 6%. Vector-only
native gains are small; combining it with row reuse also improves the in-memory production path.
The identical-baseline-copy participant measures variation rather than implementing another policy.
Retain the modest control costs: the combined macOS control median is about 0.9% slower, with
individual control medians reaching about 2.5%; Linux controls are about 0.5% faster at the median.
These controls do not establish a functional effect from code that their path never executes.

## Implementations and lifetimes

- Baseline uses the existing per-entry owning cell vector and stateless row encoder.
- Vector-only reserves lazily once, reuses vector capacity, emits or copies the row, then clears
  cells immediately. It does not retain large individual cell string allocations between entries.
- Row-only keeps the per-entry vector but emits CSV/TSV through `EncodedRecordBuffer::EmitRow`.
- Combined applies both. The existing stateless `EncodeTabularRow` API remains unchanged.

The buffer owns encoded bytes until its synchronous consumer returns. Queued/ranked consumers
retain their own copies, and `TableStream` retains its owning row before the cells are cleared.
The consumer must not re-enter the same buffer. Capacity above 1 MiB is released after consumption.
Unsupported formats preserve the existing empty-record callback. CSV/TSV cell rendering and path
encoding semantics stay unchanged, including invalid bytes covered in dedicated Unicode tests.
Empty-match commands do not allocate a cell-vector backing store just to set up output.

## In-memory mechanisms and costs

There are 64 direct row cases: CSV/TSV, zero/one/four/16 cells, zero/eight/64/4,096-byte prefixes,
and stateless/reused execution. Every case checks full encoded bytes before timing. Another 64
production-executor cases use CSV/TSV with one/four/16 columns plus aligned/Markdown controls,
zero/10/1,000/10,000 files and one/three jobs. All production fixtures use the in-memory filesystem.

Combined 10,000-entry engine session ratios have medians about 0.939 macOS and 0.968 Linux.
The direct zero-cell Linux encoder costs about 5.0 ns rather than 2.6 ns: preserve that roughly
1.9x tiny-call regression instead of hiding it inside aggregate gains. Real empty-root output
remains covered separately, and all slower primitive and engine cells remain in the raw evidence.

## Protocol and qualification

Build four release/ThinLTO variants with fixed compiler identities, configuration, source hashes,
link actions and executable hashes. Never overlap a host's timing with builds, tests, lint, fuzz
or heavy fixture creation. Rotate participant order and shuffle cases in each of three sessions.
Linux records one/three-CPU affinity and puts native fixtures in `/dev/shm`; macOS uses local
fixtures without claiming affinity. Do not infer release performance from instrumented runs.

Each host runs 128 kernel cases x four variants x three sessions: 1,536 case/session/variant
records and 13,824 raw timed repetitions. Across both hosts that is 3,072 records and 27,648
raw rounds. Native tests have 80 cases, all four variants and an identical baseline copy, three
sessions, nine measured rounds and one discarded warmup: 12,000 invocations per host, 24,000 total.
Every native invocation checks its exit status and complete stdout against independent path/name/
stem/extension construction, Python CSV quoting and explicit TSV escapes. Shapes include 10/1,000/
10,000 ordinary files, 1,000 punctuation-bearing names, 1,000 long names, and empty roots.

The five product-source files pass full 314-target release suites on both hosts and nine relevant
ASan suites on both, plus MSan/TSan on Linux. Actual four changed product translation units/header
checks and both manual benchmark checks pass on both hosts. Each partial strategy passes the five
affected release suites; all four participants pass all 128 new manual dry-run cases per host.
An initial Unicode test spelling and manual benchmark lint issues were corrected before timings.
The rebuild harness also learned to unlink read-only copied executables before replacing them.

After the parent coverage repair, the same candidate source bytes are requalified with full release
suites and eleven affected sanitizer suites (ASan both, MSan/TSan Linux). The parent only removes
unreachable private similarity checks and expands width-zero fresh-read tests; frozen timing sources
remain archived with their original identities rather than being relabeled as a later revision.

## Footprint

| Host  | Baseline full CLI bytes | Combined full CLI bytes | Increase |
| :---- | ----------------------: | ----------------------: | -------: |
| macos |               8,205,592 |               8,206,808 |   +1,216 |
| linux |              18,266,440 |              18,269,920 |   +3,480 |

The measured build checkout is `b11d01728cc5dae1a55c24e7259502214aa54c5b`, whose tree matches the
original threshold PR head. Post-parent qualification uses `236276ae4248face46cab61d3ec095c5546cbdaf`.
Actual build/source/compiler/link identities and later qualification provenance remain separate.

Run the committed manual cases with:

```sh
bazel run //xff/presentation/render:output_benchmark --config=clang_release -- --benchmark_filter=^output/encoded-row/
bazel run //xff/engine:expression_run_benchmark --config=clang_release -- --benchmark_filter=^engine/column-rows/
```

Reusable numeric summary cells, borrowed structured field values and streaming/backpressure
contracts remain separate candidates. This change adds no worker, lock, filesystem operation,
flag or changed default output. Raw/NUL listings retain their previously qualified buffer.

## Evidence

The compressed evidence retains all variants, complete raw rounds, independent oracle drivers,
qualified source snapshots, build/link identities, original failures/fixes, and validation logs.
Generated fixtures, executable/object payloads and canonical compile databases are excluded.
`column-row-reuse-20261104.json.gz`: 7,278,470 bytes, 588 labeled files; SHA-256
`48e1adc4bcdc4ba2db2d6db206b5ef87903d0b6607010b615daed86b26cd443b`.
