# Avoid empty libc time-format flushes

A two-line guard in the pinned Abseil CCTZ formatter returns immediately when a deferred
`strftime` format fragment is empty. Existing output is preserved; no timezone, locale,
worker, allocator, safety, build-default or metadata policy changes. Both complete native
and in-memory trials support adopting this change.

The ordinary private parent is #1110 plus the separately qualified compiled link-target
demand change. Archive descendant skipping, short-target/input-path storage, owned-target
transfer and PGO are absent. Do not add their percentages to this independent result.

## Why this matters

Numeric CCTZ directives already format without libc. However, flushing an empty deferred
fragment still entered `strftime`. An untimed Mac preflight sample found 641 of 746 main-thread
samples in `FormatTM`, including 640 in Darwin `strftime_l` and 636 in timezone loading.
This is a diagnostic observation, not a sampled timing invocation. It explains why asking
for UTC output alone did not avoid that libc path.

The patch skips only an empty fragment. Nonempty locale fallback, fixed/UTC timezone
selection, escaped percent signs, literal and embedded-NUL bytes remain tested. Existing
lazy CoreFoundation loading is retained. No environment value is changed to hide the cost.

The exact patch targets Abseil 20260817.0. There is no new dependency or license. The ordinary
release compiler on both hosts is Clang 23.1.4; actual CCTZ actions prove `-O2`, ThinLTO and
`-fno-exceptions`, without PGO or sanitizer options. The prior PGO compiler-identity record
is not evidence of current profile use; a separate current-action record proves the flags.

## Full native command results

Each cell is the median case change across three independent sessions. Each session rotates
the parent, candidate and an unchanged copied-parent image; each case retains the mean of its
fastest seven of nine measured invocations after a checked warmup. Negative means less elapsed
time. The copied-parent result is shown independently and is not subtracted.

| Host  | Output family             | Cases | Candidate change | Copied parent | All sessions faster | All sessions slower |
| ----- | ------------------------- | ----: | ---------------: | ------------: | ------------------: | ------------------: |
| linux | `compare-symlinks/status` |     4 |          +0.300% |       +1.877% |                   0 |                   0 |
| linux | `control/help`            |     1 |          +0.667% |       +1.046% |                   0 |                   0 |
| linux | `control/help=compare`    |     1 |          +0.513% |       +1.648% |                   0 |                   0 |
| linux | `control/version`         |     1 |          -0.703% |       -0.127% |                   0 |                   0 |
| linux | `mixed/metadata`          |    12 |          -0.768% |       +0.421% |                   3 |                   1 |
| linux | `regular/eager-csv`       |    12 |          -0.140% |       +0.210% |                   2 |                   2 |
| linux | `regular/files`           |     4 |          +0.034% |       -0.012% |                   1 |                   1 |
| linux | `symlinks/metadata`       |    12 |          -1.415% |       +0.462% |                   8 |                   0 |
| linux | `symlinks/names`          |     4 |          -0.004% |       -0.129% |                   1 |                   1 |
| macos | `compare-symlinks/status` |     4 |          -0.109% |       -0.017% |                   0 |                   0 |
| macos | `control/help`            |     1 |          -1.060% |       -1.282% |                   1 |                   0 |
| macos | `control/help=compare`    |     1 |          +0.111% |       -0.783% |                   0 |                   0 |
| macos | `control/version`         |     1 |          +0.189% |       -1.667% |                   0 |                   0 |
| macos | `mixed/metadata`          |    12 |         -91.884% |       -0.022% |                  12 |                   0 |
| macos | `regular/eager-csv`       |    12 |          +0.188% |       +0.484% |                   0 |                   2 |
| macos | `regular/files`           |     4 |          -0.078% |       -0.105% |                   0 |                   0 |
| macos | `symlinks/metadata`       |    12 |         -91.793% |       -0.142% |                  12 |                   0 |
| macos | `symlinks/names`          |     4 |          -0.083% |       -0.013% |                   0 |                   0 |

