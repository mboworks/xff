# Lazy evaluation and shard setup qualification (2026-11-08)

## Outcome and scope

This follow-up to PR #1143 reduces ordinary predicate evaluation work and avoids
constructing deferred-entry maps and shard regular expressions when unused.
Qualification compares production full-feature CLIs with merged PR #1149
(`efffe2529f26c1c7ca61763fa5fbb17efd573f6f`) and equivalent traditional tools.
The measured code revision is `1ea02a41d7dcf25c043e1079c98a9e60d6d73290`.

Three workers remain the primary practical target. One diagnoses serial work;
ten exposes contention. Every declared size and broad/deep shape is retained.
The results do not establish that XFF always beats GNU find or ripgrep.

## Implementation

- Bind the required immutable predicate descriptor once in the shared evaluator.
  Repeated OptionalRef dereferences performed repeated presence checks; one binding
  preserves the required check and all metadata, safety, invalidation and control
  dispatch ordering.
- Construct each entry's deferred evaluation memo only when the expression has
  deferred nodes. Initial decisions are immutable and run-scoped; retained
  candidates still own their memo, outputs and decisions. Ordinary entries avoid
  constructing unused maps. Deferred expressions retain their existing path.
- Build `shard::Matcher` only for shard grouping or `-shard-status`. Startup profiles
  showed `RunFindCore -> shard::Matcher::Make -> RE2::Init` compiling the built-in
  shard patterns even for plain `-type f -print0`. Optional observers let ordinary
  collections and `-top` operate without that matcher. Explicit shard consumers
  keep eager pattern validation and existing matching/grouping behavior.

## Qualification contract

Both hosts built clean exact checkouts of `//xff/cli:xff_full` with
`--config=clang_release --config=xff_full`, without system/home Bazel rc files.
The configured executable was queried with matching flags, immediately copied,
and SHA256 checked. Retained CppLink actions establish ThinLTO. Builds and tests
finished before measurement. Adding this report does not change the measured
C++ sources; the frozen source revision above identifies the binaries.

Linux used the verified tmpfs fixture on drogon. Guest affinity uses CPUs reported
as distinct guest cores; this does not establish physical host pinning. macOS used
an explicitly approved disposable 2 GiB case-sensitive HFS+ RAM image, whose live
UUID/device/image were checked against its new ownership receipt. macOS worker
counts are requests without CPU affinity. Neither measured fixture is on SSD;
absolute times from these two filesystems are not pooled.

The separate `extended-production-v1` suite checks file listing, native-case name
filtering, metadata/time formatting, regex content search and TAR listing against
GNU find, ripgrep and GNU tar. Native sizes are 100, 10,000 and 100,000; archive
sizes are 100, 1,000 and 10,000. Workers 1/3/10 and both shapes give 90 cases per
revision, with a separate identical-binary control for every case.

Each round rotates candidate, fixed baseline and traditional reference through
execution order. Every output must match an independent multiset oracle, including
record counts and duplicates, with zero exit status and no diagnostics. Warmups
and all raw samples are retained. Final binary, fixture, tool and resource-launcher
checks must pass. The chart estimator is the mean of the fastest measured rounds;
per-cell 95% bootstrap intervals use all paired rounds and describe within-cell
variability, not independent-machine confidence.

Initial 100/100,000-file listing screens use 27 rounds/21 keep. Full five-task
qualification uses 9/7 after those screens. A separate Linux small-workload repeat
uses 54/42. These are distinct immutable campaigns; samples are never relabeled
or silently pooled. No profiler samples enter accepted wall-time results.

Seven existing correctness suites pass on both platforms: engine run, expression
execution, expression program, parallel matching, record buffer, shard matcher
and shard grouping. Cached suites are reported as cached in retained Bazel logs.

## Results

The primary three-worker results below equally weight broad/deep shapes. Negative
elapsed changes mean less time. Reference ratios below one mean XFF was faster.
All other cells, intervals and controls are retained in the companion
[qualification data](lazy-evaluation-shard-setup-20261108.json).

| Platform | Task            |   Files | Elapsed change | Candidate/reference | Identical control change |
| -------- | --------------- | ------: | -------------: | ------------------: | -----------------------: |
| linux    | `files-exact`   | 100,000 |         -0.76% |              1.1126 |                   -0.36% |
| linux    | `name-native`   | 100,000 |         -3.37% |              1.0216 |                   -0.49% |
| linux    | `metadata-time` | 100,000 |         -0.98% |              1.1251 |                   -1.18% |
| linux    | `content-regex` | 100,000 |         -0.67% |              0.6809 |                   -0.34% |
| linux    | `archive-tar`   |  10,000 |         -4.91% |              0.7486 |                   -1.39% |
| macos    | `files-exact`   | 100,000 |         -5.03% |              1.0061 |                   -7.58% |
| macos    | `name-native`   | 100,000 |         -4.11% |              0.7205 |                   +0.75% |
| macos    | `metadata-time` | 100,000 |         -1.14% |              0.6658 |                   +2.67% |
| macos    | `content-regex` | 100,000 |         -0.40% |              0.8876 |                   +2.89% |
| macos    | `archive-tar`   |  10,000 |         -0.38% |              1.1899 |                  +27.95% |

