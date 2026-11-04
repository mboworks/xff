<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Fixed-size numeric summary cells

Use `std::array<std::string, 4>` for the four count/size metrics instead of allocating a
temporary vector for each column. Reserve the final labeled row once, append its labels,
and move the numeric strings into their destination. Delimited exports also move their
temporary numeric strings rather than copying them.

This is a small allocation reduction after the larger selected-output change. It is not
a universal speedup: native gains are mostly around one percent on macOS and up to two
percent for larger ordinary Linux tables; comparison cohorts are often close to baseline.
The slower small-command and individual large-command cells remain in the evidence.

## Policy decision

- `display_only`: fixed-size display metrics and one final labeled-row allocation.
- `fixed_metrics`: the display change plus fixed-size export metrics and moved insertion.
- `baseline_repeat`: an executable copy of the baseline, with exactly the same hash.

Retain the complete `fixed_metrics` policy. It removes known temporary container allocations
without adding an API, worker, cache, ownership wrapper, exception path or output change.
The partial policy has no consistent cross-platform advantage. Its complete samples remain.

## Native results

Ratios are candidate elapsed time divided by the paired baseline; below one improves.
Each case uses the median of four session ratios. Each session retains the mean fastest
seven of nine measured invocations after a discarded, correctness-checked warmup.
Cohorts take medians across cases, never pool the operating systems, and do not normalize
away the baseline-copy drift. Plain/aligned comparison cohorts include both wide
1,000-column and ordinary 110-column presentation cases.

| Host  | Workflow | Format   | Display only | Complete | Baseline copy | Cases |
| :---- | :------- | :------- | -----------: | -------: | ------------: | ----: |
| macos | ordinary | plain    |       0.9884 |   0.9931 |        0.9978 |     4 |
| macos | ordinary | aligned  |       0.9940 |   0.9915 |        0.9997 |     4 |
| macos | ordinary | markdown |       0.9911 |   0.9917 |        0.9967 |     4 |
| macos | ordinary | csv      |       0.9957 |   0.9918 |        0.9983 |     4 |
| macos | ordinary | tsv      |       1.0050 |   0.9968 |        1.0009 |     4 |
| macos | ordinary | jsonl    |       0.9985 |   0.9966 |        0.9969 |     4 |
| macos | scoped   | plain    |       0.9918 |   0.9889 |        0.9973 |    12 |
| macos | scoped   | aligned  |       0.9908 |   0.9918 |        0.9983 |    12 |
| macos | scoped   | markdown |       0.9900 |   0.9902 |        0.9958 |     8 |
| macos | scoped   | csv      |       0.9973 |   0.9890 |        1.0007 |     8 |
| macos | scoped   | tsv      |       0.9972 |   0.9917 |        0.9999 |     8 |
| macos | scoped   | jsonl    |       0.9960 |   0.9938 |        0.9974 |     8 |
| linux | ordinary | plain    |       0.9862 |   0.9807 |        0.9968 |     4 |
| linux | ordinary | aligned  |       0.9908 |   0.9786 |        1.0019 |     4 |
| linux | ordinary | markdown |       0.9927 |   0.9903 |        1.0006 |     4 |
| linux | ordinary | csv      |       0.9965 |   0.9966 |        1.0006 |     4 |
| linux | ordinary | tsv      |       1.0013 |   0.9952 |        1.0007 |     4 |
| linux | ordinary | jsonl    |       0.9943 |   0.9920 |        1.0013 |     4 |
| linux | scoped   | plain    |       1.0047 |   0.9970 |        1.0026 |    12 |
| linux | scoped   | aligned  |       0.9968 |   0.9990 |        1.0002 |    12 |
| linux | scoped   | markdown |       1.0000 |   0.9978 |        0.9990 |     8 |
| linux | scoped   | csv      |       1.0064 |   1.0069 |        0.9992 |     8 |
| linux | scoped   | tsv      |       1.0052 |   0.9951 |        1.0043 |     8 |
| linux | scoped   | jsonl    |       0.9925 |   0.9904 |        1.0036 |     8 |

