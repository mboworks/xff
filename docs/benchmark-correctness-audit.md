# Benchmark correctness audit and native investigation

## Scope and status

Implementation and native-session handoff: [PR #990](https://github.com/mboworks/xff/pull/990),
branch `audit/benchmark-correctness`. Fetch the branch and record the exact driver commit before running.

The first audit uses the retained Pages snapshot
`d7d0ac89d7b0f74ecc610d4a5d81354a394b793b`, with code based on main
`772994ab7905be77915978f06ea90ae5934aaeef`. Native Linux profiling remains required before attributing its performance
to a particular engine mechanism. No engine performance change has been accepted from this audit.

The snapshot contains 491 raw reports: 125 ordinary CI reports, 38 hosted backfill reports,
214 local reports and 114 PR previews. Of these, 472 contain tool comparisons; 19 early reports
contain only paired measurements. The offline checks cover 316,140 task records, 5,173,200 raw
samples and 3,054,240 plotted values across 6,684 figure views.

## Data and rendering checks

The following checks found no mismatches in the retained snapshot:

- Task keys are unique, belong to their declared grids, and report the advertised input counts.
- Regenerated legacy and prepared fixture models match recorded file counts and fixture hashes.
- Scenario output counts, output hashes and byte counts match the generated fixture expectations.
- Samples have the declared repetitions, finite positive elapsed times, acceptable exits,
  empty diagnostics and consistent output lengths. First output occurs within the elapsed time.
- Measured XFF and reference commands use the recorded jobs and thread allocations.
- Independently calculated fastest-sample estimates match stored relative results.
- All chart orders, CPU selections and metrics map to the correct raw cells: percent height
  is `100 * (1 - XFF/reference)` and factor height is `-log10(XFF/reference)`.
- Packed gzip data reconstructs the expected figures and normalization values.
- Available normalization windows obey their arithmetic and fixture compatibility constraints.

Fixture/oracle checks reuse the driver's fixture model and scenario definitions. They validate
internal consistency, not a separate implementation of the workload specification. Recorded
affinity and source identities are evidence about an earlier run, not an independent observation
of that machine. Arithmetic checks cannot prove that an earlier process executed the claimed
source or exclude thermal and scheduling effects.

The 20k, 50k and 100k CI cells come from the separate Benchmarks workflow and historical backfills.
The Test workflow's smaller grid does not define the entire published CI dataset. Those larger
cells have actual recorded samples; they are not chart interpolation.

### Normalization selection defect

The previous implementation chose one entire report for a commit and machine series before
partitioning by compatible workload. On Linux, newer local v2 reports therefore hid v1
normalization cells. It also discarded older cells missing from a newer partial grid.

Selection now occurs within each compatible cell, with at most one selected measurement per
commit. In the retained local snapshot this restores 49,140 Linux v1 normalization cells.
The 50,544 Linux v2 and 50,544 macOS v1 available cells remain available. The raw chart ratios
were already calculated separately; this defect does not explain the Linux v2 timing jump.

## Historical executable provenance

All 72 available retained Mac executables match their local build manifests and published report
hashes. All 252 reports with build records have tracked build configuration hashes matching the
named source commits: `.bazelrc`, `.bazelversion`, `MODULE.bazel` and
`bazelmod/llvm.MODULE.bazel`. Their `MODULE.bazel.lock` files are generated and ignored, so their
hashes cannot be compared to a tracked Git blob. The 180 other reports' native executables are
on Linux or hosted runners and could not be inspected on this Mac.

Local Linux v2 history contains 72 commits, 38 distinct executable hashes and 72 distinct raw
timing signatures. Representative engine changes, including the arena/segmented storage work
(`#915`), worker regex reuse (`#925`), broad stat parallelism (`#929`) and content batch balancing
(`#930`), each have distinct recorded Linux executable hashes. Repeated executable hashes across
other commits are not sufficient evidence of stale builds; documentation and viewer changes can
leave the executable unchanged.

The driver already built each detached revision with its historical release configuration and
copied the executable into `binaries/COMMIT/xff` before moving the checkout. Building the entire
batch first avoids compilation competing with measurement. The missing safeguard was an
explicit comparison to that build identity at the start of the later measurement phase.

The driver now verifies that handoff, the resulting report identity and the executable after
measurement. New builds also verify clean source/HEAD/tree before and after compilation, disable
system/home RC files, reject a workspace user RC, and resolve the configured executable through
Bazel. Native build output is visible while remaining in the retained log.

A native Mac pilot rebuilt `c5f3dc93efd18fdd18fc3b7a5538c23bf3bafda0` and
`0c80a02e7329f89595f3c9a63274d61ecbff3496`, then measured both saved executables after all builds
finished. They produced distinct hashes; both reports matched their own build. Repeating the
identical command reused both verified executables and completed reports. This checks the
actual checkout/build/copy/measure sequence, not just its mocked unit tests. The pilot used
10-file fixtures and one sample; it is a provenance check, not a performance conclusion.

The isolated audit checkout ran 227 Python benchmark tests with five skipped; after applying
the changes to the working checkout, its expanded suites ran 238 tests with five skipped. Bazel targets
`//tools:benchmark_backfill_test` and `//tools:benchmark_normalization_test` also pass. Skipped
native comparison and Linux affinity cases still need their native environments.

### Binary evidence states

Use these four states for the measured XFF executable:

- **Not recorded:** no usable measured binary hash is retained.
- **Not verified:** a hash is recorded, but corroborating evidence is missing, invalid or
  contradictory. Preserve and report the specific reason; a mismatch must never count as consistent.
- **Consistent:** all available, independently comparable hashes and revision identities agree.
  At least one corroborating identity is required. This is a check of recorded evidence, not an
  independent verification of the executable bytes or compiler behavior.
- **Verified against Retention:** the original retained executable was read, its SHA-256 matches
  the measured hash, and the available report/build identities are consistent. Record which
  retained artifact was checked, the verification time and the report identity it verifies.

The viewer derives these states from the available report and retention evidence. The native
investigation should retain that evidence independently of the UI. All 491 downloaded reports contain a
binary hash: 472 have a tool comparison hash, and the remaining 19 have paired measurement hashes.
Recover any missing display metadata from those original observations. A new rebuild cannot
retrofit the binary identity of a historical measurement.

Regular CI retains report artifacts for 30 days and executable build artifacts for three days;
hosted backfill artifacts omit executables. Preserve available original binaries, build manifests
and logs before cleanup or artifact expiry. The initial Mac audit left Linux executable
verification outstanding; the native Linux follow-up below now covers the available local
Linux artifacts. The 72 checked Mac binaries do not verify the Linux measurements.

On Linux, verify each retained executable against its original report with the PR's
`tools/benchmark_provenance.py --report=... --binary=... --retention-reference=...` command.
Retain the resulting `binary-verification.json` next to that report; it binds the check to the
measurement without changing the historical observations. See
[Binary evidence in Version](benchmark-comparisons.md#binary-evidence-in-version) for CI archive
verification and the full state semantics. Record mismatches before attempting new builds.

## Linux timing evidence and first hypothesis

For local Linux v2 `files` versus `rg`, the medians across retained reports show:

| Layout | Workers | Files | XFF ms | rg ms |
| :----- | ------: | ----: | -----: | ----: |
| broad  |       3 |    10 |  1.420 | 2.705 |
| broad  |       3 |    20 |  4.843 | 2.699 |
| broad  |       1 |    10 |  1.363 | 1.318 |
| broad  |       1 |    20 |  1.413 | 1.332 |
| deep   |       3 |  1000 |  1.905 | 2.719 |
| deep   |       3 |  2000 |  5.511 | 3.211 |

These are medians of per-report fastest-sample estimates, not pooled sample estimates. XFF's
absolute timing rises at the same boundary; the trough is not merely a faster reference tool.

Prepared broad layouts acquire a second descendable sibling at 20 files, and deep layouts at
2,000 files on the published grid. `Walker::SubmitSubdirReads` starts its `ReadPool` when there
are at least two siblings. This matches the discontinuity and is a stronger initial hypothesis
than an arena capacity guess. It does not prove whether thread creation, Abseil mutex setup,
queue/future allocation, worker wakeups or another associated operation supplies the cost.

Large-file scaling also remains worse than `rg` for several tasks. Removing a fixed startup
cost would not by itself fix that per-entry slope. Investigate the two effects separately.

## Directed native investigations

### Shared setup and evidence exchange

Use source revision `772994ab7905be77915978f06ea90ae5934aaeef` initially on both systems, with the same audit driver changes.
Record the driver's hashes and exact source commit before either session edits code. Start new
batch directories: driver changes intentionally fail the frozen-contract resume check.

The audit driver is on [PR #990](https://github.com/mboworks/xff/pull/990), branch
`audit/benchmark-correctness`. Checking out the baseline
source revision alone does not install the driver fixes. Fetch this branch into a separate
worktree, record its exact commit, and keep that checkout unchanged for the first baseline run:

```sh
git fetch origin audit/benchmark-correctness
git worktree add --detach /tmp/xff-native-audit FETCH_HEAD
git -C /tmp/xff-native-audit rev-parse HEAD
```

Run the PR's driver with `--repo` pointing to a repository containing the baseline commit; it
will create its own historical build checkout. Copy the worktree before instrumenting the driver.
This separates the measured source revision from the audit driver revision. The resulting
`batch.json` records the driver hashes; exchange those along with the PR checkout commit.

Use the complete published file grid:
`10 20 50 100 200 500 1000 2000 5000 10000 20000 50000 100000`, with workers `1 3 10`,
`broad/v2` and `deep/v2`, nine repetitions and the fastest seven estimator. Prepared layout
topology depends on the full grid; substituting a smaller grid changes the workload. If the
Linux affinity allowance cannot provide ten physical cores, record the smaller allocation
explicitly and align the control experiment rather than silently changing it.

Build with the revision's `clang_release` configuration. Keep historical toolchain comparisons
separate from experiments that deliberately freeze a compiler across different source commits.
Preserve `batch.json`, `build.json`, `build.log`, reports, fixture hashes and the complete raw
sample lists. Verify native Linux retained executables against their existing manifests and
published hashes before reusing them as historical evidence.

For the unmodified baseline, start with this command, adding each grid size as a repeated
`--files` argument if overriding defaults:

```sh
python3.13 /tmp/xff-native-audit/tools/benchmark_backfill.py \
  --repo="$PWD" --output=/tmp/xff-perf-baseline-v2 \
  --series=linux-perf-audit --revision=772994ab7905be77915978f06ea90ae5934aaeef \
  --cpus=1 --cpus=3 --cpus=10 --layout-revision=v2 \
  --repetitions=9 --keep=7 --fixture-parent=/dev/shm --require-memory --run
```

On macOS, change the series and fixture parent to the actual local storage used, and omit
`--require-memory` unless that storage is a verified RAM disk. Record this difference; raw
milliseconds from unlike hosts/storage are not directly comparable.

Exchange findings after provenance verification, after profiles, and before accepting a change.
For each result share commit/tree, compiler/build flags, binary hash, fixture hash, command,
affinity, raw timings, correctness output and profile artifact. Share measured hypotheses,
including rejected ones, rather than screenshots alone.

### Linux priority

1. Reproduce the broad 10-to-20 and deep 1k-to-2k jumps with the unchanged binary and complete
   prepared layout grid. Repeat independent sessions and inspect all nine samples. Reorder
   measurement cells in a controlled harness while preserving fixtures and warm-up policy to
   test fixed traversal-order, cache and thermal bias.
2. Compare `--jobs=1` to `--jobs=3` and `--jobs=10` on exactly the same roots. Instrument or profile
   first pool creation, thread startup, mutex initialization, each queue/future operation and
   teardown. Use `perf stat` for task clock, context switches, faults and instructions, then
   `perf record` for stacks. For sub-millisecond intervals, aggregate repetitions while retaining
   fresh processes and correctness checks. Profile overhead must not enter accepted timing data.
3. In an experimental branch, force directory reads to remain serial while leaving content
   matching unchanged. Then test lazy initialization and a minimum amount of independent work
   before pool creation. If the jump disappears, isolate which operation accounts for it before
   choosing a threshold. Include stat-heavy and slow-storage workloads so a memory-backed
   fixture optimization does not suppress useful I/O parallelism.
4. Profile 20k/50k/100k single-worker cases separately: directory enumeration, entry/path
   construction, collection allocation, sorting, expression dispatch and output formatting.
   Measure allocations and actual peak memory before changing arena capacity or reserves.
5. Examine matcher startup thresholds (16/64 entries), content batch sizing, coordinator work
   and nested pool oversubscription for content tasks. A directory-pool result is not proof that
   the content matcher should use the same threshold.

Linux reported RSS needs an independent check: small XFF, find and rg runs all show a similar
roughly 25 MB floor. The forked Python launcher may contribute a pre-exec high-water mark to
`wait4` RSS. This is a hypothesis, not an established defect. Compare a direct native launcher
and `/usr/bin/time -v` against the Python worker before using this metric to diagnose arenas.

### macOS control and acceptance

Run the same fixtures and experimental commits on the M5 Pro. Profile thread creation,
coordinator work and allocation with native sampling/Instruments. Record performance/efficiency
core scheduling limits; a worker count is not Linux-style verified physical-core affinity.

Accept an engine change only after repeated A/B measurements preserve result hashes and improve
the targeted cells, with no material regressions in single-worker, large-grid, deep, content,
metadata and slow-storage controls on both platforms. Run affected correctness/concurrency tests
and appropriate sanitizers. Report absolute XFF time, reference time and same-run ratio together.
The current evidence supports investigating specific costs; it does not establish that XFF
will beat every reference tool on every workload.

## Native Linux follow-up (2026-10-09)

The native session uses merged audit driver commit
`4ea895f63106f05df7c90b629d20433ae1e03016`. The original engine baseline remains
`772994ab7905be77915978f06ea90ae5934aaeef`; both source revisions produce the same
retained executable SHA-256,
`f97833a8eb45f49b596665b47911c2df9a2558619778512fd31d1b38f2e56106`.
Baseline builds use `clang_release`, with the historical ICU 70 build-library
path supplied explicitly. Complete prepared-grid runs retain nine raw samples
per cell, use the fastest seven estimator and store fixtures on verified tmpfs.

### Retained Linux executables

All 142 completed reports under the local Linux benchmark batch inventory match
their original retained executables and their original build manifests. They
cover 72 distinct commits and 38 distinct executable hashes; no mismatches were
found. Verification uses `benchmark_provenance.verify_retention`, checks the
standalone manifest against the report's embedded build record, and records a
measurement-bound proof for each report. Historical observations are unchanged.

The original reports and binaries remain under `/home/marcus/xff-benchmarks/batches`.
The native proof inventory is retained at
`/tmp/xff-linux-retention-verification/summary.json`, with per-report
`binary-verification.json` files below that directory. These checks verify the
available local Linux artifacts; they do not verify expired hosted artifacts.

### Worker scheduling experiment

Linux `kernel.perf_event_paranoid` is now `0`, and native `perf` hardware counters
are available. Startup-boundary profiles include worker creation and teardown,
Abseil mutex slow paths and scheduling overhead. The later symbolized controls
retained in [PR #995](https://github.com/mboworks/xff/pull/995) identify the
Linux/x86 one-time Abseil TSC frequency calibration (1 ms and 2 ms sleeps) on
the first queue mutex path as the dominant fixed cliff. They do not attribute
those sleeps to each thread's creation. Keeping directory reads serial
removes the small-work cliff but loses useful concurrency on large broad trees.
[PR #991](https://github.com/mboworks/xff/pull/991) admits directory reads only
when at least 64 independent sibling directories are available. Its hosted Linux
and macOS tests, sanitizers, all three benchmark shards on each platform and
combined benchmark comparison pass.

The initial shared-executor experiment shares one traversal-owned executor across directory read-ahead,
eager stat work and eligible content matching. Each admitted batch submits at
most one drain job per useful worker. Directory jobs repeatedly claim independent
listing reads; stat jobs claim 128-entry chunks; matcher jobs claim entry chunks.
Only the coordinator recurses, evaluates traversal controls and consumes ordered
results. Its pools are traversal-owned, so two comparison-side coordinators can
still create separate executors before a later comparison-only pool starts.

A separate command-ownership follow-up removes that gap: `RunFind` owns one
executor, each comparison side walks on the calling coordinator, and eligible
file pairs reuse the same executor after both inventories are collected. The
`--jobs=N` participant budget includes the caller, leaving at most `N-1`
background workers. The caller also claims independent stat, content and
comparison chunks. This is a concurrency experiment, not a performance
acceptance claim. Directory drain fairness, bounded read-ahead, cancellation,
remaining-work admission, slow-storage controls and native M5 Pro validation
remain outstanding. The fresh native accounting and exact-source baseline
evidence live separately in PRs #994 and #995; this branch does not mix their
measurement artifacts into a new engine result.

Correctness checks include nested read-ahead above the admission threshold,
partial stat chunks with missing entries, repeated matcher transitions and
executor reuse. An early-stop test deliberately holds a prefetched sibling read
open while the coordinator quits: `Walk` must wait before destroying the walker,
even though its executor survives for another phase. Explicit Abseil completion
signals replaced the experiment's standard-library futures after expanded TSan
coverage reported a shared-state completion/deallocation race.

The interrupted `/tmp/xff-perf-shared-executor-v2` run is diagnostic only:
correctness builds overlapped its measurements, and its measured source predates
the final scheduling changes. It supplies no acceptance timing evidence.

### Bounded directory-frontier experiment

The command-ownership follow-up is draft [PR #996](https://github.com/mboworks/xff/pull/996).
A separate candidate based on that branch replaces whole-sibling directory drain
jobs with an ordered read-ahead window. Its persistent command executor dispatches
one leaf directory read per speculative task and alternates speculative and
foreground queues when both contain work. This is nonpreemptive job-boundary
fairness, not a guarantee against long foreground drains or blocking VFS calls.

Nested and mounted-filesystem walks share at most twice the started background
worker count in unfinished reads and a provisional 8 MiB charged-storage allowance.
Pending requests charge bookkeeping/path storage before dispatch; completed
speculative listings add vector, string and status charges. Finished listings
release execution slots while keeping byte charges until consumed or destroyed.
This distinction permits deeper reads while older sibling listings remain cached.
Required listings, retained ancestor listings, in-flight VFS storage and allocator
overhead are excluded: the allowance is not a total-memory or RSS bound.

Per-request and frontier cancellation skip unneeded queued reads after pruning or
termination. Already-running VFS calls finish, and the walker still joins its jobs
before borrowed state is destroyed. Oversized completed speculative listings are
discarded and read on demand; a listing already required by the caller belongs to
ordinary traversal working memory and can be transferred without a duplicate read.

In-memory controls cover shared slot/byte admission, concurrent retention,
reservation ownership, queue-class fairness, pruning before filesystem access,
early-stop read counts, and nested progress while parent siblings remain cached.
The inherited directory/stat/content admission policies are deliberately unchanged
in this experiment. In particular, accumulated completed content work is still
not valid evidence of sufficient remaining work. The bookkeeping estimate, retained
byte policy and task granularity need allocation profiles and repeated native
measurements. No timing, total-memory improvement or macOS acceptance is claimed.

### Caller-participation control

A deterministic 65-sibling in-memory control identified a separate gap in the
bounded-frontier candidate: with `--jobs=2`, only one background reader touched
child directories, peak concurrent reads were one, and the two-directory
rendezvous timed out. The caller never read a child directory. With larger
budgets, the observed reader identities likewise excluded the caller. Ordered
output, exact-once reads and the thread cap still passed; those properties alone
did not demonstrate useful caller participation.

The follow-up reserves the first uncached required directory for the caller and
queues later siblings. A required queued read is represented by move-only work
that either the caller or worker can claim once. An already-claimed read still
requires waiting for its result; no arbitrary unrelated job is run on that wait.
The command budget remains caller-inclusive, with at most `N-1` background
threads, reused across both comparison inventories and pair comparisons.

Controls verify caller/worker overlap for budgets 2/3/10, serial-equivalent
ordered visits and exactly one read per child. An occupied-worker control forces
the caller to read its required queued directory before releasing that worker.
Executor controls cover queued claims, already-running claims and simultaneous
caller/worker claims, including move-only producer ownership. Cancellation,
shared byte/slot accounting, error reporting and borrowed-state lifetime remain
part of the required validation. The extra claim state and callback allocation
need native allocation and timing evidence. Remaining-cost admission and useful
task size are unchanged; no performance or native macOS acceptance is claimed.

### Memory-accounting finding and remaining acceptance work

Direct `/usr/bin/time -v` runs show approximately 5.3-5.4 MiB peak RSS for a small
XFF invocation, while the Python comparison worker reports approximately
22-23 MiB for the same command. A trivial native command also reproduces the
Python worker's high floor. The forked launcher's inherited high-water mark must
be corrected or explicitly excluded before using those reported peaks to choose
arena sizes. This is separate from the worker scheduler experiment.

The complete audit remains open. Final shared-executor timing must be collected
after every build has finished, using the complete prepared grid. Repeated
controls must cover content, eager metadata and slow storage. Separate evidence
in [PR #995](https://github.com/mboworks/xff/pull/995) now retains the exact-source
Linux baseline, enumeration-only allocation profiles and coarse counters/stacks
for 20k/50k/100k single-worker workloads. Those baseline diagnostics do not complete
precise phase-level attribution or allocation controls for the other workloads,
platforms or new scheduler candidates. Hosted macOS checks provide portability evidence for PR #991;
they do not replace the specified native M5 Pro A/B and sampling controls for
the shared-executor experiment.