Across the complete 90-case candidate grid, the elapsed geometric ratio is
`0.991092` on Linux (control `0.998757`) and `0.987598` on macOS (control
`1.005969`). These averages are descriptive. Full-grid identical-control p95
factors are `1.0393` and `1.2213`, respectively; several macOS content-search
cells are noisy. Do not interpret the aggregate as a precise universal speedup.

The independent 27-round listing screens show 100,000-file ratios `0.985856`
on Linux and `0.977096` on macOS, equally weighting six shape/worker cases.
Corresponding controls are `0.999689` and `1.001620`. This supports a modest
listing gain independent of the noisier full-grid population.

Linux full qualification had a 4.7% slower small metadata cell. The separate
54-round repeat did not reproduce it: the 30-case small-workload aggregate was
`1.001746`, with control `0.993124`. Two different deep/single-worker content
and archive cells still have slower intervals, around 1%. The data does not
establish improvement in every small task or a precisely zero startup cost.

Separate Linux userspace instruction counters, nine validated rounds per
binary/size, give candidate/baseline ratios `0.905680`, `0.912077` and `0.912544`
for broad single-worker listing at 100, 10,000 and 100,000 files. About 19.2 million
fewer instructions per 100,000 files is distinct from the smaller wall-time gain.

Each full campaign contains 180 completed cells and 5,400 independently checked
participant executions including warmups. The small Linux repeat adds 9,900
checked executions; each listing screen adds 2,016. Final integrity checks passed.

## Minimal production CLI cross-check

Continuous CI uses `//xff/cli:xff --config=clang_release`, without the full
extension configuration. Both hosts therefore also built and froze that exact
target at both revisions, checked the ThinLTO link actions, and measured the
same 100/100,000-file listing screen at workers 1/3/10 (27 rounds/21 keep).
This separate population covers listing only; it makes no minimal-CLI archive
or other-task claim.

| Platform |   Files | Candidate/baseline | Identical control | Interpretation                                       |
| -------- | ------: | -----------------: | ----------------: | ---------------------------------------------------- |
| Linux    |     100 |           0.976608 |          0.989229 | Small gain with noisy controls                       |
| Linux    | 100,000 |           0.985538 |          1.010545 | Large gain agrees with the full-feature screens      |
| macOS    |     100 |           1.004484 |          0.993932 | No demonstrated small-case speedup                   |
| macOS    | 100,000 |           0.968880 |          1.056338 | Inconclusive elapsed gain: controls are too variable |

All 24 cells per host and final checks passed. Control p95 factors were `1.0608`
on Linux and `1.4369` on macOS. All six large macOS control intervals cross one;
there is no established systematic bias, but this run cannot prove a small
minimal-CLI speedup. It remains retained rather than being silently omitted.
Full-feature macOS and minimal macOS results must not be conflated.

The owned RAM disk was released after all campaigns and integrity checks finished.
The release receipt binds the live volume UUID, device and `ram://` image before
detach and verifies that the image/mount are absent afterward. Retained raw data
is outside that mount and has a separate hashed archive on drogon.

## Remaining find gap

A separate post-collection syscall check of the minimal production CLI and GNU
find used 100,000 files and three workers, with output validation on all four
commands. Both tools issued 124 `getdents64` and 440 `write` calls on the broad
tree; both issued 202 and 3,443, respectively, on the deep tree. There is no
extra metadata syscall per file in these listing cases. Traced durations are
diagnostics and are not accepted benchmark timings.

Ordinary minimal-CLI samples still show roughly 4-6 ms extra userspace CPU per
100,000 files relative to GNU find. Directory/output syscall counts therefore
do not explain that remaining userspace gap. Predicate dispatch, entry setup and
output processing remain concrete profiling targets. These observations do not
attribute the gap to physical disk throughput.

## Rejected experiments and limits

Conditional deferred state alone saved about 0.6% of userspace listing instructions
but did not establish a wall-time gain. A borrowed record-terminator capture removed
a sampled stack store/load sequence but did not establish a gain; that code is
excluded. Both experiments and their raw results remain available.

The descriptor-only follow-up saved about 8.5% of large-listing instructions and
improved large listings, but its Linux small-workload regression was independently
confirmed at roughly 2.6%-3.0% across five task groups. It was not published as the
accepted change. Skipping unused shard compilation addresses concrete startup
work; the exact cause of that earlier timing regression was not established.

All task groups, controls and per-cell results must remain visible, including
noisy or slower cells. Instruction reductions are distinct from elapsed-time
reductions. Kernel traversal, process startup and output remain significant.
This change does not alter the core CI benchmark contract or claim that the
130-revision extended history is already integrated into continuous publishing.

## Retained evidence

Raw samples, manifests, executable hashes, build logs/link actions, correctness
logs, controls, profiles and generated 3D viewers are retained under
`/private/tmp/xff-per-entry-evidence-20261108` locally. Linux originals are under
`/home/marcus/Documents/xff-per-entry-followup-20261108` on drogon. The approved
RAM disk is temporary fixture storage, not the retained evidence location.