The primary cohorts have 1,000/10,000 paths and one/three requested workers. Small and
empty cases are startup controls. Their costs matter and are retained rather than being
presented as allocation throughput. On Linux the largest native slowdown is 3.12% for a
ten-file ordinary TSV command: the median baseline is 0.881126 ms and the complete policy
is 0.907446 ms, about 26 microseconds. A 1,000-path, three-worker aligned comparison cell
has a 1.83% ratio increase; its median times are 3.423514 and 3.470120 ms. All four session
ratios and baseline-copy controls are retained. The largest macOS native cell increase is
0.86%. These are tradeoffs, not evidence that every summary improves.

JSON does not call the changed numeric-cell builders. Display-only CSV/TSV also retain
their old export implementation. Movement in those controls cannot be attributed to
eliminating metric-vector allocation; code layout and measurement variation remain possible.

## Whole-engine in-memory matrix

The manual benchmark adds 96 ordinary summary cases to the existing 144 comparison cases:
six formats, name/extension grouping, zero/10/1,000/10,000 entries and one/three workers.
Comparison cases retain two/six selected scope columns. Ordinary cases walk one synthetic
root; comparison cases walk two. An inherited raw protocol caption says two trees for
both families; this paragraph clarifies that caption without relabeling the raw records.

Each of 240 cases has four participants, four sessions and nine raw rounds: 3,840 records
and 34,560 raw measurements per host, 7,680 and 69,120 across both. Fixed CPU-compatible
batches have stable IDs, their execution order shuffles each session, and participant
rotation gives each executable every ordinal position exactly once across four sessions.
Linux pins the first one/three allowed logical CPUs; macOS records worker requests without
claiming affinity. Fixture creation and expression parsing precede measured engine work.

| Host  | Workflow | Format   | Display only | Complete | Baseline copy |
| :---- | :------- | :------- | -----------: | -------: | ------------: |
| macos | ordinary | plain    |       0.9703 |   0.9695 |        0.9999 |
| macos | ordinary | aligned  |       0.9504 |   0.9750 |        0.9908 |
| macos | ordinary | markdown |       0.9776 |   0.9856 |        0.9957 |
| macos | ordinary | csv      |       1.0178 |   0.9866 |        1.0052 |
| macos | ordinary | tsv      |       0.9956 |   0.9820 |        1.0021 |
| macos | ordinary | jsonl    |       1.0024 |   0.9891 |        1.0076 |
| macos | scoped   | plain    |       0.9719 |   0.9736 |        0.9965 |
| macos | scoped   | aligned  |       0.9635 |   0.9786 |        1.0028 |
| macos | scoped   | markdown |       0.9706 |   0.9809 |        1.0043 |
| macos | scoped   | csv      |       1.0034 |   0.9759 |        1.0010 |
| macos | scoped   | tsv      |       1.0093 |   0.9776 |        1.0025 |
| macos | scoped   | jsonl    |       1.0085 |   1.0093 |        1.0089 |
| linux | ordinary | plain    |       0.9912 |   0.9863 |        1.0035 |
| linux | ordinary | aligned  |       0.9819 |   0.9847 |        0.9980 |
| linux | ordinary | markdown |       0.9891 |   0.9798 |        0.9979 |
| linux | ordinary | csv      |       0.9938 |   0.9891 |        0.9949 |
| linux | ordinary | tsv      |       1.0033 |   0.9834 |        0.9960 |
| linux | ordinary | jsonl    |       0.9861 |   0.9819 |        0.9964 |
| linux | scoped   | plain    |       0.9900 |   0.9875 |        0.9947 |
| linux | scoped   | aligned  |       0.9941 |   0.9904 |        0.9985 |
| linux | scoped   | markdown |       0.9995 |   0.9970 |        0.9986 |
| linux | scoped   | csv      |       1.0047 |   1.0000 |        0.9960 |
| linux | scoped   | tsv      |       1.0075 |   0.9906 |        0.9989 |
| linux | scoped   | jsonl    |       1.0165 |   0.9972 |        1.0014 |

