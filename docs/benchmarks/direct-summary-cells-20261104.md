<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Direct summary-cell encoding

CSV/TSV summary export now encodes identity fields directly from their borrowed views and
consumes each fixed-size numeric array immediately. `DelimitedRow` owns the encoded output;
its synchronous `Append` never retains a cell view, and `Finish` returns an owning string.
This removes the intermediate cell vectors and repeated copies of roots, labels and group
names. The existing listing and column encoders, numeric formatting, sparse-cell semantics,
ordering, workers, VFS and safety policies are unchanged.

## Decision and native results

Retain direct cell encoding. It produces useful native CSV/TSV improvements on both hosts
without a new dependency or larger CLI. Gains are clearest in macOS ordinary summaries and
comparison exports on both hosts. Ordinary Linux TSV is close to baseline in the full cohort;
individual slower cases remain. This is not a universal speedup.

Ratios are candidate wall time divided by the paired baseline; below one improves. Each case
uses the median of three session ratios. Each session retains the mean fastest seven of nine
measurements after a correctness-checked discarded warmup. Cohorts take medians across the
1,000/10,000-file cases and 1/3-worker requests. The baseline-copy executable has exactly the
baseline hash; its drift is shown separately and is never normalized away.

| Host  | Workflow | Format   | Direct cells | Baseline copy | Cases |
| :---- | :------- | :------- | -----------: | ------------: | ----: |
| macos | ordinary | plain    |       1.0000 |        1.0017 |     4 |
| macos | ordinary | aligned  |       0.9981 |        1.0000 |     4 |
| macos | ordinary | markdown |       0.9972 |        0.9970 |     4 |
| macos | ordinary | csv      |       0.9690 |        1.0000 |     4 |
| macos | ordinary | tsv      |       0.9655 |        0.9999 |     4 |
| macos | ordinary | jsonl    |       1.0000 |        0.9995 |     4 |
| macos | scoped   | plain    |       0.9995 |        0.9999 |    12 |
| macos | scoped   | aligned  |       0.9995 |        0.9998 |    12 |
| macos | scoped   | markdown |       1.0058 |        1.0023 |     8 |
| macos | scoped   | csv      |       0.9739 |        0.9983 |     8 |
| macos | scoped   | tsv      |       0.9749 |        0.9992 |     8 |
| macos | scoped   | jsonl    |       1.0004 |        0.9991 |     8 |
| linux | ordinary | plain    |       1.0084 |        1.0038 |     4 |
| linux | ordinary | aligned  |       1.0102 |        1.0034 |     4 |
| linux | ordinary | markdown |       1.0183 |        0.9972 |     4 |
| linux | ordinary | csv      |       0.9838 |        0.9942 |     4 |
| linux | ordinary | tsv      |       0.9992 |        1.0049 |     4 |
| linux | ordinary | jsonl    |       1.0029 |        1.0014 |     4 |
| linux | scoped   | plain    |       1.0066 |        1.0030 |    12 |
| linux | scoped   | aligned  |       1.0029 |        1.0053 |    12 |
| linux | scoped   | markdown |       1.0027 |        1.0042 |     8 |
| linux | scoped   | csv      |       0.9777 |        1.0044 |     8 |
| linux | scoped   | tsv      |       0.9830 |        0.9999 |     8 |
| linux | scoped   | jsonl    |       0.9973 |        1.0030 |     8 |

## Slower cases and confirmation

The complete Linux matrix contains an unchanged-format 10,000-file, three-worker Markdown
kernel with ratios `1.1131`, `1.1135`, `1.0026`. A separate focused confirmation using the same
immutable binaries gives `0.9907`, `1.0067`, `1.0083`; the original 11% median does not repeat.
The confirmation is additional evidence, not a replacement for the original results.

A smaller Linux aligned-output cost does repeat: the 1,000-file, three-worker, six-scope,
110-column native case has a full-matrix ratio `1.0383` and confirmation ratio `1.0282`.
Session-median elapsed times are about `4.602 -> 4.778 ms` initially and
`4.523 -> 4.673 ms` in confirmation. Those are modest absolute costs, not improvements.
The baseline-copy ratios for that case are `1.0123` and `0.9980` respectively.

The Linux focused confirmation also retains an eight-case kernel matrix and eight native
cases. Its three-worker six-scope CSV kernel is `1.0523` while native CSV/TSV comparison
commands are `0.9704` / `0.9712`; these distinct workloads must not be conflated. Large
ordinary native CSV/TSV commands confirm at `0.9836` / `0.9701`. Some empty/startup cases
cost about 1-2%, around tens of microseconds. All raw control and slower cells remain.

On macOS the slowest native scoped case is a 1,000-file, three-worker plain table at
`1.0129` (baseline copy `1.0031`); the slowest listing control is `1.0077` (copy `1.0068`).
A small extension-control CSV kernel has a `1.0489` median with very uneven session ratios
`1.1843`, `0.9600`, `1.0489`. The targeted native CSV/TSV cohorts above are more stable;
no blanket gain is attributed to unchanged formats.

