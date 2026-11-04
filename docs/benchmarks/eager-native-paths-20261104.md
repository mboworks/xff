<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Reuse eager native stat paths

Use names-only native directory listings even when metadata is required eagerly. Reuse one path
buffer per stat chunk, retain owned paths for actual directories and reconstruct leaf callback
paths from the unchanged parent/name. macOS native-case 100,000-file listings improve about 4-6%;
Linux long-name eager summaries improve about 9-15%. In-memory large eager names-only cases
independently improve on both hosts. Complete-path and metadata-free controls, small costs and
unchanged-parent drift remain visible. These are measured cases, not a universal speedup claim.

## Ownership and behavior

The backend must explicitly support the existing ReadDirNames contract: direct native child names,
empty entry paths and the unchanged parent prefix. Backends without that capability retain their
complete paths. The archive-all/mounter exclusion remains unchanged. This is not a reinterpretation
of arbitrary archive separators or an extension of the VFS public API.

Eager native records keep complete success/error observations. Each StatEntries call allocates its
scratch prefix only upon its first eager name; subsequent names resize and append into that buffer.
Successful actual directories retain a path for recursion, including entries whose hint changed.
Leaves and failures retain names and cached observations; existing callback scratch provides the
same visible paths. Eager empty-name fallback remains available for complete-path backends.

The existing coordinator still schedules 128-entry stat slices at the same worker, eager-demand,
512-entry, archive-source and filesystem-owner gates. Requests own the parent prefix needed by a
slice. Each worker owns its path buffer; no new lock, clock, thread, shared owner, per-leaf path
allocation or public flag is introduced. Read-ahead directory tasks remain leaf jobs. Lazy fields,
required metadata masks, symlink following, ordering, errors, traversal and mutation authorization
retain their previous semantics.

## Protocol

Compare against compact traversal metadata storage, with native segmented collection and uniform
native-case selection already present. Each host has a 22-case pilot, a 34-case expanded native
matrix and 12 long-name controls. Three balanced sessions use one discarded warmup and nine
measured invocations per participant, retaining the fastest seven. Parent, candidate and an identical
parent-copy participant are interleaved. Every native invocation checks exit status and complete
stdout against the parent. There are 1,980 + 3,060 + 1,080 = 6,120 invocations per host including
warmups. Ratios below one are faster; displayed ratios are medians of session ratios, rather than
ratios of the displayed median milliseconds. Parent-copy drift is retained without correction.

Native cases cover broad/nested 10/1k/100k trees, one/three workers, files/selective names,
absolute/relative roots, explicit exact/default-native case, eager CSV and on-demand size.
Long-name cases use 180-character ASCII prefixes at 10/10k/100k entries. The nested fixture has
two component levels rather than a long chain. Linux invocations use one/three allowed logical
CPUs; no macOS pinning is claimed. No competing same-host agent build/test/lint ran during timing.

## Native results

