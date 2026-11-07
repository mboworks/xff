# Basic-operation optimization qualification (2026-11-07)

## Objective

Reduce the production binary's startup and per-entry work for ordinary listing, name filtering,
and metadata output. The reference is the traditional tool performing the same task, with every
output checked against an independent fixture oracle. A small improvement is worth retaining
when the measurements and instruction counts support it; the chart's visual scale is not an
acceptance threshold.

The starting point already contains the adopted [lazy content snapshot](lazy-content-state-20261107.md).
Its retained production source is `90abd2b3ef63b35833e8f9d96745475c6b9f182a`. The source for the four-change
comparison is `5bcfcdac9399ddc0426df5d88c2b260fcfa378f0`. The intervening commits contain
four independent changes, measured incrementally on Linux and together against the original
baseline on both Linux and macOS. None of these commits is presented as a merged PR.
A fifth change removes unnecessary native-case metadata lookups, measured separately against
the four-change binary. Its Mac name-filtering results and Linux controls are included below.

## Three-core priority and coverage

Three cores are the primary optimization target. One core diagnoses serial overhead and parallel
speedup; ten cores expose contention and filesystem saturation. Future qualification should use
all three settings. This completed campaign measured one and three only: it makes no ten-core
claim, and its frozen measurements are not relabeled or extrapolated.

At 100,000 files, the three-core results equally weighting broad/deep shapes are:

| Platform and task  | Comparison                                   | Elapsed change | Candidate/GNU find |
| :----------------- | :------------------------------------------- | -------------: | -----------------: |
| Linux listing      | First four changes versus adopted baseline   |         -2.38% |             1.1396 |
| Mac listing        | First four changes versus adopted baseline   |         -5.24% |             1.0255 |
| Mac name filtering | Native-case change versus four-change binary |        -72.13% |             0.7563 |
| Mac listing        | Native-case change versus four-change binary |         +0.89% |             1.0616 |

The large Mac name-filtering gain is 24.37% less elapsed time than GNU find. Ordinary listing
still has a gap, and the native-case change does not improve it. The separate comparisons must
not be compounded into a claimed total gain. On Mac, three means requested XFF workers, not
three pinned physical cores.

## Implemented changes

1. **Archive suffix lookup:** build the lookup on first use, and borrow each reader's format
   records while doing so. Registering several extras previously rebuilt the lookup repeatedly
   and copied all their descriptive strings. Registration still precedes concurrent registry
   reads; the first concurrent lookup publishes the immutable set under a mutex.
2. **Ordinary expression results:** carry two booleans through the prepared recursive evaluator
   when neither fuzzy scoring nor deferred evaluation is active. The general result path remains
   for those capabilities. Both paths share predicate metadata checks, effects, invalidation,
   dry-run safety and error handling. This does not rewrite or recognize a benchmark command.
3. **Explicit path output:** reuse the coordinator's record storage for `-print` and `-print0`,
   including mixed terminators, instead of allocating a temporary path string for each output.
   Retention remains bounded; sinks consume each complete borrowed record before returning.
   Worker threads do not share the buffer.
4. **Directory prefix reuse:** retain the parent prefix while iterating a directory's name-only
   children, truncating the previous basename before appending the next. The metadata-bearing
   path already used this pattern. Nested directory visits own separate scratch storage.

The production command remains `bazel build //xff/cli:xff_full --config=clang_release
--config=xff_full`. Every configured executable is queried, immediately copied and hashed;
actual link actions retain the ThinLTO evidence. No test executable or non-LTO CLI substitutes
for the production binary.

## Measurement contract

The Linux fixture is on `/dev/shm` tmpfs. macOS uses an owned 2 GiB case-sensitive HFS+ RAM disk;
its device, volume UUID and live `ram://` image are checked before collection. Neither fixture is
on an SSD. HFS+ on a RAM disk and Linux tmpfs have different kernel behavior, so their absolute
timings are not pooled.

The frozen extended workloads are file listing, native-case name filtering, metadata and UTC date
formatting, content regex, and TAR-member filtering. Broad and deep shapes span 100, 10,000 and
100,000 native files, or 100, 1,000 and 10,000 archive members. The incremental Linux comparisons
use all 30 one-CPU cases. Combined comparisons use all 60 cases, including one and three workers.
Linux restricts the process family to one or three allowed guest cores. macOS does not claim CPU
affinity: the worker setting is explicit, and the OS schedules all participants normally.

Every case contains a candidate block and an identical-baseline control block. Each block rotates
its XFF binary, the fixed baseline and the traditional tool through one discarded warm-up round
and 27 measured rounds. The chart retains the fastest 21 samples per participant. Paired bootstrap
intervals use all measured rounds and describe within-cell variability; they are not independent
host confidence intervals, multiple-comparison corrections, or intervals for the fastest-sample
estimator. The direct combined comparison establishes the total effect; incremental percentages
must not be added together.

