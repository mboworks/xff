<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# O20o: reuse terminated native input paths for ReadLink, 2026-11-05

**Decision:** retain the implemented prototype and both completed screens without
adopting it. The required-target groups do not show a consistent cross-platform
benefit. No full measurement or complete 331-target release qualification was run
for this rejected candidate. A small source-level allocation reduction is not, by
itself, evidence of an end-to-end performance improvement.

## Isolated change and correctness

The parent is the qualified five-file compiled target-demand overlay on base
commit `d148059dc631f15d9dcf6ca70ea256c8bb0658ac` (#1110), subsequently
published as [#1113](https://github.com/mboworks/xff/pull/1113). Its frozen,
ordinary uninstrumented executable is used directly; this is not the unchanged
#1110 image. Completed target-demand measurements are not relabeled as results
for this additional change.

The prototype renames the existing terminated stat-path helper to
`TerminatedPath` and reuses it for the required `ReadLink` syscall input. Views
shorter than 512 bytes use its existing uninitialized short storage; longer
views retain the owning fallback. Its narrowly annotated `CStr` accessor exists
only to satisfy the POSIX NUL-terminated input ABI. Exact view lengths and embedded
suffix exclusion are preserved. The ordinary 1,024-byte target buffer and growing
fallback are unchanged; this does not include the short-target-buffer trial,
owned-target transfer, PGO, worker policies or new flags.

Both hosts pass five focused release suites, native filesystem ASan on Mac or
MSan on Linux, and actual lint for the two changed translation units. New tests
cover a missing link and exact 511/512/513-byte input views, including a suffix
outside the view. Repeated separators reach the same existing fixture inode;
these tests introduce no new native mutation path.

| Image            | Linux bytes | Mac bytes |
| :--------------- | ----------: | --------: |
| Qualified parent |  18,437,376 | 8,274,840 |
| Candidate        |  18,437,256 | 8,274,840 |
| Change           |        -120 |         0 |

## Screen contract

Each host retains the original 228 native cases unchanged and adds 16 explicit
long-input controls. A parent proof verifies that the latter preserve input paths
at least 600 bytes long; Python `readlink` independently verifies their targets.
All 488 parent/candidate preflights per host pass complete stdout, stderr and exit
checks. No original fixture is rewritten.

Each 244-case screen uses balanced three-repetition timing retaining the fastest
two, an excluded warm-up, and a separate byte-identical copied parent. There are
2,196 measured invocations and 732 excluded warm-ups per host, plus separately
recorded resume warm-ups. The native helper excludes Python startup. Linux uses
one/three affinity CPUs; Mac requests one/three workers without claiming pinning.
Every timed invocation checks its complete output oracle. Both screens pass.
Mac uses cached APFS and Linux RAM-backed fixtures; these are native CLI tests,
not an in-memory-VFS-only comparison.

Negative numbers mean lower elapsed time. Each value is the median across the
group's one-session case percentages. Copy controls remain separate and are not
subtracted from the candidate. These screens do not support claims about
three-session repeatability.

| Group                          | Cases | Linux candidate | Linux copy | Mac candidate | Mac copy |
| :----------------------------- | ----: | --------------: | ---------: | ------------: | -------: |
| compare-symlinks/status        |     4 |         -0.347% |    +1.339% |       +0.651% |  -0.347% |
| mixed/long-input-target-csv    |     8 |         +0.478% |    -0.493% |       +0.430% |  -0.967% |
| mixed/metadata                 |    20 |         +0.048% |    +0.179% |       -0.035% |  -1.045% |
| mixed/names                    |    20 |         -0.139% |    -0.214% |       +0.438% |  +0.258% |
| mixed/summary                  |    20 |         +0.042% |    +0.337% |       -1.304% |  +0.412% |
| mixed/target-csv               |    12 |         -0.919% |    -0.164% |       +0.490% |  -0.046% |
| regular/eager-csv              |    20 |         +0.104% |    -0.146% |       +0.809% |  +0.175% |
| regular/eager-summary          |    20 |         -0.260% |    -0.236% |       -0.571% |  -0.470% |
| regular/files                  |    20 |         +0.812% |    +0.203% |       +0.083% |  -0.149% |
| regular/native-name            |    20 |         +0.078% |    +0.588% |       -0.358% |  -1.028% |
| symlinks/long-input-target-csv |     8 |         +0.509% |    +0.141% |       +0.123% |  -1.097% |
| symlinks/metadata              |    20 |         +0.363% |    +0.025% |       +0.487% |  +0.304% |
| symlinks/names                 |    20 |         -0.812% |    +0.037% |       +0.564% |  +0.867% |
| symlinks/summary               |    20 |         -0.178% |    +0.232% |       +0.507% |  +0.050% |
| symlinks/target-csv            |    12 |         -0.671% |    -0.445% |       +0.054% |  +0.904% |

Required-target CSV medians are approximately 0.67%/0.92% faster on Linux
(symlink/mixed trees), but 0.05%/0.49% slower on Mac. Long-input fallback controls
are flat or slower in both screens. Ordinary controls are mixed and include
individual slower cells; the evidence preserves every case rather than reporting
only favorable groups. Further full qualification is not justified by this screen.

## Retained failures and recovery

The initial Linux lint found a real unsafe-looking `string_view::data()` syscall
boundary. The prototype was corrected to expose the terminated ABI pointer
explicitly; release/MSan checks and the failed production translation unit were
rerun. The unchanged test translation unit's earlier successful lint remains
recorded. Mac lint covers both final changed units.

An initial Linux parent-image copy hit the per-user `/tmp` quota, despite global
free space. It produced no valid executable or timing sample. The exact partial
prefix was verified before removal; eight completed older executable images were
losslessly compressed on disk, hash-verified before unlinking, and remain
restorable with original-path inventories. No source, fixture or measurement
record was discarded. The successful parent copy matches the frozen parent hash.

The new single-column empty-target oracle initially quoted the empty string while
the existing CLI emitted a blank row. The parent rejected that expectation before
any timing; only the new control oracle was corrected, with all original 228
oracles unchanged. A separate Mac launch from the user checkout was stopped by
the source guard before any warm-up or measurement; the unchanged driver was
restarted with the explicit private worktree. Original failures and successful
recoveries remain distinguishable in the archive.

## Evidence

The [lossless archive](readlink-input-path-20261105.json.gz) is 972,377 bytes,
SHA-256 `182ea98920087d9a5f50261b3b3def182112e78e95dace489a0c1d4084faafde`.
Its 81 named records reference 62 content-addressed payloads, reconstructing
8,961,779 bytes of source snapshots, scripts, contracts, tests, lint, preflights,
screens and diagnostics. Executables are inventoried by original host and hash,
not embedded. No runtime source from this rejected trial is installed.

Decompress gzip to JSON format
`xff-lossless-experiment-group-text-evidence-v1`. `contents[sha256]` contains
UTF-8 text; `experiments[].hosts[].records[]` supplies each original relative
path, byte length and content hash. Verify those before reconstructing a record.
All records, complete screens, focused checks and measured image identities were
verified when producing this archive. Initial failed source/check records are
kept under their own names and must not be confused with the final candidate.
