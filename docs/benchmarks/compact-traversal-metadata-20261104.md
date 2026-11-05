<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Compact traversal metadata records

Keep compact sortable children with listing-owned complete metadata. Broad 100,000-file native
metadata-free listings improve about 5-6% on macOS and 8-9% on Linux. The representative macOS
peak RSS falls from 35.9 MiB to 24.4 MiB. In-memory whole-engine controls independently improve
large broad cases about 12-14% on macOS and 11-17% on Linux. This has costs: Linux eager CSV
controls repeatedly cost about 3.4%, and some nested/small controls stay neutral or slower.
Those observations remain in the decision and raw evidence. This is not a universal speedup.

## Ownership and semantics

A sortable child owns its path/name strings, type/source hints, original eager-success state and
an optional reference to complete metadata. A listing owns exact-reserved contiguous eager
records and any transferred stat-chunk vector owners. Their buffers remain stable while child
records move or sort. Eager-count admission uses the same existing type/follow/demand predicate;
it introduces a hint-count pass, not extra filesystem calls. Worker stat slices keep their original
size, dispatch, ordering and thread policy. Root metadata remains stack-owned for its subtree.

Lazy records use a listing-owned stable MBO segmented pool with 64 records per segment and no
initial pointer reservation. One coordinator owns a completed listing; no new lock, per-file heap
owner, reference counting, thread, public flag or host-I/O route is added. Cached errors and
successful type changes survive later traversal passes. Each callback sees one stable Metadata
reference: a lazily filled hint is updated in place, while the complete observation remains cached
in its listing. Eager visits avoid constructing an unused hint. Deferred/worker consumers keep
their existing owned Visit snapshots, including filesystem ownership.

Complete batch owners are explicitly move-only, so an accidental copy cannot leave children
referencing another batch's metadata. Sortable observers explicitly rebind their OptionalRef on
move; implicit assignment would be invalid for MBO's reference type. Preserve the prior basename
fallback for eager entries with empty listing names. Required fields, follow-link fallback, stat
order, lazy-error behavior, source tagging, recursion, pruning, archive handling and safety are
unchanged. This does not extend names-only path admission to eager metadata or add bulk stat.

## Protocol and retained policies

The frozen parent includes 4,096-entry native collection. Each host has an initial 22-case native
screen, a 34-case expanded matrix with parent/refined/first-policy/identical-parent participants,
12 long-name cases and an eight-case final ownership-declaration confirmation with the pre-declaration
runtime as an additional control. Three balanced sessions use one discarded warmup, nine measured
invocations and fastest seven. Every native invocation checks exit status and complete stdout
against the parent. Counts are 1,980 + 4,080 + 1,080 + 960 = 8,100 invocations per host including
warmups; failed count-assertion preflights are retained separately and not counted as measurements.

Short names cover broad/nested 10/1k/100k trees, one/three workers, files/selective names,
absolute/relative roots, eager CSV, on-demand size and default-native controls. Long names have
180-character ASCII prefixes at 10/10k/100k counts. The fixture called deep has two nested
component levels, not a long directory chain. Linux pins invocations to one/three allowed logical
CPUs; no macOS pinning is claimed. There is no competing same-host agent build/test/lint during
timing. Copy drift is retained and never silently removed. Ratios below one are faster.

The first policy eagerly initialized a hint even for complete observations. Retain it as a
control; the selected policy constructs that optional hint only when needed. The final type-contract
change adds explicit destructor/move-only declarations. Strict binary inspection finds identical
instructions on both hosts. Linux allocated data differs only in the validated loop-error
source_location line (848 -> 857); macOS code/data and resolved symbols are exact after that same
validated line and sandbox debug-path normalization. Original Mach-O signatures validate. Debug,
symbol and dependent build identities are retained, and the final binary is timed again in the
confirmation. No binary is modified to obtain a matching fingerprint.

## Native measurements

Rows show medians of session ratios; they are not ratios of the displayed median milliseconds.
The full matrix precedes the final ownership declarations; the confirmation directly measures the
final source and retains the previous executable as a control.