## Retained alternatives

- Floating `std::to_chars` was implemented and checked against the existing formatter,
  including all CLI precisions, unsigned edge values and rounding modes. The full Linux
  commands gave only marginal summary gains, repeatable 1-2.4% listing costs, and 117,480
  additional CLI bytes. It is rejected. Apple's floating overload is strictly unavailable
  below macOS 13.3: the shipping macOS 11 build retains Abseil and is not claimed as a
  conversion speedup. Separate 13.3 diagnostic builds exercise the actual new backend.
- Cached Abseil `ParsedFormat` passes 2,003,610 exact comparisons per rounding-mode run on
  each host. Its isolated conversion gains (roughly 10-21% macOS and 12-19% Linux) do not
  translate into material gains across its 20-case native pilot. It is not selected.
- An initial vector of borrowed cells avoids identity copies but still allocates two
  temporary containers per row. Its pilot is weaker than direct encoding, especially
  on Linux. Its complete implementation, six-target tests and samples remain retained;
  no full-suite or sanitizer qualification is claimed for that rejected policy.

None of these alternatives is included in the production change. The negative floating
conversion trial includes its full 240-case kernel and 172-case native matrices on both
hosts; no subset is substituted for those results.

## Correctness and qualification

New tests cover bounded borrowed slices, returned ownership after backing storage changes,
empty rows, all unsupported formats, literal quoting/escaping and long summary identities.
Missing scopes remain empty while present zeroes retain their requested precision. Numeric
text remains alive throughout each synchronous append; no borrowed value reaches a queued
consumer. Each summary call retains only its current encoded row, identity views and one
four-cell numeric array, rather than a growing collection of owning intermediate cells.

Both complete 314-target release suites pass, including all extensions and required Linux
FUSE coverage. Ten affected targets pass ASan on both hosts and MSan/TSan on Linux. Four
changed translation units and their associated headers pass scoped clang-tidy on both.
Release CLI/manual benchmark builds and link-action inspection pass. Instrumented runs
verify correctness, never speed.

Scoped lint found two existing renderer tests at cognitive complexity 42 and 35. Their
per-format checks were extracted without dropping cases or raising the limit. Original
failed logs and source are retained. Only test source changed after freezing the production
binaries; all production inputs remain identical. Each host then qualifies the final test
source on merged main `ba9b2baaf8d7ed7a7ffdeac7a16bd63f43255071`.

Local development qualification is separate from the fresh PR-context CI required before
merging. Frozen timing builds retain their actual base `814214400ae71bae359dc0ba3eb01f49e4a1b5db`;
ancestry synchronization proves the same baseline tree and never relabels measured samples.

## Measurement coverage and size

Each host runs 240 production-engine in-memory cases, three sessions and three participants:
2,160 case/session/participant records and 19,440 raw rounds. Each also runs 172 native cases:
15,480 invocations including discarded warmups, with every exit and complete stdout checked.
CSV/TSV goldens and JSON records use independent fixture models. Empty, small, extension,
width, ordering, sparse-scope and no-summary controls remain in the matrices. The additional
Linux confirmation adds 648 raw kernel rounds and 720 verified native invocations.

The engine matrix uses controlled in-memory trees; native files and exact goldens use the
retained deterministic models. Linux pins the first one/three allowed logical CPUs; macOS
records worker requests and claims no affinity. Hosts are not pooled. Same-host builds,
tests, lint and heavy fixture work do not overlap timing. Python launch/oracle processing
is outside the native wall interval. Source/binary hashes, compiler flags, link actions,
raw samples, failures and final qualification logs remain in the bundle.

| Host  | Baseline CLI bytes | Direct CLI bytes | Change |
| :---- | -----------------: | ---------------: | -----: |
| macos |          8,207,032 |        8,207,000 |    -32 |
| linux |         18,276,792 |       18,269,792 | -7,000 |

The existing stateless renderer does not gain a retained buffer or destructor. Command-owned
summary-buffer reuse, other borrowed structured fields, completed comparison frontiers,
shared worker budgets, bulk metadata and pipeline backpressure remain separate work.

## Evidence

`direct-summary-cells-20261104.json.gz`: 33,970,885 bytes, 3,396 labeled files; SHA-256
`7483a45e00d42eb453b5181105dcb2bb450b5ba5f299ca84747f7916af84ea95`.

The two host payloads retain raw data and source for selected and rejected policies. Exact
golden bytes and inherited deterministic generators are included; generated input trees,
executables and canonical compile databases are omitted. Each retained file carries its
original byte size and SHA-256. Earlier fixed-metric baseline evidence is identified by its
own archive checksum. Diagnostic files retain their actual platform names and contexts.
