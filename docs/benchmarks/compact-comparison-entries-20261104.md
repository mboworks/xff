<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Compact retained comparison entries

Retain compact comparison entries. Larger native comparisons improve modestly on both
hosts, Linux gains more, and retained memory falls. Ordinary summaries are broadly neutral
on macOS and have small Linux costs. Some tiny comparisons are slower. This is not a
universal speedup and does not implement completed-directory streaming.

## Representation and behavior

Matching and side summaries consume full VFS metadata during traversal. Later comparison
and patch generation need only type, source, size and mode; `ComparisonMetadata` names
that smaller contract instead of presenting incomplete metadata as a full VFS record.

`ComparisonEntries` copies each retained path into a lazy arena. Relative keys borrow a
suffix of that owned path where possible; unrelated/root keys are copied separately.
Stable segmented records retain backend ownership. A hash index rejects duplicate keys
before copying, preserving the first entry. Sorting moves only key/index records.

Both walks still finish before comparisons begin. Bytewise relative-path order, directory
and file categories, comparison/read ordering, error precedence, patches, side summaries,
archive ownership and effect timing remain unchanged. There are no new workers, locks,
clock reads, public flags, dependencies or filesystem operations. Arena chunks grow no
larger than the existing 1 MiB policy for ordinary allocation, with MBO's separate oversized
allocation path; this is not a maximum path or tree size.

## Native wall-time results

Ratios are candidate time divided by baseline time; below one improves. Each case uses
the median of three session ratios, with the mean fastest seven of nine measured rounds
per session after one discarded, checked warmup. Cohorts below take medians across the
1,000/10,000-file cases and 1/3-worker requests. Scoped summaries also cover requested
widths and scope counts. The identical baseline-copy hash is verified and its drift is
shown separately, never normalized away.

| Workflow                           | macOS candidate | macOS copy | Linux candidate | Linux copy |
| :--------------------------------- | --------------: | ---------: | --------------: | ---------: |
| Status, plain                      |          0.9829 |     0.9983 |          0.9529 |     1.0089 |
| Status, JSONL                      |          0.9873 |     1.0010 |          0.9581 |     0.9886 |
| Git patches, plain                 |          0.9911 |     1.0002 |          0.9690 |     0.9975 |
| Scoped summary, plain              |          0.9897 |     1.0019 |          0.9683 |     0.9916 |
| Scoped summary, aligned            |          0.9909 |     0.9988 |          0.9594 |     0.9968 |
| Scoped summary, markdown           |          0.9923 |     0.9987 |          0.9696 |     1.0094 |
| Scoped summary, csv                |          0.9909 |     1.0011 |          0.9570 |     0.9884 |
| Scoped summary, tsv                |          0.9941 |     1.0006 |          0.9698 |     0.9934 |
| Scoped summary, jsonl              |          0.9915 |     1.0006 |          0.9765 |     1.0034 |
| Ordinary summary control, plain    |          1.0044 |     0.9949 |          1.0097 |     1.0051 |
| Ordinary summary control, aligned  |          1.0014 |     1.0026 |          1.0107 |     1.0004 |
| Ordinary summary control, markdown |          0.9989 |     1.0036 |          1.0162 |     1.0059 |
| Ordinary summary control, csv      |          0.9974 |     0.9980 |          1.0089 |     1.0031 |
| Ordinary summary control, tsv      |          1.0036 |     1.0005 |          1.0055 |     0.9933 |
| Ordinary summary control, jsonl    |          0.9984 |     0.9962 |          1.0189 |     1.0005 |

## Larger in-memory totals

The same diagnostic source extends the existing totals matrix to 100,000 entries.
These rows use three workers. They exercise complete RunFind comparison and summary
execution over controlled immutable in-memory trees, not filesystem throughput.

| Workload                                                | macOS candidate | macOS copy | Linux candidate | Linux copy |
| :------------------------------------------------------ | --------------: | ---------: | --------------: | ---------: |
| totals/ext/compare                                      |          0.9103 |     0.9354 |          0.6485 |     0.9924 |
| totals/ext/left-total,left-only                         |          0.9478 |     0.9828 |          0.8298 |     1.0162 |
| totals/overall/compare                                  |          0.8604 |     0.9274 |          0.6493 |     0.9832 |
| totals/overall/left-only,right-only,different,identical |          0.9405 |     0.9556 |          0.8273 |     0.9977 |
| totals/type/compare                                     |          0.9196 |     0.9004 |          0.6397 |     0.9930 |

Mac baseline copies drift substantially in some 100k cells. Do not attribute the entire
raw candidate ratio there to this change. The complete smaller-scale, one-worker and
unchanged-format records remain alongside these selected large rows.

## Memory and binary footprint

Peak resident memory is measured for each native child. Values below are session medians
of per-case median RSS across all nine rounds, in MiB; they are independent of the
fastest-seven wall-time selection.

| Host  | Native case                      | Baseline MiB | Compact MiB |
| :---- | :------------------------------- | -----------: | ----------: |
| macos | comparison-status/10000/1/plain  |       15.859 |      14.453 |
| macos | scoped/10000/1/plain/name/2/1000 |       24.672 |      23.297 |
| macos | scoped/10000/1/plain/name/6/1000 |       33.141 |      31.734 |
| linux | comparison-status/10000/1/plain  |       13.762 |      12.469 |
| linux | scoped/10000/1/plain/name/2/1000 |       22.500 |      20.930 |
| linux | scoped/10000/1/plain/name/6/1000 |       31.016 |      29.566 |