| Host  | Matrix       | Case                                    | Parent ms | Candidate ms |  Ratio | Parent-copy ratio |
| :---- | :----------- | :-------------------------------------- | --------: | -----------: | -----: | ----------------: |
| macOS | full         | exact/broad/10/1/files                  |     4.289 |        4.290 | 1.0028 |            1.0002 |
| macOS | full         | exact/broad/100000/1/files              |    76.544 |       72.088 | 0.9454 |            0.9970 |
| macOS | full         | exact/broad/100000/3/name-txt           |    71.582 |       67.855 | 0.9461 |            1.0037 |
| macOS | full         | exact/deep/100000/3/name-txt            |    48.934 |       48.739 | 0.9950 |            0.9994 |
| macOS | full         | exact/eager/broad/10000/3/csv           |    19.717 |       19.648 | 1.0005 |            1.0043 |
| macOS | full         | exact/eager/deep/10000/3/csv            |    17.540 |       17.498 | 1.0018 |            1.0012 |
| macOS | full         | exact/on-demand/broad/10000/3/name-size |    12.340 |       12.072 | 0.9756 |            1.0022 |
| macOS | full         | default-native/broad/100000/1/name-txt  |   251.364 |      253.730 | 1.0014 |            0.9988 |
| macOS | confirmation | exact/broad/100000/1/files              |    76.015 |       71.798 | 0.9441 |            1.0021 |
| macOS | confirmation | exact/eager/broad/10000/3/csv           |    19.882 |       19.973 | 1.0046 |            1.0011 |
| macOS | confirmation | exact/eager/deep/10000/3/csv            |    18.222 |       18.146 | 0.9934 |            0.9963 |
| macOS | confirmation | exact/on-demand/broad/10000/3/name-size |    12.721 |       12.402 | 0.9711 |            1.0013 |
| macOS | confirmation | default-native/broad/100000/1/name-txt  |   257.875 |      258.939 | 1.0065 |            1.0020 |
| Linux | full         | exact/broad/10/1/files                  |     0.814 |        0.817 | 1.0000 |            0.9938 |
| Linux | full         | exact/broad/100000/1/files              |    44.824 |       40.706 | 0.9100 |            0.9964 |
| Linux | full         | exact/broad/100000/3/name-txt           |    43.693 |       40.017 | 0.9164 |            0.9931 |
| Linux | full         | exact/deep/100000/3/name-txt            |    17.418 |       16.686 | 0.9580 |            1.0071 |
| Linux | full         | exact/eager/broad/10000/3/csv           |     6.083 |        6.558 | 1.0342 |            1.0225 |
| Linux | full         | exact/eager/deep/10000/3/csv            |     4.320 |        4.377 | 1.0012 |            0.9938 |
| Linux | full         | exact/on-demand/broad/10000/3/name-size |     3.321 |        3.246 | 0.9681 |            1.0048 |
| Linux | full         | default-native/broad/100000/1/name-txt  |    43.788 |       40.338 | 0.9211 |            0.9949 |
| Linux | confirmation | exact/broad/100000/1/files              |    44.433 |       40.483 | 0.9083 |            1.0037 |
| Linux | confirmation | exact/eager/broad/10000/3/csv           |     6.194 |        6.409 | 1.0346 |            0.9714 |
| Linux | confirmation | exact/eager/deep/10000/3/csv            |     4.212 |        4.316 | 1.0349 |            0.9994 |
| Linux | confirmation | exact/on-demand/broad/10000/3/name-size |     3.273 |        3.140 | 0.9593 |            1.0021 |
| Linux | confirmation | default-native/broad/100000/1/name-txt  |    43.531 |       40.009 | 0.9191 |            0.9746 |

Linux eager broad CSV is 3.42% slower in the full matrix and 3.46% slower in confirmation;
parent-copy drift is +2.25% and -2.86% respectively. Nested eager CSV changes from +0.12% to +3.49%.
Retain both costs and their absolute times rather than attributing every small delta to the code.
macOS default-native one-worker confirmation costs 0.65%, with +0.20% copy drift. Its existing
volume-case policy still requires eager metadata and owned paths, so no large default-native gain
is claimed. Acceptance trades explicit small/eager costs against wide metadata-free gains and
substantial memory reduction. Eager names-only paths remain a separate candidate.