GNU find, ripgrep and GNU tar are retained by path, version and executable hash on each platform.
The Mac uses Homebrew GNU find and GNU tar for these same recipes; this is not a BSD-find claim.
All invocations check exact record multiplicity and content against the fixture oracle. Fixture
contents/metadata and executable hashes are checked again after collection. Raw samples and
failed attempts are preserved. Diagnostic `perf` runs are separate and never enter the chart.

## Combined Linux results

The complete combined comparison contains 120 cells and 10,080 validated invocations. Its 60
identical-baseline control cells have a 95th percentile multiplicative distance from parity of
1.0161. Within-cell paired intervals exclude parity in the faster direction for 42 candidate
cases and in the slower direction for none. The complete results, including inconclusive cells,
are in [the result data](find-listing-optimization-20261107.json).

The table equally weights both shapes and CPU allowances at each size. Negative elapsed change
means the candidate is faster; a candidate/reference ratio below one means XFF beats that tool.

| Task                  | Files or members | Elapsed change | Candidate/reference |
| :-------------------- | ---------------: | -------------: | ------------------: |
| File listing          |              100 |         -1.92% |              1.4245 |
| File listing          |           10,000 |         -2.29% |              1.3548 |
| File listing          |          100,000 |         -2.23% |              1.1341 |
| Name filtering        |          100,000 |         -4.30% |              1.0421 |
| Metadata and UTC date |          100,000 |         -2.11% |              1.1238 |
| Content regex         |          100,000 |         -1.13% |              0.7036 |
| TAR member filtering  |           10,000 |         -2.54% |              0.8377 |

For one allowed CPU, 100,000-file broad listing improves by 1.46% and remains 9.35% slower than
find; the deep listing improves by 2.68% and remains 16.47% slower. The individual incremental
populations suggest larger accumulated gains than this direct comparison. The retained candidate
and baseline hashes match across the stages. The combined measurement takes precedence; summing
incremental percentages would overstate the supported improvement.

## Four-change macOS results

The complete Mac comparison also contains 120 cells and 10,080 validated invocations. Its
identical-baseline controls have a 95th percentile factor of 1.0248. Sixteen candidate cells have
within-cell intervals wholly below parity; one lies wholly above parity (broad 100-member TAR
with one worker, +3.00%, interval 1.00004-1.06040). This single interval is not adjusted for the
60 comparisons. All cells, including that counterexample, remain in the result data.

| Task                       | Files or members | Elapsed change | Candidate/reference |
| :------------------------- | ---------------: | -------------: | ------------------: |
| File listing               |              100 |         -0.16% |              1.3450 |
| File listing               |           10,000 |         -2.15% |              1.3362 |
| File listing               |          100,000 |         -6.12% |              1.0225 |
| Native-case name filtering |          100,000 |         -1.59% |              2.6180 |
| Metadata and UTC date      |          100,000 |         -2.05% |              0.6680 |
| Content regex              |          100,000 |         +0.82% |              0.8118 |
| TAR member filtering       |           10,000 |         -1.12% |              1.1295 |

These equally weighted shape/worker summaries expose a distinct Mac problem: native-case name
filtering remains 2.62 times find's elapsed time, even while metadata/date output is faster.
The run driver eagerly requests full metadata for every entry when filesystem case sensitivity
is not uniform. On macOS this is done to obtain the device for the existing case-rule cache,
even when the pattern would have the same result on either kind of filesystem.

## Follow-up: resolve native case only when it changes a result

Source `3fee2a73241c6ebe0b2a8dbdffce5c02b048a23b` removes that eager metadata requirement.
When native case is unknown, glob matching evaluates exact and folded truth values. Equal
answers need no filesystem observation. Different answers call a per-entry resolver, which loads
metadata and uses the existing device cache. The resolver does not assume anything about parent
directories, roots or mount boundaries. Explicit folding and backend-wide guarantees retain their
existing direct path. Fuzzy matching resolves native case when reached; deferred replay keeps
its complete-metadata path.

Both truth values must be checked: `[!A]` against `a` can match exactly and fail under folding.
Required metadata failures remain unknown and cannot become matches through negation. Regression
fixtures count metadata calls and case probes, reject unwanted metadata accesses, test explicit
folding, and check shared resolution across multiple reached predicates. The existing glob
matrix checks direct, prepared and iterative evaluation against native `fnmatch` under both case
rules, with exact resolver call counts.

