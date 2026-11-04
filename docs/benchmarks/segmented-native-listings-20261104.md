<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Segmented native directory accumulation

Keep 4,096-entry segments for native directory accumulation. This is a small improvement:
large Linux native listings generally gain about 0.5-2.5%; macOS is mostly neutral, with some
small gains and costs. Native wide-directory peak memory also falls modestly. Reject the
256-entry trial because it repeatedly slows Linux heap-backed long-name cases by about 2.5-3%.
The larger policy removes that regression. This is not a universal speedup.

## Representation and limits

`LocalFs::ReadDirectory` accumulates owned entries in MBO's `SegmentedVector` with 4,096 entries
per segment and zero initial segment-pointer reservation. Appending no longer relocates existing
entries as a contiguous vector grows. The success path then allocates the existing public
`std::vector<Entry>` result once from the sized range, moving each entry into that result. Both
complete-path and names-only APIs use this implementation. No public API, metadata query, ordering,
error propagation, handle ownership, host-I/O route, safety policy, thread or flag changes.

The final contiguous conversion remains necessary for the existing VFS interface. Segments and
the result coexist during that conversion; this does not eliminate all copies, allocations or
peak memory. First-segment capacity is larger for small directories. The measured small-directory
costs are retained below. MBO is already a dependency; only its existing container target is added.
This does not compact traversal Metadata records or make archive backends use native readdir.

## Protocol

Both platforms compare the frozen uniform-case parent with 256- and 4,096-entry policies.
The initial short-name matrix has 34 cases and three participants (parent, 256, identical parent
copy); the second repeats all 34 with four participants (parent, 4,096, retained 256, parent copy).
Each has three balanced sessions, one discarded warmup and nine measured invocations per
participant and case, retaining the fastest seven. Long-name matrices have 12 cases and three
participants for each policy. The evidence contains all 9,300 invocations per host, including
warmups: 3,060 + 4,080 + 1,080 + 1,080. Every invocation verifies the exit code and complete stdout
against the parent, rather than timing a command whose output might have changed.

Short-name coverage includes broad/nested 10/1k/100k trees, one/three requested workers, files and
selective names, relative roots, eager CSV, on-demand size and two default-native-case controls.
The fixture called `deep` has two nested component levels, not a long directory chain. Most cases
use `--exact` to isolate native collection from volume-case policy. Long names have a 180-character
ASCII prefix and cover 10/10k/100k entries with one/three workers and files/name output. Linux
pins invocations to one/three allowed logical CPUs; macOS pinning is not claimed. No competing
same-host build, test or lint runs during timing. These are warm native filesystem measurements,
not filesystem-independent or cold-storage claims. An in-memory VFS would bypass this adapter.

Ratios are candidate / parent; below one is faster. Copy drift is shown separately and is never
silently subtracted. Rows report medians of per-session ratios, not ratios of the displayed medians.