The 51 paired cases are exactly 24 timestamp-bearing mixed/symlink metadata commands,
12 regular-file path/size CSV controls, eight 100,000-entry names controls, four comparison
controls and three startup controls. Trees are broad/deep, with one/three requested workers.
The frozen driver labels the first 36 a metadata/temporal cohort; its regular `path,size` CSV
cases do not format time. A retained clarification makes that naming limitation explicit;
the measured arguments and original output oracles were never changed.

Timestamp-bearing native performance cases use 10, 100 and 1,000 entries. The unfixed Darwin
parent is disproportionately expensive at larger counts, so no balanced 100,000-row temporal
speedup is claimed. Ten additional candidate-only checks retain independent large complete-output
oracles: eight timestamp-bearing native/archive cases and two path/size controls up to 100,000
entries. All 112 parent/candidate preflights pass on each host. This bounds timing cost without
turning a candidate-derived output into its own correctness oracle.

Linux whole-command gains are modest because its libc formatter is already inexpensive. Mac
timestamp output improves strongly in every measured case/session; names-only, ordinary path/size,
startup and comparison controls remain visible. No general startup or traversal gain is claimed.

## In-memory formatter results

Fixed UTC time and independently specified complete strings remove filesystem work. Each of eight
formats passes its output preflight. Three balanced process sessions per variant each contain nine
randomly interleaved measured rounds per format, retaining seven. The registered minimum time is
20 ms and warmup is 10 ms; it overrides the screen CLI minimum of 5 ms. Warmups and calibration
are excluded. Linux uses a single allowed CPU; Mac worker requests are not CPU pinning.

| Host  | Format            |  Parent us | Candidate us |   Change | Copied parent |
| ----- | ----------------- | ---------: | -----------: | -------: | ------------: |
| linux | `compact`         |   0.134626 |     0.094253 | -29.999% |       -0.048% |
| linux | `numeric-space`   |   0.150798 |     0.110140 | -26.947% |       +0.346% |
| linux | `iso`             |   0.148561 |     0.109075 | -26.557% |       +0.077% |
| linux | `date`            |   0.052655 |     0.041556 | -21.151% |       -0.188% |
| linux | `escaped`         |   0.070552 |     0.055667 | -21.098% |       -1.467% |
| linux | `empty`           |   0.034742 |     0.029506 | -15.123% |       -0.053% |
| linux | `locale-fallback` |   0.105693 |     0.091134 | -13.692% |       +0.226% |
| linux | `literal`         |   0.039195 |     0.034254 | -12.767% |       +0.014% |
| macos | `compact`         | 599.229756 |     0.172863 | -99.971% |       -0.439% |
| macos | `iso`             | 598.054478 |     0.192573 | -99.968% |       -0.170% |
| macos | `numeric-space`   | 596.094480 |     0.192554 | -99.968% |       -0.420% |
| macos | `date`            | 149.715685 |     0.057308 | -99.962% |       -0.515% |
| macos | `escaped`         | 147.915189 |     0.076251 | -99.949% |       +0.021% |
| macos | `empty`           |  74.494804 |     0.041941 | -99.944% |       +0.426% |
| macos | `literal`         |  74.732631 |     0.052467 | -99.930% |       -0.590% |
| macos | `locale-fallback` | 298.203095 |    74.849403 | -74.885% |       -0.872% |

The locale-fallback format also contains numeric directives. Its improvement therefore reflects
avoided empty flushes around those directives, not removal of the required nonempty libc fallback.
These isolated results are not whole-command speedup estimates.

## Slower controls and limits

The following are each host's six largest positive case changes, retained without dropping small
or inconvenient cells. Absolute deltas and session signs help distinguish a code-path claim from
startup noise. Every original case/session remains in the evidence.