| Host  | Baseline CLI bytes | Compact CLI bytes | Difference |
| :---- | -----------------: | ----------------: | ---------: |
| macos |          8,207,000 |         8,227,176 |    +20,176 |
| linux |         18,269,792 |        18,322,944 |    +53,152 |

The additional code and small-case arena costs are explicit tradeoffs. No maximum RSS
or allocation-count improvement is inferred solely from a smaller C++ record.

## Slower controls and confirmation

Linux ordinary-summary cohorts cost roughly 0.5-1.9% in the full matrix. Its initially
slowest ordinary JSONL cell, 10,000 files and three workers, is `1.0398`, but its baseline
copy is also `1.0377`. A separate eight-case native confirmation gives `1.0102` for that
cell; the original result remains retained. Large aligned output confirms near neutral
at `1.0020`.

Small comparison costs do repeat. Ten-file, one-worker comparison-summary commands
confirm around `1.0140` plain and `1.0279` Markdown. Session-median elapsed times are
about `0.9652 -> 0.9728 ms` and `0.9431 -> 0.9614 ms`; these ratios and absolute medians
are distinct statistics and need not divide exactly. A ten-file, three-worker, two-scope
Markdown case confirms at `1.0206` (about `1.0260 -> 1.0514 ms`). Do not label these gains.
The same confirmation retains larger status and patch wins and all unchanged-copy values.

Mac's slowest native ordinary cell is the 1,000-file, three-worker Markdown case at
`1.0202` (copy `1.0130`). Its ten-file plain comparison summary is `1.0192` (copy `0.9907`).
All slower native and in-memory cases are retained, including empty inputs and variable
session results. No conversion, output-format or worker-policy optimization is claimed.

## Alternative and validation

The implemented metadata-only control keeps the original ordered map and owning strings.
It improves less consistently than the compact representation in the initial twelve-case,
three-session screen on each host. Its sources, binaries' hashes, exact-output checks and
raw samples are retained separately. Screen samples are not pooled with final qualification.

New tests cover temporary/long path storage, multiple arena blocks and segments, empty
keys, non-suffix keys, root keys, punctuation-sensitive ordering, repeated sorting and
insertion, duplicate first-wins metadata, all retained fields, and backend lifetime.

Both 315-target release suites pass, including all extensions and required Linux FUSE
coverage. Eight affected targets pass ASan on both hosts and MSan/TSan on Linux. Scoped
clang-tidy checks seven translation units and affected headers. One inefficient test
concatenation was fixed; the failed logs remain, release/sanitizer checks were revalidated,
and only the changed test TU needed another lint pass. Instrumented runs verify correctness,
never performance.

Final buildifier changes only the new test's dependency ordering. Affected tests pass
again in release/ASan, plus Linux MSan/TSan. All three Linux runtime binaries remain exactly
byte-identical. Mac rebuilds differ by sandbox-directory digits in symbol-table source
strings and dependent ad-hoc signatures only: all other bytes, including code/data and load
commands, match exactly, and both original signatures verify. The comparison is read-only;
no binary is altered or sample relabeled. Exact original and rebuilt hashes are retained.

Both hosts use identical compiler binaries to their respective frozen parents, LLVM 23.1.4.
Local development qualification is separate from fresh CI at the pushed PR head. The parent
is merged main `f7bdc5d248e76a54278aef5030c0a3468505bd2f`, whose post-merge CI succeeds.

## Protocol, evidence and remaining work

Per host, 240 engine cases produce 19,440 raw rounds; 74 content/status/patch/totals cases
produce another 5,994. The legacy content diagnostic retains its existing 1/4-worker cases;
totals and the main engine matrix use 1/3. There are three sessions and three participants
in each matrix. The 190 native cases produce 17,100 invocations including warmups, with
every exit and complete stdout checked. Linux confirmation adds 720 native invocations.
The native driver checkpoints each completed case as JSONL and writes the aggregate once
at completion, outside timed child execution.

All 16,514 declared native fixture files on each host match their retained lengths and
SHA-256 values. Native status/patch oracles use the frozen parent plus the committed
semantic suite; inherited summary machine-format models/goldens remain retained. Linux
pins to the first requested allowed logical CPUs. Mac records worker requests without
claiming CPU affinity. There are no concurrent same-host builds, lints or tests during timing.

O16b completed frontiers, bounded streaming, coordinated stage workers and overlap remain
open. A separate candidate can replace the category map with appended borrowed records:
its sole consumer iterates entries already produced once in sorted comparison order.
Avoid another sort/hash table, preserve exact accumulation order, and measure it separately.
Further independent controls can test moving archive-owner records and a smaller initial
comparison arena before accepting either policy.

Packed evidence: `compact-comparison-entries-20261104.json.gz`, 21,577,857 bytes,
2,336 labeled files; SHA-256
`28b5fdd0b25ef10653aaf1f7a71103a6b2b60a3f55a7ff50e215020ef535390b`. Every retained file has its original byte count and hash.
All raw rounds, controls, failed attempts, selected/alternative sources, fixture generators,
goldens, compiler/link provenance and qualification results remain. Executables, generated
fixture trees and canonical compiler databases are excluded.
