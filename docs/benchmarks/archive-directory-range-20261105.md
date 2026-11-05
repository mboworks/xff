<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Bound archive directory lookup to its ordered subtree

Archive directories use the existing ordered node map to select their subtree before filtering
for direct children. A nonroot lookup no longer scans unrelated archive members. Root listing,
entry order and metadata, validation, path normalization and returned ownership remain unchanged.
No new retained index, cache, worker, lock, dependency or user-facing option is added.

## Contract and limits

For a validated nonroot directory K, select the map range `[K + '/', K + '0')`: the exclusive
upper bound changes only the appended slash to its immediate successor byte. One temporary string
is reused for both bounds. Existing direct-parent filtering still excludes grandchildren and
preserves lexicographic order; sibling prefixes such as `dir-empty`, `dir0` and `directory` stay
outside `dir/`. Empty directories and invalid/missing/nondirectory paths retain existing behavior.

The cost is O(log N + subtree nodes), not O(direct children). A single long chain can still require
quadratic work across all ancestor queries. Root queries still scan all nodes and flat archives
have no nonroot directory lookup to improve. Small-directory bounds have a real cost, retained in
these measurements rather than hidden behind an unmeasured threshold or hybrid strategy.

Five behavior cases pass on both unchanged parent and candidate on both hosts: root and exact
sorted children, lexical-prefix traps and grandchildren, stored/synthesized metadata, normalization
and error status, nested/custom/URI paths, and literal absolute/slash-root semantics. A shared
fixture-only memory TAR writer creates the new fixtures without host I/O. Each C++ test source
has its own test target. The manual benchmark is included in the canonical explicit lint index.

## Protocol

The hosts are Apple M5 Pro (18 logical CPUs, arm64) and AMD Ryzen 9 9950X 16-Core Processor (10 allowed logical CPUs, x86_64). Native CPU requests remain 1/3 on each.

The private parent is pushed #1104 commit `bb2a888de79dd526987a31b5b53e478b93aa125e`.
Private source and binary identities are provenance, not a substitute for a new public head's own
checks. Three balanced sessions use nine repetitions, retaining the fastest seven. Negative elapsed
change improves; percentages are medians of paired session ratios minus one, and independently
medianed times need not divide to that ratio. A byte-identical copied-parent control is retained;
its drift is never subtracted. Builds, sanitizers and lint do not overlap own-host timing.

The 44-case isolated lookup matrix covers flat, wide, deep and chain trees; root, one-directory and
ten-directory queries; and 10/100/1k/10k/100k stored members. Chains stop at 1k because actual path
storage is quadratic. Fixture creation, indexing and independent complete-entry preflight are
outside timed iterations. All parent and candidate preflights pass before timing. These lookup
results are not whole-command speedup claims.

The 66-case native matrix includes ordinary broad/deep 10/10k-file controls; flat TAR/ZIP controls;
wide/deep TAR/ZIP with 10/1k/10k members; names and CSV path/type/size/mode/mtime output; and two
complete 100k-member TAR traversals. Worker requests are 1/3, except the explicit 100k controls use
one worker. Every invocation checks full stdout hash and length, exit status and empty stderr.
The matrix retains 5,940 checked CLI invocations per host including ordinary warmups, plus any
separately recorded checked resume warmups. Linux uses allowed CPU affinity; Mac makes no CPU
pinning claim. Public merge-hook pauses occur between individual CLI invocations.

## Complete-command results

Cohort medians include every selected case, including slower cases, and are not weighted workload
speedups. All individual rows and sessions are retained in the evidence archive.

| Cohort                 | Cases | Mac elapsed | Mac copy | Linux elapsed | Linux copy |
| :--------------------- | ----: | ----------: | -------: | ------------: | ---------: |
| Ordinary native        |     8 |      +0.40% |   +0.24% |        +0.11% |     +0.14% |
| Flat archives          |     8 |      +0.11% |   +0.15% |        -0.70% |     -0.26% |
| Wide archives, <=10k   |    24 |      -3.56% |   +0.00% |       -10.62% |     -0.00% |
| Deep archives, <=10k   |    24 |      -4.16% |   -0.17% |       -10.94% |     -0.15% |
| Complete 100k archives |     2 |     -98.68% |   +2.23% |       -98.88% |     +0.28% |

Complete 100k traversals (full member output, not a leaf-query shortcut):