| Host  | Case                             |  Change |  Delta ms | Copied parent | Slower sessions |
| ----- | -------------------------------- | ------: | --------: | ------------: | --------------: |
| linux | `compare-symlinks/4096/3/status` | +2.356% | +0.106502 |       +4.172% |             2/3 |
| linux | `mixed/deep/100/3/metadata`      | +1.611% | +0.019809 |       +1.825% |             2/3 |
| linux | `regular/broad/1000/3/eager-csv` | +1.499% | +0.023630 |       -1.105% |             2/3 |
| linux | `symlinks/broad/10/1/metadata`   | +1.258% | +0.010255 |       +1.153% |             2/3 |
| linux | `compare-symlinks/128/1/status`  | +0.945% | +0.010710 |       +0.655% |             2/3 |
| linux | `regular/deep/1000/3/eager-csv`  | +0.776% | +0.018170 |       +1.808% |             3/3 |
| macos | `regular/deep/100000/1/files`    | +1.530% | +1.110649 |       -1.341% |             2/3 |
| macos | `regular/deep/10/3/eager-csv`    | +1.246% | +0.062315 |       +1.176% |             2/3 |
| macos | `regular/broad/1000/1/eager-csv` | +1.000% | +0.056381 |       +0.749% |             2/3 |
| macos | `regular/deep/100/3/eager-csv`   | +0.875% | +0.053685 |       -2.289% |             2/3 |
| macos | `regular/broad/100/1/eager-csv`  | +0.803% | +0.040762 |       +0.913% |             3/3 |
| macos | `regular/deep/1000/3/eager-csv`  | +0.798% | +0.074524 |       +3.070% |             3/3 |

Shared-host conditions remain a limitation. Mac timings are not pinned to physical cores. The
capture records the current `TZ`, `LANG`, `LC_ALL` and `LC_TIME` values without changing them.
Earlier independent experiments did not record all four values; this diagnosis does not prove
their environment was identical or retroactively establish the cause of every baseline shift.

## Qualification and binary size

Both hosts pass all 332 release targets across the main tree, enabled extras and extras API.
Five relevant suites pass ASan on both hosts, and MSan/TSan on Linux. Three formatter/field suites
ran in the focused qualification; the two engine suites were added after the full timing trial.
Both new translation units pass actual clang-tidy, not merely compilation-database preparation.
The benchmark's initial unused-loop-token warning was corrected explicitly and the affected
translation unit alone was relinted. The untouched test translation unit's successful result is
retained. Original compile-database and benchmark-lint failures remain available.

Regression tests have a separate `cc_test` and passed on the unpatched parent before the library
patch was installed. They cover empty/literal/escaped fragments, UTC and positive/negative fixed
offsets, nonempty locale fallback and exact ASCII/NUL bytes. The changed production code is in a
third-party patched source, outside the ordinary Xff-owned changed-line coverage scope. No
coverage threshold or policy exception is introduced; no changed-line coverage percentage is
invented for that external patch.

| Host  | Parent CLI bytes | Candidate CLI bytes | Difference | Final measured identity         |
| ----- | ---------------: | ------------------: | ---------: | ------------------------------- |
| linux |       18,437,376 |          18,437,472 |        +96 | CLI and benchmark exact SHA-256 |
| macos |        8,274,840 |           8,274,840 |         +0 | CLI and benchmark exact SHA-256 |

The final rebuilt ordinary release CLI and microbenchmark match the frozen measured binaries
exactly on each host. A Linux temporary-directory quota failure during a new image copy was
recovered only after verifying the two owned duplicate/partial files against retained full bytes;
the images now live in RAM-backed storage. Older experiment files were not broadly removed. A
Mac full-suite startup initially lacked sandbox cache access and exited before any test ran;
the same driver then ran with cache access. Neither incident changed production source or
replaced measured results.

Run the manual formatter benchmark with the normal release configuration:

```sh
bazel run --config=clang_release --config=xff_full //tools:time_format_benchmark -- \
  --benchmark_repetitions=9 --benchmark_enable_random_interleaving=true
```

## Retained evidence

[Lossless evidence](cctz-empty-format-20261105.json.gz) retains source/prototype snapshots,
all screen/full case/session samples, independent expected-output hashes, complete-output
preflights, original failures, recovery proofs, actual release flags, qualification logs and
exact binary inventories. Executables remain on their original hosts rather than being
committed. The format stores text once per SHA-256 and reconstructs every named byte string.

Archive: 1,406,215 bytes; SHA-256 `cf9d32c73a8ad4cb65f3924480735f3f0a7900624ca4db1d8abc2eba9a20f25c`.
All 243 named records and 211 unique contents were decoded and verified.

Adopt this narrow library guard after its own public branch is current and passes required CI.
Private qualification does not substitute for the eventual public pull-request context.
