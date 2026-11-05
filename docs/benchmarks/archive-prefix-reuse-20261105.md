<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Reuse the archive session for format detection

Use the existing session cursor and its first normal block for XAR format detection, then deliver
that same block to libarchive. The normal path no longer opens a second cursor merely to read
four bytes. Nested-source replay and decoder work are reduced without adding an index, retained
member cache, extra buffer, thread, lock, dependency or user-facing setting.

## Contract and correctness

The session retains its existing 64 KiB shared-budget reservation. Its optional prefetched view
borrows the session's caller-owned buffer or owned fallback string until the first callback consumes
it. A successful seek invalidates that view. Automatic libarchive filter/format bidding consumes
the first read before member-data skips; a speculative pending-prefetch check in the skip callback
was removed after reviewing the pinned implementation. There is no test-only callback API,
coverage exclusion or lowered threshold.

A short first read is not assumed to mean EOF: one-, two- and three-byte initial reads retain the
existing independent complete four-byte format probe, while the original cursor replays its own
bytes exactly. Ordinary reads use one cursor; tests deliberately make redundant reopening fail.
Normal listing/content extraction works within exactly one 64 KiB reservation, without the former
four-byte transient probe reservation. Initial and later read errors, changing capabilities, sparse
content, skip failures, XAR seek behavior and shared reservation/owner lifetimes remain covered.
An oversized owned read is rejected as DataLoss before format detection, matching caller-owned
bounds validation. The removed second cursor's artificial reopen failure is intentionally no longer
an observable normal-path failure; actual required cursor errors remain errors.

## Protocol

The hosts are Apple M5 Pro (18 logical CPUs, arm64) and AMD Ryzen 9 9950X 16-Core Processor (ten allowed logical CPUs, x86_64). CPU requests in the native matrix remain 1/3, rather than the full host capacity.