| Host  | Case                                 | Workers | Parent ms | Candidate ms |  Ratio | Parent-copy ratio |
| :---- | :----------------------------------- | ------: | --------: | -----------: | -----: | ----------------: |
| macOS | exact/broad/10/files                 |       1 |     4.119 |        4.152 | 1.0061 |            1.0046 |
| macOS | exact/broad/100000/files             |       1 |    75.868 |       75.815 | 0.9998 |            1.0027 |
| macOS | exact/broad/100000/name-txt          |       1 |    71.831 |       71.452 | 0.9958 |            0.9968 |
| macOS | exact/broad/100000/name-txt          |       3 |    71.875 |       71.613 | 0.9959 |            1.0004 |
| macOS | exact/deep/100000/name-txt           |       3 |    48.908 |       48.974 | 1.0014 |            1.0011 |
| macOS | exact/relative/broad/100000/name-txt |       1 |    71.077 |       71.103 | 1.0004 |            1.0005 |
| macOS | default-native/broad/100000/name-txt |       1 |   258.187 |      256.343 | 0.9928 |            0.9941 |
| macOS | long/100000/files                    |       1 |   154.134 |      153.293 | 0.9945 |            1.0015 |
| macOS | long/100000/name-txt                 |       1 |   148.150 |      148.798 | 1.0044 |            1.0000 |
| macOS | long/100000/name-txt                 |       3 |   148.834 |      148.122 | 0.9967 |            0.9966 |
| Linux | exact/broad/10/files                 |       1 |     0.800 |        0.814 | 1.0059 |            1.0025 |
| Linux | exact/broad/100000/files             |       1 |    44.775 |       44.352 | 0.9890 |            0.9984 |
| Linux | exact/broad/100000/name-txt          |       1 |    44.023 |       43.410 | 0.9861 |            1.0003 |
| Linux | exact/broad/100000/name-txt          |       3 |    43.845 |       43.352 | 0.9911 |            0.9955 |
| Linux | exact/deep/100000/name-txt           |       3 |    18.174 |       17.809 | 0.9753 |            0.9957 |
| Linux | exact/relative/broad/100000/name-txt |       1 |    43.527 |       43.123 | 0.9916 |            1.0005 |
| Linux | default-native/broad/100000/name-txt |       1 |    43.893 |       43.652 | 0.9922 |            1.0024 |
| Linux | long/100000/files                    |       1 |    71.181 |       70.918 | 0.9947 |            1.0027 |
| Linux | long/100000/name-txt                 |       1 |    68.928 |       68.086 | 0.9880 |            0.9989 |
| Linux | long/100000/name-txt                 |       3 |    68.444 |       67.681 | 0.9888 |            1.0001 |

The retained 256-entry Linux long/100k ratios are about 1.025-1.029. With 4,096 entries they
become about 0.988-0.996. macOS long/100k selective names with one worker still cost 0.44%;
its parent copy is neutral. The 4,096-entry small-tree matrices also retain costs around 1-2%
in some cases. Acceptance trades these small costs against reduced relocation and modest large
native gains; it does not infer causality from noisy sub-percent cells. The complete per-case
analysis and raw session values are in the companion archive.

## Memory, footprint and verification

RSS is the native process high-water mark, including its pre-exec footprint. The table uses the
median of all 27 measured samples per participant for representative one-worker files cases.
It is not an allocation count or a proof that reserved capacity was fully touched.

| Host  | Tree / files    | Parent MiB | 4,096-entry MiB | Parent-copy MiB |
| :---- | :-------------- | ---------: | --------------: | --------------: |
| macOS | broad / 10      |      4.906 |           5.000 |           4.906 |
| macOS | broad / 100,000 |     37.062 |          35.922 |          37.062 |
| macOS | deep / 100,000  |      5.000 |           5.047 |           5.000 |
| Linux | broad / 10      |      6.676 |           6.703 |           6.715 |
| Linux | broad / 100,000 |     31.281 |          31.090 |          31.305 |
| Linux | deep / 100,000  |      6.730 |           6.691 |           6.711 |

Release CLI sizes are macOS 8,247,352 -> 8,248,056 bytes (+704) and Linux
18,359,008 -> 18,370,664 bytes (+11,656). Rebuilt release binaries exactly match the frozen timed
4,096-entry binaries on both hosts. The later committed regression adds no runtime changes.

All 316 enabled release targets passed on both hosts; Linux includes required real FUSE tests.
Eight affected targets passed ASan on both hosts and MSan/TSan on Linux. The added 5,003-entry
LocalFs regression compares complete and names-only listings with alternating short and heap-backed
long names, existing directories and symlinks, every expected name, path, type, source and writable
attribute. It creates files through the VFS mutation API. Existing tests retain empty/missing/error
and special-entry behavior. Its release/sanitizer qualification and changed-file lint are retained
in the evidence. Local development qualification does not replace current-head PR CI.

The lossless companion JSON.gz contains original source stages, drivers, every timing/RSS sample,
stdout hashes, binary fingerprints, per-case analysis and qualification logs. Fixture trees, complete
native stdout goldens and frozen executables remain local; their generators and hashes are retained.
Compact traversal records, first-device case-cache allocation, bulk metadata and pipeline work
remain separate candidates.