| Host  | Matrix | Case                                    | Parent ms | Candidate ms |  Ratio | Parent-copy ratio |
| :---- | :----- | :-------------------------------------- | --------: | -----------: | -----: | ----------------: |
| macOS | final  | exact/broad/10/1/files                  |     4.706 |        4.692 | 0.9888 |            0.9941 |
| macOS | final  | exact/deep/10/1/name-txt                |     4.742 |        4.915 | 1.0202 |            1.0037 |
| macOS | final  | exact/deep/1000/3/files                 |     8.233 |        8.310 | 0.9852 |            0.9998 |
| macOS | final  | exact/broad/100000/1/files              |    72.914 |       73.310 | 1.0036 |            1.0020 |
| macOS | final  | exact/deep/100000/3/name-txt            |    49.365 |       49.563 | 1.0005 |            1.0038 |
| macOS | final  | exact/eager/broad/10000/3/csv           |    20.656 |       19.826 | 0.9685 |            1.0022 |
| macOS | final  | exact/eager/deep/10000/3/csv            |    18.576 |       18.606 | 0.9998 |            1.0012 |
| macOS | final  | exact/on-demand/broad/10000/3/name-size |    12.933 |       12.817 | 1.0105 |            1.0090 |
| macOS | final  | default-native/broad/100000/1/name-txt  |   264.946 |      253.241 | 0.9577 |            0.9982 |
| macOS | final  | default-native/broad/100000/3/name-txt  |   183.866 |      173.045 | 0.9409 |            0.9997 |
| macOS | long   | long/100000/1/native-name               |   594.093 |      568.477 | 0.9571 |            1.0055 |
| macOS | long   | long/100000/3/native-name               |   452.629 |      423.060 | 0.9361 |            0.9965 |
| macOS | long   | long/100000/1/eager-summary             |   417.895 |      396.431 | 0.9485 |            1.0006 |
| macOS | long   | long/100000/3/eager-summary             |   273.401 |      251.219 | 0.9204 |            0.9937 |
| Linux | final  | exact/broad/10/1/files                  |     0.833 |        0.826 | 0.9921 |            0.9936 |
| Linux | final  | exact/deep/10/1/name-txt                |     0.862 |        0.857 | 1.0086 |            1.0045 |
| Linux | final  | exact/deep/1000/3/files                 |     2.016 |        2.085 | 1.0230 |            1.0127 |
| Linux | final  | exact/broad/100000/1/files              |    40.760 |       40.748 | 0.9990 |            1.0008 |
| Linux | final  | exact/deep/100000/3/name-txt            |    17.306 |       17.108 | 0.9907 |            1.0119 |
| Linux | final  | exact/eager/broad/10000/3/csv           |     6.944 |        6.337 | 0.9146 |            0.9647 |
| Linux | final  | exact/eager/deep/10000/3/csv            |     4.677 |        4.251 | 0.9176 |            1.0150 |
| Linux | final  | exact/on-demand/broad/10000/3/name-size |     3.223 |        3.205 | 0.9981 |            1.0058 |
| Linux | final  | default-native/broad/100000/1/name-txt  |    40.097 |       40.148 | 1.0013 |            1.0022 |
| Linux | final  | default-native/broad/100000/3/name-txt  |    40.341 |       40.300 | 0.9998 |            0.9978 |
| Linux | long   | long/100000/1/native-name               |    63.128 |       63.026 | 0.9987 |            0.9988 |
| Linux | long   | long/100000/3/native-name               |    63.351 |       63.102 | 0.9961 |            0.9963 |
| Linux | long   | long/100000/1/eager-summary             |   171.669 |      155.245 | 0.9061 |            1.0137 |
| Linux | long   | long/100000/3/eager-summary             |   127.636 |      108.914 | 0.8545 |            0.9947 |

macOS default-native wide gains repeat in the pilot (ratios 0.9589/0.9381 for one/three workers)
and expanded matrix (0.9577/0.9409). Linux native-case controls are mostly unchanged because their
existing uniform-case path already avoids eager stat. macOS nested ten-entry names cost 2.02%
(copy +0.37%); Linux nested 1k files with three workers cost 2.30% (copy +1.27%). Linux broad eager
CSV improves 8.54%, with a 3.53% improvement in the identical-parent copy; the nested eager CSV
improvement and long-name controls independently support the change. Do not attribute every small
control delta to the implementation.

## In-memory whole-engine controls

Extend the existing name_listing_benchmark with explicit eager summaries. Both variants use the
same extended benchmark source: 160 cases covering five counts (10/100/1k/10k/100k), one/three
workers, broad/nested fixtures, files/selective names, complete/name-only VFS policies and
hint/eager metadata. Fixture construction and parsing stay outside measurement; engine setup,
traversal, VFS calls, worker dispatch and output accounting stay inside. Every case checks exact
complete/name-only output before timing.

Each host has three balanced process-order sessions per participant and CPU count. Within each
process, benchmark/repetition order is randomized; nine repeats retain the fastest seven. These
are block-interleaved executables, not native round-robin invocations: 12,960 measured repetitions
per host. The complete-path control is deliberately unchanged in semantics, although it shares
StatEntries and the final binary layout. Its timing variation is reported rather than assumed zero.