| Case                              | Mac parent ms | Mac candidate ms | Mac change | Linux parent ms | Linux candidate ms | Linux change |
| :-------------------------------- | ------------: | ---------------: | ---------: | --------------: | -----------------: | -----------: |
| `archive/tar/deep/100000/1/names` |      12714.95 |           187.05 |    -98.50% |        14075.55 |             171.32 |      -98.78% |
| `archive/tar/wide/100000/1/names` |      10821.55 |           122.60 |    -98.86% |         9260.80 |              94.42 |      -98.99% |

### Separate Linux metadata confirmation

The independent 12-case confirmation uses three balanced nine-repeat/fastest-seven sessions, the original frozen CLI binaries, complete-output checks and the same CPU-affinity rules. It is not pooled with the original 66-case matrix. The four small three-worker costs reproduce while serial and 10k controls still improve.

| Case                                | Original change | Confirmation change | Parent ms | Candidate ms | Confirmation copy |
| :---------------------------------- | --------------: | ------------------: | --------: | -----------: | ----------------: |
| `archive/tar/deep/1000/1/metadata`  |         -18.45% |             -18.90% |      3.20 |         2.65 |            -0.32% |
| `archive/tar/deep/1000/3/metadata`  |          +7.89% |             +11.21% |      3.18 |         3.49 |            -2.27% |
| `archive/tar/deep/10000/3/metadata` |         -40.22% |             -38.15% |     44.80 |        27.98 |            +1.43% |
| `archive/tar/wide/1000/1/metadata`  |         -20.87% |             -20.94% |      2.52 |         1.99 |            -0.13% |
| `archive/tar/wide/1000/3/metadata`  |         +13.46% |             +13.63% |      2.50 |         2.84 |            +3.08% |
| `archive/tar/wide/10000/3/metadata` |         -45.48% |             -44.86% |     35.56 |        19.59 |            -0.04% |
| `archive/zip/deep/1000/1/metadata`  |         -17.04% |             -16.75% |      3.61 |         3.01 |            -0.30% |
| `archive/zip/deep/1000/3/metadata`  |          +7.02% |              +6.44% |      3.77 |         4.01 |            -3.97% |
| `archive/zip/deep/10000/3/metadata` |         -37.47% |             -37.17% |     48.75 |        30.40 |            -0.33% |
| `archive/zip/wide/1000/1/metadata`  |         -18.10% |             -17.26% |      2.99 |         2.47 |            -0.28% |
| `archive/zip/wide/1000/3/metadata`  |         +10.12% |              +8.73% |      3.03 |         3.29 |            -0.10% |
| `archive/zip/wide/10000/3/metadata` |         -46.73% |             -47.84% |     43.95 |        22.93 |            -0.03% |

## Costs and controls

Largest Mac complete-command elapsed costs, including copied-parent drift:

- `archive/tar/wide/10/3/names`: 4.95 to 5.15 ms; +2.14% (copy +2.98%).
- `native/broad/10/3/names`: 4.36 to 4.50 ms; +1.77% (copy +2.29%).
- `archive/tar/wide/1000/3/metadata`: 9.91 to 10.02 ms; +1.05% (copy +0.47%).
- `archive/tar/deep/10/3/names`: 4.59 to 4.60 ms; +1.05% (copy +0.94%).
- `archive/tar/wide/10/3/metadata`: 6.96 to 7.03 ms; +1.02% (copy +1.39%).

Largest Mac isolated lookup costs, including all three paired sessions:

- `archive-directory/chain/ten/10`: 2.072 to 2.249 us; +7.60% (copy +1.15%). Sessions: +7.60%, +6.90%, +8.53%.
- `archive-directory/chain/root/100`: 0.742 to 0.786 us; +5.89% (copy +0.15%). Sessions: +6.82%, +5.75%, +5.89%.
- `archive-directory/deep/root/1000`: 5.811 to 6.016 us; +3.76% (copy +1.29%). Sessions: +3.76%, +1.73%, +4.21%.
- `archive-directory/chain/root/10`: 0.099 to 0.101 us; +3.73% (copy +0.15%). Sessions: +3.73%, +4.18%, +1.98%.
- `archive-directory/wide/one/10`: 0.631 to 0.648 us; +3.45% (copy -1.57%). Sessions: +3.45%, -3.44%, +4.77%.

Largest Linux complete-command elapsed costs, including copied-parent drift:

