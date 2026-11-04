<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Borrowed comparison category sequence

Retain the sequence policy. Targeted native named summaries improve about 1-3% and
use less memory on both hosts. Most other workflows are near neutral; two small
Linux control costs repeat. It is not a universal speedup or a streaming comparison.

## Storage and lookup

Comparison pairs already arrive exactly once in global bytewise path order from
the merge of deduplicated retained trees. The category map's only consumer iterates
it. Append compact path/category views instead of allocating another ordered-map
node and owning string pair for every comparison. There is no second sort or hash.

The complete trees own path bytes until summary consumption; labels point into the
existing static category table. Reserve the larger side's count only when per-entry
retention is active. It is a lower bound on union size, avoiding twice the capacity
for fully matching trees. Ordinary directly aggregated totals retain no categories.

Contribution lookup takes a string view and uses transparent inner-map comparison.
This avoids constructing temporary path keys, including the former conditional's
string materialization. Owning contribution keys and bytewise ordering remain.

Integer accumulation, grouping, sparse/zero cells, scopes, repeated summary requests,
errors, patches, archive ownership and effect timing keep their existing behavior.
No public flag, worker, lock, filesystem observation or dependency changes. Borrowing
here requires complete-tree lifetime; bounded streaming must not release those owners
until incremental consumption has its own qualified contract.

## Native results

Ratios are candidate/baseline elapsed time; below one improves. Each case uses the
median of three session ratios. Each session keeps the mean fastest seven of nine
rounds after one discarded, output-checked warmup. Cohorts take medians over the
1,000/10,000-file and 1/3-worker cases; named summaries also cover scope counts and
widths. The identical baseline copy is never normalized away.

| Workflow                         | macOS candidate | macOS copy | Linux candidate | Linux copy |
| :------------------------------- | --------------: | ---------: | --------------: | ---------: |
| Named summary: plain             |          0.9890 |     0.9997 |          0.9824 |     1.0043 |
| Named summary: aligned           |          0.9892 |     1.0011 |          0.9799 |     1.0006 |
| Named summary: markdown          |          0.9861 |     0.9993 |          0.9821 |     0.9984 |
| Named summary: csv               |          0.9873 |     1.0002 |          0.9801 |     1.0015 |
| Named summary: tsv               |          0.9877 |     0.9988 |          0.9784 |     0.9953 |
| Named summary: jsonl             |          0.9861 |     0.9981 |          0.9710 |     0.9946 |
| Extension-summary control, plain |          0.9960 |     1.0015 |          1.0059 |     0.9980 |
| Ordinary-summary control, plain  |          0.9904 |     0.9972 |          0.9984 |     1.0026 |
| Ordinary-summary control, JSONL  |          1.0013 |     0.9991 |          0.9942 |     0.9989 |
| Listing control                  |          0.9994 |     1.0027 |          1.0004 |     0.9997 |
| Status control                   |          0.9953 |     1.0010 |          1.0025 |     1.0018 |
| Patch control                    |          0.9998 |     1.0027 |          1.0045 |     1.0002 |

## Memory and binary size

RSS values are session medians of per-case median child peak RSS across all nine
rounds, independent of fastest-seven time selection, in MiB.

| Host  | Native case                      | Baseline MiB | Sequence MiB |
| :---- | :------------------------------- | -----------: | -----------: |
| macos | scoped/10000/1/plain/name/2/1000 |       23.297 |       22.859 |
| macos | scoped/10000/1/plain/name/6/1000 |       31.734 |       31.266 |
| linux | scoped/10000/1/plain/name/2/1000 |       20.938 |       20.336 |
| linux | scoped/10000/1/plain/name/6/1000 |       29.562 |       29.027 |

| Host  | Baseline CLI bytes | Sequence CLI bytes | Difference |
| :---- | -----------------: | -----------------: | ---------: |
| macos |          8,227,176 |          8,227,800 |       +624 |
| linux |         18,322,944 |         18,318,168 |     -4,776 |

## Slower cases and confirmation

Linux's 1,000-file, three-worker patch control is `1.0291` in the full matrix and
`1.0328` in an independent eight-case confirmation (copy `0.9995` in confirmation).
Session-median elapsed times are about `3.6890 -> 3.8366 ms`. A 1,000-file,
three-worker, extension-grouped TSV case repeats at `1.0289`, about
`2.1955 -> 2.2443 ms` (copy `1.0145`). These are costs, not improvements; medians of
ratios and absolute times are different statistics and need not divide exactly.

The initially slower ordinary 10k plain control does not repeat: confirmation is
`0.9749`, with a drifting copy at `0.9616`. The extension plain control confirms
near neutral at `1.0041`. Confirmation retains named 10k CSV/JSONL/plain gains at
`0.9813`, `0.9692`, `0.9705` respectively, plus listing/copy controls. Both original
and confirmation samples remain.

Mac's slowest native case is an empty TSV comparison summary at `1.0099` (copy
`1.0091`); its 10k one-worker status control is `1.0092` (copy `1.0033`). Every
slower native/kernel cell remains in the archive. The twelve-case initial screen
has larger gains in some kernels and drifting copies; it is separate evidence,
not substituted for the full native results.

## Qualification and provenance

Both complete 315-target release suites pass, including all extensions and required
Linux FUSE tests. Eight affected targets pass ASan on both hosts and MSan/TSan on
Linux. The changed production TU passes scoped clang-tidy on both; the preceding
canonical compiler database is reused because no headers, targets or compile flags
change. Instrumented runs verify correctness, never performance. Existing committed
comparison/summary tests cover the behavioral contract; no test mirrors the container.

Each host runs 240 whole-engine cases and 74 content/status/patch/totals cases, three
sessions and three participants, nine raw rounds: 25,434 raw kernel observations.
There are 190 native cases and 17,100 invocations including warmups, every exit and
complete stdout checked. Linux confirmation adds 720 verified invocations. All
16,514 declared fixture files per host match their size and SHA-256 values.
Native/main-engine worker requests are 1/3; the legacy content diagnostic retains
its existing 1/4 cases. Linux pins allowed logical CPUs; Mac makes no affinity claim.

The performance compiler matches each frozen parent exactly, LLVM 23.1.4. Source
and binary identities, full logs, link actions and slower controls remain. The first
compile attempt exposed an owning-string lookup signature; the final policy changes
that interface and its comparator together rather than adding a temporary conversion.

Frozen builds retain their actual parent `311c0c4ecb9744da30503aca192605dcf899c5e8`.
After #1088 merged, both worktrees synchronized to main
`9bd70fc9b4607cd8a586615c630706f85416e62c`; the parent trees and candidate source bytes
are identical. Three affected committed targets pass against that main. No sample is
relabeled. Local development results are distinct from fresh CI at the pushed PR head.

Packed evidence `comparison-category-sequence-20261104.json.gz`: 18,907,767 bytes,
2,203 labeled files, SHA-256
`11935c5aeccfce025c359e1f17cf2bcdff006094103fc9ea8a6cef246fd9011e`. Each retained file has its original byte size and hash.
Raw rounds, controls, failed attempts, exact sources, compiler/link proof, generators,
goldens, qualifications and explicit future design notes remain; executables,
generated trees and canonical compiler databases are excluded.

Completed frontiers, stage coordination/overlap, compact name-only native listings,
and unused ordinary-scope construction are separate candidates. This change does
not claim any of them implemented or qualified.