The follow-up compares against the retained four-change binary, not the original baseline.
Linux runs the complete 60 cases. macOS predeclares 56 cases: all listing, native name, metadata
and archive cases, plus the 100/10,000-file content cases. Only the four 100,000-file content
regex cases are omitted; that recipe explicitly disables native case. The full four-change
population above retains those cases. The same RAM fixtures and measurement protocol are used.
The effect is expected to depend on how often exact and folded pattern answers agree; it is not
a promise that every native-case search can avoid metadata.

### Completed follow-up results

The Mac follow-up contains 112 cells and 9,408 validated invocations. Its identical-baseline
control factor is 1.0143 at the 95th percentile. Eleven candidate intervals exclude parity in the
faster direction. One listing interval excludes it in the slower direction: deep 100,000 files,
three workers, +0.59% (1.00162-1.02859). The following summaries equally weight shapes and workers.

| Task                       |   Files | Elapsed change versus four-change XFF | Candidate/GNU find |
| :------------------------- | ------: | ------------------------------------: | -----------------: |
| Native-case name filtering |     100 |                                -1.50% |             1.3506 |
| Native-case name filtering |  10,000 |                               -45.47% |             1.1505 |
| Native-case name filtering | 100,000 |                               -71.10% |             0.7544 |
| File listing               | 100,000 |                                +0.64% |             1.0495 |
| Metadata and UTC date      | 100,000 |                                +0.31% |             0.6649 |

All four 100,000-file name-filtering cells beat GNU find: elapsed time is 22.45-26.35% lower.
The improvement over the previous XFF binary is 68.78-73.97%. This is a production-binary result
on the retained RAM fixture; smaller directories still expose startup costs. Listing and
metadata/date already avoid this particular case-resolution cost, so they do not show the same
improvement. The other tasks and all counterexamples remain in the result data.

The complete Linux follow-up has 120 cells, 10,080 validated invocations and a control factor of
1.0121. Five candidate intervals exclude parity in the faster direction. One excludes it in the
slower direction: deep 10,000-file name filtering on one CPU, +0.69% (1.00278-1.02234).
Linux supplies a uniform case rule, so the large Mac improvement is not expected there.
At 100,000 files, Linux listing changes by -0.13%, native name filtering by +0.13% and
metadata/date by +0.29%; these differences do not establish a useful improvement or regression.

A separately declared seven-case Linux repeat retained the same binaries, 27 rounds and seven
identical-baseline controls, adding 14 cells and 1,176 validated invocations. The suspected name
regression did not repeat: -0.98%, interval 0.95729-1.00002. Another repeat cell, broad 1,000-member
TAR on one CPU, was +1.55% with interval 1.00186-1.04264; the full population had -0.55% for that
cell with an interval crossing parity. Both populations are retained. These small, inconsistent
effects are not evidence that the change improves every Linux workload.

The fifth change is retained for the large, consistent Mac name-filtering gain and its tested
lazy-metadata behavior. It does not close the Linux listing gap. Do not multiply its separately
measured percentage by the four-change result to claim a total improvement.

## Instruction counts and remaining targets

Eleven `perf stat` repetitions per executable count userspace instructions on the same Linux
fixtures. Profiling writes to `/dev/null`, after independent output validation, and is excluded
from the chart's output-capture timings. These counters show reduced executed work, not an equal
percentage reduction in elapsed time.

| Case, one allowed CPU        | Baseline instructions | Candidate instructions | GNU find instructions |
| :--------------------------- | --------------------: | ---------------------: | --------------------: |
| Broad listing, 100 files     |             1,904,767 |              1,727,276 |               566,412 |
| Broad listing, 100,000 files |           233,207,036 |            218,843,739 |           106,212,516 |
| Deep listing, 100,000 files  |           245,730,492 |            220,817,669 |           110,644,735 |

The candidate removes 9.32%, 6.16% and 10.14% of the baseline's instructions respectively.
Large listings still execute approximately twice find's userspace instructions. CPU sampling
of the final broad listing attributes about 18% of userspace cycles to the run visitor, 18% to
`readdir`, 13% to explicit path output, 8% to string append and 4% to the compact evaluator.
These are sampling shares of userspace cycles, not shares of elapsed time or exact savings
available from deleting a function.

The next investigations should follow this evidence:

1. Move run-invariant evaluation options out of per-entry construction; instantiate deferred maps,
   captures and other optional state only when a consumer needs them. Preserve independent entry
   state and sparse deferred replay. Measure narrow changes before redesigning the context API.
2. Trace the remaining path copies and sink dispatch through traversal, explicit output and
   implicit listing. Keep complete-record boundaries, arbitrary path bytes and bounded storage.
