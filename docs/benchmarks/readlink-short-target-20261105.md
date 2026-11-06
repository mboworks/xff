<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# O20m: short symlink target storage, 2026-11-05

**Decision:** retain this implemented experiment without adopting its runtime
change. Linux has small gains in required-target workloads; the Mac screen does
not establish a benefit beyond the copied-parent control. No full Mac experiment
or complete 331-target release qualification is claimed for this rejected trial.
The separate compiled target-demand optimization addresses unnecessary calls;
these measurements are not results for that later change.

## Change and exact parent

The ordinary parent is XFF commit
`d148059dc631f15d9dcf6ca70ea256c8bb0658ac` (#1110), built with the existing
`--config=clang_release --config=xff_full` settings on both hosts. The experiment
changes only `LocalFs::ReadLink` target storage plus focused tests. It initially
reads into an uninitialized 1,024-byte stack buffer and constructs the returned
owning string from exactly the bytes read. A full buffer is not assumed complete:
the fallback starts at 2,048 bytes and retains the original growing-buffer loop.
The input path still has its ordinary owning, NUL-terminated string.

There is no PGO, native-input-path optimization, owned-target transfer, worker
policy, new CLI flag or safety-policy change in these images. The exact native
compiler/toolchain context is the existing Clang 23.1.4 release configuration.
No percentage from this trial should be added to another candidate's results.

| Image     | Linux bytes | macOS bytes |
| :-------- | ----------: | ----------: |
| Parent    |  18,435,008 |   8,274,424 |
| Candidate |  18,436,888 |   8,274,840 |
| Change    |      +1,880 |        +416 |

## Verification and measurement

Both hosts pass the five focused release test targets covering local/native
filesystem access, walking, parallel walking and execution. The local filesystem
suite passes ASan on Mac and MSan on Linux. Actual clang-tidy ran on both changed
translation units on each host with no failures. Candidate source and executable
hashes are recorded and checked before and after timing.

Functional tests cover missing paths, dangling targets, nonterminated input
views and the existing regular-file error. Linux additionally tests exact
1,023/1,024/1,025/1,500-byte stored targets across the short-buffer boundary and
fallback. The tests do not assume `readlink` writes a terminator or returns a
complete target when its buffer is full. The Mac host does not claim support for
Linux's long-target fixture sizes.

Each platform has 228 independently checked native CLI cases: regular, symlink
and mixed broad/deep trees; declared 10/100/1,000/10,000/100,000-entry fixtures;
one/three workers; ordinary names, metadata listings, summaries and target CSV;
and four comparison-status cases. These are native filesystem workloads, not
in-memory-VFS-only timings. Mac uses cached APFS fixtures and Linux uses
RAM-backed fixtures as recorded in each case.

A balanced screen uses three measured repetitions, retaining the fastest two,
plus an excluded warm-up and a separate byte-identical copied-parent control.
Both 228-case screens pass every exit, stderr and complete stdout oracle. Linux
then completes three independently balanced sessions, each with nine repetitions
retaining seven: 18,468 measured invocations plus 2,052 excluded warm-ups. The
Mac screen contains 2,052 measured invocations plus 684 excluded warm-ups.
Resume warm-ups, when present, are checked and excluded separately.

The native timing helper excludes Python startup. Linux pins invocations to the
first one or three allowed affinity CPUs; Mac requests one or three workers
without claiming pinning. The shared pause protocol permits public qualification
between individual invocations, then checks an excluded rewarm. The copied-parent
control is reported independently, not subtracted from candidate measurements.

## Results and limits

Negative values mean less elapsed time. Linux values are medians across per-case
three-session medians. Mac values are medians across its one-session case values;
these are explicitly different qualification depths, not equal-confidence
platform estimates.

| Group                   | Cases | Linux full | Linux copy | Mac screen | Mac copy |
| :---------------------- | ----: | ---------: | ---------: | ---------: | -------: |
| compare-symlinks/status |     4 |    -1.193% |    -0.271% |    -0.714% |  -0.051% |
| mixed/metadata          |    20 |    -1.735% |    -0.470% |    +0.631% |  +0.862% |
| mixed/names             |    20 |    +0.085% |    +0.247% |    -0.889% |  -0.079% |
| mixed/summary           |    20 |    +0.197% |    -0.038% |    -0.943% |  -0.148% |
| mixed/target-csv        |    12 |    -1.243% |    -0.285% |    +0.500% |  +0.257% |
| regular/eager-csv       |    20 |    -1.269% |    -0.827% |    -0.025% |  -0.113% |
| regular/eager-summary   |    20 |    +0.032% |    +0.184% |    +0.292% |  +0.370% |
| regular/files           |    20 |    +0.244% |    +0.233% |    -0.086% |  +0.015% |
| regular/native-name     |    20 |    -0.351% |    -0.203% |    +0.086% |  -0.032% |
| symlinks/metadata       |    20 |    -2.078% |    -0.261% |    +0.939% |  -0.616% |
| symlinks/names          |    20 |    -0.063% |    +0.212% |    +0.659% |  +0.524% |
| symlinks/summary        |    20 |    +0.055% |    +0.124% |    -0.260% |  +0.079% |
| symlinks/target-csv     |    12 |    -2.719% |    -0.109% |    -1.676% |  -1.623% |

The clearest Linux group, symlink target CSV, improves about 2.72%; its copied
parent is 0.11% faster. Ordinary groups are mostly flat or mixed. There are slower
cells: mixed deep/100,000/three-worker name output is 3.22% slower (1.450 ms) in
all three sessions, and mixed deep/10,000/three-worker summary is 5.78% slower
(0.329 ms) in two sessions. All original cells, including these, remain retained.

The apparent Mac symlink-target CSV gain is 1.68%, while its byte-identical
copied parent is already 1.62% faster. Mac symlink metadata is 0.94% slower and
mixed metadata is 0.63% slower. These observations do not justify a cross-platform
runtime default, especially after avoiding unnecessary target reads independently.
The decision stops this trial before a full Mac run and broader release gates;
it is not a claim that the stack-buffer technique can never help another workload.

## Retained evidence

The [lossless evidence archive](readlink-short-target-20261105.json.gz) is
660,461 bytes, SHA-256
`22ab63ebf3aee2c09b0336237d79ead26ac9779aab643efb32675df0e65858c9`.
It retains 66 named records as 49 content-addressed text payloads, reconstructing
9,911,805 named-record bytes. Both source snapshots, patches, test/lint logs,
contracts, output preflights, original screens and complete Linux full population
are retained. Executables remain on their original hosts with hash inventories;
no executable or runtime source from this experiment is installed by this report.

Decompress gzip to JSON. Format `xff-lossless-experiment-group-text-evidence-v1`
uses `contents[sha256]` for UTF-8 text and
`experiments[].hosts[].records[]` for each relative path, byte length and hash.
Validate these before reconstructing a record. Every record, candidate identity,
complete measured population and focused check result was verified for publication.
Earlier draft scripts are retained as provenance; the protocol recorded in each
successful `results.json` identifies the driver actually used for that population.
