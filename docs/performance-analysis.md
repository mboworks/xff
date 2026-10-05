# Traversal performance analysis

## Planned prepared expression execution

EP01 now has a preparation contract, an isolated initial behavioral oracle and separate preparation/
execution microbenchmarks. Production still uses the existing evaluator. The CI build jobs retain
nine-round JSON artifacts for Linux/macOS, including fastest-seven statistics; no local benchmark
campaign is needed. CI run 37203099430 produced valid release-mode artifacts on both platforms.
The initial all-match matrix does not yet replace mixed-selectivity or end-to-end qualification.

The [implementation, benchmark and decision plan](design-expression-program.md) specifies bound
operations, prepared operands, an immutable execution program and a conservative optimizer.
It separates once-per-command parsing/preparation from per-entry execution and records the
data-structure audit against XFF's pinned MBO revision and a newer local MBO checkout.

Existing constexpr ordered maps/sets, strong IDs, vectors and arenas are sufficient to start.
A frozen/perfect-hash map/set is a separate optional startup-lookup improvement; the prepared
execution path should avoid name lookup altogether. EP01-EP07 in TODO track the implementation.
No production speedup is claimed yet. Append each stage's measured results and retain/revise/reject
decision here, including preparation, memory, portability and unsuccessful experiments.

MBO [PR #550](https://github.com/mboworks/mbo/pull/550) merged experimental `FrozenMap` / `FrozenSet`
at `bf65c21c495fc789feab79f4516d1ae7215cb292`. The previous missing-container finding is resolved
upstream; XFF still pins the earlier dependency. Their immutable constexpr index and checked
optional-reference lookup fit the planned exact per-mode startup experiment. Application-level
lookup, parsing, compiler-budget and storage measurements remain necessary before adoption; MBO's
synthetic container timings are not evidence of an XFF speedup. The detailed adoption checklist is
in the [data-structure audit](design-expression-program.md#frozenmapset-adoption-check-after-mbo-550).
The user subsequently reported substantial lookup regressions against standard unordered
containers. Raw results are not yet part of this record. API availability does not qualify the
containers for adoption; retain the current index and require a measured XFF workload win.

### EP01 baseline and EP02 dispatch experiment

[CI run 37203099430](https://github.com/mboworks/xff/actions/runs/37203099430) retained nine raw rounds
for each of 72 cases, plus aggregates, in `expression-baseline-linux` and
`expression-baseline-macos`. No case reported a correctness error. The following fastest-seven
means describe the original tree evaluator; kernel values are divided by their 1,000-entry batch.

| Platform     | Prepare one type predicate, ns | Execute one type predicate, ns/entry | Execute 64 type predicates, ns/entry |
| :----------- | -----------------------------: | -----------------------------------: | -----------------------------------: |
| Linux x86-64 |                         526.23 |                                49.51 |                             4,332.50 |
| macOS ARM64  |                         449.58 |                                49.81 |                             4,093.23 |

Preparation in that initial baseline includes the diagnostic contract table as well as parse/bind
and teardown. EP02's `prepare/tree` removes that diagnostic-only work so the comparison reflects
the shipping tree path. Do not compare the two preparation series as a product speedup.

The EP02 experiment resolves the existing dispatch table during `BoundExpression::Prepare` and
uses direct function pointers thereafter. A reserved node vector holds source references, typed
child indices and cached fuzzy-only properties. The AST and bound matcher data remain owned by
the command; no per-entry clone is made. Recursive boolean control and every effect/error path are
shared with the reference executor through compile-time cursor types. Original node identities
continue to key counters and deferred replay, preserving exactly-once prefix effects.

Production still selects the tree. Both executors run the same trace assertions, including
truth/short-circuit order, metadata failures, dry-run unknowns, fuzzy scoring, independent counters,
deferred replay and denied mutation. Registry coverage rejects an unclassified missing handler;
configuration/traversal-only behavior is explicit metadata rather than a silent new fallback.

CI compares preparation and kernel results in the same release binary with randomized interleaving
of nine repetitions and a fastest-seven statistic. This avoids measuring every baseline repetition
before every candidate repetition; it is not a claim that arbitrary repetition indices are exact
temporal pairs. The report records the revision and sampling policy.

Two independent CI sessions retained 144 valid cases on each platform, with no untimed correctness
failures: [37204470952](https://github.com/mboworks/xff/actions/runs/37204470952) and
[37205289267](https://github.com/mboworks/xff/actions/runs/37205289267). Each value below is the
geometric mean of bound/tree elapsed-time ratios across six kernel cases (1/16/64 predicates,
10/1,000 entries). Smaller means faster; these are in-process evaluator results, not CLI speedups.

| Family     | Linux, first | Linux, second | macOS, first | macOS, second |
| :--------- | -----------: | ------------: | -----------: | ------------: |
| Type       |        0.503 |         0.520 |        0.448 |         0.444 |
| Name       |        0.636 |         0.629 |        0.759 |         0.786 |
| Size       |        0.519 |         0.519 |        0.473 |         0.467 |
| Permission |        0.460 |         0.456 |        0.400 |         0.422 |
| Content    |        0.582 |         0.572 |        0.505 |         0.539 |
| Regex      |        0.745 |         0.726 |        0.692 |         0.659 |
| Fuzzy      |        0.500 |         0.510 |        0.415 |         0.401 |
| Output     |        0.592 |         0.595 |        0.462 |         0.470 |

The first session added 124-176 ns of preparation for the one-predicate cases; its measured
1,000-entry kernels recovered that cost after roughly 4-6 entries. This crossover excludes the
rest of CLI startup and traversal. Decision: retain bound dispatch as an experimental baseline for
operand/control-flow work. Whole-engine measurements, mixed selectivity, memory/size accounting,
and final production adoption remain open. Rebased CI still has to finish all required checks.

### EP03 typed scalar operand candidate

`PreparedExpression` prepares type lists, size/block specifications, unsigned numeric comparisons,
and permission masks once. Its operand factories live alongside their handlers in the existing
engine dispatch table; both evaluators share numeric and permission decoders. A separate typed
operand pool keeps the largest payload out of boolean nodes. Missing or malformed operands keep
existing no-match behavior; normal whole-command validation still applies. Symlink target reads
remain conditional, and default size units still use the evaluation context's block size.

The differential corpus reuses one prepared object across metadata values and block sizes, with
unsigned boundaries, malformed suffixes, octal/symbolic modes, every file type, and broken or valid
symlink targets. All existing trace cases also run through the candidate. The release benchmark
now compares tree, bound-only and prepared variants in 270 cases, including symbolic permissions
and numeric fields. `extra_bytes`, `nodes`, and `operands` counters report owned buffer capacities;
allocator headers and the shared AST are excluded. Allocation counts and final binary-size impact
remain to be measured, including any code retained by the enlarged dispatch table.

CI coverage exposed two gaps: the binding factory was declared `constexpr` even though every
invocation is compile-time, and most ordinary-handler adapters had not run through the small
oracle. The factory is now `consteval`; the existing full evaluator suite runs as named tree,
bound and prepared cases with independent fixtures. This exercises the established conditional
read, output, controlled action and failure tests through each implementation rather than
inventing no-op calls just to mark adapter functions covered. Coverage thresholds are unchanged.

The first Linux scalar session (run 37207735076) produced 270 valid cases. Prepared/bound kernel
family ratios were 0.786 for type, 0.740 for size, 0.909 for octal permission and 0.595 for symbolic
permission. Numeric tests were 1.034; unprepared families ranged from 1.007 to 1.033. These early
results justify continuing the size/type/permission experiment, but do not justify accepting the
numeric specialization or the overhead on other families without repeated cross-platform data.

The candidate is not selected by production. CI measurements are pending; indexed worker matchers
and the other invariant families in the operand audit still remain to implement or reject with
evidence. No additional speedup is claimed for scalar preparation yet.

### EP03 indexed worker matcher candidate

The follow-up assigns dense matcher slots while preparing path/content regex operations, then
forks execution state once per worker. Its callbacks index worker-owned storage directly; other
predicates do not gain a matcher lookup. The API binds workers to the prepared expression to avoid
cross-program slot mismatches. Missing matchers and failed forks retain existing behavior.

The 333-case matrix retains tree/bound/prepared scalar measurements and adds `tree-worker` and
`prepared-worker` comparisons for path and content regexes. Preparation includes worker setup;
kernels reuse the same worker. Stored capacities exclude regex-backend allocations, which must
be measured separately. CI timing and complete driver integration remain pending. Keep this as an
experiment until the oracle, sanitizer checks and repeated native measurements support adoption.

### EP04 linear control-flow pilot

The candidate compiles boolean control into explicit jumps and a small saved-value stack. Workers
reserve scratch once, and predicate instructions reuse the existing prepared callbacks and
metadata/safety boundary. The experiment compares switch and function-pointer dispatch over an
identical instruction layout, including preparation, instructions and owned capacities.

The 666-case core matrix adds early AND failure, early OR success and all-false OR chains to the
existing families. It reports reached predicates rather than counting skipped ones. Program
benchmarks reject accidental fallback before timing. Score-collecting and deferred contexts are
currently reported whole-expression fallbacks, covered separately by tests; they are not evidence
of linear execution. Cross-platform timing and production integration remain pending.

## Hosted capacity and benchmark backfill

Benchmark run 36831896372 failed macOS aggregation because one shard recorded five host CPUs
while the other two recorded three. Source, tools and the remaining contract fields matched.
The aggregator correctly refused to combine them, but its diagnostic did not identify the field.
The driver also checked capacity only when Linux affinity was available, allowing the macOS
four-worker request on a three-CPU host. Retrying aggregation cannot repair those measurements.

GitHub's documented standard ARM64 macOS capacity is three CPUs; Linux remains four.
The workflows use one/three workers on both platforms. Ordinary PR and main comparisons use
three complete-matrix sample shards per platform, so each host measures both tree shapes and
worker counts. Historical backfill still keeps each replayed revision on one host.
Linux pins three of the four available vCPUs, leaving headroom for background work; this is
not an exclusive CPU reservation and does not eliminate hosted contention.
Both platforms now check capacity before build/measurement. Unpinned historical comparisons also
require equal host CPU counts. See [runner specifications](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
and [the measurement contract](benchmark-comparisons.md).

The local Apple M5 Pro has 18 CPUs and 64 GiB RAM. A direct `thread_policy_get` probe for
`THREAD_AFFINITY_POLICY`, outside the execution sandbox, returned `KERN_NOT_SUPPORTED` (46).
Even where supported, that policy represents cache-affinity hints rather than a CPU mask;
see [Apple's definition](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/mach/thread_policy.h).
Local measurements are practical, but must not be labeled pinned CPU runs or substituted for
GitHub-hosted samples. A macOS VM would constrain guest capacity while introducing another
measurement environment. Local backfill uses selected revisions with one common measurement
driver, fixture contract and reference-tool set, preserving compiler/build provenance.
A completed M5 Pro pilot used 10 files in both tree shapes, one/four workers and the fastest two
of three samples: the cached historical build took about 34 seconds and measurements about
20 seconds. Resume reused the verified binary and results. This validates execution and resuming;
it is not a full campaign or a reliable estimate for the 100,000-file matrix.

The CI-only workflow collects replacements for the macOS and Linux hosted histories on the 1/3
allocation grid. The standalone tool collects separate local Mac and Zen 5 histories. The local
1/3/10 grid adds a shared three-worker view and a ten-worker scaling point, with approximately
logarithmic spacing that could later extend to 30. Equal counts do not equate different machines.
Local Linux selects one allowed hardware thread per physical core using sysfs topology, pins
all measured tool processes, and records the IDs. Unknown topology or insufficient cores fail.
Mac allocations remain worker requests; M5 Pro and M2 Ultra are separate series.

CI campaigns schedule one complete revision per job, with at most eight concurrent jobs. The
whole campaign is validated before publication; original observations and paired results remain
available. Different revisions may run on different machines, while all measurements and reference
tools for one revision share a host. A successful earlier-attempt job can remain in the campaign;
a retried revision must restart its complete matrix on the new host.

The previous successful hosted run 36907159606 spent 52.4 aggregate measurement minutes on macOS
versus 22.1 on Linux. Three concurrent Mac shards reduced the visible measurement wait to 22.6
minutes. Run 36926663737's single Mac job measured all 572 cases and 9,360 samples in 56.7 minutes.
Its aggregation failed on single-artifact download layout, not on measurement validity. The older
one-worker, 100,000-file broad absent-marker scan averaged 8.94 seconds for XFF and 8.83 for rg on
Mac, versus 0.69 and 0.60 on Linux. Linux used tmpfs; Mac used the runner filesystem. This points
to a broader host/storage difference, not evidence of an XFF-only regression or intrinsic OS cost.

Follow-up: run and verify complete published CI campaigns, then add local series in the publisher.
A valid three-worker hosted run is a different allocation from historical four-worker runs, not
inherently an invalid observation. The collection tool records purpose and replacement targets;
the publisher promotes only complete validated CI campaigns, preserving historical ordering and
keeping local additions separate.

For one compatible task/tree/file-count/allocation/reference-tool cell, let `X` be the measured
XFF time, `R` the reference time in the same run, and `B` a reference baseline averaged over five
compatible measurements. The correction is `B / R`; normalized XFF time is `X * B / R` and normalized
reference time is `B`. This is the existing XFF/reference ratio expressed in baseline time units,
not an independent performance observation. It assumes shared proportional runner effects, so
memory bandwidth, cache behavior and scheduler differences can leave residual noise.
The selected design keeps normalization in presentation: an initial forward window, followed by
current plus four prior measurements from measurement five onward. This is a moving baseline;
each view must expose its window and factor. Calibration must stay within the same machine series,
fixture, allocation and reference binary/version. Reference-tool upgrades require a new segment
or an explicit overlap calibration. The [backfill workflow](benchmark-backfill.md) collects separate
host-recorded series and preserves their original observations. The normalized historical view remains
a follow-up; current reports retain raw timings and same-run ratios.

## Measured compiler and allocator decisions

Experiments used identical xff source within each comparison, broad/deep trees, 10, 100, 1,000
and 10,000 files, and one/four workers. Linux ARM64 runs used tmpfs and enforced CPU affinity;
macOS ARM64 runs used local storage without affinity pinning. Tools ran in rotating order after
one discarded, correctness-checked warmup. Pilots retained the fastest two of three observations;
compiler confirmations at 10,000 files retained the fastest seven of nine. Percentages below are
medians across the tested cases, not guarantees for arbitrary workloads.

- `-O2` with ThinLTO replaced `-Oz`: the confirmed 10,000-file median improved 8.8% on macOS and
  24.4% on Linux ARM64. Linux metadata-light cases improved 21-30%; content cases improved 2-7%.
  The macOS experiment executable grew from 1,966,936 to 2,795,000 bytes with release stripping
  (`strip -S -x`). This deliberately trades about 0.83 MB for execution speed.
- `-O3` offered no additional macOS benefit: its confirmed median improvement was 8.7%, versus
  8.8% for `-O2`, with almost identical executable size. It was not selected.
- Modern Google TCMalloc `0.0.0-20260818-15db23d`, statically linked on Linux ARM64, had median
  slowdowns of 80.7%, 66.7%, 53.5% and 11.8% at the four sizes. It was not selected.
- Mimalloc 3.5.3 was approximately neutral in the macOS static pilot. On Linux ARM64 its medians
  were 8.8%, 9.0% and 4.0% slower at the three smaller sizes, and 2.2% faster at 10,000 files.
  The startup penalty does not justify replacing the default system allocator. Dynamic injection
  was also tried on macOS, but its loader overhead does not represent static integration.

Allocator entry points were verified in the actual executables. Linux mimalloc was built with
hermetic Clang 22 and passed all seven upstream tests. Allocator pilots do not establish behavior
above 10,000 files. Raw JSON includes commands, executable hashes, samples, CPU allocation and
fixture identity; HTML is derived from that data. Measurements exclude build time.

The compiler trials used the traversal implementation from PR 902; the Linux source was synced
through main commit `80d75576c32b364d08a30a5b1f0c5206ecc07d51`. All comparisons retained identical
optimization flags except the variable under study. Inspection of compiler actions confirmed the
last optimization argument takes effect; a transitive allocator dependency is not evidence that
its allocator is linked.

## Bazel ThinLTO configuration (2026-09-30)

`--config=clang` selects the hermetic compiler; `--config=lto` selects
`--features=thin_lto`. `--config=clang_release` composes the compiler, `-O2`, and LTO.
There are no raw `-flto=thin` compile/link options in XFF's release configuration.

The released [toolchains_llvm 1.11.0](https://github.com/bazel-contrib/toolchains_llvm/releases/tag/v1.11.0) includes
[toolchains_llvm #876](https://github.com/bazel-contrib/toolchains_llvm/pull/876) and
[#877](https://github.com/bazel-contrib/toolchains_llvm/pull/877):

- macOS translates the configuration-level feature into linker-managed ThinLTO. The toolchain
  adds `-flto=thin` to C/C++ compilation and linking. Apple ld performs indexing and backend
  work inside the link action; the absence of separate Bazel LTO actions is expected here.
- Linux uses Bazel's separate `CppLTOIndexing` and `CcLtoBackendCompile` actions. A feature
  override selects bundled LLD when `thin_lto` is enabled, retaining mold otherwise. LLD
  supports this protocol without the `LLVMgold.so` plugin absent from the mold/LLVM archives
  used in the earlier failing release links.
- `--features=-thin_lto` disables the feature, including after `--config=lto`. Execution tools
  are independent: use `--host_features=thin_lto` only when those tools should also use LTO.
  Rule-local `features` attributes and raw compiler flags do not select the feature override;
  use configuration-level feature selection.

Linux IR backend actions still inherit source-only toolchain options (`-U_FORTIFY_SOURCE`,
`-idirafter`, and `-cxx-isystem`). Under `-Werror` those unused arguments fail the build.
The Clang configuration therefore passes `--ltobackendopt=-Wno-unused-command-line-argument`.
This affects only the IR backend; C/C++ source compilation retains warnings as errors.
Upstream action-specific flag handling would let XFF remove this remaining workaround.

At the earlier upstream commit `0c21fa0a259ddd8f9096bba30a03db358c0d408e`, macOS simply ignored
`--features=thin_lto`. XFF action queries showed that replacing the raw flags removed LTO.
PR #877 changes that behavior without requiring Apple's linker to understand ELF indexing
options. The earlier Linux ARM64 experiment also reproduced the missing mold plugin and the
backend warning failure; selecting LLD plus the backend-specific exception addressed them.

Validation uses action queries and a C/C++ cross-library probe with an undefined call that can
only be eliminated by optimization across both libraries. The enabled build must link and run;
the explicitly disabled build must fail with that undefined symbol. XFF's generated reference,
notice, and main CLI tests exercise the composed release configuration separately.

This change does not establish a runtime speedup. Linker-managed ThinLTO already runs backend
work in parallel; the Linux benefit to investigate is per-backend scheduling and cache reuse.
See the [LLVM ThinLTO documentation](https://clang.llvm.org/docs/ThinLTO.html). Measure clean,
one-source incremental, and warm disk-cache builds before claiming build-time gains; compare
stripped binary size and existing traversal/content workloads before claiming runtime gains.

## Metadata and filesystem costs

`LocalFs::ReadDir` already consumes `dirent.d_type`; the walker uses it for known regular-file
and symlink types when metadata is unnecessary. Unknown types, directories needed for loop or
filesystem checks, and followed symlinks retain metadata lookup. `MetadataDemand` distinguishes
never, on-demand and always; the per-entry lazy value has exclusive coordinator ownership and
requires no synchronization.

Linux basic consumers use `stat`/`lstat`; birth-time consumers request basic fields plus birth time
in one `statx` call, with a portable fallback for incomplete/unsupported responses. The Linux
[statx interface](https://man7.org/linux/man-pages/man2/statx.2.html) defines explicit field masks.
Directory entries provide names, inode identifiers and types, not complete sizes/timestamps;
[the getdents interface](https://man7.org/linux/man-pages/man2/getdents.2.html) also requires handling
unknown types. Replacing libc enumeration with raw getdents would not remove required stat calls.

macOS offers [getattrlistbulk](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/man/man2/getattrlistbulk.2),
which can return selected attributes for many entries per call. It is a candidate for workloads
that require metadata for most files, not a replacement for cheap name-only traversal. It needs
an optional VFS listing-with-metadata capability, a checked decoder for variable records and
fallbacks for unsupported attributes, per-entry errors, followed links, mount points and firmlinks.
Apple documents that bulk attributes describe links themselves and underlying mount points, so
blindly substituting them for the current followed-target metadata would change behavior. This
larger backend change is deferred until a dedicated correctness and syscall-count experiment.

## Further implementation candidates

1. **Directory scheduling allocations (`xff/engine/walk.cc`).** Unprefetched reads currently pass
   through a packaged task/future even when immediately waited on. A future vector also reserves
   a slot for every child when no parallel reads are scheduled. A local prototype bypasses both;
   initial Linux timing changes are small and noisy (about 0.8% faster median at 10,000 files).
   The fastest-7/9 confirmation improved only 0.2% overall, within timing noise, so it was not promoted.
2. **Content buffer initialization (`xff/vfs/local_fs.cc`).** Every `ReadContent` zero-initializes
   a 64 KiB stack buffer, then consumes only the byte count returned by `read`. At 10,000 small
   files this requests roughly 625 MiB of unnecessary initialization. The fastest-7/9 Linux confirmation improved content cases by 3.7-10.5%. The change is
   selected, with empty/full/partial-chunk tests and sanitizer validation of the returned-byte boundary.
3. **Metadata demand for presentation (`xff/engine/run.cc`, `xff/presentation/fields`).** Any
   compiled output template currently selects full metadata. Path-only templates could declare
   a field-level demand just as birth time does. This needs a complete field dependency inventory,
   including composed templates and extensions, plus exact stat-count tests; guessing from flag
   names would be incorrect. Retain the conservative default for unknown/custom consumers.
4. **Content reuse and bounded reads (`xff/engine/evaluate.cc`).** Multiple predicates can each
   call `ReadContent` for the same entry. An entry-owned content cache could avoid repeated reads
   without locks. Binary sniffing could use a prefix read, but unreadable trailing data and strict
   text flavors must preserve documented behavior. Streaming regex matching also needs boundary,
   multiline and capture semantics. These require dedicated fixtures beyond the small-file pilot.
5. **Parallel content scheduling (`xff/engine/parallel_match.cc`).** Batches of 256 and chunks of
   16 bound memory and preserve output order. The coordinator waits for a batch before continuing
   traversal; a two-buffer pipeline might overlap those phases. It must preserve quit/prune,
   error ordering, cancellation and stateful actions, so it is deferred until profiles show the
   wait is a meaningful fraction of time. Cheap name/type predicates should stay serial.
6. **Path ownership and conversion (`xff/vfs/local_fs.cc`, `xff/engine/walk.cc`).** Each listing
   stores both full paths and names, then converts entries to walker records. Moving strings
   avoids a second allocation, but the two listing vectors coexist. An internal visitor or a
   shared value record could reduce peak memory. Borrowing names requires stable storage and
   must not introduce dangling views in prefetched or archived entries.
7. **Safety and output.** Safety policy is resolved before traversal; mutation checks must stay
   at the operation boundary. Neither checks nor VFS routing may be bypassed for speed. Profile
   pure filtering separately from output formatting and safety-enabled runs before changing
   policy representation or buffering emitted records.

## ISA policy

The inspected project release configuration has no explicit `-march`, `-mcpu` or `-mtune`.
The available experiment hosts are ARM64; emulated x86 timing would not establish native x86
benefit. Keep the portable x86 baseline until a native x86-64 experiment measures generic versus
v3 with identical source, allocator and fixtures. Record CPU features and effective compiler
commands. v4 is not a portable default; separate generic/v3/v4 distributions remain deferred.

## Measurement overhead

Fixture identity is now computed once per fixture, rather than repeatedly for every task.
PR and main comparisons use three full-matrix sample shards per platform, sharing the same
compiled binary. Both PR and main shards retain three measured rounds per case.
The aggregator pools all nine raw rounds and selects the fastest seven per tool.
Each sample retains its shard/round identity, and shard provenance records the hashed host ID.
Both shapes and worker settings therefore appear on every host. Within each round XFF and reference
tools share a runner, while the participant starting order rotates across shards.

This trades more aggregate work for shorter expected measurement latency: discarded warm-ups make
twelve total tool rounds per case, versus ten in a serial nine-sample run.
Fixture creation also occurs on each shard. Matching hardware contracts cannot remove background
load or host variation; fastest-run selection can favor a faster host differently for different
tools. Aggregation rejects incomplete matrices, mismatched commands/fixtures/tool identities,
wrong sample counts and incompatible CPU environments. The [measurement contract](benchmark-comparisons.md#measurement-shards)
documents the sampling and comparability tradeoffs.

PR #951's six one-round measurement shards completed in 2m19s to 3m32s in
[run 37123621346](https://github.com/mboworks/xff/actions/runs/37123621346), leaving substantial
headroom before the full test jobs finished. PR sampling therefore uses the same nine measured
rounds and fastest-seven estimator as main. PRs still stop at 10,000 files; main reaches 100,000.
This improves the sample population without adding runners or builds. PR #952's
[run 37126531617](https://github.com/mboworks/xff/actions/runs/37126531617) confirms 440 tasks and
800 tool/case entries per platform, each with nine raw samples and seven selected. Linux shard
jobs took 4m31s-5m17s; the accepted macOS shards took 4m33s-7m00s. These include runner setup,
fixture creation and warm-up. They are elapsed job observations, not isolated measurement costs.
The initial macOS set mixed three- and five-CPU hosts; strict compatibility correctly rejected it.
Retrying the five-CPU shard produced a compatible three-CPU set without changing the PR or validator.

## PCRE2 JIT/SIMD verification and RE2 comparison

Source baseline: `cb882b639a` (PR #910). The baseline investigation below describes that revision.
The JIT follow-up at the end records the implementation and measurements that supersede its
capability findings; release compiler options and the default RE2 grammar remain unchanged.

### Verified capability and dispatch

- The pinned PCRE2 10.48 Bazel package copies `config.h.generic`, leaves `SUPPORT_JIT` undefined,
  and does not add that define in its `LOCAL_DEFINES`. Building and running its own
  `@@pcre2+//:pcre2test -C` reports **No just-in-time compiler support**. Merely compiling
  `pcre2_jit_compile.c`, or publishing the SLJIT license, does not establish usable JIT.
- Independently, `extra_modules/pcre2/pcre2_backend.cc` never calls `pcre2_jit_compile`.
  `PartialMatch`, `FindFirst`, captures and substitution therefore use interpreted PCRE2 today.
- `FullMatch` and `FullMatchCaptures` pass `PCRE2_ANCHORED | PCRE2_ENDANCHORED` at match time.
  Those options force interpreter fallback even for a JIT-compiled pattern. Supporting JIT for
  these operations needs a semantics-preserving anchored compilation strategy, not merely a
  new call after compilation. [PCRE2 JIT documentation](https://pcre2project.github.io/pcre2/doc/pcre2jit/).
- PCRE2's explicit SIMD fast-forward implementation is inside its JIT path. The pinned source
  includes ARM64 NEON and x86 vector implementations; its AVX2 fast-forward path is explicitly
  disabled in `pcre2_jit_simd_inc.h`. None of that JIT-generated scanning is active in the current
  xff build. This does **not** claim that compiler-generated code or libc uses no SIMD.
- RE2 is pinned to `2025-11-05.bcr.1`. Its source has an AVX2 prefix-search path guarded by
  `__AVX2__`; this ARM64 measurement does not establish native x86 behavior. JIT and SIMD are
  separate capabilities, and a library's build support does not prove a particular match used it.
- This Mac's external ripgrep is 15.2.0, reports compile/runtime NEON and PCRE2 10.45 with JIT
  available. Its version and execution model differ from xff, so its times are a tool comparison,
  not an isolated measurement of JIT's benefit. Its availability report alone also does not prove
  per-match JIT dispatch.

### Reproducible measurement contract

`//tools:benchmark_regex` compares the same xff full binary with `--regextype=RE2` and
`--regextype=PCRE2`, plus `rg --pcre2`. It uses an ASCII log-record pattern with alternation,
character classes, bounded repetition, literal punctuation and an optional field. Each 16 KiB
file contains repeated near misses; the mixed population has one late match in every third file,
while the absent population has no matches. The pattern cannot match across lines, so xff's
whole-content search and rg's line-based search have equivalent accepted file sets here.
An independent Python regex oracle tests the fixture labels. Every measured command, including
warmups, must return exactly the expected NUL-delimited path multiset and a valid exit status.

The matrix uses 10, 100, 1,000 and 10,000 files with one/four requested workers, a discarded warmup,
rotating participant order, and the mean of the fastest seven of nine samples. macOS worker counts
are **not** CPU affinity limits. Storage is an explicitly labeled host filesystem with warm-cache
reuse, not a claimed in-memory filesystem. Raw JSON records commands, binary hashes, tool versions,
fixture hashes, sample times, CPU accounting, RSS and the measurement contract. Compilation and
other benchmark runs are not intentionally run alongside the final measurements.

`//xff/matching/regex:regex_benchmark` separately exercises the production Matcher backends on
in-memory subjects. It times 100 pattern compilations independently from 1,000 matches against two
reused 16 KiB subjects (half matching), checks every result, rotates engine order over ten rounds,
and outputs raw CSV. Round zero is warmup. This isolates repeated matching from filesystem and
process startup, but does not isolate allocator costs inside each backend: PCRE2 currently
allocates/frees match data on every call. It is single-threaded and is not a traversal benchmark.
Timing is informational; neither benchmark adds a CI performance gate or automatic workflow run.

```sh
bazel build --config=clang_release --config=xff_full //xff/cli:xff_full
python3.13 tools/benchmark_regex.py --binary "$PWD/bazel-bin/xff/cli/xff_full" \
  --build-label 'SOURCE_REV; clang_release; xff_full' --output /tmp/regex-macos-arm64.json
bazel run --config=clang_release //xff/matching/regex:regex_benchmark \
  > /tmp/regex-inmemory-macos-arm64.csv
```

The same harness supports Linux affinity and `--fixture-parent=/dev/shm`; verify the JSON storage
identity says `tmpfs`. Linux was not measured in this investigation: the available local container
service has no configured kernel. Native Linux/x86 results remain required before generalizing.

### macOS ARM64 results (2026-09-24)

Host: Apple M5 Pro, 64 GiB RAM. Release configuration: `--config=clang_release --config=xff_full`, `-O2` with ThinLTO.
The in-memory target uses `--config=clang_release` and links the same PCRE2 backend directly.
Raw [end-to-end samples](measurements/regex-macos-arm64.json),
[in-memory samples](measurements/regex-inmemory-macos-arm64.csv), and
[PCRE2 capability output](measurements/pcre2-config-macos-arm64.txt) are retained here.
The following compile and matching means independently retain the fastest seven post-warmup rounds.

| Engine | Compile per pattern (us) | Match 1,000 subjects (ms) |
| :----- | -----------------------: | ------------------------: |
| RE2    |                    28.03 |                     28.58 |
| PCRE2  |                     2.69 |                     38.04 |

RE2 saves about 25% of repeated matching time in this bounded near-miss workload; interpreted
PCRE2 compiles substantially faster. These are production-backend costs, including their different
allocation behavior, not guarantees for every regular expression or input distribution.

| Files | Workers | Population | xff RE2 ms | xff PCRE2 ms | rg PCRE2 ms | PCRE2 / RE2 |
| ----: | ------: | :--------- | ---------: | -----------: | ----------: | ----------: |
|    10 |       1 | mixed      |      14.20 |        12.48 |        8.21 |        0.88 |
|    10 |       4 | mixed      |      21.28 |        23.06 |       18.34 |        1.08 |
|    10 |       1 | absent     |      30.08 |        25.66 |       13.30 |        0.85 |
|    10 |       4 | absent     |      28.88 |        31.15 |       21.87 |        1.08 |
|   100 |       1 | mixed      |      23.40 |        25.47 |       17.34 |        1.09 |
|   100 |       4 | mixed      |      14.16 |        14.99 |        9.48 |        1.06 |
|   100 |       1 | absent     |      14.00 |        14.86 |        8.09 |        1.06 |
|   100 |       4 | absent     |      14.09 |        15.14 |        9.40 |        1.07 |
|  1000 |       1 | mixed      |      55.26 |        64.28 |       37.19 |        1.16 |
|  1000 |       4 | mixed      |      55.23 |        64.27 |       21.53 |        1.16 |
|  1000 |       1 | absent     |      54.38 |        64.10 |       32.23 |        1.18 |
|  1000 |       4 | absent     |      54.26 |        64.51 |       18.87 |        1.19 |
| 10000 |       1 | mixed      |     840.95 |      1045.92 |      654.74 |        1.24 |
| 10000 |       4 | mixed      |    1269.15 |      1649.13 |      451.18 |        1.30 |
| 10000 |       1 | absent     |    1177.64 |      1488.68 |      876.81 |        1.26 |
| 10000 |       4 | absent     |     495.79 |       575.45 |      122.91 |        1.16 |

PCRE2 / RE2 above is elapsed time, so values above one favor RE2. The smallest file counts are
startup-heavy; increasing requested workers does not guarantee more active matcher threads or
better throughput. The external rg result includes its different read/match scheduling and PCRE2
version. It cannot be attributed to JIT alone. This matrix covers a flat file tree and one complex
pattern with two result populations; it does not replace broad/deep or multi-pattern coverage.

A separate [confirmation run](measurements/regex-confirm-macos-arm64.json) repeated the larger
populations. Both runs are retained because absolute times varied materially on this shared host;
these are informational samples rather than confidence-bounded throughput claims. Confirmation:

| Files | Workers | Population | xff RE2 ms | xff PCRE2 ms | rg PCRE2 ms | PCRE2 / RE2 |
| ----: | ------: | :--------- | ---------: | -----------: | ----------: | ----------: |
|  1000 |       1 | mixed      |      54.01 |        63.90 |       36.80 |        1.18 |
|  1000 |       4 | mixed      |      54.82 |        64.86 |       21.78 |        1.18 |
|  1000 |       1 | absent     |      54.32 |        64.06 |       31.92 |        1.18 |
|  1000 |       4 | absent     |      54.43 |        64.08 |       18.88 |        1.18 |
| 10000 |       1 | mixed      |     478.03 |       577.43 |      340.18 |        1.21 |
| 10000 |       4 | mixed      |     477.21 |       574.71 |      151.38 |        1.20 |
| 10000 |       1 | absent     |     478.23 |       577.13 |      292.86 |        1.21 |
| 10000 |       4 | absent     |     476.06 |       572.97 |      124.42 |        1.20 |

### Follow-up implementation criteria

1. Enable JIT in the dependency build for supported targets, then explicitly compile eligible
   patterns. Verify capability, nonzero `PCRE2_INFO_JITSIZE`, and an actual match-time JIT callback;
   retain interpreter fallback when executable memory or pattern support is unavailable.
2. Measure startup/compile overhead and repeated matching before choosing eager versus lazy JIT.
   Do not introduce locks into the shared matching hot path. Preserve concurrent matching and
   bounded per-thread execution state.
3. Preserve resource protections: JIT does not enforce the interpreter depth limit, and its match
   limit accounting differs. Test bounded JIT-stack exhaustion, match limits, fallback and error
   behavior explicitly before enabling it in production.
4. Cover full matches, captures and rewrites as well as unanchored content matches. Treat
   match-data allocation reuse as a separate measured optimization, not part of a claimed JIT gain.

### JIT implementation follow-up

The PCRE2 extension enables upstream JIT for ARM64 and x86-64. Other architectures retain the
interpreter. SLJIT is bundled in the pinned PCRE2 source; no additional module or shared library
is introduced. The 33 SLJIT C/header files used by the build carry the BSD-2-Clause notice, already
included in xff's full notices. PCRE2 remains BSD-3-Clause WITH PCRE2-exception. Neither GPL nor
LGPL code is added. The ARM64 JIT includes upstream NEON scanning; x86 uses upstream SIMD support,
without enabling its deliberately disabled AVX2 path or changing the portable compiler target.

Eligible patterns compile JIT code eagerly. Full matching uses a second pattern compiled with
both anchors, because match-time anchoring bypasses JIT. This preserves alternatives, directives,
and capture numbering without textual pattern rewriting. The compiled patterns and context are
immutable during matching; match data is invocation-local and the default 32 KiB JIT stack is
thread-local machine-stack storage. No matching lock is added.

Unsupported patterns, unsupported targets, and executable-memory allocation failures retain the
interpreter. Explicit `(*LIMIT_DEPTH=...)` or `(*LIMIT_HEAP=...)` patterns stay interpreted because
JIT does not enforce those limits. Both engines retain the configured match-work limit, though
PCRE2 accounts work differently in JIT and interpreter execution. JIT stack exhaustion retries
with the bounded interpreter, including substitutions. Other match errors retain existing
no-match/unchanged-replacement behavior. RE2 remains the default and offers its existing
linear-time guarantee; JIT does not make backtracking linear-time.

Tests observe PCRE2's match-time JIT callback for partial matching, full matching, spans, captures,
and rewriting. They also exercise `(*NO_JIT)`, explicit interpreter limits, match limits,
backtracking alternatives, a large backtracking stack, and concurrent use of one backend.
Production registration installs no callback. Direct LLVM coverage reports 164/164 backend lines
and 55/58 branches covered. Local LLVM coverage confirms stack-limit retries
for matching and rewriting. Coverage builds use atomic profile counters in this backend so the
concurrent test does not corrupt counts. PCRE2 already carries MSan annotations; span lookup
allocates the full capture vector to avoid its unannotated `rc == 0` case, and capture extraction
does not read trailing nonparticipating offsets beyond the reported capture count.

#### macOS ARM64 JIT measurements

Measured on the same Apple M5 Pro / 64 GiB host with `clang_release` (O2 + ThinLTO), against
`aebd53b799` plus this JIT implementation. Each result is the fastest seven of nine samples after
one discarded warmup. Binary identity, fixture hashes, commands, and raw samples are retained in
[the end-to-end report](measurements/pcre2-jit-macos-arm64.json). These are warm host-filesystem
measurements, not verified tmpfs; workers on macOS are requested concurrency, not CPU affinity.

| Engine            | Compile per pattern (us) | Match 1,000 subjects (ms) |
| :---------------- | -----------------------: | ------------------------: |
| PCRE2 interpreter |                     2.43 |                     38.30 |
| PCRE2 JIT         |                    14.37 |                      5.25 |
| RE2 control       |                    26.84 |                     28.74 |

The JIT compile cost includes both search and anchored patterns. The in-memory fixture is one
complex pattern over 1,000 reused 16 KiB subjects, half late matches and half near misses.
[Interpreter raw samples](measurements/pcre2-jit-baseline-inmemory-macos-arm64.csv) and
[JIT/control raw samples](measurements/pcre2-jit-inmemory-macos-arm64.csv) retain all rounds.

| Files | Workers | Population | xff RE2 ms | xff PCRE2 ms | rg PCRE2 ms | PCRE2 / RE2 |
| ----: | ------: | :--------- | ---------: | -----------: | ----------: | ----------: |
|    10 |       1 | mixed      |      10.02 |         9.73 |        5.86 |        0.97 |
|    10 |       4 | mixed      |      10.03 |         9.66 |        6.71 |        0.96 |
|    10 |       1 | absent     |      10.01 |         9.72 |        5.71 |        0.97 |
|    10 |       4 | absent     |      10.05 |         9.74 |        6.88 |        0.97 |
|   100 |       1 | mixed      |      14.31 |        11.84 |        8.77 |        0.83 |
|   100 |       4 | mixed      |      14.12 |        11.82 |        9.11 |        0.84 |
|   100 |       1 | absent     |      14.17 |        11.82 |        8.22 |        0.83 |
|   100 |       4 | absent     |      14.16 |        11.81 |        8.78 |        0.83 |
|  1000 |       1 | mixed      |      54.67 |        31.00 |       37.17 |        0.57 |
|  1000 |       4 | mixed      |      54.70 |        31.06 |       21.02 |        0.57 |
|  1000 |       1 | absent     |      54.71 |        31.02 |       32.18 |        0.57 |
|  1000 |       4 | absent     |      54.64 |        30.83 |       18.74 |        0.56 |
| 10000 |       1 | mixed      |     482.34 |       242.55 |      342.48 |        0.50 |
| 10000 |       4 | mixed      |     480.14 |       243.70 |      151.40 |        0.51 |
| 10000 |       1 | absent     |     482.60 |       243.85 |      294.26 |        0.51 |
| 10000 |       4 | absent     |     484.20 |       244.74 |      124.08 |        0.51 |

At 10,000 files JIT roughly halves xff's elapsed time compared with RE2 for this pattern. It does
not fix traversal/scheduling scaling: four requested workers provide little benefit here, while
ripgrep benefits substantially. These results do not justify changing the default grammar or
claiming PCRE2 is faster for every pattern.

The stripped full macOS ARM64 binary grows from 6,251,328 to 6,648,288 bytes: **396,960 bytes
(6.35%)**. Both builds use `strip -S -x`, the same compiler, optimization, and extension selection.
Their dynamic-library dependency sets are identical. The final release build, including generated
help and capture-handling refinements, retains this size. Linux/x86 performance and size remain
to be measured independently.

## MBO segmented collection storage and arenas

The Git module override pins MBO `c854476fb9bf22b6dc372a756d80831f7655d60e`. `SegmentedVector` supplies stable
addresses, random-access iterators and explicit empty-directory reservation. `Arena` supplies raw
aligned bytes and bulk lifetime, with no internal locks. Neither supplies contiguous element spans;
`Arena` does not construct or destroy arbitrary C++ objects.

The first adoption is `Collections`, the population retained by `-collect` for later summaries,
histograms and shard grouping. An unknown-size vector previously relocated complete records while
growing; each record owned three strings. Records now occupy 64-element segments, with zero initial
directory reservation, and their text views refer to a collection-owned byte arena. The arena grows
from 4 KiB to 64 KiB normal blocks. These are initial measured policies, not universal optimums.
Many small named collections can waste tail slots; ordinary runs without collection allocate none
of these blocks. Metadata and filesystem owners remain real, destructed C++ members. Arena text is
released only with its owning collection, and moves preserve references. Consumers may not retain
these views after the collection dies. Row/byte budgets continue to count the same logical text;
they are not physical allocator-memory caps. VFS routing and safety enforcement are unchanged.

### In-memory storage measurements

These measurements use MBO `ac4c11113de868b27226c0fe74028f818dc6b4b8`, where the container was named
`SegmentedSequence`. The current pin uses the renamed `SegmentedVector` and shared `SegmentedOptions`,
and includes upstream source-construction improvements. XFF keeps the same segment and arena settings.
The results below describe the measured revision; no additional speedup is claimed for the dependency
update. `SegmentedDeque` is not used: collections append entries and retain them until destruction,
without inserting or removing entries at the front.

The committed `//xff/engine:collect_benchmark` constructs paths outside timing, then measures
collection append, full iteration and destruction. Cases use 10 through 100,000 records, root
lengths 1 and 128, and one/four independent benchmark threads. This excludes filesystem I/O and
engine traversal. Four threads do not represent four CPUs assigned to a single XFF walk; there is
no affinity on this Mac. Compiler configuration is identical `clang_release` (O2 + ThinLTO), including
the MBO pin, for the vector baseline, sequence-only variant and sequence-plus-arena variant.

One discarded warmup precedes nine rounds with rotating variant order. Each invocation uses
`--benchmark_min_time=0.02s`; the table averages the fastest seven CPU-time observations per case.
The unchanged vector baseline uses the parent collection implementation. The sequence-only variant
changes collection storage to `SegmentedSequence<CollectedEntry>` with the same segment options,
retaining owned strings. The final variant is the implementation in this PR.
[Raw samples, hashes, configuration and memory observations](measurements/collection-storage-macos-arm64.json)
retain the full measurements. These are local Apple M5 Pro/macOS results, not cross-platform or
whole-command speedup claims. Linux confirmation and traversal-inclusive measurements remain open.

| Entries | Root bytes | Threads | Vector us | Sequence us | Sequence + arena us | CPU time reduction |
| ------: | ---------: | ------: | --------: | ----------: | ------------------: | -----------------: |
|      10 |          1 |       1 |      0.51 |        0.29 |                0.36 |              30.0% |
|      10 |          1 |       4 |      0.52 |        0.30 |                0.36 |              30.1% |
|     100 |          1 |       1 |      3.58 |        2.61 |                2.96 |              17.2% |
|     100 |          1 |       4 |      3.60 |        2.64 |                2.99 |              17.1% |
|   1,000 |          1 |       1 |     37.14 |       29.30 |               32.61 |              12.2% |
|   1,000 |          1 |       4 |     36.73 |       29.48 |               32.59 |              11.3% |
|  10,000 |          1 |       1 |    745.34 |      310.64 |              322.57 |              56.7% |
|  10,000 |          1 |       4 |    922.45 |      309.06 |              316.34 |              65.7% |
| 100,000 |          1 |       1 |   5325.74 |     4669.87 |             5049.80 |               5.2% |
| 100,000 |          1 |       4 |   9735.54 |     6666.86 |             6916.11 |              29.0% |
|      10 |        128 |       1 |      0.86 |        0.62 |                0.41 |              52.6% |
|      10 |        128 |       4 |      1.04 |        0.61 |                0.40 |              61.3% |
|     100 |        128 |       1 |      8.65 |        7.30 |                3.47 |              59.9% |
|     100 |        128 |       4 |     12.27 |       11.61 |                3.46 |              71.8% |
|   1,000 |        128 |       1 |    128.69 |       88.48 |               43.39 |              66.3% |
|   1,000 |        128 |       4 |    183.60 |      150.23 |               44.74 |              75.6% |
|  10,000 |        128 |       1 |   1413.85 |      924.07 |              465.05 |              67.1% |
|  10,000 |        128 |       4 |   2619.48 |     1783.85 |              520.21 |              80.1% |
| 100,000 |        128 |       1 |  17202.86 |    13451.64 |             7858.08 |              54.3% |
| 100,000 |        128 |       4 |  29769.00 |    26798.29 |            11813.75 |              60.3% |

Sequence-only wins every measured case. Arena text improves long-path cases substantially but is
slower than sequence-only for short paths, where owned strings often fit their inline buffers.
The combined implementation still improves every measured case versus the existing vector baseline.
One-shot peak RSS for 100,000 long-root entries was 120,471,552 bytes (vector), 91,373,568 (sequence),
and 86,933,504 (sequence plus arena). A repeated 0.05-second campaign instead peaked at 174,047,232,
121,290,752 and 185,761,792 bytes, respectively. These process-wide samples include fixtures and
allocator retention; arenas are not a promise of lower long-running RSS, despite lower one-shot
peak memory here. Neither campaign identifies a leak. An embedded application repeatedly creating
collections needs its own lifetime/retention measurement.

Run the benchmark with:

```sh
bazel run --config=clang_release //xff/engine:collect_benchmark -- \
  --benchmark_min_time=0.02s --benchmark_repetitions=9 --benchmark_out=collection-storage.json
```

For inter-variant comparisons, rotate separate executable invocations as in the recorded experiment;
a single executable's consecutive repetitions do not provide that interleaving.

### Other candidates and API boundaries

| Site                                      | Assessment                                                                                              | Next step                                                                                                     |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| Deferred `-top` / shard-status candidates | Large records appended without known count; sequence avoids relocating maps and owned state             | Benchmark whole result-set rounds before adoption                                                             |
| `LocalFs::ReadDir`                        | Unknown count, but the public VFS API currently returns a vector                                        | Measure a shared listing abstraction; do not materialize a sequence only to copy it into a vector             |
| Walker `Stated` children                  | Exact size is known and already reserved; indexed and sorted traversal benefits from contiguous storage | Keep vector until traversal-level measurements justify a change                                               |
| Parallel matcher batches                  | Bounded 256-entry batches with indexed worker access and publication boundaries                         | Test buffer reuse first; a shared arena must not mutate while workers read it                                 |
| Ranked output                             | Appends then sorts; stable addresses are not required                                                   | Measure sorting/cache costs as well as append time                                                            |
| Parser AST                                | Nontrivial nodes currently have individual ownership/destruction                                        | Raw Arena is not an object-lifetime layer; design explicit destruction/ownership before replacing allocations |
| Collection text                           | Raw bytes have collection lifetime and are copied once                                                  | Implemented through a narrow adapter to MBO's byte API; no upstream API blocker                               |

No blanket vector replacement is warranted. A known size plus `reserve`, range construction, sorting,
contiguous spans, or short-lived small batches can favor vector. Segmented storage is most promising
where unknown-size append growth currently moves large records or invalidates references.

## Rg integration review

The PRs 916-919 integration review identified these code paths. Restoring selective VFS
operations is included with the correctness fixes:
`RgInputFs` now forwards host `StatFields` and `ReadContentRange` requests unchanged and
implements bounded reads directly for its virtual stdin entry. Before the fix, inherited
fallbacks requested full metadata (including birth time on the host) and materialized whole
files for range probes. Tests verify exact request forwarding, error propagation, EOF, empty
input, and oversized offsets without relying on host filesystem timing.

Ordinary rg searches and audited native content expressions now use the bounded matcher pool.
Host entries are scheduled independently of archive-root probing: the full binary's default
archive support no longer prevents host-file parallelism. Mounted/archive-member entries flush
the pending host batch and retain serial evaluation. Stateful or metadata-dependent native
expressions, reductions and full-metadata output also retain the coordinator path. The first
batch below 64 entries avoids thread startup; larger batches hold at most 256 entries and
workers acquire chunks of 16. Ordered emission happens after the batch, including errors and
maximum-result limits; workers never invoke the external output sink.

An initial implementation shared the compiled RE2 object for every worker. The scaling fixture
showed severe contention in its mutable DFA caches for line/count output, despite correct and
thread-safe results. Each worker now forks RE2 state once at startup and reuses it across
batches; translated glob grammars preserve the same pattern and case settings. PCRE2 and
other immutable compiled backends remain shared. This spends additional RE2 cache memory
per worker to avoid contention; it does not introduce locks into entry metadata or content
snapshots. Tests preserve grammar/case behavior after the original matcher is destroyed.

Native content predicates and match output share a lazy, owned snapshot per evaluation.
Stateful predicates and actions invalidate it before execution. Deferred result-set passes
start fresh instead of retaining content for the whole walk. One evaluator owns each snapshot,
so it needs no lock. Workers perform content selection and rendering, then release source bytes;
batches retain rendered records instead of up to 256 whole input files. Large output records
and individual long lines can still consume substantial memory. Tests verify one read per
regular file, serial/parallel output identity across batch boundaries and output modes, and
actual overlapping content reads without relying on timing for the assertion.

Filename/count/quiet modes now use the VFS sequential source with 64 KiB read requests and
retain only an incomplete line. Counts accumulate directly; filename and quiet selection stop
regex evaluation after the first selected line. The reader still drains the source to preserve
binary detection and late read-error reporting. Native grep sniffs its existing 8,000-byte
prefix; rg checks the whole source unless `--text` is active. An already owned predicate snapshot
is reused without another read. A single very long line still requires memory proportional to
that line; legacy backends may materialize their source, and CLI stdin remains staged. Full
line/context output remains buffered. Chunk-boundary, long-line, late-NUL and late-error tests
cover the streaming path.

Rg option aliases, argument syntax, translation effects, and help summaries now share the
parser metadata. Help renders every descriptor; tests enforce unique names and shorts, required
arguments, nonempty summaries, and complete rendered coverage.

Use equivalent native `-rxc`, native `-M`, and rg-style fixtures, with RE2 and PCRE2/JIT,
when comparing these changes. Verify selected files and output before interpreting timings;
traversal worker count alone does not establish parallel content execution.

### Rg follow-up measurements

`//xff/engine:rg_benchmark` compares the review baseline `6063f3d0e3` with these four follow-ups.
The in-memory fixture contains 10, 100, 1,000 or 10,000 regular files; each has one matching
line followed by 128 nonmatching lines. Both RE2 and PCRE2/JIT use
`(alpha|beta)[0-9]{3}.*needle`. Every case verifies successful selection and output record counts
before timing. The native filter is a control; native-lines adds `-M`; rg cases use ordinary
lines, filenames (`-l`), counts (`-c`) and quiet (`-q`).

Apple M5 Pro, 18 CPUs, 64 GiB RAM, macOS ARM64; `--config=clang_release --config=xff_full`
(`-O2` with ThinLTO). Results are elapsed wall time, averaging the fastest seven of nine
repetitions (`--benchmark_min_time=0.02s --benchmark_repetitions=9`). Parsing/native binding is
outside the measured region; rg preparation is inside `RunFind`. These engine measurements
exclude CLI startup and physical filesystem I/O, use requested workers rather than CPU affinity,
and are illustrative rather than a regression gate. The baseline ran with one remaining local
clang-tidy process; these are not isolated-machine measurements.

Values are baseline time divided by candidate time: greater than one means faster. Raw
aggregated timings and fixture metadata are in
[`measurements/rg-review-macos-arm64.json`](measurements/rg-review-macos-arm64.json).

| Engine / workload    | 10 files, 1 worker | 100 files, 1 worker | 1,000 files, 1 worker | 10,000 files, 1 worker | 10 files, 4 workers | 100 files, 4 workers | 1,000 files, 4 workers | 10,000 files, 4 workers |
| :------------------- | -----------------: | ------------------: | --------------------: | ---------------------: | ------------------: | -------------------: | ---------------------: | ----------------------: |
| RE2 / filter         |              1.00x |               0.99x |                 0.98x |                  0.99x |               0.98x |                0.99x |                  1.02x |                   0.98x |
| RE2 / native-lines   |              0.96x |               0.97x |                 0.97x |                  0.98x |               0.98x |                2.31x |                  2.98x |                   3.05x |
| RE2 / rg-lines       |              0.97x |               0.97x |                 0.99x |                  0.99x |               0.98x |                2.13x |                  2.96x |                   3.05x |
| RE2 / rg-files       |              2.98x |               9.25x |                12.21x |                 12.79x |               3.30x |                5.86x |                 13.32x |                  14.51x |
| RE2 / rg-counts      |              0.98x |               0.98x |                 0.98x |                  1.09x |               1.06x |                2.12x |                  3.17x |                   3.17x |
| RE2 / rg-quiet       |              3.11x |              10.42x |                15.43x |                 16.56x |               3.48x |                5.75x |                 13.09x |                  14.76x |
| PCRE2 / filter       |              0.88x |               0.93x |                 0.94x |                  0.95x |               0.97x |                0.95x |                  0.94x |                   0.94x |
| PCRE2 / native-lines |              0.97x |               1.00x |                 1.01x |                  1.02x |               1.01x |                2.01x |                  2.60x |                   2.69x |
| PCRE2 / rg-lines     |              0.93x |               0.99x |                 1.00x |                  1.01x |               0.97x |                1.65x |                  2.57x |                   2.48x |
| PCRE2 / rg-files     |              2.48x |               6.90x |                 9.52x |                  9.86x |               2.51x |                3.78x |                  8.50x |                   9.38x |
| PCRE2 / rg-counts    |              0.97x |               0.99x |                 1.14x |                  1.03x |               1.09x |                1.74x |                  2.57x |                   2.46x |
| PCRE2 / rg-quiet     |              2.41x |               6.11x |                 8.28x |                  8.62x |               2.50x |                3.75x |                  8.23x |                   9.12x |

At 10,000 files and four workers, line/count workloads improve about 2.5-3.2 times. Filename
and quiet searches improve about 9-15 times with an early match, because they stop evaluating
unneeded lines while retaining binary/error checks. Serial line output stays close to the
baseline; small filter cases show some regression, including 0.88 times for PCRE2 at ten
files. These results do not establish a gain for every pattern, file size, match density or
storage device. Full line output still needs a streaming renderer; archive member searches
retain their serial path.

## Applying the worker model elsewhere

Analysis of the PR #921 implementation at `a99a4c1734`, including its current base. These are
implementation candidates, not additional measured speedups. The measurements above establish
the benefit for the tested rg and match-output workloads only.

The reusable design is owned per-entry inputs, worker-local mutable scratch, bounded batches,
and coordinator-owned publication. Workers return values and buffered records; the coordinator
preserves ordering, errors, reductions, and effects. Entry content and metadata still need no
locks when ownership passes exclusively between stages. This does not mean shared scheduling
or archive read-budget state is lock-free.

### Priorities and concrete consumers

| Priority | Consumer                                               | Current limitation                                                                                         | Proposed application                                                                                                               |
| :------- | :----------------------------------------------------- | :--------------------------------------------------------------------------------------------------------- | :--------------------------------------------------------------------------------------------------------------------------------- |
| 1        | Native regex/content predicates                        | `ParallelMatch::Run` forks `MatchOutput`, but `EvaluateEntry` still evaluates the shared native expression | Give each worker private RE2 state for native predicates, prepared once and reused across batches                                  |
| 2        | PCRE2 matching                                         | `Pcre2Backend::Matches`, `FindFirst`, and captures allocate match data for each call                       | Share compiled code and give each worker reusable match scratch; preserve the existing shared backend's concurrency contract       |
| 3        | Content filters with summaries or metadata output      | `full_metadata` disables matcher workers for every reduction, template, callback, and colored listing      | Separate metadata demand from worker eligibility; evaluate audited filters on owned entries, then reduce/render on the coordinator |
| 4        | Hashes, line counts, text checks, and expensive fields | Several consumers reread content; field and histogram calculations run serially                            | Compute requested per-entry values in workers, reuse content or stream once, and return small results                              |
| 5        | Tree comparison and patches                            | Two scans overlap, but paired content checks and patch creation are serial after collection                | Schedule bounded independent pairs and publish statuses, patches, and category contributions in path order                         |
| 6        | Broad-directory metadata                               | A directory-read job stats its children serially; parallel read-ahead requires sibling directories         | Chunk required stat work after enumeration, retaining indexed results and coordinator-owned traversal decisions                    |
| 7        | Archive-member matching                                | Every owned/mounted or archive-member visit takes the serial path                                          | Use retained ownership and independent read cursors selectively, with backend-specific replay and memory measurements              |
| 8        | Pipeline overlap and scheduling                        | A batch blocks traversal; fixed chunks can strand workers on uneven files                                  | Measure two bounded buffers, smaller tail chunks, and work estimates before changing scheduling                                    |

### Native matchers and reusable regex scratch

`ParallelMatch::EvaluateEntry` passes the same expression to `EvaluateDeferred` on every worker.
`EvalRxc` and `EvalRegex` use that expression's matcher. `ForkMatchOutput` only replaces the
matchers used for output selection/rendering. Consequently, native `-rxc` filtering has not yet
received the worker-private RE2 cache change that helped line output. This is the smallest
extension to investigate first. Sharing RE2 is correct; the concern is cache contention, whose
size depends on the workload.

Prepare worker bindings once, preserving pattern translation, case settings, expression identity,
and error behavior. An immutable expression with indexed worker bindings avoids copying unrelated
AST data per entry. The existing compile-once-per-worker approach is the first useful baseline;
a general expression-cloning framework is not a prerequisite. Continue sharing immutable compiled
backends unless their actual mutable execution state needs separation.

PCRE2 exposes a different opportunity. In `extra_modules/pcre2/pcre2_backend.cc`, even the boolean
`Matches` path creates and frees `pcre2_match_data`; span and capture operations allocate their
own vectors through `pcre2_match_data_create_from_pattern`. A worker-owned execution object could
reuse those allocations while sharing compiled JIT code. Do not put mutable scratch on the
currently shared backend or add a mutex around every match. The new interface must also handle
serial callers, captures, repeated span searches, reentrancy, JIT-stack fallback, and the existing
match/depth limits. Allocator savings and binary-size changes need measurement.

### Metadata, reductions, and entry-derived values

In `RunFindCore`, `full_metadata` combines metadata demand with reasons to avoid worker evaluation.
An otherwise eligible content search becomes serial when `--summary=ext`, a template, colored
listing, or the comparison callback is enabled. These consumers do not all require serial
matching. For example, immutable metadata can accompany each owned entry, while the existing
summary maps remain exclusively coordinator-owned. The first extension can leave all summary
updates serial and parallelize only expensive filtering.

Removing the gate alone is incorrect. `CollectedEntry::AsVisit` does not preserve a lazy metadata
loader, and the worker context currently uses an epoch clock and UTC rather than the run's full
evaluation settings. A broader worker plan must declare and carry its dependencies: metadata,
time, filesystem case behavior, templates, capture requirements, and typed result contributions.
Predicates needing metadata also require an explicit independence audit; `pure` alone is not an
adequate scheduling contract. Resolve known metadata before handing it off, or transfer an owned
lazy state to exactly one worker. Never share a mutable per-entry lazy cache.

Concrete read-reuse gaps remain in `EvalText`, `EvalBinary`, `EvalEofTerminator`, `DigestOfEntry`,
`HashField`, `ReadEntryLineCount`, and `HistogramValue`. The new snapshot currently covers
`ContentToSearch` and match output, not all of these. Compositions such as text classification,
end-of-line checks, and content matching can therefore reread the same entry. Extend the owned
snapshot first where full content is already needed. For hash/line-only consumers, prefer a
streaming scan with incremental digest/count state instead of materializing every file merely
to cache it. The current xff hash API accepts whole content, so incremental hashing needs an API
change. Avoid reading unrelated files or filling every possible derived field speculatively.

Return verification verdicts and summary keys as values. In particular, `-hasheq` can contribute
a failed verification even when the expression does not match; a worker result must preserve that
distinction and expression short-circuiting. Keep collection appends, shard selection, `-first`,
`-top`, traversal controls, and arbitrary callbacks on the coordinator. Pure stdout producers
such as `-hash` or `-grep` need a separate audited buffered-output capability, not a blanket
exception for actions.

Field rewrites have another serial cost to remove before adding threads: `ApplyRewrite` invokes
`CompileChain` during rendering. Bind valid rewrite/extraction programs once and provide private
RE2 execution state when workers use them. This needs field-level dependency information so
captures, named outputs, and per-entry values keep their existing meaning.

### Comparison and broad-directory work

`RunTreeCompare` runs the left scan with `std::async` while executing the right scan on its
caller. Both populate complete maps before the merge loop calls `SameTreeEntry` and
`TreeEntryPatch`. Thus concurrent traversal exists, but content comparison is not currently
pipelined directory by directory. Parallel pair evaluation is an incremental improvement;
implementing the intended completed-directory pipeline is a separate architectural change.
Do not confuse the two or claim that batching pairs removes whole-tree retention.

Most useful parallel work is equality for equal-size regular files and diff generation. Missing
counterparts, type mismatches, and size mismatches are cheap. Keep those cheap decisions on the
coordinator unless batching them demonstrably helps. Retain sorted emission and the current first
reported error, with workers returning statuses rather than calling sinks. Category counts and
left/right size contributions must still be accumulated exactly once.

There is also avoidable I/O inside a pair: `SameTreeEntry` calls `ReadContentRange` separately for
each 64 KiB chunk, and `LocalFs::ReadContentRange` opens/closes the file for each call. A VFS cursor
per side can retain an open handle for the comparison. Preserve early-mismatch, short-read,
concurrent-change, and error semantics explicitly. Patches then reread content today; reusable
pair state can avoid some duplicate work without retaining bytes for every pair. Full diff inputs
and generated patches require byte-aware memory limits, not just a pair-count limit.

In `Walker::ReadDir`, enumeration and the required stats of all children belong to one job.
`SubmitSubdirReads` starts parallel work only when at least two descendable sibling directories
exist. A broad directory with many files therefore cannot distribute eager stat work using that
mechanism. The content pool's indexed chunk approach can apply after enumeration. Schedule at
the coordinator boundary or use a nonblocking continuation: a directory worker must not submit
child jobs to its own saturated pool and wait for them. Skip this entirely for metadata-free
entries, preserve basic versus birth-time requests, and keep pruning and error publication ordered.

### Archive and scheduling limits

The VFS already requires concurrent-safe calls, and `ReadSource` explicitly allows concurrent
`Open` calls with independently owned single-threaded cursors. `CollectedEntry` can retain the
mounted filesystem owner. These contracts make selected archive parallelism plausible; it is
not inherently forbidden by virtual paths or ownership.

However, `MemberSource::Open` creates a fresh archive session and scans headers to its target.
Compressed members may repeat substantial decompression, and nested sources share memory/replay
budgets. More workers can increase retained decoder state and exhaust a budget even when serial
processing fits. Start with independent containers or cheap restartable sources, and measure
compressed members separately. A serial extraction stage feeding owned member data to parallel
matchers may outperform independently reopening the same compressed container. Do not simply
remove the `fs_owner`/archive guard, duplicate budgets per worker, or bypass VFS reads.

The present content pool has three further tuning constraints:

- Chunks contain 16 entries and normal batches contain at most 256. At most 16 chunks can be
  active in a normal batch. Worker count is fixed when the first non-inline batch starts;
  a smaller early batch can permanently start fewer workers than later batches could use.
- A batch below 64 entries avoids startup only before threads exist. Entry count is a rough cost
  proxy: ten huge files may justify parallelism while many cheap name tests do not. First measure
  heterogeneous file sizes and expensive fuzzy models; do not request stats solely to estimate work.
- Traversal waits for matching, and the earliest slow result delays ordered publication. Two
  buffers could overlap traversal and matching, but they need ownership, bounded queued bytes,
  cancellation, and preserved error order. Full line output can retain large strings even though
  input snapshots die per worker entry. An entry-count bound alone is not a byte bound.

`--jobs` currently limits directory workers, content workers, and child processes independently.
Comparison creates two traversal engines. Adding stages must not silently multiply runnable CPU
work without measurement. A shared CPU allowance or explicit stage allocation is a separate
scheduling design; do not change the existing meaning of `--jobs` incidentally.

### Implementation and verification order

Start with native worker-local RE2 bindings and PCRE2 scratch reuse as independently measured
changes. Next decouple audited filtering from metadata/summary consumption, then add derived-value
workers and pair comparison. Broad-directory stats and archive parallelism need backend-specific
measurements. Pipeline overlap follows only if profiling still identifies the batch barrier as
a significant cost. Cheap path/type filtering and lightweight rendering stay inline unless measured
results justify scheduling overhead.

For each candidate, preserve exact serial/parallel output, result counts, summary sizes, ordered
errors, short-circuit behavior, and limits. Test actual overlap with controlled VFS operations,
not elapsed-time assertions. Include metadata/read counts, unchanged safety enforcement, mutation
boundaries, owner lifetime, failed reads, and sanitizer coverage. Add archive budget exhaustion
tests and PCRE2 scratch/reentrancy tests where those features are widened.

Extend the existing 10/100/1,000/10,000-entry, one/four-worker in-memory matrix with mixed sizes,
late/no matches, metadata-plus-content filters, summary fields, equal/different pairs, and broad
directories. Keep small-population regressions visible. Add host and compressed-container runs
where I/O or decoding dominates; in-memory results cannot establish those gains. Report wall
time, CPU use, peak retained bytes, reads/stat calls, and active worker counts. Use the established
warmup and best-seven-of-nine measurements and change one mechanism at a time.

### Rg compatibility follow-up

The R01-R05 work in `docs/design-rg.md` extends the streaming path to full line/context
output, retaining only pending context and an incomplete line from the input. Rendered
records remain transactional until EOF. Multiline searches intentionally retain the whole
subject. Earlier observations above describe the reviewed parent snapshot; ordinary line
output no longer needs a second whole-file snapshot or an all-lines vector.

Explicit UTF-8/byte mode reaches RE2 compilation, PCRE2 compilation and worker forks.
Word boundaries use a concept-checked encoding interface with an ASCII fast path. UTF-8
empty-match advancement preserves code-point boundaries. Type rules compile once per
invocation and run before content reads; an unfiltered run does not initialize the type
catalog. None of P01-P08 is claimed complete by this compatibility work. Measure Unicode
word scans, multiline retained bytes and output-heavy worker batches before claiming a
performance gain; the new streaming tests establish read/retention behavior, not speed.

## P01/P02: native worker bindings and reusable PCRE2 scratch

Decision: retain both mechanisms. Native predicates now bind worker-private regex execution
state once when each worker starts. Expression nodes and their identities remain unchanged;
small inline batches retain their original compiled matchers. RE2 recompiles per worker as
already done for line output. PCRE2 forks share compiled/JIT code and the immutable match
context, while retaining their own capture-sized match-data block. Calls on the original
backend remain concurrent-safe; worker forks require exclusive ownership.

The PCRE2 scratch lease moves cached data out during a call. Reentrant matching therefore gets
a separate block, and cannot overwrite an outer call's offsets. At most one block returns to
the cache. Interpreter heap frames above 1 MiB are released at return instead of being retained
for the worker lifetime; this is a retention ceiling, not a new subject-size or match-work limit.
The existing match/depth limits, pattern limits, UTF-8/byte mode, default per-thread JIT stack
and interpreter fallback remain in effect. No new locks or thread-local global cache are added.
The [PCRE2 API contract](https://www.pcre.org/current/doc/html/pcre2api.html#SEC32) permits reused
match blocks while requiring independent blocks for concurrent calls.

Validation covers shared-original concurrency, independent forks, original/fork destruction,
repeated captures with unmatched groups, offset searches, reentrant JIT callbacks, interpreter
limits and JIT-stack fallback, explicit text modes, native Boolean operators and repeated batches.
The full line/count/search suites also run unchanged. Allocation lifetime is bounded by each
worker, not the process. PCRE2 bool/span/capture/rewrite calls reuse the same full capture vector;
returned strings/spans remain value results.

### Measurements and limits

Baseline is main `c5f3dc93ef` with the same expanded benchmark fixture. Both builds use
`--config=clang_release --config=xff_full` (O2 and ThinLTO), on macOS arm64 / Apple M5 Pro.
The fixture stays in memory: 128 nonmatching lines and one matching line per file; `filter-late`
places the matching line last. The unchanged early-match and count cases remain available.
Ten baseline/candidate rounds alternate executable order, with the first round discarded.
Each cell averages the fastest seven of the remaining nine observations. The per-case Google
Benchmark minimum is 0.02 seconds. One/four are worker allowances; macOS CPU affinity is not
available here. Raw observations and the invocation contract are in
[performance-p01-p02.json](performance-p01-p02.json).

Four-worker early-match native RE2 filtering improved 16.6% at 1,000 files and 19.2% at 10,000.
Late-match RE2 filtering was within 3%, including a 1.9% slowdown at 10,000. PCRE2 count output
improved 15.8-18.1% for 100-10,000 files. Ten-file paths avoid starting workers; one-worker
results and other PCRE2 filtering cases were within 4%. These are workload-specific results:
long-subject regex scanning can dominate scratch allocation, while many short line matches
benefit more. A separate 16 KiB matching microbenchmark showed no meaningful scratch benefit.

Host load increased sharply in the last round; the raw samples expose the outliers instead of
hiding them. An initial non-interleaved run showed a 29% RE2 startup regression at 100 files,
which did not reproduce in the alternating series. Linux and isolated-host confirmation remain
necessary before making a cross-platform claim or changing scheduling thresholds. P08 will
separately measure startup/batch policy. There is no new performance merge gate.

A separate 10,000-file/four-worker PCRE2 count run reported peak resident memory of
27,885,568 bytes for baseline and 27,230,208 bytes for candidate. These single process-level
observations include executable/runtime memory and are not allocation-count measurements.
Input reads remain one per file; matching workers retain one reusable scratch block per pattern.
The microbenchmark's default CPU column covers its calling thread, not total worker CPU time,
so it is not used to claim CPU savings.

Times are wall microseconds; change is candidate/baseline minus one (negative is faster).

| Engine | Workload    |  Files | Workers | Baseline us | Candidate us | Change |
| :----- | :---------- | -----: | ------: | ----------: | -----------: | -----: |
| RE2    | filter      |     10 |       1 |        23.8 |         23.6 |  -0.9% |
| RE2    | filter      |    100 |       1 |        56.2 |         54.9 |  -2.4% |
| RE2    | filter      |  1,000 |       1 |       381.5 |        369.2 |  -3.2% |
| RE2    | filter      | 10,000 |       1 |      3768.4 |       3659.0 |  -2.9% |
| RE2    | filter      |     10 |       4 |        23.9 |         23.9 |  +0.1% |
| RE2    | filter      |    100 |       4 |       128.6 |        127.0 |  -1.3% |
| RE2    | filter      |  1,000 |       4 |       427.8 |        356.9 | -16.6% |
| RE2    | filter      | 10,000 |       4 |      3559.5 |       2875.8 | -19.2% |
| RE2    | filter-late |     10 |       1 |       107.4 |        107.1 |  -0.2% |
| RE2    | filter-late |    100 |       1 |       891.2 |        890.1 |  -0.1% |
| RE2    | filter-late |  1,000 |       1 |      8746.4 |       8731.6 |  -0.2% |
| RE2    | filter-late | 10,000 |       1 |     85856.3 |      84270.2 |  -1.8% |
| RE2    | filter-late |     10 |       4 |       108.3 |        107.5 |  -0.8% |
| RE2    | filter-late |    100 |       4 |       397.7 |        394.4 |  -0.8% |
| RE2    | filter-late |  1,000 |       4 |      2555.6 |       2482.8 |  -2.9% |
| RE2    | filter-late | 10,000 |       4 |     23885.6 |      24351.0 |  +1.9% |
| RE2    | rg-counts   |     10 |       1 |       125.5 |        127.1 |  +1.3% |
| RE2    | rg-counts   |    100 |       1 |       937.2 |        927.9 |  -1.0% |
| RE2    | rg-counts   |  1,000 |       1 |      9015.4 |       9066.1 |  +0.6% |
| RE2    | rg-counts   | 10,000 |       1 |     89380.8 |      90298.5 |  +1.0% |
| RE2    | rg-counts   |     10 |       4 |       128.1 |        127.2 |  -0.7% |
| RE2    | rg-counts   |    100 |       4 |       449.3 |        442.3 |  -1.6% |
| RE2    | rg-counts   |  1,000 |       4 |      2918.0 |       2876.1 |  -1.4% |
| RE2    | rg-counts   | 10,000 |       4 |     27745.8 |      27750.0 |  +0.0% |
| PCRE2  | filter      |     10 |       1 |        24.3 |         24.3 |  +0.3% |
| PCRE2  | filter      |    100 |       1 |        58.3 |         57.4 |  -1.6% |
| PCRE2  | filter      |  1,000 |       1 |       386.9 |        375.8 |  -2.9% |
| PCRE2  | filter      | 10,000 |       1 |      3838.7 |       3704.4 |  -3.5% |
| PCRE2  | filter      |     10 |       4 |        24.8 |         24.7 |  -0.5% |
| PCRE2  | filter      |    100 |       4 |       125.8 |        130.2 |  +3.4% |
| PCRE2  | filter      |  1,000 |       4 |       359.8 |        364.2 |  +1.2% |
| PCRE2  | filter      | 10,000 |       4 |      2991.7 |       2932.6 |  -2.0% |
| PCRE2  | filter-late |     10 |       1 |        26.5 |         26.2 |  -1.0% |
| PCRE2  | filter-late |    100 |       1 |        80.5 |         79.1 |  -1.8% |
| PCRE2  | filter-late |  1,000 |       1 |       616.6 |        603.2 |  -2.2% |
| PCRE2  | filter-late | 10,000 |       1 |      6087.9 |       5984.5 |  -1.7% |
| PCRE2  | filter-late |     10 |       4 |        27.3 |         26.7 |  -2.4% |
| PCRE2  | filter-late |    100 |       4 |       134.7 |        134.8 |  +0.1% |
| PCRE2  | filter-late |  1,000 |       4 |       424.4 |        418.1 |  -1.5% |
| PCRE2  | filter-late | 10,000 |       4 |      3515.0 |       3593.7 |  +2.2% |
| PCRE2  | rg-counts   |     10 |       1 |        92.1 |         90.2 |  -2.1% |
| PCRE2  | rg-counts   |    100 |       1 |       579.3 |        600.3 |  +3.6% |
| PCRE2  | rg-counts   |  1,000 |       1 |      5567.1 |       5593.5 |  +0.5% |
| PCRE2  | rg-counts   | 10,000 |       1 |     55242.8 |      55721.1 |  +0.9% |
| PCRE2  | rg-counts   |     10 |       4 |        93.5 |         92.9 |  -0.6% |
| PCRE2  | rg-counts   |    100 |       4 |       340.4 |        278.7 | -18.1% |
| PCRE2  | rg-counts   |  1,000 |       4 |      2012.8 |       1694.9 | -15.8% |
| PCRE2  | rg-counts   | 10,000 |       4 |     19038.8 |      15825.9 | -16.9% |

## P03: parallel filtering with metadata-consuming output

Keep metadata collection and output ownership separate from matching eligibility. Summaries,
column/template output, colored listings and matched-entry callbacks still receive complete metadata,
but audited metadata-free predicates and rg content selection can now run in workers. Reductions,
rendering, traversal decisions and error publication remain on the coordinator. Native filesystem
case probes, collections, deferred predicates, dived containers and mounted/archive members retain
the serial evaluator. No metadata-dependent predicate becomes worker-eligible in this change.
Fuzzy scores are retained for presentation, and queued content results are drained before later
traversal errors so diagnostics preserve serial ordering.

Tests compare serial/parallel text and actual read-thread overlap for native and rg filters with
extension/type summaries, CSV columns, templates and color. They also verify one content read per
entry, fuzzy scores and mixed metadata/content error order. Existing run/evaluation cases remain
part of validation. This is filtering parallelism; summary maps are not concurrently mutated.

### Measurement and decision

Retain the change for substantial workloads, with the small native-PCRE2 startup cost recorded as
an explicit P08 scheduling input. The baseline is P01/P02 head `5b9db14f22`, with the identical
expanded `rg_benchmark` fixture. Both executables use `clang_release` (O2 + ThinLTO) and `xff_full`
on macOS arm64 / Apple M5 Pro. Each in-memory file has 128 nonmatching lines followed by one
matching line; both workloads request `--summary=ext --format=csv`. Before timing, each case checks
the selected-file total. Measurements alternate executable order for ten rounds, discard round one,
and average the fastest seven of nine observations (`--benchmark_min_time=0.02s`). Raw data and
invocation identity are in [performance-p03.json](performance-p03.json).

At 10,000 files/four workers, native RE2 summaries improve 70.5%, rg RE2 summaries 65.5%,
native PCRE2 summaries 36.8%, and rg PCRE2 summaries 70.6%. At 100 files, native PCRE2 is
64.6% slower (108 to 177 microseconds): very cheap JIT matching does not amortize pool startup.
The same small RE2 workload improves 53.8%, so a blanket higher entry-count threshold would lose
useful parallelism. P08 must consider cost and startup together. Ten-file cases remain inline;
one-worker measurements vary within 4.6%. These are elapsed-time observations on an unisolated
host, not CPU-affinity or total-worker CPU measurements, and do not establish Linux results.

The existing bounded 256-entry batches and per-worker scratch lifetime remain unchanged. Eager
metadata already existed for these output consumers. The change adds worker threads/results in
cases previously evaluated serially; no claim of reduced peak memory is made.

Times are wall microseconds; change is candidate/baseline minus one (negative is faster).

| Engine | Workload       |  Files | Workers | Baseline us | Candidate us | Change |
| :----- | :------------- | -----: | ------: | ----------: | -----------: | -----: |
| RE2    | filter-summary |     10 |       1 |       131.8 |        131.3 |  -0.4% |
| RE2    | filter-summary |    100 |       1 |      1045.6 |       1049.0 |  +0.3% |
| RE2    | filter-summary |  1,000 |       1 |      9959.8 |       9927.0 |  -0.3% |
| RE2    | filter-summary | 10,000 |       1 |     99378.0 |      97966.5 |  -1.4% |
| RE2    | filter-summary |     10 |       4 |       131.1 |        131.4 |  +0.2% |
| RE2    | filter-summary |    100 |       4 |      1040.0 |        480.1 | -53.8% |
| RE2    | filter-summary |  1,000 |       4 |     10119.9 |       3057.4 | -69.8% |
| RE2    | filter-summary | 10,000 |       4 |     98175.8 |      28959.2 | -70.5% |
| RE2    | rg-summary     |     10 |       1 |       165.4 |        172.2 |  +4.1% |
| RE2    | rg-summary     |    100 |       1 |      1151.1 |       1204.6 |  +4.6% |
| RE2    | rg-summary     |  1,000 |       1 |     11029.4 |      10934.5 |  -0.9% |
| RE2    | rg-summary     | 10,000 |       1 |    110340.3 |     108789.5 |  -1.4% |
| RE2    | rg-summary     |     10 |       4 |       166.9 |        168.4 |  +0.9% |
| RE2    | rg-summary     |    100 |       4 |      1173.3 |        579.9 | -50.6% |
| RE2    | rg-summary     |  1,000 |       4 |     11464.3 |       3752.5 | -67.3% |
| RE2    | rg-summary     | 10,000 |       4 |    110308.9 |      38004.0 | -65.5% |
| PCRE2  | filter-summary |     10 |       1 |        39.1 |         39.3 |  +0.4% |
| PCRE2  | filter-summary |    100 |       1 |       107.8 |        109.0 |  +1.1% |
| PCRE2  | filter-summary |  1,000 |       1 |       795.1 |        777.8 |  -2.2% |
| PCRE2  | filter-summary | 10,000 |       1 |      7660.1 |       7689.3 |  +0.4% |
| PCRE2  | filter-summary |     10 |       4 |        39.3 |         40.1 |  +1.9% |
| PCRE2  | filter-summary |    100 |       4 |       107.8 |        177.4 | +64.6% |
| PCRE2  | filter-summary |  1,000 |       4 |       796.2 |        593.1 | -25.5% |
| PCRE2  | filter-summary | 10,000 |       4 |      7631.0 |       4819.6 | -36.8% |
| PCRE2  | rg-summary     |     10 |       1 |       123.3 |        125.5 |  +1.8% |
| PCRE2  | rg-summary     |    100 |       1 |       780.9 |        744.7 |  -4.6% |
| PCRE2  | rg-summary     |  1,000 |       1 |      7239.1 |       7023.3 |  -3.0% |
| PCRE2  | rg-summary     | 10,000 |       1 |     71382.6 |      68100.0 |  -4.6% |
| PCRE2  | rg-summary     |     10 |       4 |       128.1 |        123.6 |  -3.5% |
| PCRE2  | rg-summary     |    100 |       4 |       803.6 |        365.9 | -54.5% |
| PCRE2  | rg-summary     |  1,000 |       4 |      7557.6 |       2103.0 | -72.2% |
| PCRE2  | rg-summary     | 10,000 |       4 |     74674.2 |      21946.4 | -70.6% |

## P04: entry content reuse and bound field transformations

Move the evaluator's content snapshot into the shared content layer. Each snapshot lazily owns
one entry's bytes, cached read failure, requested digest/encoding pairs and line count. Text/binary
classification, final-terminator checks and hashing reuse it. The serial evaluator hands it to
summary, histogram, column and template rendering; repeated hash/line fields share it, including
histogram line buckets plus line metrics. Standalone template rendering gets a local snapshot and
uses the VFS for both host paths and mounted content. No cache crosses entries or deferred passes.

The snapshot has one evaluator owner and no locks. Const read access can fill its private lazy
cache; it does not imply concurrent access is allowed. Effects and stateful predicates invalidate
before evaluation, and again afterward: a field-producing action may read bytes before its output
sink changes that same file. A regression case verifies that the following predicate sees the
new content. Failed hash-verification checks still contribute their verdict independently of the
whole expression's result; short-circuiting stays unchanged.

Field templates own compiled rewrite/extraction chains, replacements and reducer separators.
Render calls no longer parse or compile those regex programs per entry. Copies share immutable
programs, and tests destroy/change source text and copy/move templates before rendering. Existing
malformed-pattern, replacement, scalar/list, reducer, capture and escaping cases remain applicable.
Rendering remains coordinator-owned; this does not introduce shared mutable worker caches.

This change reuses the whole-file reads these consumers already required; it does not turn a
streaming rg filename/count/quiet search into a full-file read. It also does not retain raw worker
content in an entire result batch. Therefore a worker-filtered entry followed by content-derived
coordinator output can still require a second read. Carrying requested derived values instead of
unbounded content is the next extension if measurements justify it. The MBO digest algorithms do
have incremental stream state; exposing that through XFF's name-dispatched hash API is a separate
memory optimization from this reuse experiment. No new whole-file size limit is introduced.

### Measurement and decision

Retain both changes. Against P03 head `1e3cb82356`, the content-field workload improves
46-50% across 10/100/1,000/10,000 files: it combines text/end-of-line/content predicates with two
MD5 fields and two line-count fields. Integration tests confirm one read per regular entry instead
of repeated reads, and only requested digests/line counts are cached. The rewrite workload uses
two field chains and improves about 59% at ten files and 96.3% at 10,000 files (147 ms to 5.5 ms).
These intentionally exercise the redundant-work paths; ordinary searches should not expect those
percentages. The unchanged early-filter controls are within -2.2% to +3.7%.

Both executables use the same expanded `rg_benchmark`, O2+ThinLTO full builds and in-memory
SearchTree fixture on macOS arm64 / Apple M5 Pro. Ten rounds alternate binary order, discard the
first, and average the fastest seven of nine observations, with a 0.02-second per-case minimum.
Raw samples and invocation identity are in [performance-p04.json](performance-p04.json).
One/four are worker allowances, not affinity. Content-field and rewrite rendering remain serial;
the duplicated worker/backend settings demonstrate that this gain does not depend on enabling
additional concurrency. Field rewrites always use their specified RE2 semantics, regardless of
the command's content regex setting. This is a local elapsed-time result, not a CPU or RSS claim.

The memory tradeoff is one entry's existing whole-file materialization plus only its requested
small digest results and count. Compiled field programs live for the template's lifetime, replacing
per-entry compilation. No cross-entry content map, process-global result cache or shared lock is
introduced. Native Linux results and streaming hash memory improvements remain distinct analyses.

Times are wall microseconds; change is candidate/baseline minus one (negative is faster).

| Engine | Workload       |  Files | Workers | Baseline us | Candidate us | Change |
| :----- | :------------- | -----: | ------: | ----------: | -----------: | -----: |
| RE2    | filter         |     10 |       1 |        23.7 |         23.7 |  +0.4% |
| RE2    | filter         |    100 |       1 |        57.1 |         56.9 |  -0.4% |
| RE2    | filter         |  1,000 |       1 |       385.8 |        385.6 |  -0.1% |
| RE2    | filter         | 10,000 |       1 |      3819.2 |       3805.2 |  -0.4% |
| RE2    | filter         |     10 |       4 |        24.2 |         24.2 |  -0.1% |
| RE2    | filter         |    100 |       4 |       128.8 |        133.6 |  +3.7% |
| RE2    | filter         |  1,000 |       4 |       367.0 |        367.4 |  +0.1% |
| RE2    | filter         | 10,000 |       4 |      3019.6 |       2957.2 |  -2.1% |
| RE2    | content-fields |     10 |       1 |       467.0 |        247.5 | -47.0% |
| RE2    | content-fields |    100 |       1 |      4319.7 |       2242.5 | -48.1% |
| RE2    | content-fields |  1,000 |       1 |     43122.4 |      22299.9 | -48.3% |
| RE2    | content-fields | 10,000 |       1 |    438073.0 |     219022.4 | -50.0% |
| RE2    | content-fields |     10 |       4 |       457.3 |        247.1 | -46.0% |
| RE2    | content-fields |    100 |       4 |      4514.4 |       2331.5 | -48.4% |
| RE2    | content-fields |  1,000 |       4 |     42680.8 |      22200.6 | -48.0% |
| RE2    | content-fields | 10,000 |       4 |    434193.9 |     220559.4 | -49.2% |
| RE2    | rewrites       |     10 |       1 |       178.6 |         72.4 | -59.5% |
| RE2    | rewrites       |    100 |       1 |      1459.9 |        118.9 | -91.9% |
| RE2    | rewrites       |  1,000 |       1 |     14734.9 |        589.1 | -96.0% |
| RE2    | rewrites       | 10,000 |       1 |    147152.4 |       5508.3 | -96.3% |
| RE2    | rewrites       |     10 |       4 |       176.6 |         71.7 | -59.4% |
| RE2    | rewrites       |    100 |       4 |      1470.0 |        120.5 | -91.8% |
| RE2    | rewrites       |  1,000 |       4 |     14629.3 |        589.2 | -96.0% |
| RE2    | rewrites       | 10,000 |       4 |    147699.5 |       5528.3 | -96.3% |
| PCRE2  | filter         |     10 |       1 |        23.8 |         24.0 |  +0.5% |
| PCRE2  | filter         |    100 |       1 |        57.5 |         57.2 |  -0.6% |
| PCRE2  | filter         |  1,000 |       1 |       394.9 |        392.5 |  -0.6% |
| PCRE2  | filter         | 10,000 |       1 |      3847.2 |       3851.6 |  +0.1% |
| PCRE2  | filter         |     10 |       4 |        24.4 |         24.4 |  +0.1% |
| PCRE2  | filter         |    100 |       4 |       129.1 |        126.2 |  -2.2% |
| PCRE2  | filter         |  1,000 |       4 |       366.6 |        365.3 |  -0.4% |
| PCRE2  | filter         | 10,000 |       4 |      2915.3 |       2928.7 |  +0.5% |
| PCRE2  | content-fields |     10 |       1 |       464.6 |        247.5 | -46.7% |
| PCRE2  | content-fields |    100 |       1 |      4420.2 |       2247.2 | -49.2% |
| PCRE2  | content-fields |  1,000 |       1 |     43602.4 |      22098.6 | -49.3% |
| PCRE2  | content-fields | 10,000 |       1 |    435068.1 |     218591.1 | -49.8% |
| PCRE2  | content-fields |     10 |       4 |       466.7 |        249.8 | -46.5% |
| PCRE2  | content-fields |    100 |       4 |      4478.6 |       2311.9 | -48.4% |
| PCRE2  | content-fields |  1,000 |       4 |     43687.8 |      22215.1 | -49.2% |
| PCRE2  | content-fields | 10,000 |       4 |    432579.3 |     220612.4 | -49.0% |
| PCRE2  | rewrites       |     10 |       1 |       179.7 |         72.0 | -59.9% |
| PCRE2  | rewrites       |    100 |       1 |      1450.6 |        120.0 | -91.7% |
| PCRE2  | rewrites       |  1,000 |       1 |     14651.2 |        586.7 | -96.0% |
| PCRE2  | rewrites       | 10,000 |       1 |    146807.5 |       5494.9 | -96.3% |
| PCRE2  | rewrites       |     10 |       4 |       177.7 |         71.8 | -59.6% |
| PCRE2  | rewrites       |    100 |       4 |      1472.6 |        118.9 | -91.9% |
| PCRE2  | rewrites       |  1,000 |       4 |     14688.4 |        580.8 | -96.0% |
| PCRE2  | rewrites       | 10,000 |       4 |    148554.3 |       5513.5 | -96.3% |

## P05: independent comparison pairs and persistent cursors

Retain bounded pair comparisons, persistent cursors for multi-chunk files, and small patch-input
reuse. Both root scans still finish before pairing; this is not a directory-by-directory comparison
pipeline. The coordinator merges relative paths in batches of at most 64, workers return indexed
values, and the coordinator publishes statuses, errors, patches and summary contributions in the
same order. Workers never invoke output or mutation callbacks. Read-ahead may inspect later pairs
before an earlier error is published, as with the existing matcher worker batches.

Files of at most 64 KiB retain the single-range path. A batch starts workers only when at least two
larger equal-size regular-file pairs need reads and their estimated combined left-side size reaches
256 KiB. Metadata-only and small-file batches stay inline. Owned or archive-member sources keep
serial comparisons. Pools persist across batches and grow only when useful work warrants it.

Larger regular files open one cursor per side and keep it through the comparison, instead of
reopening or replaying the source for each 64 KiB range. Short non-EOF reads are accumulated;
empty reads mean EOF; read and open failures remain errors. Left-side read errors retain priority
over right-side open errors. Each live cursor reserves a 64 KiB buffer from its source budget and
releases it with the comparison. Metadata size still bounds comparison if a file changes after the
scan; this does not establish an atomic filesystem snapshot.

For selected patches, complete differing inputs of at most 64 KiB per side can be retained.
An extra byte detects growth beyond the metadata size, preventing truncated patch reuse. The
64-pair batch retains at most 8 MiB of these bytes; large patches retain their existing serial
whole-input materialization. Constructing the diff's owned input records directly removes redundant
copies and binary-sniff substrings. No output-producing action moves into a worker.

Tests cover serial/parallel status, diff and JSON-summary equivalence across more than two batches,
actual concurrent reads, indexed errors, repeated batches, short reads/EOF, stale sizes, cursor
failures, read-budget exhaustion/release, missing entries, type/size shortcuts, symlinks and owned
source guards. The benchmark checks output before timing and uses an efficient in-memory range
implementation for the parent, avoiding an artificial baseline that copies a whole file per range.

### Measurement and decision

Parent `a885ceb6ce` and candidate use the identical `compare_benchmark` fixture, optimized
`clang_release` (O2 + ThinLTO), `xff_full`, macOS arm64 / Apple M5 Pro. Four workloads cover 4 KiB
identical files, 256 KiB identical files, 256 KiB files differing at the first byte, and 4 KiB text
patches. Each is measured with 10/100/1,000/10,000 pairs and one/four worker allowances. Ten
alternating baseline/candidate rounds discard the first round and average the fastest seven of
nine; Google Benchmark minimum time is 0.02 seconds. No CPU affinity, host-I/O speedup, Linux result
or total CPU-saving claim follows from these in-memory measurements.

The rejected first prototype started workers for 32 small pairs: 100/1,000/10,000 small-file
four-worker cases became 86%/50%/37% slower. Restricting startup to larger pairs and restoring the
single-range small-file path removed that regression. This is why entry count alone is not a useful
universal worker threshold. Raw retained observations are in
[performance-p05.json](performance-p05.json).

The retained four-worker large-identical workload improves 44-79%; large early differences improve
62-68% at 100-10,000 pairs. Small comparisons vary from -0.4% to +2.6%, and small patches improve
3-8% after removing redundant copies. Tradeoffs remain explicit: ten large early-different pairs
are 13% slower (158 to 178 microseconds), and one-worker large-identical cases range from -2% to
+18%. Persistent source/cursor setup is extra work for an already cheap in-memory range backend;
the experiment does not prove a serial win. Retain the substantially faster parallel path and
bounded read reuse, with one-worker and early-exit costs available for P08 decisions. The data is
noisy for the large one-worker workloads, so these are observations rather than universal limits.

| Workload              |  Pairs | Workers | Parent us | Candidate us | Change |
| :-------------------- | -----: | ------: | --------: | -----------: | -----: |
| small-identical       |     10 |       1 |      58.4 |         58.9 |  +0.9% |
| small-identical       |    100 |       1 |     111.5 |        113.7 |  +1.9% |
| small-identical       |  1,000 |       1 |     706.6 |        703.4 |  -0.4% |
| small-identical       | 10,000 |       1 |    7061.1 |       7184.3 |  +1.7% |
| small-identical       |     10 |       4 |      53.3 |         54.7 |  +2.6% |
| small-identical       |    100 |       4 |     110.9 |        113.0 |  +1.9% |
| small-identical       |  1,000 |       4 |     700.4 |        712.5 |  +1.7% |
| small-identical       | 10,000 |       4 |    7165.8 |       7153.8 |  -0.2% |
| large-identical       |     10 |       1 |     415.7 |        459.8 | +10.6% |
| large-identical       |    100 |       1 |    3676.2 |       3754.3 |  +2.1% |
| large-identical       |  1,000 |       1 |   29890.7 |      35322.9 | +18.2% |
| large-identical       | 10,000 |       1 |  348410.1 |     341924.8 |  -1.9% |
| large-identical       |     10 |       4 |     480.9 |        269.7 | -43.9% |
| large-identical       |    100 |       4 |    4127.7 |       1097.5 | -73.4% |
| large-identical       |  1,000 |       4 |   37478.2 |       9794.9 | -73.9% |
| large-identical       | 10,000 |       4 |  415270.8 |      89338.2 | -78.5% |
| large-early-different |     10 |       1 |     124.2 |        120.5 |  -3.0% |
| large-early-different |    100 |       1 |    1041.0 |        683.6 | -34.3% |
| large-early-different |  1,000 |       1 |    7457.0 |       7203.8 |  -3.4% |
| large-early-different | 10,000 |       1 |   62761.9 |      67086.7 |  +6.9% |
| large-early-different |     10 |       4 |     158.0 |        178.1 | +12.7% |
| large-early-different |    100 |       4 |    1140.5 |        436.7 | -61.7% |
| large-early-different |  1,000 |       4 |   10166.4 |       3220.8 | -68.3% |
| large-early-different | 10,000 |       4 |   86961.9 |      28479.0 | -67.3% |
| small-patches         |     10 |       1 |      90.5 |         87.6 |  -3.3% |
| small-patches         |    100 |       1 |     446.9 |        421.6 |  -5.7% |
| small-patches         |  1,000 |       1 |    4008.3 |       3689.2 |  -8.0% |
| small-patches         | 10,000 |       1 |   40041.9 |      37272.6 |  -6.9% |
| small-patches         |     10 |       4 |      91.4 |         87.7 |  -4.0% |
| small-patches         |    100 |       4 |     449.2 |        424.1 |  -5.6% |
| small-patches         |  1,000 |       4 |    4031.3 |       3756.2 |  -6.8% |
| small-patches         | 10,000 |       4 |   40145.6 |      36939.2 |  -8.0% |

## P06: eager metadata within broad directories

Retain stat chunking for large coordinator-read directory listings. Existing read-ahead jobs still
list/stat their own directories as independent leaf jobs. The coordinator can instead enumerate a
listing, submit 128-entry stat chunks to the same pool, and join their values in original listing
order. No worker waits for another job in its own pool, and the visitor, sorting, pruning, errors,
and lazy metadata ownership remain on the coordinator.

Only eagerly required metadata, at least 512 children, and more than one allowed worker activate
this path. Small, lazy, metadata-free, owned and archive-source listings retain the existing serial
stat path. Basic/birth-time requests, followed-link fallback and readdir-race handling are preserved.
Already-prefetched directory jobs retain their existing whole-directory work; this does not split
every directory anywhere in the read-ahead tree.

At most one 128-entry chunk per allowed worker is pending. Consuming the next ordered chunk refills
that slot, bounding completed-but-unconsumed metadata to the worker window rather than retaining a
second whole listing. Enumeration and the final listing still use their existing linear storage.
Tests verify actual overlapping stats, serial/parallel visit and error equality, unchanged field
selection, race suppression, lazy/no-stat behavior, and archive/owned-source guards.

### Measurement and decision

This experiment uses the host filesystem because an in-memory no-op `Stat` would measure scheduling
and allocation rather than system-call cost. Parent PR #928 code at `cb0a6a5881` and candidate are
optimized O2 + ThinLTO full builds on macOS arm64 / Apple M5 Pro. Independent flat roots contain
10/100/1,000/10,000 files of eight bytes each. Ten rounds alternate parent/candidate order within
each workload/size/worker pair; discard the first round and average the fastest seven of nine.
Timings include subprocess startup, configuration loading and captured output, with warm filesystem
caches. No CPU affinity or cold-disk/network/Linux improvement is claimed. Raw samples and invocation
identity are in [performance-p06.json](performance-p06.json).

At four workers, 1,000-file metadata workloads improve 8-10% and 10,000-file workloads improve
33-35%. One-worker equivalents remain within 2%. The byte-exact name-listing control, which avoids
child stats, remains within 3.3% (less than 0.3 ms for the largest relative increase). Small cases
are dominated by roughly 7-8 ms of process/setup overhead; they do not start stat workers.

The distinction between native and byte-exact name matching matters: ordinary XFF `-name` respects
the volume's case rules and currently requires each entry's device metadata. Consequently both
`-name '*.txt'` and that expression followed by `-printf '.'` exercise eager stats. The `--exact`
control does not. The tests additionally verify directly that metadata-free/lazy visits issue no
child stats, independent of subprocess timing noise.

| Workload     |  Files | Workers | Parent ms | Candidate ms | Change |
| :----------- | -----: | ------: | --------: | -----------: | -----: |
| stat-summary |     10 |       1 |     7.785 |        7.700 |  -1.1% |
| stat-summary |    100 |       1 |     7.598 |        7.660 |  +0.8% |
| stat-summary |  1,000 |       1 |     9.730 |        9.576 |  -1.6% |
| stat-summary | 10,000 |       1 |    30.372 |       30.161 |  -0.7% |
| stat-summary |     10 |       4 |     7.846 |        7.868 |  +0.3% |
| stat-summary |    100 |       4 |     7.738 |        7.753 |  +0.2% |
| stat-summary |  1,000 |       4 |     9.877 |        9.080 |  -8.1% |
| stat-summary | 10,000 |       4 |    31.194 |       20.378 | -34.7% |
| print-action |     10 |       1 |     7.815 |        7.719 |  -1.2% |
| print-action |    100 |       1 |     7.543 |        7.463 |  -1.1% |
| print-action |  1,000 |       1 |     9.924 |        9.882 |  -0.4% |
| print-action | 10,000 |       1 |    32.478 |       32.245 |  -0.7% |
| print-action |     10 |       4 |     7.754 |        7.761 |  +0.1% |
| print-action |    100 |       4 |     7.591 |        7.536 |  -0.7% |
| print-action |  1,000 |       4 |     9.682 |        8.840 |  -8.7% |
| print-action | 10,000 |       4 |    32.436 |       21.697 | -33.1% |
| native-name  |     10 |       1 |     7.690 |        7.882 |  +2.5% |
| native-name  |    100 |       1 |     7.754 |        7.591 |  -2.1% |
| native-name  |  1,000 |       1 |     9.920 |       10.008 |  +0.9% |
| native-name  | 10,000 |       1 |    32.252 |       32.435 |  +0.6% |
| native-name  |     10 |       4 |     7.668 |        7.853 |  +2.4% |
| native-name  |    100 |       4 |     7.706 |        7.736 |  +0.4% |
| native-name  |  1,000 |       4 |     9.923 |        8.960 |  -9.7% |
| native-name  | 10,000 |       4 |    32.559 |       21.809 | -33.0% |
| exact-name   |     10 |       1 |     7.432 |        7.286 |  -2.0% |
| exact-name   |    100 |       1 |     7.153 |        7.390 |  +3.3% |
| exact-name   |  1,000 |       1 |     7.909 |        7.841 |  -0.9% |
| exact-name   | 10,000 |       1 |    14.530 |       14.128 |  -2.8% |
| exact-name   |     10 |       4 |     7.414 |        7.404 |  -0.1% |
| exact-name   |    100 |       4 |     7.166 |        7.174 |  +0.1% |
| exact-name   |  1,000 |       4 |     7.888 |        7.826 |  -0.8% |
| exact-name   | 10,000 |       4 |    14.205 |       14.330 |  +0.9% |

## P07: archive-member concurrency decision

Retain serial archive-member matching. The measurement-only prototype removed the owning-filesystem
and archive-member exclusions from `parallel_entry`, retaining the existing `!visit.dived` exclusion
and copying each filesystem owner into the pending entries. No safety checks or read budgets were
bypassed. Ordered output matched the parent for every retained sample.

TAR members show substantial available parallel work: at 1,000 members four workers reduce elapsed
time by 65% for uncompressed TAR and 73% for gzip TAR. ZIP behaves differently: 100 members regress
13%, and a single exploratory 1,000-member ZIP invocation took roughly 38 seconds serially and
44 seconds with the prototype. Those single exploratory durations are not repeated performance
claims; they motivated bounding the ZIP matrix at 100 members. Format-specific reader cost needs
investigation before a general archive-parallel flag or default is justified.

More importantly, `ConcurrentMemberCursorsShareMemoryAndReleaseItBeforeSerialRetry` now proves the
resource issue using an in-memory gzip TAR. A shared budget admits either member's 64 KiB decoder
buffer and four-byte probe, but rejects the second cursor while the first is alive. Releasing the
first cursor allows the second to complete and returns memory use to zero. This intentionally small
budget models the same admission problem with nested decoders and larger default budgets. Repeated
opens also retain the existing cumulative replay accounting. A generic retry cannot refund consumed
replay work. Ownership and thread safety alone are insufficient admission criteria.

The next design should expose a backend capability and bounded admission, or extract members
serially into a byte-bounded queue for parallel matching. It must preserve shared memory/replay
limits, error ordering, and cancellation. Keep this separate from host-file scheduling and measure
ZIP indexing/restart cost independently. No rejected prototype code is shipped.

### Measurement protocol

Parent: `4f8b3bc72acd49f16a173408264f8893bc848ff0`; macOS arm64, Apple M5 Pro,
`clang_release` O2/ThinLTO with all extras. These are warm host-file archive reads, not the
in-memory engine matrix. Python `tarfile` produced uncompressed and gzip TAR; `zipfile` used
`ZIP_DEFLATED`. Members are `000000.txt`, `000001.txt`, etc., each containing 128 repetitions of
`unmatched content for line selection\n` and a final `alpha123 needle\n` (4,752 bytes).

Run both binaries with `--no-config --jobs=N --archive=roots --sort=dir ARCHIVE -rxc
'(alpha|beta)[0-9]{3}.*needle'`. Five paired rounds alternate which binary runs first; discard
the warm-up round and average the fastest three of four. Complete process startup and output
capture are included; exact output and member counts are checked. Worker allowances do not pin
CPU affinity. Raw samples and the prototype change are in
[`performance-p07.json`](performance-p07.json). Positive percentages mean less elapsed time.

| Format | Members | Workers | Parent ms | Prototype ms | Time saved |
| ------ | ------: | ------: | --------: | -----------: | ---------: |
| tar    |      10 |       1 |     8.971 |        8.846 |      +1.4% |
| tar    |      10 |       4 |     8.297 |        8.241 |      +0.7% |
| tar    |     100 |       1 |    14.984 |       15.209 |      -1.5% |
| tar    |     100 |       4 |    15.351 |       11.480 |     +25.2% |
| tar    |    1000 |       1 |   332.569 |      339.372 |      -2.0% |
| tar    |    1000 |       4 |   341.784 |      120.589 |     +64.7% |
| tgz    |      10 |       1 |     9.180 |        8.911 |      +2.9% |
| tgz    |      10 |       4 |     8.810 |        9.068 |      -2.9% |
| tgz    |     100 |       1 |    24.935 |       24.351 |      +2.3% |
| tgz    |     100 |       4 |    24.817 |       14.939 |     +39.8% |
| tgz    |    1000 |       1 |  1131.095 |     1087.916 |      +3.8% |
| tgz    |    1000 |       4 |  1138.521 |      303.981 |     +73.3% |
| zip    |      10 |       1 |    13.956 |       14.150 |      -1.4% |
| zip    |      10 |       4 |    13.903 |       13.639 |      +1.9% |
| zip    |     100 |       1 |   405.933 |      404.428 |      +0.4% |
| zip    |     100 |       4 |   400.163 |      451.010 |     -12.7% |

## P08: heterogeneous scheduling, startup, and buffering

Retain four-entry matcher chunks, growth of the worker pool for later larger batches, and inline
execution for fewer than 16 entries or a one-worker allowance. Decision-only batches can hold
1,024 entries; batches carrying rendered content keep their 256-entry limit. The initial parallel threshold stays
at 64 entries. Threads still receive indexed independent entries; results, errors, effects and
rendering remain ordered on the coordinator. Entry content and lazy metadata acquire no locks.

Previously a first 64-entry batch could permanently cap a requested 12-worker pool at four workers.
The pool can now grow to the requested allowance when a later full batch has enough work. The
regression test holds readers at a barrier and observes four then twelve distinct workers, followed
by an eight-entry tail on the coordinator. Existing serial/parallel output-mode and error-order
tests cover the unchanged publication contract. Archive-member reads remain serial.

### Retained change measurements

The parent is `4f8b3bc72acd49f16a173408264f8893bc848ff0`. Both binaries use the same extended
`//xff/engine:rg_benchmark` fixtures, with no host file I/O. The uniform input is 4,752 bytes per
file. Heterogeneous cases replace either the last 16 entries in each 256-entry block (clustered),
or one entry in every 16 (interleaved), with a 131,088-byte file. Its only match is at the end.
Counts are 10, 100, 1,000 and 10,000, with one/four worker allowances. The 10- and 100-entry
clustered cases contain no large entries; they are intentional small-batch controls.

On macOS arm64 / Apple M5 Pro, O2/ThinLTO with all extras, ten alternating paired rounds discard
the warm-up and average the fastest seven of nine (`--benchmark_min_time=0.02s`, real time).
Every case validates its selected output count before timing. Raw samples, including one-worker
controls, are in [`performance-p08.json`](performance-p08.json).

At 1,000/10,000 clustered files, the final combination improves RE2 by 61/64% and PCRE2 by 45/51%.
Uniform early filters improve 13-21% at those sizes, and interleaved RE2 improves 1-5%.
One-worker controls range from 4.6% faster to 3.1% slower. The largest four-worker
regression in this matrix is 2.3% on the 100-file PCRE2 early-filter case (124.0 to 126.9 us).
These are workload-specific macOS results, not a universal or cross-platform speedup claim.

| Workload                 | Files | Workers | Parent us | Candidate us | Time saved |
| ------------------------ | ----: | ------: | --------: | -----------: | ---------: |
| RE2 filter               |    10 |       4 |      24.1 |         23.9 |      +1.0% |
| RE2 filter               |   100 |       4 |     127.5 |        125.5 |      +1.6% |
| RE2 filter               |  1000 |       4 |     372.7 |        298.5 |     +19.9% |
| RE2 filter               | 10000 |       4 |    3073.0 |       2418.2 |     +21.3% |
| RE2 filter-late          |    10 |       4 |     107.8 |        107.5 |      +0.2% |
| RE2 filter-late          |   100 |       4 |     397.6 |        351.5 |     +11.6% |
| RE2 filter-late          |  1000 |       4 |    2535.4 |       2407.9 |      +5.0% |
| RE2 filter-late          | 10000 |       4 |   24321.2 |      23331.7 |      +4.1% |
| RE2 filter-clustered     |    10 |       4 |     107.9 |        107.2 |      +0.7% |
| RE2 filter-clustered     |   100 |       4 |     395.8 |        352.2 |     +11.0% |
| RE2 filter-clustered     |  1000 |       4 |   13002.6 |       5100.9 |     +60.8% |
| RE2 filter-clustered     | 10000 |       4 |  161518.7 |      58815.8 |     +63.6% |
| RE2 filter-interleaved   |    10 |       4 |     107.6 |        107.4 |      +0.2% |
| RE2 filter-interleaved   |   100 |       4 |     844.3 |        771.8 |      +8.6% |
| RE2 filter-interleaved   |  1000 |       4 |    6142.3 |       5824.1 |      +5.2% |
| RE2 filter-interleaved   | 10000 |       4 |   60133.3 |      59284.5 |      +1.4% |
| RE2 filter-summary       |    10 |       4 |     112.9 |        112.7 |      +0.1% |
| RE2 filter-summary       |   100 |       4 |     402.6 |        359.5 |     +10.7% |
| RE2 filter-summary       |  1000 |       4 |    2649.7 |       2488.4 |      +6.1% |
| RE2 filter-summary       | 10000 |       4 |   24561.0 |      22851.2 |      +7.0% |
| RE2 rg-counts            |    10 |       4 |     127.1 |        127.2 |      -0.1% |
| RE2 rg-counts            |   100 |       4 |     442.6 |        398.7 |      +9.9% |
| RE2 rg-counts            |  1000 |       4 |    2865.3 |       2801.8 |      +2.2% |
| RE2 rg-counts            | 10000 |       4 |   27476.6 |      27026.6 |      +1.6% |
| PCRE2 filter             |    10 |       4 |      24.6 |         24.5 |      +0.5% |
| PCRE2 filter             |   100 |       4 |     124.0 |        126.9 |      -2.3% |
| PCRE2 filter             |  1000 |       4 |     373.5 |        312.9 |     +16.2% |
| PCRE2 filter             | 10000 |       4 |    3025.3 |       2619.3 |     +13.4% |
| PCRE2 filter-late        |    10 |       4 |      26.7 |         27.0 |      -1.1% |
| PCRE2 filter-late        |   100 |       4 |     130.0 |        132.1 |      -1.6% |
| PCRE2 filter-late        |  1000 |       4 |     429.6 |        362.1 |     +15.7% |
| PCRE2 filter-late        | 10000 |       4 |    3614.8 |       2996.6 |     +17.1% |
| PCRE2 filter-clustered   |    10 |       4 |      26.6 |         26.5 |      +0.4% |
| PCRE2 filter-clustered   |   100 |       4 |     131.4 |        130.7 |      +0.6% |
| PCRE2 filter-clustered   |  1000 |       4 |     854.4 |        466.9 |     +45.4% |
| PCRE2 filter-clustered   | 10000 |       4 |    8875.5 |       4316.8 |     +51.4% |
| PCRE2 filter-interleaved |    10 |       4 |      26.7 |         26.6 |      +0.3% |
| PCRE2 filter-interleaved |   100 |       4 |     148.2 |        146.8 |      +1.0% |
| PCRE2 filter-interleaved |  1000 |       4 |     589.7 |        494.8 |     +16.1% |
| PCRE2 filter-interleaved | 10000 |       4 |    5148.0 |       4294.1 |     +16.6% |
| PCRE2 filter-summary     |    10 |       4 |      32.6 |         32.0 |      +1.8% |
| PCRE2 filter-summary     |   100 |       4 |     139.9 |        137.3 |      +1.8% |
| PCRE2 filter-summary     |  1000 |       4 |     552.6 |        474.1 |     +14.2% |
| PCRE2 filter-summary     | 10000 |       4 |    3969.3 |       3333.5 |     +16.0% |
| PCRE2 rg-counts          |    10 |       4 |      92.9 |         93.0 |      -0.1% |
| PCRE2 rg-counts          |   100 |       4 |     276.1 |        267.2 |      +3.2% |
| PCRE2 rg-counts          |  1000 |       4 |    1644.6 |       1572.5 |      +4.4% |
| PCRE2 rg-counts          | 10000 |       4 |   15853.1 |      14748.1 |      +7.0% |

### Rejected startup and one-entry variants

A four-round screening pass (discard first, best two of three, 0.01-second minimum per case)
compared chunk sizes one and four while raising the startup threshold to 128. Cheap 100-file
filters improved about 54%, but late RE2 filters and rg counts more than doubled in duration when
forced serial. Keep the 64-entry startup threshold; an entry-count-only increase is not a sound
cost model. One-entry scheduling offered little extra benefit for clustered inputs and regressed
several uniform cases by 4-10%, so retain four-entry chunks. Screening samples, separately labeled
from the final measurement protocol, are in
[`performance-p08-experiments.json`](performance-p08-experiments.json).

### Batch size, retained bytes, and pipeline overlap

Additional real-time experiments use the retained four-entry scheduler, with 64, 256 or 1,024
entries per batch. Large-output fixtures select the entire 131,088-byte line from each file;
the benchmark counts records without keeping an additional full-output copy. RSS is whole-process
peak, averaged across three fresh processes, not a precise allocator or decoder budget. The timing
protocol is best two of three after one warm-up round.

For the 10,000-file early-filter case, 1,024-entry batches improve time by about 23%, while
64-entry batches regress about 65%. Larger batches also help the heterogeneous cases. Retain
1,024 entries only for decision-only matching, where workers release source bytes immediately and
the results contain no rendered content records. Keep the 256-entry limit for content output.

Large-output measurements demonstrate why one entry cap cannot represent a byte budget: at
10,000 files, 64/256/1,024-entry variants peak around 63/125/335 MiB. The 64-entry variant saves
about 4% elapsed time here, but its much higher coordinator/barrier cost for ordinary searches
rules out a universal smaller batch. Neither cap limits one long input line or selected result.
A true byte-aware queue needs incremental output admission and ordered backpressure; merely
estimating from file size would require metadata and would miss formatting expansion. That design
is recorded as a follow-up rather than claiming a byte bound from this experiment.

The overlap prototype starts one matcher batch asynchronously while traversal accumulates the
next, then joins and emits on the coordinator before serial effects or errors. It preserves
owning entries and passes the benchmark output-count checks. It improves the tested large-output
case by less than 1%, leaves clustered/interleaved heavy work close, and makes the cheap 10,000-file
filter about 17% slower. Reject this implementation: it adds an async coordinator and queued batch
without a useful measured gain. A persistent pipeline could avoid per-batch async startup, but
requires the same byte-aware admission and a shared CPU allowance across stages. These results
do not claim that every possible overlapping pipeline is slower.

| Workload                 | Variant    | Time ms | Time saved vs 256 | Mean peak RSS MiB |
| ------------------------ | ---------- | ------: | ----------------: | ----------------: |
| large-output/1000        | buffer256  |  40.675 |             +0.0% |              74.4 |
| large-output/1000        | buffer64   |  39.216 |             +3.6% |              38.1 |
| large-output/1000        | buffer1024 |  47.182 |            -16.0% |             211.6 |
| large-output/1000        | overlap    |  40.495 |             +0.4% |              71.5 |
| large-output/10000       | buffer256  | 407.194 |             +0.0% |             125.4 |
| large-output/10000       | buffer64   | 389.151 |             +4.4% |              62.7 |
| large-output/10000       | buffer1024 | 471.622 |            -15.8% |             334.5 |
| large-output/10000       | overlap    | 403.952 |             +0.8% |             101.5 |
| traversal/10000          | buffer256  |   1.419 |             +0.0% |              17.1 |
| traversal/10000          | buffer64   |   1.390 |             +2.1% |              15.0 |
| traversal/10000          | buffer1024 |   1.424 |             -0.4% |              15.6 |
| traversal/10000          | overlap    |   1.426 |             -0.4% |              15.9 |
| filter/10000             | buffer256  |   3.122 |             +0.0% |              25.8 |
| filter/10000             | buffer64   |   5.165 |            -65.5% |              26.6 |
| filter/10000             | buffer1024 |   2.412 |            +22.7% |              29.3 |
| filter/10000             | overlap    |   3.657 |            -17.2% |              25.2 |
| filter-clustered/10000   | buffer256  |  61.859 |             +0.0% |              16.9 |
| filter-clustered/10000   | buffer64   |  66.356 |             -7.3% |              16.4 |
| filter-clustered/10000   | buffer1024 |  59.048 |             +4.5% |              20.3 |
| filter-clustered/10000   | overlap    |  62.143 |             -0.5% |              16.9 |
| filter-interleaved/10000 | buffer256  |  63.238 |             +0.0% |              17.9 |
| filter-interleaved/10000 | buffer64   |  77.577 |            -22.7% |              16.4 |
| filter-interleaved/10000 | buffer1024 |  59.212 |             +6.4% |              20.1 |
| filter-interleaved/10000 | overlap    |  62.940 |             +0.5% |              16.3 |

## macOS startup investigation (2026-10-02)

Investigated current main `91a865617c` in a separate worktree using the production
`--config=clang_release` lean executable on the local M5 Pro. The published local backfill
currently ends at `6574a7e18b` (#934), before the later compiler/linker changes; it is useful history,
but is not the current-main executable used here. The local timezone/framework prototype and its
measurements follow the initial investigation below. Raw observations are in [performance-startup.json](performance-startup.json).

### Measured fixed costs

Rotate commands, discard two warmups and retain 40 subprocess wall-time samples per case, with
stdout redirected to `/dev/null`. Separately instrument CLI stages with `steady_clock` and collect
20 samples after two warmups. Stage logging adds a little overhead, so these are attribution
measurements rather than release-binary speed claims. Temporary fixtures use warm local APFS;
there is no CPU affinity. Do not combine absolute medians from different experiment sessions:
background load changed substantially during later linker trials. The launch measurements include
Python's child-process creation/waiting overhead as well as dyld, static initialization and exit.

| Probe                                                 |        Median | Interpretation                                                    |
| :---------------------------------------------------- | ------------: | :---------------------------------------------------------------- |
| Minimal C++ program with iostream                     |      3.178 ms | Launch/loader/control baseline, not XFF work                      |
| Same program linked to CoreFoundation                 |      4.447 ms | About 1.27 ms additional fixed cost in this session               |
| Same program linked to both Foundation frameworks     |      4.461 ms | CoreFoundation already brings the expensive dependency tree       |
| Current-main XFF `--version`                          |      5.630 ms | Full process invocation                                           |
| Current-main XFF empty `-type f` search               |      7.608 ms | Full invocation with enforced config discovery                    |
| Same empty search with `--timezone=UTC`               |      6.821 ms | Controlled timezone diagnostic, not a proposed change of defaults |
| Instrumented XFF `--version`, application only        |      0.064 ms | Most version-command elapsed time is outside CLI execution        |
| Instrumented empty search, application only           |      1.764 ms | Includes config, execution preparation and an empty walk          |
| OS-account home lookup within empty search            | about 0.89 ms | Required authoritative identity for the user-config path          |
| Default local-timezone resolution within empty search |      0.590 ms | Done even when no matcher or output consumes civil time           |
| Explicit UTC resolution within empty search           |      0.009 ms | Avoids querying the macOS default timezone                        |

The home-lookup estimate is the difference between median adjacent stage timestamps, not a
separately isolated end-to-end optimization. Default timezone and account lookup dominate the
approximately 1.76 ms application work; other work is roughly 0.3 ms. The existing parser
microbenchmark takes approximately 1 us for one `-name` predicate, 2.7 us for 16, and 46 us for 256. It does not establish the cost of every flag, cold lookup, config file or regex.

`DYLD_PRINT_INITIALIZERS=1` reports five initializer calls for a minimal C++ executable, and 404
when the same executable links CoreFoundation. A minimal program performing `getpwuid_r` still
has five and loads no frameworks: the account lookup does not itself require CoreFoundation.
XFF's own five initializers are four notice/license-registration translation units and Abseil's
stacktrace setup. The only undefined CoreFoundation imports in the lean XFF executable are the
six functions used by Abseil/CCTZ's macOS local-timezone lookup. The framework dependency is
currently a link-time requirement, even for `--version` and filename-only searches.

### Prioritized implementation work

1. **Resolve civil timezone only when consumed.** `RunFindCore` unconditionally calls
   `ResolveTimeZone`. Add explicit requirement metadata to expression descriptors and compiled
   output/template plans, then resolve the local zone once before workers only when needed.
   Preserve validation of explicit `--timezone` values even when output does not use them.
   Cover `-daystart`, absolute-date comparisons, `-ls`, printf/time fields, summaries with time
   fields, timezone suffixes, configured templates and comparisons. Ordinary elapsed age tests
   do not all require a civil timezone. Public `EvalContext` / `RenderContext` defaults and
   convenience rendering helpers also need review so they cannot accidentally reintroduce eager
   resolution. The measured 0.59 ms resolver is a concrete target; the 0.79 ms process difference
   is indicative, not a promised additive saving.
2. **Remove eager framework loading while preserving macOS timezone semantics.** CCTZ directly
   imports CoreFoundation under `__APPLE__`, and both its link dependency and XFF's explicit
   framework flags retain it. Investigate an upstream-supported lazy platform adapter, resolving
   the same APIs only when local civil time is requested. Simply deleting the link flag leaves
   unresolved symbols, and simply enabling dead dylib stripping cannot remove a used dependency.
   A POSIX `/etc/localtime` implementation needs a semantic audit before substituting it for
   `CFTimeZoneCopyDefault`; keep explicit zones, environment behavior and DST correctness.
   This is the larger cross-cutting startup opportunity, not a constexpr calculation.
3. **Generate immutable core language/MIME catalogs at build/constexpr time.** Source tables are
   already constexpr, but `CoreVocabulary`, `Finalize`, `CatalogTypes` and `Catalog::Compile`
   construct runtime maps, aliases, overlap lists and memberships on first use. Provide an
   immutable precomputed base with runtime overlays for user catalogs and edits. Preserve
   multi-language suffix matches and ambiguous-alias errors. These catalogs are already lazy;
   this helps type/language/MIME requests and relevant color/output paths, not every plain scan.
4. **Derive compact flag indexes from the existing constexpr registry.** `LookupGlobal`,
   `LookupGlobalArgument`, `LookupCompatibilityOption` and expression `Lookup` still scan the
   descriptor arrays. Generate mode-aware name/alias/sign-form indexes and precompute valued-flag
   acceptance and config repetition identities from the same source of truth. Measure global,
   rg, late-table and miss cases before replacing the expression scan: common `-name` is first
   today, so an indexed search need not beat that case. This is a smaller target than framework
   and timezone initialization; the descriptors themselves are already constexpr.
5. **Generate immutable notice/extension registration data.** Four of the lean program's five
   own static initializer functions populate notice/license vectors. Build-selected arrays can
   remove those allocations and constructors, while keeping test registration isolated and full
   extension lists correct. Time this separately; the number of constructors does not establish
   their duration, and full-extension binaries need their own measurements.

The existing field dispatch is already a constexpr `mbo::container::LimitedMap`; regex compilation
already occurs after the final configuration/case selection. Neither should be listed as missing
work. `env::Prewarm` currently calls the locked `Get` once per name, despite the CLI comment saying
one locked pass; the measured prewarm stage is only about 11 us, so fixing that is low priority.
Do not trade away the authoritative OS account lookup or mandatory config handling for startup
numbers. A persistent daemon or changing the fixed config location is a separate product decision.

### Loader experiments and limits of constexpr

The baseline Mach-O has 6,930 rebases, 351 binds and 216 lazy binds, with about 208 KiB of
`__DATA_CONST`. Constant string-view tables still contain addresses the loader must relocate;
constexpr does not make all startup work disappear. Reducing pointer-bearing help/catalog data
could be investigated later using generated offsets into shared text storage, but requires evidence
that this matters before changing the descriptor APIs.

Tested two local link-only variants on the same source and toolchain:

- `-Wl,-fixup_chains`: produced valid chained fixups with the existing macOS 11 deployment target;
  executable size fell from 3,774,288 to 3,765,200 bytes. Across version, empty, UTC-empty and
  10/100/1,000-file cases, median changes ranged from 4.0% faster to 6.6% slower. No consistent win.
- Add `-Wl,-exported_symbol,_main`: reduced reported exports from 651 to two and size to 3,710,800
  bytes. Empty search improved 6.8%, UTC-empty 2.5%, but version regressed 5.0% in that interleaved
  session. No startup-wide win; full-extension/linkage validation is also outstanding.

Neither variant is enabled. The host was substantially slower during those later sessions, which
is visible in the raw absolute timings; comparisons were interleaved within each session. These
are screening results, not release qualification or evidence that a linker option is universally
bad. All temporary timing instrumentation was removed from the source checkout.

Next validation should include current-main lean/full binaries, empty and 10/100/1,000-file cases,
then representative larger workloads with one/three workers. Include config safety and failure
paths, explicit invalid zones, local-zone/DST formatting and feature-specific catalogs. Keep fixed
startup gains separate from per-file throughput and thread scheduling. A crossover at 20,000 or
100,000 files does not, by itself, prove that all of the deficit is startup cost.

### Demand-driven timezone and framework implementation

Implemented and measured on the same M5 Pro, with the same LLVM 23.1.2, `-O2` and native Apple
ThinLTO settings. The combined implementation retains both measured changes below.

The engine now derives civil-time demand from expression registry metadata and compiled time fields.
It resolves the local zone once before traversal when needed; workers receive the resolved value,
with no per-entry lazy state or lock. Explicit `--timezone` values remain validated even when unused.
UTC is only an unconsumed placeholder on paths without a timezone consumer, not a new user default.
The implementation conservatively resolves for every printf action and day-duration matcher, including
those particular invocations that only use paths or fixed durations. Public standalone rendering
context defaults still resolve local time; they do not affect the measured engine fast path.

A small Abseil/CCTZ patch opens the system CoreFoundation framework by absolute path on first local
zone lookup and resolves the same six CoreFoundation functions. It retains the framework for the
process lifetime. `CFTimeZoneCopyDefault`, environment override handling and DST interpretation
remain in the same lookup path. Linux does not compile the Apple adapter. No alternative filesystem
zone-discovery semantics or config-path shortcuts were substituted.

The first loader experiment exposed another dependency: toolchains_llvm adds `-fobjc-link-runtime`,
which tells Clang to link Foundation even though XFF uses no Objective-C API. Clang has no matching
`-fno-objc-link-runtime` flag. Apple's `-Wl,-dead_strip_dylibs` removes this unused dependency.
Both this setting and removal of the explicit CoreFoundation link are needed for this experiment.
The resulting lean executable directly links only libc++ and libSystem. Loader diagnostics still
list delayed framework images, so searching `DYLD_PRINT_LIBRARIES` for a framework name alone is
not proof that it initialized. `DYLD_PRINT_INITIALIZERS` shows 409 calls for the baseline, versus
10 for the candidate's version/ordinary search and 409 when local civil-time output needs it.

The confirmation run interleaved all four variants and all nine cases in shuffled order, discarded
four warmups, then retained 80 samples per cell. There were no concurrent builds. Values are median
whole-process milliseconds, including launch/wait overhead, against warm local APFS fixtures.
`TZ` was unset. Raw samples, hashes, arguments and the measurement program are stored in
[performance-startup.json](performance-startup.json). Do not compare absolute values across sessions.

| Case                        | Baseline ms | Demand only ms | Framework only ms | Combined ms | Combined elapsed reduction |
| :-------------------------- | ----------: | -------------: | ----------------: | ----------: | -------------------------: |
| Version                     |       6.036 |          6.093 |             4.503 |       4.525 |                     +25.0% |
| Empty search                |       8.767 |          7.455 |             8.897 |       5.862 |                     +33.1% |
| Empty search, explicit UTC  |       7.572 |          7.489 |             5.847 |       5.988 |                     +20.9% |
| 10 files                    |       8.247 |          7.439 |             8.795 |       6.007 |                     +27.2% |
| 100 files                   |       8.457 |          7.659 |             8.605 |       6.040 |                     +28.6% |
| 1,000 files                 |       9.128 |          8.403 |             9.620 |       6.837 |                     +25.1% |
| Content match, 1,000 files  |      19.025 |         18.793 |            19.861 |      16.818 |                     +11.6% |
| Local time fields, 10 files |       8.390 |          8.738 |             8.838 |       9.300 |                     -10.8% |
| UTC time fields, 10 files   |       8.003 |          7.610 |             6.083 |       6.188 |                     +22.7% |

The first independent 60-sample session found 25-30% reductions for version/ordinary searches,
11.5% for the content case, and a 3.3% regression for local time fields. The confirmation found
25-33%, 11.6%, and a 10.8% regression respectively. Local-time users still need initialization;
dynamic framework loading moves it later and can add cost. Framework-only is insufficient for
ordinary searches while the engine still asks for local time unconditionally. Retain the two-part
implementation together if adopting it, and weigh the local-time regression explicitly.

The lean executable changed from 3,774,288 to 3,774,080 bytes before stripping (208 bytes smaller);
there is no material binary-size penalty in this experiment. These local startup results do not
establish Linux gains, large-tree throughput, or equivalent gains in every extension build.

Six targeted suites pass locally: engine run/evaluation, datetime, fields, registry, and the new
CLI timezone suite. The new checks exercise winter/summer New York offsets, calendar comparisons,
time fields in columns/summaries/INI configuration, and whether macOS actually initializes the
framework. Existing engine tests also cover explicit invalid zones and `-daystart`. Full CI,
Linux validation, and upstream review of the dependency patch remain separate qualification work.

The full extension executable also builds and passes the release smoke checks, as does the lean
candidate. The full binary is 7,801,992 bytes before stripping (no matching full baseline was
measured), directly links only libc++/libSystem, and has 40 initializer calls for `--version`,
with no CoreFoundation initializer. Formatting/policy hooks passed at the measurement stage;
clang-tidy and CI validation are tracked separately in the pull request.

## Registered mode parsing and lookup experiments (PR #956)

The command parser now has one global-argument pass with immediate mode switches and one
expression-building pass. It consumes declared operands before checking another flag, retains
resolved descriptors and borrows token text until constructing the owned expression. Context
directions and boolean operators have typed effects in their constexpr registrations. Native,
find and rg lookups use separate generated sorted indexes; no runtime catalog scan or speculative
grammar retry is needed. This is an interim representation behind the lookup API, suitable for
replacement by the planned constexpr perfect-hash map or trie.

A local macOS ARM64 pilot used optimized Clang builds and five repetitions of at least 0.1 seconds
per case; the table reports median CPU time. The unchanged old executable was rerun after the
experiments to check drift. These parser timings exclude process startup, filesystem work, config
loading and per-file evaluation. They do not explain or dismiss CI's whole-program advisories.

| Native name predicates | Previous parser (us) | Registered passes (us) | Change |
| ---------------------: | -------------------: | ---------------------: | -----: |
|                      1 |                1.197 |                  0.212 | -82.3% |
|                      4 |                1.588 |                  0.848 | -46.6% |
|                     16 |                2.922 |                  3.163 |  +8.2% |
|                     64 |               10.754 |                 14.575 | +35.5% |
|                    256 |               46.991 |                 62.442 | +32.9% |

Common short commands improve; long repeated expressions still cost more because the global
pass now resolves every operator and operand boundary. Reusing descriptors and borrowing token
text reduces that overhead but does not remove it. The next lookup implementation must retain
these long-expression cases alongside startup cases, rather than optimizing only a tiny command.

The reproducible `//xff/cli:globals_benchmark` compares identical explicitly registered mode
vocabularies, with equal numbers of successful lookups and unknown spellings formed by appending
`-unknown`. It validates all implementations against production lookup before timing. Table
construction is excluded; each algorithm has a separate mode-specific table. Results below are
median nanoseconds per lookup. The flat-hash prototype stores precomputed hashes with linear
probing; it is not yet the proposed collision-free constexpr map. The sparse trie uses sibling
edges; the direct automaton uses 128 character transitions per node and therefore more memory.

| Lookup representation        | XFF (ns) | rg (ns) |
| ---------------------------- | -------: | ------: |
| Original linear catalog scan |   170.79 |  182.99 |
| Generated sorted index       |    33.44 |   33.77 |
| Flat hash prototype          |    16.41 |   15.55 |
| Sparse trie                  |    76.12 |   72.71 |
| Direct character automaton   |    23.60 |   22.99 |

Both hash lookup and direct transitions beat the interim sorted index in this warm-cache pilot.
A sparse trie does not. No theoretical complexity claim substitutes for measuring the layout.
Repeat with realistic flag frequencies, cold caches, expression vocabularies and Linux before
choosing the shared library implementation. Measure table/binary size as well as lookup latency.
HAMT persistence and structural sharing are not needed for an immutable flag vocabulary;
string interning adds little when names already have static storage, unless it performs the
name-to-descriptor mapping directly. Unknown inputs must still be rejected by an exact spelling
check, even if hashes of all registered keys are collision-free.

## Indexed-worker cursor layout qualification (EP03)

Run `37209991588` completed 333 correctness-checked cases on each native platform. Each family
below summarizes six kernel cases using the geometric mean of per-case fastest-seven means
from nine interleaved repetitions. A ratio below one favors the numerator. These are isolated
kernels, not whole-command gains.

| Comparison                               | Linux x86-64 | macOS ARM64 |
| :--------------------------------------- | -----------: | ----------: |
| Indexed / tree-map worker: path regex    |        0.856 |       0.582 |
| Indexed / tree-map worker: content regex |        0.881 |       0.659 |
| Prepared / bound: type                   |        1.146 |       0.722 |
| Prepared / bound: size                   |        1.074 |       0.683 |
| Prepared / bound: ordinary content       |        1.505 |       1.095 |
| Prepared / bound: name                   |        1.289 |       1.041 |

The same-session prepared/bound regression on Linux prevents adopting this layout. The preceding
scalar-only run `37209923516` instead measured type/size ratios of 0.791/0.744 on Linux and
0.661/0.629 on macOS, with ordinary content 1.061/1.009. This is a diagnostic comparison between
runs; the within-run paired controls above are the decision evidence.

Adding a matcher span enlarged the recursively copied cursor from two references to two references
plus a two-word span. The revised candidate stores that span in a shared evaluation environment,
restoring the small cursor. Worker environments are bound once after matcher storage is finalized;
serial evaluation creates one stack environment per expression. Workers still own independent
matcher state, and moving the worker preserves its backing storage. CI must measure the revised
layout before attributing the regression to argument passing or claiming a recovery. Existing
move, persistent-worker and complete evaluator oracle tests cover the lifetime change. No local
build or benchmark campaign was run.

## Iterative score and replay qualification (EP05)

The next candidate removes recursive fallback from score-consuming and deferred program contexts.
It executes dense prepared nodes with reusable continuation frames while retaining sparse memo
identities for exactly-once replay. Ordinary boolean programs keep their compact jump executor.
Stateful scratch allocates once per worker on first use, stays bounded by node count, and is
included in reported worker storage after warmup; no frame vector belongs to a suspended entry.

Full evaluator fixtures and the isolated oracle run through both paths. Dedicated tests cover
all operator score combinations, nested scratch reuse, error cleanup, two replay frontiers and
restoration of caller-owned score/incoming state. The benchmark adds scored fuzzy AND and OR to
all five core variants (756 total cases), checks scores against the tree before timing, and
reports stateful selection and zero recursive fallback. Measurements and full CI are pending;
this work makes no production selection or performance claim. The first-use scratch allocation
is excluded from warm execution and must be included in the later setup/whole-run decision.

### Retried benchmark shard identity

Run 37219669899 first measured macOS shards on hosts reporting 3, 3 and 5 CPUs. Strict
contract validation correctly refused to combine them. A focused retry produced a replacement
three-CPU shard with the same source and tool identities, but both artifacts remained under the
same name; wildcard downloading could still select the old report. PR aggregation now selects
the newest creation timestamp for each exact shard name before downloading by artifact ID.
All raw attempts remain stored and CPU/tool/revision compatibility checks remain mandatory.
Artifact IDs alone are not chronological: the replacement had a lower ID than its predecessor.
The download action also deduplicates by highest ID before applying explicit ID selection. The
workflow therefore downloads selected ZIPs directly through the REST API, verifies their SHA-256
digests, and reads only `benchmark-shard.json`; it does not extract arbitrary archive paths.
PR and post-merge benchmark aggregation use the same local download action so retry handling
cannot diverge between the two workflows. Both aggregate jobs request only read access to artifacts.

## Native cursor and program comparison results

The revised small cursor was measured in native run `37212464515` (333 valid cases/platform).
Run `37212554009` measured the compact programs (666 valid cases/platform), and run
`37212730981` measured scored iterative execution (756 valid cases/platform). These measurements
precede the test-only restoration-assertion repair; they do not certify later heads. Each value
is the geometric mean of six same-session case ratios, using fastest-seven means from nine
interleaved repetitions. Ratios below one favor the first executor. No local timing was run.

| Run         | Comparison                                | Linux x86-64 | macOS ARM64 |
| ----------- | ----------------------------------------- | -----------: | ----------: |
| 37212464515 | Prepared / bound: type                    |        0.709 |       0.648 |
| 37212464515 | Prepared / bound: size                    |        0.700 |       0.619 |
| 37212464515 | Prepared / bound: ordinary content        |        1.032 |       1.007 |
| 37212464515 | Prepared / bound: name                    |        1.020 |       0.934 |
| 37212464515 | Indexed / tree-map worker: path regex     |        0.685 |       0.575 |
| 37212464515 | Indexed / tree-map worker: content regex  |        0.706 |       0.611 |
| 37212554009 | Program functions / bound: type           |        0.656 |       0.641 |
| 37212554009 | Program functions / bound: size           |        0.559 |       0.667 |
| 37212554009 | Program functions / bound: early AND miss |        0.419 |       0.554 |
| 37212554009 | Program functions / bound: early OR hit   |        0.403 |       0.569 |
| 37212730981 | Stateful program / bound: scored AND      |        1.163 |       1.161 |
| 37212730981 | Stateful program / bound: scored OR       |        1.120 |       1.101 |

The smaller cursor recovers the substantial Linux regression seen with the larger recursively
copied cursor. Typed scalar preparation and indexed matchers retain useful isolated gains.
Compact branching improves these aggregate boolean kernels, but a one-predicate case can still
lose, and layout/session variation remains visible. The iterative scored path costs about
10-16% more than bound recursion in both platforms. Do not adopt it unchanged solely because
it removes recursion. Its explicit state and replay correctness remain useful for qualification.

## Whole-engine executor qualification

`RunFind` now has an internal executor selector for tests and benchmarks. CLI and INI cannot
select it. The ordinary tree remains the default; changing production selection requires the
EP07 decision. Candidate preparation follows the original command's validation, safety,
implicit-output and traversal decisions. The coordinator, parallel predicate subtree and any
coordinator-owned trailing output each retain immutable prepared storage. Each actual matcher
thread creates and reuses its own worker. Small batches use a separate coordinator worker.
Deferred entries retain the original sparse memo, not the worker's dense continuation frames.

The integration oracle compares exact output, errors, truth, metadata/content read counts and
mutation attempts against the original whole-run path. It covers implicit output, summaries,
comparison, pruning/quit, counters, multi-frontier replay, failures, blocked deletion, dry-run,
native match output and native filters followed by rg semantics. These are isolated VFS fixtures;
no fixture executes commands or permits a host mutation. Existing worker eligibility and ordered
output remain unchanged.

`//xff/engine:expression_run_benchmark` measures 250 combinations: five executors, five workloads,
0/1/10/1,000/10,000 files and 1/3 workers. Parsing and fixture construction are outside timing;
every iteration includes preflight, preparation, worker startup, traversal, effects/rendering and
teardown. Outputs are checked against the reference before timing. Native CI publishes raw nine
interleaved rounds and fastest-seven summaries alongside the existing kernel artifacts. Zero and
one-file cases expose fixed setup cost; they are not excluded from the adoption decision.

The qualification selector currently links every candidate into the CLI library, and separately
prepares serial and parallel subtrees. This makes preparation and code-size overhead visible;
final selection must remove losing production paths and avoid redundant preparation. Process
startup, binary/RSS accounting, archive/configuration composition coverage, remaining operand
families and optimizer passes still require their separate acceptance evidence. No whole-command
speedup is claimed before those results exist.

### Whole-engine expression qualification, first native session

CI run `37216302909`, PR #966 head `43fe90d250`, produced 250 valid in-memory engine cases
on both Linux x86-64 and macOS ARM64. Each case retains all nine randomized/interleaved raw rounds
and reports the fastest-seven mean. There were no correctness-check failures. The fixture covers
0/1/10/1,000/10,000 files and requested workers 1/3; it times complete `RunFind` preparation,
traversal, evaluation, output and teardown, excluding command parsing and process startup.
Artifacts `expression-baseline-{linux,macos}` contain `expression-engine-{linux,macos}.json`.
These are initial session results, not a production decision or a whole-process speedup claim.

The table gives candidate/tree elapsed-time ratios at 10,000 files (smaller is better):

| Workload      | Linux bound, 1 worker | Linux prepared, 1 worker | Linux prepared, 3 workers | macOS prepared, 1 worker | macOS prepared, 3 workers |
| ------------- | --------------------: | -----------------------: | ------------------------: | -----------------------: | ------------------------: |
| Name          |                 0.868 |                    0.875 |                     0.860 |                    0.912 |                     0.935 |
| Scalar chain  |                 0.758 |                    0.652 |                     0.660 |                    0.768 |                     0.692 |
| Regex output  |                 0.792 |                    0.771 |                     0.889 |                    0.751 |                     1.192 |
| Summary       |                 0.827 |                    0.793 |                     0.837 |                    0.714 |                     1.087 |
| Scored replay |                 0.928 |                    0.907 |                     0.921 |                    0.892 |                     0.932 |

Linux small-run results expose preparation overhead: eager indexed regex forks add roughly
4-13 microseconds to empty/single-file cases. Do not turn this into an unconditional default
without reducing or amortizing preparation. The iterative program's scored-replay path is
12-18 percent slower than the tree at 1,000/10,000 files on Linux; recursive bound/prepared
execution remains a measured alternative for this semantic family.

macOS exhibits much wider sample variation, including apparently faster empty runs for candidates
that perform strictly more preparation. Its pooled content/summary measurements also disagree with
Linux. Repeat native sessions and inspect spread before attributing these differences to a specific
executor or making a platform-specific selection. The next EP06 matrix measures each boolean
rewrite independently and their combined whole-engine candidate; it does not enable production use.

### EP06 native session and repeated whole-engine evidence

Run `37220141643`, PR head `cc1589eec0`, retained 1,494 valid kernel/preparation cases and 300
valid whole-engine cases on each native platform in `expression-baseline-{linux,macos}`. The JSON
records GitHub's tested merge revision `52c2614124b75331eb8e7b90d091328cdef4da1e`. Nine interleaved
rounds and fastest-seven means use the same protocol as the earlier session. Untimed benchmark correctness checks passed;
the separate coverage suite found an incorrect expected error in the new unreachable-deletion
test, including for the original tree, so these measurements do not establish merge readiness.
Remaining checks were still running when this evidence was recorded.

At 10,000 files with one requested worker, elapsed-time ratios to the same-session tree were:

| Workload      | Bound | Prepared | Program switch | Program functions | Optimized program |
| ------------- | ----: | -------: | -------------: | ----------------: | ----------------: |
| Name          | 0.854 |    0.864 |          0.901 |             0.961 |             0.965 |
| Scalar chain  | 0.759 |    0.645 |          0.650 |             0.657 |             0.650 |
| Regex output  | 0.779 |    0.799 |          0.813 |             0.818 |             0.821 |
| Summary       | 0.831 |    0.848 |          1.059 |             1.063 |             1.062 |
| Scored replay | 0.933 |    0.944 |          1.195 |             1.196 |             1.190 |

This repeats the earlier Linux evidence favoring bound/prepared execution, particularly prepared
scalars, and rejecting an unconditional iterative-program default for scored/replay contexts.
Small-run setup remains material: empty regex searches were 23.6/23.8 microseconds for tree
at requested workers 1/3, and prepared ratios were 1.179/1.376. On-demand worker initialization
is being evaluated separately rather than assuming a large-file win excuses this setup cost.

Against the unoptimized function-dispatch program, geometric means of six corresponding kernel
case ratios show jump threading at 0.502 for early AND misses and 0.488 for early OR hits.
Constant folding measures 0.108 for literal-true chains and 0.358 for literal-false chains.
These are targeted control-flow wins, not general file-search speedups. Branch fusion alone is
between 0.962 and 1.010 for the listed type, size, short-circuit and constant families; it has
not established a repeatable general benefit. Neither the combined passes nor the program
representation has displaced recursive prepared execution as the whole-engine candidate to beat.
Keep per-pass selection and the simpler executors; the macOS observations below provide the
same-run cross-platform check.

The macOS one-worker ratios at 10,000 files were:

| Workload      | Bound | Prepared | Program switch | Program functions | Optimized program |
| ------------- | ----: | -------: | -------------: | ----------------: | ----------------: |
| Name          | 0.825 |    0.889 |          0.759 |             0.825 |             0.868 |
| Scalar chain  | 0.717 |    0.709 |          0.711 |             0.716 |             0.618 |
| Regex output  | 0.773 |    0.758 |          0.748 |             0.738 |             0.727 |
| Summary       | 0.919 |    0.848 |          0.901 |             0.950 |             0.913 |
| Scored replay | 1.002 |    0.889 |          1.029 |             1.058 |             1.110 |

Mac timing spread remains a constraint on small differences: the nine tree/name/10,000-file
rounds have sample coefficient of variation 9.1% with one worker and 13.4% with three, versus
0.5% and 1.2% on Linux. The combined optimizer improves macOS type/size kernels to 0.702/0.671
of the unoptimized function program, while jump threading improves early AND/OR to 0.225/0.236.
These larger targeted gains merit retaining the candidate. The variable whole-engine results
do not justify a universal program default; scalar preparation and bound dispatch remain the
more consistently useful choices across the two sessions/platforms.

### CI binary-size accounting for the expression stack

Compare the saved `benchmark-head` executables from main run `37205106353` (main `0ac0e62aa0`)
and PR run `37220141643` (tested merge `52c2614124`). Both build `//xff/cli:xff` using
`--config=clang_release`, which retains symbols. Strip copies of both downloaded artifacts with
the same cached `llvm-strip --strip-all`; this requires no local compilation or execution of the
benchmarked binaries. Byte counts are file sizes, not Mach-O virtual segment totals.

| Platform     | Main artifact | Candidate artifact | Main stripped | Candidate stripped | Stripped increase | Increase |
| ------------ | ------------: | -----------------: | ------------: | -----------------: | ----------------: | -------: |
| Linux x86-64 |    11,705,976 |         11,975,456 |     3,437,480 |          3,483,208 |            45,728 |    1.33% |
| macOS ARM64  |     3,849,200 |          3,964,208 |     3,086,016 |          3,152,688 |            66,672 |    2.16% |

The qualification stack exceeds the plan's 1% size-review budget on both platforms; macOS also
exceeds 64 KiB. It still includes multiple candidate executors and registry preparation callbacks
while production uses the original tree. Remove losing production paths or explicitly justify
their retained diagnostic cost before final adoption. These are the benchmark CLI target's sizes,
not a measurement of every separately packaged lean/full release variant. Raw artifacts remain
associated with the named CI runs; final shipping-size accounting remains part of EP07.

### Candidate response: initialize only reached worker matchers

The initial whole-engine session measured roughly 4-13 microseconds of eager regex-fork setup
in empty/single-file searches. The next candidate allocates the indexed slots once, but forks
each backend only when its regex predicate is first reached. Initialization is private to a
non-concurrently used worker, with no locks. Absent and failed forks are remembered rather than
retried; the validated immutable matcher remains the fallback. Pattern compilation/validation
and all conditional content/metadata reads retain their earlier boundaries.

Retain an eager policy for same-session comparison. The complete-engine matrix grows to 420
cases: seven executors, six workloads (including an entirely skipped regex), five file counts
and two requested worker counts. Kernel comparisons include eager/on-demand worker preparation
and warm matching, initialized slot counts and owned storage bytes. Both policies share the new
slot bookkeeping; comparisons against the unchanged tree and previous candidate sessions must
also check total hot-loop overhead. The initialization flag can increase aligned slot size.

This is a response to measured setup cost, not a measured improvement yet. Native Linux/macOS
CI must establish empty, first-match and large-search costs before production adoption. No
local C++ compilation or timing campaign is required, and the production executor remains the tree.

### Numeric age preparation candidate and remaining operand audit

Numeric ages still decoded the comparison, decimal count and optional BSD suffix on every reached
entry. The candidate binds those operations through the existing dispatch factory for `-used` and
the modification/access/change/birth age families. Signed counts and the seconds-per-unit record
fit within the existing operand size/alignment; they add no text allocation or shared mutable state.
The clock and timestamps remain dynamic, and unsupported birth-time observations retain their order.

The audit also found unchecked signed decimal accumulation in both `MatchesSignedNumeric` and
`MatchesTime`. The shared checked decoder rejects out-of-range counts rather than invoking signed
overflow. This is a reference-path correctness change as well as prepared-path work, so native
comparisons must show any baseline-cost change rather than attributing it to the executor.
The four new kernel families are `age-days`, `age-suffix`, `age-minutes` and `used-days`; the full
engine adds an age/scalar/name chain at 0/1/10/1,000/10,000 files and requested workers 1/3.
No local compilation or timing campaign is used; native CI must establish whether to retain it.

Remaining invariant work has distinct boundaries. MIME pattern lowercasing is a possible separate
text-pool candidate; embedding an owning string in every operand could enlarge unrelated scalar
slots. Word/calendar durations depend on the final evaluation clock/timezone and currently keep
that interpretation dynamic. Reference-file metadata and account databases remain observable and
conditional; this work does not silently cache them. Existing regex and output templates are
already compiled by their owners and should be reused, not compiled again in the executor.

Downloaded binary symbols from run `37220141643` confirm that `ExpressionProgram::Prepare`, its
worker evaluator, `BoundExpression` and `PreparedExpression` remain linked into the benchmark CLI
on both platforms despite the tree default; the main artifacts have none of these symbols. This
supports separating qualification-only alternatives from the production dependency path before
final size review. It is binary inspection, not a new local build or timing result.

### Removing qualification candidates from production linkage

The downloaded native CLI artifacts described above retained every experimental executor despite
using the original tree. Whole-run engine preparation now accepts an optional function factory;
only test-only qualification code maps the candidate enum to a factory. Bazel marks the iterative
program and the selection adapter `testonly`, making an accidental CLI dependency an analysis error.
The immutable-plan/worker interface remains available to the engine for the eventual measured winner.
The reference and default still take the original direct tree path. All seven candidate integration
cases, error/move checks and native whole-engine benchmarks continue through the same engine seam.

Qualification adds one virtual worker-entry call per evaluated entry, without changing recursive
or instruction dispatch inside the candidate. CI must quantify that cost and verify the expected
link-size reduction; neither a size saving nor a performance result is claimed from source inspection.
This is dependency isolation, not a decision to enable or abandon any candidate. Native stripped
artifacts and symbol inspection remain the evidence for the final production-size decision.