## In-memory whole-engine controls

The existing committed `name_listing_benchmark` runs the same broad/nested trees with both
complete-path and names-only VFS policies. Parsing and fixture construction stay outside timing;
per-run engine setup, traversal, workers and output accounting stay inside. Each case checks exact
full/name-only output first. Parent/candidate/parent-copy executables use three balanced process-order
sessions, randomized case/repetition interleaving within each process, nine repeats and fastest
seven. This is block interleaving between executables, not native round-robin invocation ordering.
All 80 cases per host (10/100/1k/10k/100k, one/three workers, two trees, files/names, both VFS
policies) retain raw observations: 6,480 measured repetitions per host.

| Host  | In-memory case                             |  Ratio | Parent-copy ratio |
| :---- | :----------------------------------------- | -----: | ----------------: |
| macOS | broad/files/names/100000/1/real_time       | 0.8572 |            1.0013 |
| macOS | broad/name-txt/complete/100000/3/real_time | 0.8825 |            1.0051 |
| macOS | deep/files/names/100000/3/real_time        | 0.8178 |            0.9921 |
| macOS | deep/name-txt/complete/100000/1/real_time  | 0.9161 |            0.9976 |
| Linux | broad/files/names/100000/1/real_time       | 0.8723 |            0.9950 |
| Linux | broad/name-txt/complete/100000/3/real_time | 0.8838 |            0.9757 |
| Linux | deep/files/names/100000/3/real_time        | 1.0120 |            1.0018 |
| Linux | deep/name-txt/complete/100000/1/real_time  | 0.9705 |            0.9992 |

Linux nested names-only files with three workers cost 1.20% (copy +0.18%); do not hide that control.
These results isolate filesystem access speed but still include production VFS and engine work.
The kernel data does not predict every native command's elapsed time or storage behavior.

## Memory, footprint and verification

Native high-water RSS includes the pre-exec process footprint. It is not an allocation count.

| Host  | Native case                   | Parent MiB | Candidate MiB | Parent-copy MiB |
| :---- | :---------------------------- | ---------: | ------------: | --------------: |
| macOS | exact/broad/100000/1/files    |     35.922 |        24.391 |          35.922 |
| macOS | exact/deep/100000/3/files     |      6.938 |         6.578 |           6.969 |
| macOS | exact/eager/broad/10000/3/csv |      9.672 |         9.500 |           9.688 |
| Linux | exact/broad/100000/1/files    |     31.066 |        19.590 |          31.098 |
| Linux | exact/deep/100000/3/files     |      7.891 |         7.453 |           7.840 |
| Linux | exact/eager/broad/10000/3/csv |     10.219 |        10.117 |          10.133 |

Final release CLI sizes: macOS 8,248,056 -> 8,250,696 bytes (+2,640), Linux
18,370,664 -> 18,398,856 bytes (+28,192). Footprints include the same enabled extras.

All 316 enabled release targets pass on both hosts, including required Linux FUSE tests.
Eight affected targets pass ASan on both hosts, plus MSan/TSan on Linux. After the final type-only
ownership declarations, the four traversal/run/case targets pass release and all applicable
sanitizers again; actual walker lint passes on both hosts. Linux also lints the changed test TU.
The normal Mac hooks provide the final changed-test lint and all policy/formatting checks.
The committed regressions pass on both the parent and candidate: lazy type changes remain visible
after 131 cached entries cross segment boundaries, and eager empty names keep their basename
fallback. Existing suites cover cached failures, archive ownership, birth-time fields, sorting,
pruning, read-ahead, parallel stat errors and deferred output. PR CI remains the final merge gate.

The lossless companion JSON.gz retains source stages, all raw native/memory observations,
parent-copy controls, failed setup/compile diagnostics, exact-output hashes, binary fingerprints,
strict identity proofs and qualification logs. Frozen binaries and native stdout goldens remain
local; fixture generators and hashes are retained. No ASan timing is used. Larger pipeline,
streaming/frontier, archive-index and native-case worker decisions remain independent.