| Host  | In-memory case                                  |  Ratio | Parent-copy ratio |
| :---- | :---------------------------------------------- | -----: | ----------------: |
| macOS | broad/eager/files/names/100000/1/real_time      | 0.8718 |            0.9960 |
| macOS | broad/eager/files/names/100000/3/real_time      | 0.8610 |            0.9832 |
| macOS | broad/eager/name-txt/names/100000/3/real_time   | 0.8659 |            1.0005 |
| macOS | deep/eager/files/names/100000/3/real_time       | 0.9881 |            1.0235 |
| macOS | deep/eager/name-txt/names/100000/1/real_time    | 0.9093 |            0.9947 |
| macOS | broad/eager/files/complete/100000/1/real_time   | 1.0106 |            1.0121 |
| macOS | deep/eager/name-txt/complete/100000/3/real_time | 1.0180 |            0.9976 |
| macOS | broad/files/names/100000/3/real_time            | 1.0172 |            0.9948 |
| Linux | broad/eager/files/names/100000/1/real_time      | 0.8535 |            1.0019 |
| Linux | broad/eager/files/names/100000/3/real_time      | 0.8114 |            1.0456 |
| Linux | broad/eager/name-txt/names/100000/3/real_time   | 0.8136 |            0.9922 |
| Linux | deep/eager/files/names/100000/3/real_time       | 0.9468 |            0.9921 |
| Linux | deep/eager/name-txt/names/100000/1/real_time    | 0.9145 |            1.0105 |
| Linux | broad/eager/files/complete/100000/1/real_time   | 0.8907 |            0.9959 |
| Linux | deep/eager/name-txt/complete/100000/3/real_time | 1.0064 |            1.0034 |
| Linux | broad/files/names/100000/3/real_time            | 0.9957 |            1.0026 |

macOS wide eager names-only cases improve about 12-14%; nested gains are smaller, including a
three-worker files result near neutral. Linux wide eager names-only cases improve about 15-19%;
nested cases about 5-9%. Linux complete-path broad files also improves 10.93% despite no intended
path-allocation change there, so that observation is not evidence for the proposed mechanism.
Linux metadata-free broad/name-txt/complete with one worker costs 4.87% (copy +0.59%); macOS
nested eager/name-txt/complete with three workers costs 1.80% (copy -0.24%). Retain those controls
and the full raw matrix; native metadata-free cases do not show a matching large regression.

## Memory and executable footprint

Native high-water RSS includes pre-exec footprint and is not an allocation count.

| Host  | Native case                            | Parent MiB | Candidate MiB | Parent-copy MiB |
| :---- | :------------------------------------- | ---------: | ------------: | --------------: |
| macOS | default-native/broad/100000/1/name-txt |     46.078 |        36.750 |          46.094 |
| macOS | exact/eager/broad/10000/3/csv          |      9.828 |         8.953 |           9.812 |
| macOS | exact/broad/100000/1/files             |     24.531 |        24.484 |          24.531 |
| Linux | default-native/broad/100000/1/name-txt |     19.555 |        19.598 |          19.586 |
| Linux | exact/eager/broad/10000/3/csv          |     10.148 |         9.227 |          10.125 |
| Linux | exact/broad/100000/1/files             |     19.590 |        19.559 |          19.586 |

Final release CLI sizes with the same enabled extras: macOS 8,250,696 -> 8,250,504 bytes (-192);
Linux 18,398,856 -> 18,401,176 bytes (+2,320). The rebuilt Linux CLI is byte-identical to its frozen
measured executable. macOS runtime instructions, data and resolved symbols are identical after
normalizing sandbox debug paths and dependent symbol/signature layout. Both original Mach-O
signatures validate; no binary was changed to manufacture a matching hash. The final lint fixes
only split benchmark registration, format the fixture and use the standard status-binding macro
with its direct test dependency; runtime source is unchanged. Full fingerprints, exact proofs and source snapshots are in the companion archive.

## Verification and decision

All 316 enabled release test targets pass on both hosts, with required FUSE tests on Linux.
Eight affected targets pass ASan on both hosts and MSan/TSan on Linux. The new eager names-only
parallel regression checks basic/birth fields, complete callback paths and sizes, successful and
failed stat observations, coordinator-only visitors, multiple stat threads and exactly one names-only
read over 1,024 children. Existing conformance covers all sort/link/metadata/worker/order choices,
lazy errors, root paths, archive fallbacks and deferred ownership. After the lint-only test edit,
the affected test passes release again on both hosts. Final benchmark registration has all 160
unchanged case names and all 32 ten-entry eager/hint/policy/worker cases pass exact-output smoke
checks on both hosts. Actual scoped Linux lint passes the walker, test and benchmark; normal Mac
commit/push hooks provide changed-C++ lint and policy checks. PR CI remains the merge gate.

Accept the change for repeated eager gains, reduced per-leaf path ownership and preserved parallel
stat behavior without a new public contract or threshold. Keep the measured small/control costs
visible. This does not address the remaining LocalFs stat-path copy required for a terminated C
string; evaluate short uninitialized scratch with a long-path fallback separately.

The lossless JSON.gz includes both hosts' source stages, native and memory observations, controls,
output hashes, drivers, final binary proofs and qualification logs. Fixtures, native stdout goldens
and frozen executables remain local and reproducible from the drivers and recorded hashes. No
ASan timings are used.
