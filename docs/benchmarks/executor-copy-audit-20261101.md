# Executor and allocation audit after the Abseil clock correction

Keep main's runtime scheduling. Neither the directory-admission policy in #991 nor the
remaining-work policy in #1001 is ready to merge. Shared executors and bounded read-ahead
have useful Linux results, but their current combinations also regress macOS and specific
content workloads. Removing obsolete matching admission guards fixes the largest content
regressions; it does not establish a general improvement from the full executor stack.

The narrow implementation follow-up removes temporary collection-key and comparison-path
construction. It preserves owned keys and retained entry lifetimes. Batch-vector recycling
remains an experiment: the added API and lifetime transition do not have a sufficiently
consistent benefit to justify adoption.

## Scope and provenance

The seven open runtime/measurement PRs were #991, #992, #996, #997, #998, #1000 and
#1001. Their historical ancestry is not their minimal implementation dependency graph.

A concurrent inventory review classified [PR #1036](https://github.com/mboworks/xff/pull/1036)
as an independent parallel uploader component: published/local revision-overlap inventory
and selection display. It adds one read-only scan of published batch/status files plus set
intersections outside the xff CLI and benchmark measurement loop. It changes uploader
documentation and Python tooling, with no C++ target, normalization or measurement-data
write change. It merged independently and needs no runtime timing; it is not absorbed
into the seven-PR runtime graph or the measurements below. Subsequently merged
[PR #1038](https://github.com/mboworks/xff/pull/1038) adds uploader dataset-recipe
overlap/incomplete-batch inventory and bounded publication rebase/regenerate/retry in
Python, workflow and documentation files. It likewise changes no C++ runtime or
benchmark measurement loop and does not alter the retained raw samples. The publication
branch includes current main `b68a9c3a58`; the measured binaries retain the historical
baseline and source identities below.

Experiments use main `974d137026818bba1c1deaf6cfc4fffe676b314e`, including #1034's
`ABSL_USE_UNSCALED_CYCLECLOCK=0` for target and execution configurations. The macOS
baseline executable uses the earlier clock-fix commit with the identical complete Git
tree `cf0755b74daea7bfdec70a96f260f327c37af885`. The shared manifest generator also
labels Linux main with that tree-equivalent earlier commit; the actual Linux build is
merged main `974d137026`. The archive records this metadata correction explicitly and
retains the original manifest labels.

Existing runtime PR heads were not rewritten to manufacture a green stack. Synthetic
local snapshots port their runtime changes onto current main and exclude #991 entirely.
The retained evidence includes complete source patches, exact commits, binary identities,
raw samples, output identities and drivers. Results validate those snapshots, not the
unmodified GitHub heads.

Linux is drogon's KVM guest reporting a Ryzen 9950X CPU, with ten available vCPUs and tmpfs fixtures.
macOS is the Apple M5 Pro with eighteen logical CPUs and warm APFS fixtures. Use
within-platform paired comparisons; these are different storage and hardware contracts.
Both builds use `--config=clang_release`, Clang 23.1.4 and ThinLTO. Native
measurements use `//xff/cli:xff`; engine regex measurements filter RE2 cases. These
are not full-plugin release startup, archive-content, PCRE2 or JIT measurements. Builds and lint finish
before timing begins on each host.

## Measurement contracts

Fresh-command tests span ten through 65,536 files, broad and nested trees, listing,
metadata, content matching, long collection names, and small/large comparison entries.
The comparison fixture has independently generated left-only, right-only, different and
identical categories. `--compare-select=all` explicitly requests all four. The collection
fixture uses a valid long identifier; the initial pilot caught and corrected an invalid
hyphenated test name before final measurement.

Each native case has a discarded warmup, nine interleaved measurements per variant,
the mean fastest seven, and three sessions with forward, reversed and seeded case
orders. Linux pins the launched command to its requested 1/3/10 CPU allowance; macOS
uses the worker allowance without pinning. A native fork/exec/wait helper excludes
Python startup, fixture construction and output verification from elapsed time.
Every invocation must pass an independent path/size digest or complete JSON-record oracle.
The initial ten-variant native matrix has 53 cases, 159 case/session rows and 15,900
successful invocations per platform. Supplemental tests retain fresh main and original
candidate controls rather than comparing measurements taken hours apart.

Separate Google Benchmark executables exercise the in-memory VFS, collection ownership,
and comparisons. The rg and comparison cases use registered 1/4 engine job allowances; collector cases
use one/four Google Benchmark threads with independent `Collections` instances. They use
nine repetitions,
mean fastest seven and two independently ordered variant sessions, with a 0.02-second
minimum measurement interval. These run without CPU affinity. Their fixture setup,
calibration and repeated in-process execution differ from fresh-command timing: do not
pool their absolute times with native results. The collector fixture always uses the short key `all`; its `1`/`128` argument is
path length, not collection-key length. Its checksum is retained with `DoNotOptimize`,
not checked against an independent oracle. The rg/comparison harnesses check initial
output counts/shape and successful iterations, rather than every complete output record.
The in-memory tests cover cheap uniform
content, clustered/interleaved expensive content, native match lines and rg file selection.

Percentages below are elapsed-time changes: negative is faster. Native cells use the
median of the paired session changes. In-memory cells use the median of two paired
session changes. Small differences are observations, not universal wins or significance
claims. In particular, byte-identical Linux collection benchmark executables under the
key-only and recycler labels produced different timings. Those are negative controls,
not evidence that recycling changes collector performance. The macOS recycler also has
an unexplained single-session content outlier; this is not promoted into a causal claim.

## #991: reject the current directory admission policy

The independent reassessment has 9,240 oracle-correct invocations across both platforms,
including Linux injected-latency and warmed-pool controls. The four-millisecond gate and
synchronous first-child sample remain a poor tradeoff after the clock correction.

| Case                                        |     Main |     #991 | Time change |
| :------------------------------------------ | -------: | -------: | ----------: |
| Linux, eight cheap files, 3 workers         | 0.822 ms | 0.608 ms |      -25.9% |
| Linux, eight slow children, 3 workers       | 7.325 ms | 9.361 ms |      +27.8% |
| Linux, eight slow children, 10 workers      | 3.411 ms | 5.447 ms |      +59.0% |
| macOS, broad 4,096-file listing, 3 workers  | 6.311 ms | 6.798 ms |       +7.3% |
| macOS, nested 4,096-file listing, 3 workers | 6.857 ms | 7.465 ms |       +8.8% |

The remaining tiny Linux saving is about 0.21 ms, rather than the former roughly
3.4 ms calibration cliff. The slow-child case pays an unnecessary serial sample before
parallel work. Large ordinary scans mostly remain near baseline. Do not replace the
old gate with another constant fitted to these machines. JC07 remains open for genuine
startup/queue improvements and heterogeneous/physical slow-storage validation.

## Runtime integration tradeoffs

The following cells compare cumulative snapshots against main, with three workers.
They deliberately retain both improvements and regressions.

| Workload                           | Linux #996 | Linux #998 | macOS #996 | macOS #998 |
| :--------------------------------- | ---------: | ---------: | ---------: | ---------: |
| Broad 65,536-file listing          |     -21.7% |     -19.0% |     +11.8% |     +11.7% |
| Broad 65,536-file metadata         |      -1.8% |     -12.7% |      +0.6% |      -1.5% |
| Nested 4,096-file listing          |      -6.4% |      -1.6% |      +6.1% |      +2.3% |
| Long-name collection, 65,536 files |      +8.9% |      +9.2% |     +39.6% |     +36.3% |

#997's marginal Linux change relative to #996 improves the large metadata case by
11.9%, but slows nested listing by 17.5%. #998 recovers 10.4% of that nested-listing
cost while retaining most large-tree metadata improvement. Its claimable-read/frontier
mechanism therefore has useful evidence, but not a universal acceptance result.

The bounded frontier also has a substantial memory benefit in the already measured
65,536-file metadata case with three workers. Median whole-process peak RSS across all
27 measured invocations per variant is:

| Platform |      Main |      #996 |     #997 |     #998 |
| :------- | --------: | --------: | -------: | -------: |
| Linux    | 20.81 MiB | 20.06 MiB | 6.25 MiB | 6.39 MiB |
| macOS    | 18.09 MiB | 17.34 MiB | 5.44 MiB | 5.47 MiB |

#997 therefore reduces peak RSS by about 69% relative to its immediate #996 predecessor
on both platforms. #998 retains that reduction. This is a demonstrated benefit for this
fixture, independent of a universal latency claim. It is not a worst-case memory bound
for arbitrary directory width, path size, archive nesting or storage behavior.

#996 changes command ownership and makes the caller part of the allowance. Main's
phase-specific pools can have different simultaneous runnable-thread counts. Linux
native affinity constrains both to the same CPU set; macOS does not enforce affinity.
Do not describe these results as pure queue microbenchmarks. Profiling must separate
budget accounting, directory coordination and collection callback costs before changing
those mechanisms. The macOS collection regression has not been proven to have a single
cause.

#1001's marginal effect relative to #998 slows the nested 4,096-file content case by
54.8% on Linux and 35.2% on macOS. The dedicated-pool standalone port also regresses
several Linux content cases by 45-80%. The cumulative and `996+1001` controls show that
these losses do not require a particular directory-frontier implementation. Its
3,500-microsecond cold-start allowance describes the removed calibration behavior;
keeping the policy because some cheap cases improve would hide serious counterexamples.

## What each PR contributes

| PR / control | Change isolated                                                   | Decision                                                                                                         |
| :----------- | :---------------------------------------------------------------- | :--------------------------------------------------------------------------------------------------------------- |
| #991         | Directory admission and serial first-child sampling               | Reject current policy; preserve the independent query repair in #1035.                                           |
| #992         | Shared traversal/content executor                                 | Keep draft; original matching admission is harmful, and removing it still needs workload-specific qualification. |
| #996         | Command ownership, comparison sharing, caller-inclusive allowance | Hold: Linux gains coexist with substantial macOS collection regressions.                                         |
| #997         | Bounded/cancellable read-ahead                                    | Useful latency/RSS gains; full integration retains nested-tree and macOS costs.                                  |
| #998         | Caller claims required queued directory reads                     | Useful mechanism with #997; full combination still fails a general no-regression claim.                          |
| #1000        | Executor lifecycle/dispatch diagnostics                           | Extracted into isolated #1035.                                                                                   |
| #1001        | Remaining-work matching planner                                   | Reject current cost model and retain the counterexample workloads.                                               |
| C01 + C03    | Temporary collection/category key construction                    | Retain narrow owned-key fixes; no universal wall-time claim.                                                     |
| C02          | Reclaim completed matcher batch storage                           | Defer added API pending stable benefit and allocation/RSS evidence.                                              |

#992 is the executor API prerequisite for #996. #997 uses that API and the caller-inclusive
budget inherited from #996, but does not need its comparison integration. #998 uses
#997's frontier and claimable-read machinery. #1001 does not inherently need those
directory changes: it was measured cumulatively, with #996 alone, and on main's dedicated
matcher pool. The standalone pool wakes previously started inactive workers, so its dispatch
mechanics differ from the shared executor and are explicitly retained as a limitation.

Four supplemental controls remove matching's obsolete 8,192-entry / 500-microsecond
cold-admission block: #992 alone, #996, #998, and #998 plus C01/C03. They preserve each
candidate's remaining dispatch and worker-budget rules; no replacement timing constant
is fitted. The directory policy from #991 is absent throughout. The two-directory independent
read minimum is main's existing condition, not the old #991 64-directory gate.

## Removing the obsolete matching guard

Fresh supplemental controls establish that the original 8,192-entry gate, rather than
executor reuse alone, causes the largest in-memory content losses. The table uses the
same four-worker cases and fresh main controls for each supplemental session.

| Case                             | Linux #998 | Linux #998 eager | macOS #998 | macOS #998 eager |
| :------------------------------- | ---------: | ---------------: | ---------: | ---------------: |
| Clustered content, 1,000 files   |    +241.0% |            -2.0% |    +280.8% |            -0.3% |
| Interleaved content, 1,000 files |    +237.0% |            -2.8% |    +279.1% |            +0.5% |
| Native match lines, 10,000 files |    +162.6% |            -5.9% |    +184.6% |            +1.2% |
| rg file selection, 1,000 files   |     -40.4% |           -11.5% |      +6.2% |            +5.1% |

The mechanism is observable in the code: a cheap initial prefix can keep an entire
expensive batch serial. Removing the obsolete gate restores near-baseline parallel
performance on the clustered and line-output cases on both platforms. It sacrifices
some benefit on cheap content. This is a mechanism control, not a new accepted policy.

The #992-only eager control excludes #996's command ownership/budget changes. It also
removes the large content regressions, but Linux rg file selection at 1,000 files/four
workers is 19.8% slower than its fresh main control; macOS shows 5.7% slower in the
corresponding supplemental case. This prevents a blanket recommendation even for the
smaller shared-pool integration. Queue/result-state costs and workload shape still matter.

The #998-eager plus C01/C03 combination was measured as well. It keeps the large
content regressions removed but does not establish additive gains. For example, Linux
10,000-file rg selection is 6.8% faster than main with the key fixes, versus 18.5% faster
for the pure eager control. These key changes are not exercised identically by every
case; code layout, scheduling and allocation effects require further isolation before
assigning a cause to every small or moderate difference. No composite speedup promise
is inferred from individually useful changes.

The focused #992 eager native content runs add 1,170 oracle-correct invocations on each
platform; Linux's six-variant content/comparison/collection supplement adds another
4,140. The macOS supplemental in-memory matrix covers all eight baseline/original/eager
controls. All final runs completed; the raw archive retains their separate contracts.

## Copy and move decisions

- **C01, retain:** `Collections::Add` uses transparent `try_emplace(name)` instead of
  constructing `std::string(name)` before every lookup. An inserted map key remains owned.
  A repeated long-name test changes the caller's original string and verifies lookup and
  retained entries afterwards. The short-key collector microbenchmarks
  and native long-identifier cases are retained separately; they must not be described as
  the same allocation workload. Whole-command changes are mostly small. No blanket
  percentage improvement is promised.
- **C03, retain:** comparison's category-recording helper takes `string_view`; it creates
  an owned key only when categories actually need retention. This avoids an eager
  temporary conversion when no category map is required. Native long-path comparisons
  and independent complete-record oracles cover the affected path.
- **C02, defer:** returning a completed matcher batch by value and clearing it on the
  coordinator can reuse vector capacity. Focused ownership/worker-to-inline tests pass
  on both platforms, but overall timing benefits are inconsistent. Adding an observer-
  invalidating `ReleaseEntries` API is not justified by the current evidence. Do not
  attribute differences in the byte-identical collector negative control to this change.

Retained file records must continue to own borrowed traversal text. Archive filesystem
`shared_ptr` copies preserve deferred-work lifetimes. Metadata/time records are fixed-size value snapshots, and snapshot reads already return views. Existing entry/stat range moves and
reference-wrapped callbacks already avoid the alleged copies. Moving object members out
of a result builder is not a pessimizing NRVO move. Native path adapters still need
null-terminated storage for arbitrary substring views. None of these should be weakened
by a broad reference or `string_view` conversion.

Further bounded investigations are transparent definition/capture map lookups, lexical
path-component views with complete path semantics, move-capturing the old ReadPool task
owner if that implementation remains, and immutable snapshot interfaces for digest/env
lookups. They require contract and allocation evidence before changing ownership.

## Isolated infrastructure and validation

[PR #1035](https://github.com/mboworks/xff/pull/1035) collects #991's libpfm query-download
repair and #1000's executor lifecycle/dispatch probes. The experimental executor and
probe support remain private `testonly` targets. There is no dependency path from the
shipping CLI to the experimental executor on either platform.

The Linux CLI is byte-identical to main. All fourteen non-debug Mach-O section payloads
match on macOS, including code, constants and strings; whole files differ in link-edit
metadata. These isolation proofs establish absence of a production runtime change
without inventing an infrastructure timing gain. Both-platform executor/probe tests,
independent dispatch checksums and changed-file clang-tidy passed on the pre-sync head.
#1035's pre-sync full CI passed after one targeted retry of runner disk exhaustion
while unpacking unchanged LLVM, without a source change. The subsequent main
synchronization has its own current-head CI context; earlier checks do not substitute
for that validation.

The copy candidates pass collection, matcher, rg and run tests on both platforms.
The six-translation-unit scoped clang-tidy run covered the retained C01/C03
code in the recycler superset: collection source/test and all four matcher-header consumers.
It was not a separate a49-only lint run; the early a49 hook had no compile database. The two retained production C++ files remain identical to the measured a49 candidate; the publication regression test uses matcher-only EqualsText style corrections, validated by a fresh focused collection test. The immutable archive retains the original measured test/source patches. Native and in-memory output checks validate the measured workloads, not every
possible cancellation, storage, memory-budget or error condition. Unretained runtime
snapshots are not presented as merge-ready implementations.

## Follow-up acceptance work

1. Rework useful pool/frontier mechanisms independently of obsolete admission guards.
   Preserve warm/cold and 16/32/63/64-entry boundary behavior explicitly; main currently
   keeps cold batches below 64 entries inline, whereas the eager controls retain the
   candidate's below-16 inline rule. Do not silently treat those policies as identical.
2. Profile the macOS large-collection and directory-budget regressions before accepting
   command-wide integration. Retain Linux and macOS workloads and negative controls.
3. For any retained executor integration, run `run_executor_test`, `command_workers_test`,
   `walk_test`, `walk_parallel_test`, `parallel_compare_test`, `parallel_match_test`,
   `rg_test` and `run_test` on both platforms, plus Linux TSan. Add `match_work_test` only
   for a retained planner. Physical slow storage, heterogeneous children, bounded RSS,
   exactly-once effects, cancellation and output ordering remain acceptance requirements.
4. Require allocation/RSS and stable whole-workload gains before promoting batch recycling
   or changing additional owned text to borrowed views.

The archive accompanying this report retains source patches for every synthetic snapshot,
all final raw samples and the earlier #991 reassessment. Drivers recreate ASCII fixtures,
verify expected output and retain build/source identities. Reproduction must preserve
optimization flags, host/CPU/storage contracts and measurement population; it must not
label a later binary or a different CPU allowance as the original run.

Raw evidence: [executor-copy-audit-20261101.json.gz](executor-copy-audit-20261101.json.gz).