3. Reduce startup construction of unused archive format/help vocabulary. The name lookup is now
   lazy, but reader and packer registration still constructs descriptive records before `main`.
4. Compare directory-entry materialization with find using syscall counts and CPU samples before
   changing VFS buffering. Adding threads is not an evidence-based default for these basic tasks.

An additional high-density startup profile is retained separately. Loader work is visible, but
the ELF inventory does not support blaming extra shared libraries: XFF has fewer direct dynamic
dependencies and relocations than this GNU find executable. Startup symbol samples identify
investigation targets; they do not prove the savings from changing registration or linking.

## Correctness checks

The C++ regressions cover:

- archive reader replacement/removal, shared suffixes, override vocabulary and concurrent first use;
- all boolean operators against the existing execution engines, including reached actions and
  metadata failures on either side of an operator;
- mixed newline/NUL explicit output, record boundaries, reusable capacity and oversized records;
- long/short alternating child names, lazy metadata resolution, and complete child paths.
- native case resolution only when exact/folded answers differ, negated character classes,
  cached sensitive and insensitive outcomes, fuzzy matching, and metadata failure under negation.

The existing expression, deferred, fuzzy, content cache, traversal and parallel-matching suites
remain part of qualification. Each implementation change passed its focused macOS targets.
The native-case Linux gate runs 18 targets across the archive extras, content snapshot, evaluator,
whole-run behavior, parallel matching, expression engines, record buffer, walker and glob matching.
Eight focused Mac targets cover the native-case change. Additional run tests cover cached
case-sensitive resolution and fuzzy behavior on both platforms.

The reporting change keeps experimental endpoint summaries keyed by full source SHA instead of
colliding at PR zero. Experiment catalogs carry a separate source label and no fabricated PR link.
The comparison's platform comes from its recorded contract. An experiment-only viewer selects
its available source automatically instead of opening on an empty merged-history selection.
Unmerged comparison sliders now put the baseline first and the candidate second, with an explicit
Comparison row in the Version card. The collector manifest uses the opposite order to choose
its fixed baseline; leaking that order into the viewer previously made the old binary appear
to be the newer measurement. The timing data and arithmetic are unchanged.
Nine report regression tests pass,
including experimental endpoint identities and renderer populations.

Across the four incremental Linux stages, two combined comparisons and three native-case
comparisons, independent audits reconcile 726 cells and 60,984 validated invocations with their
raw journals. They rehash the retained executables, build identities and ThinLTO link actions,
check matching configurations and independently reproduce the exported timing arithmetic.
The four full viewer exports also have independent figure audits and Chromium checks of all
24 dataset/revision/order/metric states per export. Missing measurements remain gaps.

## Retained evidence

The complete campaign, source bundles, production binaries, link commands, raw samples and logs
are retained below:

- macOS checkout: `bazel-extended-history-20261107/find-listing/`;
- Drogon: `/home/marcus/Documents/xff-chart-campaign-20261107/find-listing/`.

Subdirectories `archive-gate`, `compact-evaluation`, `record-buffer-qualified` and `directory-prefix`
contain incremental comparisons. `combined` contains the Linux comparison against the original
baseline; `macos` contains the corresponding Mac comparison and its explicit collector adapter.
The adapter changes host evidence, GNU-tool locations, RAM-storage validation and CPU affinity;
workload construction, output validation, interleaving and timing code remain the same.

`native-case` and `native-case-macos` retain the fifth change's Linux and Mac populations;
`native-case-repeat` retains the seven-case Linux repeat and its selection rationale. The final
result JSON records each comparison's baseline, candidate, executable hashes and per-cell
intervals. `profiles-final` and `profiles-startup-period` retain diagnostics excluded from timing
populations. Local previews are served from port 56205 under `find-listing-combined/`,
`find-listing-macos/`, `find-listing-native-case/` and `find-listing-native-case-macos/`.

`record-buffer` preserves an aborted build attempt whose abbreviated source SHA failed the
builder's full-revision identity check. No measurement from that attempt was accepted. The launch
script now resolves commit arguments to full SHAs before freezing a build manifest.

## Integration with current main

The PR branch merges `3b8a8a34e7` after the measurements. Its overlap with #1134 is resolved by
keeping lexical-template metadata demand and independent fuzzy-score collection, while retaining
lazy native-case resolution. The results above refer to their recorded source and executable
hashes; the integrated PR binary has not been remeasured. Focused correctness checks qualify the
combined metadata paths separately from the retained performance populations. All 14 focused
Bazel targets passed after integration: nine engine/field targets and five extended-benchmark
and landscape targets. The Chromium history-control regression also passed with the comparison
role visible. The local demos were regenerated from unchanged raw measurements.