The short screen had twelve cases, although an inherited screen caption says six. It
showed mixed results and baseline-copy drift, so it did not decide admission. The full
balanced matrices above replace that preliminary decision evidence; both remain archived.

## Exact outputs and ownership

Every host has 164 native cases: the previous 96 comparison-scope cases and eight narrow
console cases, plus 48 ordinary tables and twelve standalone comparison-result tables.
Each has four participants, four sessions, one discarded warmup and nine measured runs:
26,240 invocations per host and 52,480 across both. Every exit status and full stdout is
checked, including timed invocations. Expected input hashes are rechecked before timing.

Independent Python models verify complete CSV/TSV bytes and JSON objects, including raw
numbers, denominators, selector order, repeated identities, absent versus zero cells and
total-row identity. Plain/aligned/Markdown use complete frozen goldens plus committed
display tests. Fixtures retain ASCII quotes, commas, tabs, newlines, backslashes, zero-byte
files and literal `total` labels. Parent fixture/oracle generation is retained with the
earlier selected-summary evidence; current manifests contain all expected hashes and rows.

The array owns its strings; the final rows remain owning. No borrowed label or cell escapes
its lifetime. Empty export cells, missing display markers, scaled-size alignment, precision
and count/size percentages keep their existing behavior. Strings can still allocate when
their contents require it; only the fixed-size temporary container allocation is removed.

## Qualification and provenance

The complete 314-target release suite passes on both hosts, including all extensions and required
working Linux FUSE tests. Ten affected targets pass ASan on both hosts and MSan/TSan on Linux.
All three changed translation units pass scoped clang-tidy with their associated headers on both
hosts. Release CLI/manual benchmark builds and link-action inspection pass. Full suites replay
the existing committed fuzz corpora. Local pre-publication qualification is separate from the
fresh PR-context CI required before merging.

Frozen builds use base `cae7c347285291de32948c4b0fd955b3e4c69744`. Later ancestry-only
synchronization preserves the complete product tree. Linux development checks record their
actual local ancestry head; publication uses the normally signed Mac sync accepted by GitHub.
The unavailable remote signing agent and rejected unsigned push remain recorded. No signing
rule or hook is bypassed. Frozen build identities are never relabeled as later commits.

Each policy first passes the six affected release suites and all 240 manual dry runs on
both hosts. Changed translation units have actual clang-tidy checks; unchanged source
identities explicitly reuse their recorded check. Compilers, flags, source/binary hashes,
link actions, failed attempts and complete qualification logs are retained. A rebuilt Mac
baseline differs only in its `N_SO` sandbox-path debug string and derived signature; the
existing byte-level Mach-O verifier proves all other bytes, including runtime code/data
and UUID, are identical. The Linux baseline matches its parent binary exactly.

## Frozen release binary size

| Host  |   Baseline | Display only |   Complete | Complete change |
| :---- | ---------: | -----------: | ---------: | --------------: |
| macos |  8,206,936 |    8,207,016 |  8,207,032 |             +96 |
| linux | 18,273,560 |   18,275,976 | 18,276,792 |          +3,232 |

No host measurement overlaps same-host builds, tests, lint, fuzz or heavy fixture work.
Native wall time comes from the existing launcher; Python oracle/launch processing is
outside that measured interval. Instrumented binaries establish correctness, not speed.

Borrowed structured cells, bounded encoded summary rows and streaming/backpressure remain
separate candidates. The independent percentage-formatting probe is excluded from this
evidence bundle and is not included in this implementation or its measured gains.

## Evidence

`fixed-summary-metrics-20261104.json.gz`: 16,740,856 bytes, 2,432 labeled files; SHA-256
`b8090f58172ad1f28268cd2ff57884231c93473972e8a9a2462c248d7589580e`.

The bundle retains all raw cases and controls, including empty/extension groups and every slower
cell. Generated native inputs and binaries are omitted; deterministic generators, expected bytes,
input/source/binary hashes and exact commands remain. No percentage-formatting probe is mixed in.