- `archive/tar/wide/1000/3/metadata`: 2.61 to 2.96 ms; +13.46% (copy +1.25%).
- `archive/zip/wide/1000/3/metadata`: 3.00 to 3.32 ms; +10.12% (copy +0.11%).
- `archive/tar/deep/1000/3/metadata`: 3.28 to 3.57 ms; +7.89% (copy -1.00%).
- `archive/zip/deep/1000/3/metadata`: 3.69 to 3.95 ms; +7.02% (copy -0.34%).
- `archive/tar/deep/10/1/metadata`: 1.06 to 1.07 ms; +1.25% (copy +0.25%).

Largest Linux isolated lookup costs, including all three paired sessions:

- `archive-directory/wide/ten/10`: 0.219 to 0.245 us; +11.88% (copy -0.51%). Sessions: +11.37%, +11.88%, +11.98%.
- `archive-directory/wide/one/10`: 0.218 to 0.244 us; +11.85% (copy +0.30%). Sessions: +11.85%, +11.78%, +12.04%.
- `archive-directory/chain/one/10`: 0.089 to 0.098 us; +10.36% (copy -0.80%). Sessions: +10.83%, +10.36%, +9.87%.
- `archive-directory/chain/ten/10`: 0.882 to 0.939 us; +6.54% (copy -0.19%). Sessions: +6.17%, +6.54%, +7.01%.
- `archive-directory/deep/root/100`: 0.375 to 0.395 us; +2.67% (copy +2.70%). Sessions: -3.51%, +2.67%, +5.48%.

### Peak RSS

Deltas are per-case median measured peak RSS, not reservation accounting. Full samples and parent-copy deltas remain in the archive.

| Host  | Candidate delta MiB range | Copied-parent delta MiB range |
| :---- | ------------------------: | ----------------------------: |
| macos |            -1.08 to +0.27 |                -0.28 to +0.28 |
| linux |            -0.13 to +0.15 |                -0.16 to +0.12 |

## Qualification

All 330 full release tests include every extension and required real Linux FUSE. Seven focused suites
(directory-range behavior, archive source buffer, filesystem, reader, Apple PBZX, walk and engine
run) pass ASan on both hosts and MSan/TSan on Linux. Canonical Linux coverage contains actual
external archive filesystem source records. New manual/test targets require a fresh canonical
compile database; actual affected-TU lint checks production, fixture header, behavior tests and
manual benchmark. No coverage exclusion, threshold reduction or prophylactic lint suppression is
introduced. The runtime qualification rebuilds match the original measured CLIs byte for byte. Later benchmark-only fixture refactoring has its own renewed isolated measurements and exact current rebuild checks. Both Linux original/current CLIs match exactly. On Mac, exact-artifact checks additionally prove the original/current difference is confined to two sandbox N_SO paths, their debug string offsets, dependent UUID metadata and independently verified signature hashes; all executable/data bytes and non-debug symbols match, and both OS strict signatures pass. The failed original/current whole-file checks are retained rather than reported as equality.

- Mac CLI: 8,273,608 bytes (+0); SHA-256 `536a2933e3f9d63112d6f7ea4d0f045e1b4b4fd02e589b8e2afb5d124387a55a`.
- Linux CLI: 18,430,944 bytes (+1,776); SHA-256 `731d04a57027197083508232323dcc80dc8f106948335a30fc8b190edb495e69`.

Changed production coverage: 7/7 executable lines and 2/2 branches.

## Development findings retained

The original 44-case microbenchmark matrix remains separate; its measured setup-source variant is not pooled with the refactored final binary. The initial manual fixture had cognitive complexity 46 (limit 30), missing arithmetic parentheses, an unused/nonconst benchmark iterator and a redundant `c_str`. Pure setup helpers and direct values fix all findings without suppression. Both initial failing lint logs and sources remain; the final manual benchmark has fresh parent/candidate 44-case matrices and correctness-only ASan preflights. The first rebuild check also found that the unchanged parent header had not been restored along with its source. Separate clean builds restore both. Strict identity failures and the exact Mac debug-metadata proofs are retained in the evidence; no sanitizer timing is used as performance data.

Borrowed member-key lookup and immutable archive indexing/replay are separate candidates; this change does not claim those improvements.

The deterministic [lossless evidence archive](archive-directory-range-20261105.json.gz) contains 866 original text records (437 macos, 429 linux), 6,832,861 compressed bytes and 283,461,757 uncompressed bytes. Every record byte count and SHA-256 and the gzip round trip are verified. Archive SHA-256: `e4ebf0d5600c475b9a164f586217a42fd4979b2385bb9109b9cafe0f835e77e4`.

Executable files, generated fixture trees and full compilation databases are excluded. Sources, drivers, complete raw measurements, initial/final benchmark variants, failures, logs, LCOV and exact artifact provenance remain available.