The immutable private base is `8492322c38aad581f786c9da2ed36ed1386c87c6` (fixed #1102, including
#1101's owned-header lint repair). The following #1103 changes documentation only. These private
source and binary identities are experiment provenance, not final readiness of a newly pushed PR.
Parent, candidate and byte-identical parent-copy controls are frozen. Each host runs three balanced
sessions with nine repetitions and the fastest seven retained. Reported percentages are the median
of paired session elapsed-time ratios minus one; negative improves. Independently medianed times
need not divide to the paired ratio. Parent-copy drift is retained, never subtracted.

The in-memory matrix has 64 cases: TAR legacy/caller-owned/actual-memory/nested sources,
listing/last-member access, and stored/deflate ZIP sequential/memory/nested sources with names,
first/last/cycle operations, ten or 1,000 members and 128-byte content. Fixtures/index preparation
are outside timing. All 64 complete preflights pass before timed processes. Existing streamed ZIP
data-descriptor size differences remain explicit: names-only checks validate names/count/order,
not the independently tracked metadata-size bug.

The 48-case native matrix covers ordinary broad/deep 10/1k/10k files, stored ZIP 10/1k members,
compressed TAR 10/100 members, and nested TAR.gz containing stored ZIP 10/100 members; each uses
1/3 requested workers and status-only or comparison-count summary output. Both outputs explicitly select all
comparison statuses. Nested cases require `--archive-depth=2` and actual inner-member records.
Each of 4,320 CLI invocations per host, including warmups, checks the complete stdout hash/length,
exit status and empty stderr. Linux uses allowed CPU affinity; no Mac CPU-pinning claim is made.
Builds/tests do not overlap timing on the same host. Sanitizer timings are never performance data.

## Native command results

Cohort medians below summarize all cases, including slower controls; they are not weighted workload
speedups. Full cases, paired sessions and RSS samples are retained in the evidence archive.

| Cohort          | Cases | Mac elapsed | Mac copy | Linux elapsed | Linux copy |
| :-------------- | ----: | ----------: | -------: | ------------: | ---------: |
| Ordinary native |    24 |      +0.28% |   -0.04% |        +0.34% |     -0.01% |
| Stored ZIP      |     8 |      -1.57% |   +0.16% |        +1.56% |     -0.08% |
| Compressed TAR  |     8 |      -5.17% |   -0.01% |        -1.49% |     +0.50% |
| Nested TAR/ZIP  |     8 |     -18.55% |   -0.08% |       -20.76% |     +0.05% |

Selected nested cases are shown individually; the report keeps all ordinary/flat and in-memory
costs below rather than inferring a universal speedup from this useful replay reduction.

| Case                                   | Mac parent ms | Mac candidate ms | Mac change | Mac copy | Linux parent ms | Linux candidate ms | Linux change | Linux copy |
| :------------------------------------- | ------------: | ---------------: | ---------: | -------: | --------------: | -----------------: | -----------: | ---------: |
| `archive/nested-tar-zip/10/1/status`   |          7.32 |             6.34 |    -13.34% |   +0.77% |            2.07 |               1.68 |      -18.37% |     -0.18% |
| `archive/nested-tar-zip/10/1/summary`  |          7.36 |             6.29 |    -14.53% |   +0.10% |            2.10 |               1.70 |      -18.75% |     -0.20% |
| `archive/nested-tar-zip/10/3/status`   |          6.87 |             5.85 |    -14.37% |   -0.11% |            2.03 |               1.67 |      -17.82% |     -0.86% |
| `archive/nested-tar-zip/10/3/summary`  |          6.91 |             5.91 |    -14.76% |   -0.21% |            2.09 |               1.69 |      -19.53% |     -0.84% |
| `archive/nested-tar-zip/100/1/status`  |         50.34 |            38.73 |    -22.34% |   +0.75% |           17.05 |              13.19 |      -22.80% |     +0.47% |
| `archive/nested-tar-zip/100/1/summary` |         50.30 |            38.89 |    -22.37% |   -0.15% |           16.83 |              12.99 |      -22.84% |     +0.28% |
| `archive/nested-tar-zip/100/3/status`  |         49.80 |            38.60 |    -22.88% |   -0.06% |           17.45 |              13.57 |      -22.28% |     +0.68% |
| `archive/nested-tar-zip/100/3/summary` |         50.49 |            38.89 |    -22.80% |   -0.26% |           17.01 |              13.49 |      -21.99% |     +0.53% |

### Native peak RSS

Deltas summarize per-case median peak RSS across the 27 measured invocations of each binary. These are observed ranges, not reserved-budget accounting. Negative reduces RSS.

| Cohort          |  Mac delta MiB |   Mac copy MiB | Linux delta MiB | Linux copy MiB |
| :-------------- | -------------: | -------------: | --------------: | -------------: |
| Ordinary native | -0.28 to +0.33 | -0.36 to +0.25 |  -0.18 to +0.09 | -0.12 to +0.09 |
| Stored ZIP      | -0.02 to +0.28 | -0.23 to +0.30 |  -0.02 to +0.07 | -0.07 to +0.02 |
| Compressed TAR  | -0.03 to +0.00 | -0.03 to +0.14 |  -0.13 to +0.14 | -0.04 to +0.07 |
| Nested TAR/ZIP  | -0.28 to -0.19 | +0.00 to +0.02 |  -0.40 to -0.15 | -0.09 to +0.05 |

## Slower observations and limits

Largest Mac native elapsed costs (paired median; copied-parent drift):

- `native/broad/10/3/status`: +0.90% (copy -0.91%).
- `native/broad/10/1/summary`: +0.89% (copy +0.92%).
- `native/broad/10/1/status`: +0.68% (copy +0.30%).

Largest Linux native elapsed costs (paired median; copied-parent drift):

- `native/broad/10000/3/status`: +2.04% (copy +1.07%).
- `native/deep/10000/1/summary`: +1.93% (copy +0.31%).
- `archive/zip/1000/3/status`: +1.86% (copy +0.25%).

### Isolated memory costs

Largest Mac isolated costs; these are microsecond-scale cases, not CLI timings:

- `zip/stored/sequential/first/10/128`: 8.65 to 9.07 us, +15.84% (copy +3.54%). Sessions: +18.25%, -11.68%, +15.84%.
- `archive/memory/list/10/128`: 10.30 to 11.43 us, +11.73% (copy +4.14%). Sessions: +6.03%, +11.73%, +12.96%.
- `zip/stored/memory/cycle/10/128`: 55.18 to 60.12 us, +8.32% (copy +0.32%). Sessions: +8.32%, +9.16%, -0.08%.
- `archive/legacy/list/10/128`: 9.08 to 9.35 us, +7.79% (copy +0.37%). Sessions: -2.81%, +21.50%, +7.79%.

Largest Linux isolated costs; these are microsecond-scale cases, not CLI timings:

- `zip/deflate/memory/names-only/10/128`: 10.54 to 10.66 us, +0.51% (copy +0.38%). Sessions: +1.10%, -0.43%, +0.51%.
- `zip/deflate/memory/last/10/128`: 10.51 to 10.55 us, +0.16% (copy +0.61%). Sessions: -0.33%, +0.16%, +1.16%.
- `zip/stored/memory/names-only/10/128`: 8.35 to 8.31 us, -0.16% (copy -0.10%). Sessions: -0.16%, -3.17%, +0.89%.
- `zip/deflate/sequential/names-only/10/128`: 10.52 to 10.48 us, -0.21% (copy +0.87%). Sessions: -0.33%, -0.21%, -0.20%.

The isolated memory matrix is not uniformly faster. Its complete 64-case results and three
session observations are retained, including the costs listed above.
Do not treat unchanged-copy movement as permission to remove an unfavorable cell. Native 1k-member
ZIP still takes seconds on Mac: eliminating this redundant probe does not implement immutable ZIP
locator/index reuse or fix per-member replay scaling. Directory-subtree indexing and borrowed
member-key lookup are separate next experiments.

## Development failures retained

- The initial short-read test incorrectly treated an independent four-byte range-probe subspan as
  the stable complete 64 KiB callback address. Its failure and the corrected diagnostic scope remain.
- An initial owned-read prototype could accept an overreported read before detection. A targeted
  failing test reproduced the wrong InvalidArgument error; the final explicit size guard restores
  DataLoss and reservation release. Original source/results remain separate from guarded variants.
- The first nested native preflight omitted depth two. The unchanged parent failed the inner-member
  oracle before any native timers started. Corrected fixtures require actual inner members.
- The original speculative Skip-prefetch guard has 19/21 changed executable lines covered, with the
  only two unexecuted added lines being its unreachable early return. Reviewing the pinned callback
  order removed that redundant branch; the final binary has its own fresh matrices and checks.
- The initial Linux focused coverage filter missed canonical external-module aliases. Its core-only
  report remains, but actual archive coverage uses the repository's canonical extras filter and
  verifies an archive_reader.cc source record.

## Qualification and scope

All 329 release targets pass on both hosts, including all extensions and required real Linux FUSE.
Six focused suites (archive input, reader, filesystem, Apple PBZX, walk and engine run) pass ASan
on both hosts and MSan/TSan on Linux. Canonical Linux coverage instruments actual external archive
sources. Actual affected-translation-unit clang-tidy passes for the two changed files on both hosts.
Changed archive source coverage is 18/18 executable lines and 11/12 branches. Final rebuilt CLIs
are whole-file identical to the measured candidates, with source hashes unchanged. No debug-path normalization or reused check from another PR is claimed.

- Mac CLI: 8,273,608 bytes (+240); SHA-256 `821e14732321673d0f30edd2258497e8dffe3fbde5ed2c502ff4254e9f1358a8`.
- Linux CLI: 18,429,168 bytes (+112); SHA-256 `e913d4096deb6a52921716f847b5048c21ac1f9a13cb0fa0a2fbfc3d8ab8dbf1`.

The deterministic [lossless evidence archive](archive-prefix-reuse-20261105.json.gz) contains 655 text records
(327 Mac and 328 Linux), 6,205,788 compressed bytes and
74,677,130 uncompressed bytes. Every record's original byte count and SHA-256 was
verified; gzip round trip is exact. Archive SHA-256:
`667987334c8316334e29fa6f3e572556e1277058aa492330f778d4adb4fc0430`.

Executables, generated fixture trees and full compilation databases are excluded. Source snapshots,
all raw process/repetition data, initial/final forms, failed preflights, logs, canonical LCOV,
CPU provenance and byte-identical rebuilt CLI records remain available. No current-head public
CI result is inferred from this private qualification.
