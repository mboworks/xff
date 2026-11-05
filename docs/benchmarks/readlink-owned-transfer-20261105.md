<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Retain the owned symlink-target transfer trial

**Decision:** keep the current production implementation. The private prototype
removes a real owned-string copy, passes correctness qualification on both hosts,
and has small targeted gains. The mixed Mac results and retained slower controls
do not establish a convincing standalone cross-platform performance improvement.
Do not run more repetitions merely to seek a favorable result. The implementation,
original measurements and independent confirmation remain available for analysis.

## Exact change and parent

The implicit listing currently holds the `StatusOr<string>` returned by `ReadLink`
const, then copies its value into the listing context. This trial removes const
and moves the existing owned string. Two synthetic read-only VFS tests retain
2 KiB targets through template and CSV output with one/three workers. It adds no
lock, owner wrapper, field-name scan, syscall, flag, threshold or traversal policy.
The existing target field already borrows the context's string; it needs no
additional field ownership change.

Both builds use exact commit `d148059dc631f15d9dcf6ca70ea256c8bb0658ac` (#1110)
with the five-file compiled target-demand overlay later published in #1113.
This is that qualified source overlay as the parent, not unchanged #1110. Only
`run.cc` and its existing test source differ from that parent. The separate short
ReadLink buffer, terminated input-path prototype and PGO are absent. Ordinary
`--config=clang_release --config=xff_full` uses O2, actual ThinLTO and Clang
23.1.4 on both systems. Percentages from these independent trials are not additive.

## Protocol and independent output checks

Reuse all 228 original target-demand cases and their independently checked native
fixtures unchanged. Add 36 cases with 240-byte symlink targets: broad/deep layouts,
10/1,000/100,000 links, one/three workers, and names/target-template/target-CSV
outputs. The new fixtures contain 202,020 symlinks. Independent path ordering,
Python `readlink` and full byte/hash oracles precede measurements; expected output
is never generated from the candidate. Both CLIs pass all 528 preflights per host.

The exploratory screen retains the fastest two of three repetitions. Full
confirmation has three balanced sessions, nine measured repetitions and the
fastest seven, plus a byte-identical copied-parent control. Each full host
population contains 21,384 measured invocations and 2,376 excluded ordinary
warm-ups, plus separately recorded pause/resume warm-ups. Every invocation checks
complete stdout, stderr and exit status. The native timer excludes Python startup.
Linux pins the requested one/three allowed CPUs; Mac uses one/three workers
without claiming pinning. Mac fixtures use cached APFS; Linux fixtures use its
RAM-backed native filesystem. No sanitizer timing is performance evidence.

Canonical pauses happen between individual invocations and resume with checked,
excluded rewarming. The full source and driver snapshots stay unchanged throughout
the original and independent populations. No original result is replaced or pooled
with its repeat, and copied-parent drift is never subtracted to create a gain.

## Full results

Negative elapsed change is faster. Compute each case's median percentage across
three sessions, then the median of those case medians for a group. Copied-parent
percentages remain separate controls.

| Group / output               | Cases | Linux change | Linux copy | Mac change | Mac copy |
| :--------------------------- | ----: | -----------: | ---------: | ---------: | -------: |
| compare-symlinks/status      |     4 |      -0.624% |    -0.676% |    -0.173% |  -0.416% |
| long-targets/names           |    12 |      -1.371% |    -1.060% |    +0.181% |  -0.065% |
| long-targets/target-csv      |    12 |      -1.390% |    -0.268% |    -0.283% |  -0.020% |
| long-targets/target-template |    12 |      -1.655% |    -0.086% |    -0.481% |  -0.405% |
| mixed/metadata               |    20 |      -1.964% |    -1.613% |    -0.020% |  -0.221% |
| mixed/names                  |    20 |      -1.536% |    -1.244% |    +0.213% |  +0.134% |
| mixed/summary                |    20 |      -0.984% |    -0.176% |    -0.097% |  -0.333% |
| mixed/target-csv             |    12 |      -0.595% |    -0.344% |    +0.188% |  -0.032% |
| regular/eager-csv            |    20 |      -0.845% |    -0.695% |    +0.007% |  -0.048% |
| regular/eager-summary        |    20 |      -1.152% |    -0.670% |    -0.159% |  -0.363% |
| regular/files                |    20 |      -0.934% |    -1.487% |    +0.065% |  -0.031% |
| regular/native-name          |    20 |      -0.806% |    -0.624% |    +0.264% |  -0.348% |
| symlinks/metadata            |    20 |      -1.442% |    -1.568% |    +0.143% |  +0.057% |
| symlinks/names               |    20 |      -0.845% |    -1.061% |    -0.136% |  -0.035% |
| symlinks/summary             |    20 |      -1.060% |    -0.523% |    -0.044% |  -0.069% |
| symlinks/target-csv          |    12 |      -0.834% |    +0.058% |    -0.362% |  -0.172% |

Linux's ordinary controls also often improve about 1%, including the copied
parent. Those changes are not causally attributable to moving a target string.
Mac's long-target CSV and template group medians improve less than half a percent;
its mixed target CSV group is slower. Preserve these differences rather than
advertising a broad listing speedup.

The original Mac regular/deep/100,000/one-worker summary costs +2.122% (6.258 ms)
in the case-median result and is slower in all three sessions, despite not using
the target-read branch. The Linux symlink/deep/1,000/three-worker metadata case
costs +4.666% (0.086 ms), with two slower sessions. These unresolved controls
motivate a separate, fixed eight-case confirmation: six slower/unaffected controls
and two long-target positive controls, with unchanged executable hashes.

## Independent eight-case confirmation

The repeat retains three balanced sessions and fastest-seven-of-nine statistics:
648 measured invocations and 72 ordinary warm-ups per host. It preserves the
original cases, complete output oracles, copied parents and all original results.
Its inherited final console status string says 264; the frozen case assertion,
actual case list, 24 case/session rows and validated report contain eight cases.
That cosmetic status label is not used to validate the population.

| Case                                       | Original Linux | Repeat Linux | Original Mac | Repeat Mac |
| :----------------------------------------- | -------------: | -----------: | -----------: | ---------: |
| compare-symlinks/128/3/status              |        +2.919% |      +1.077% |      +0.193% |    +1.547% |
| long-targets/broad/1000/3/target-csv       |        -2.764% |      +1.291% |      +1.411% |    +0.454% |
| long-targets/deep/100000/1/target-csv      |        -2.052% |      -1.416% |      -0.675% |    -1.156% |
| long-targets/deep/100000/1/target-template |        -2.196% |      -0.856% |      -1.450% |    +0.249% |
| mixed/deep/10/3/metadata                   |        -1.318% |      -0.784% |      +2.280% |    +0.684% |
| regular/deep/100000/1/eager-summary        |        -1.291% |      -0.497% |      +2.122% |    +0.634% |
| symlinks/broad/1000/3/names                |        -0.897% |      +0.024% |      +1.801% |    +1.662% |
| symlinks/deep/1000/3/metadata              |        +4.666% |      -2.132% |      -1.040% |    -0.070% |

The large Mac summary cost shrinks to +0.634% (1.865 ms), still slower in all
three repeat sessions. Long target CSV improves in both repeats, while the Mac
long-target template does not retain its original gain. Small native name and
comparison controls remain mixed. These are useful measurements, not proof of a
consistent cross-platform standalone win.

Absolute Mac baseline timings also change substantially between populations. The
same symlink/deep/1,000 metadata command rises from about 8 ms to about 615 ms for
all three executables; the long-target baseline rises from roughly 400 ms to
540 ms. Source, argument, fixture-output and executable identities are unchanged.
This is environmental or working-set uncertainty, not evidence that the candidate
alone causes that change. Keep within-population ratios and original absolute
samples; do not normalize those populations into an invented common result.

## Correctness, footprint and retained evidence

Both hosts pass all 331 root, extras and API release targets. Linux executes 75
and reuses 256 valid cached results; Mac executes 99 and reuses 232. Required real
Linux FUSE tests are enabled. Five focused ASan suites pass on each host; the same
five MSan and TSan suites pass on Linux. Actual local clang-tidy passes both changed
translation units on both systems. Canonical Linux changed-coverage policy records
2/2 lines and 2/2 branches, with no threshold change or exclusion. Final rebuilt
CLI hashes equal the frozen measured candidate exactly on both hosts.

| Executable                     |        Linux |         Mac |
| :----------------------------- | -----------: | ----------: |
| Qualified target-demand parent | 18,437,376 B | 8,274,840 B |
| Owned-transfer candidate       | 18,437,704 B | 8,274,840 B |
| Difference                     |       +328 B |         0 B |

Peak-RSS samples remain in the evidence; no memory saving is claimed from small
process-level differences. The private source snapshots and binaries remain
retained even though only the two owned prototype paths are restored for the next
independent experiment. No production code, default or user-facing option changes
as a consequence of this decision record.

The [lossless evidence](readlink-owned-transfer-20261105.json.gz) contains 136 named records, 101 unique contents and 26,054,160 reconstructed bytes in 1,949,980 compressed bytes. Its SHA-256 is `0becc8f8551c90b5257f343ac7321af6d9ce60c56e24764953f29d197412f07d`. Every record path, length and content hash, full/screen/repeat population, qualification result and image identity is verified before publication. Sources, drivers, all samples, copied controls, logs, canonical coverage and the decision are preserved. Executable files remain on their original hosts and are represented by length/hash inventories; they are not embedded in the site artifact.
