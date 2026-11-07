# Lazy inline content snapshot state (2026-11-07)

## Conclusion

The implementation reduces work in the production binary, with modest elapsed gains and a small
repeated counterexample. Keep it as a separately reviewable candidate on
`benchmark/lazy-content-state-20261107`; the benchmark tooling branch does not adopt it. Broad
100,000-file listing is about 1% faster in both populations, while broad metadata/date output is
about 0.5% slower in both. The result supports applying lazy inline state selectively, with explicit
size and active-use measurements, rather than replacing owning pointers throughout the codebase.

## Change and scope

Candidate `90abd2b3ef63b35833e8f9d96745475c6b9f182a` moves the per-entry content snapshot's
bytes, digest map and line-count cache into inline `std::optional<State>` storage. The state is
constructed only when a content consumer reaches it; invalidation destroys it. The owning
snapshot still reserves space for the state. Public read/cache/transfer behavior is unchanged.

The explicit nonthrowing constructor initializes the optional to `std::nullopt`. This avoids
whole-snapshot zero-initialization when the snapshot is value-initialized inside its owner.
In a macOS ARM64 LLVM 23 construction probe, the original 88-byte snapshot requires several
stores covering its storage; the 96-byte candidate requires two byte stores. This probe alone
does not establish how the compiler initializes a complete evaluation context.

The original empty map allocated nothing. This change reduces construction, destruction and
movement of unused cache state; it does not remove a per-entry map allocation. Existing lazy
storage in the walker, render buffers and parallel content results already uses similar patterns.
Replacing every owning pointer with inline storage would also change object sizes and lifetimes
and is not justified by this experiment.

## Production comparison

Baseline is PR #1130, `ae8c5af3eb11dc8571f11db9b307b0db948f78c6`. The experiment follows the
completed 130-revision extended benchmark campaign on drogon's Linux KVM guest. It uses frozen
full production binaries built with `--config=clang_release --config=xff_full`, including ThinLTO.
All compilation and baseline CPU sampling finished before the full candidate timing population
began. Instruction counting ran between that population and the separate targeted repeat.

The five extended tasks, broad/deep fixtures and one/three allowed CPUs provide 60 cases. Each
case measures the candidate and an identical-baseline control in separate blocks; every block
interleaves its XFF binary, the fixed baseline and the traditional tool. There is one discarded
warm-up round and 27 measured rounds, retaining the fastest 21. Every invocation validates exact
output against an independent fixture oracle. Fixtures are on tmpfs. All 120 cells and 10,080
invocations completed successfully. This population is separate from the historical campaign.

The configuration guard initially stopped the experiment because an ignored, generated
`MODULE.bazel.lock` hash differed. Its only difference was the Aspect telemetry extension cache;
all tracked configuration inputs matched. Before allowing measurements, the baseline was rebuilt
in the candidate environment. Its executable was byte-identical to the retained baseline, and its
recorded configuration matched the candidate. Both lock files and the reproduction receipt are
retained in `lazy-content/configuration/` and `configuration-reconciliation.json`.

## Elapsed results

The table gives equally weighted geometric means over both shapes and CPU allowances. Negative
change means the candidate is faster. These are small effects: the 60 identical-baseline control
cells have a 95th percentile multiplicative distance from parity of 1.0206. Within-cell paired
bootstrap intervals exclude parity in the faster direction for 19 cases and in the slower direction
for two; they are not corrected for multiple comparisons and do not establish independent-host
reproducibility. All cells and intervals are retained in the [result data](lazy-content-state-20261107.json).

| Task                       | Files or members | Elapsed change | Total CPU change | Peak RSS change |
| :------------------------- | ---------------: | -------------: | ---------------: | --------------: |
| Exact file listing         |          100,000 |         -0.67% |           -0.74% |          +0.84% |
| Native-case name filtering |          100,000 |         -0.84% |           -0.92% |          +0.85% |
| Metadata and UTC date      |          100,000 |         +0.19% |           +0.20% |          +0.65% |
| Content regex              |          100,000 |         -0.24% |           -0.16% |          +0.67% |
| TAR member filtering       |           10,000 |         -0.43% |           -0.33% |          +0.98% |

At 10,000 files, listing and native-case filtering improve by 1.33% and 1.53% respectively.
Several 100-file/member cases improve by about 1-2%. Individual counterexamples remain visible:
deep 10,000-file listing with three CPUs is 2.31% slower, and broad 100,000-file metadata output
with one CPU is 0.49% slower in this population. A separate targeted repeat covers both, plus six
predeclared listing, name, content, archive and size controls.

That targeted repeat completed all 16 candidate/control cells and 1,344 validated invocations.
The deep-listing regression did not repeat: its candidate/baseline ratio was 0.9966, with a paired
interval spanning parity. The metadata counterexample did repeat at 1.0050. Broad 100,000-file
listing was 0.9895 (about 1.05% faster), compared with 0.9904 in the original population; native
name filtering was 0.9901. Broad content regex was 0.9955. The small-case intervals still span
parity. These selected follow-up cases are retained separately and are not substitutes for the
full population or proof of a universal improvement.

## Instruction counts

After timing collection, `perf stat` counted user-space instructions in seven runs of each retained
binary per selected case. These diagnostic invocations write to `/dev/null` and are excluded from
timing results. Output is checked independently before counting. The counters show reduced work
even where elapsed differences are small or inconclusive.

| Case, one allowed CPU              | Baseline instructions | Candidate instructions | Change |
| :--------------------------------- | --------------------: | ---------------------: | -----: |
| Broad listing, 100 files           |             1,911,425 |              1,904,808 | -0.35% |
| Broad listing, 10,000 files        |            25,487,490 |             24,827,442 | -2.59% |
| Broad listing, 100,000 files       |           239,807,162 |            233,207,020 | -2.75% |
| Deep listing, 100,000 files        |           252,332,090 |            245,730,529 | -2.62% |
| Broad metadata/date, 100,000 files |           594,265,420 |            589,065,424 | -0.88% |
| Broad regex, 100,000 files         |           737,765,795 |            735,366,924 | -0.33% |
| Broad TAR, 10,000 members          |           148,481,524 |            147,952,428 | -0.36% |

Broad listing saves approximately 66 user-space instructions per file. This is direct evidence of
reduced work in the production binary, although it does not imply a 2.75% elapsed improvement.
The remaining evaluation, traversal, formatting and kernel work is unchanged by this patch.

## Validation and retained evidence

- Five focused test targets pass on macOS and Linux: snapshot, evaluator, parallel matching,
  whole-run behavior and hash patterns. The macOS run contains 1,006 passing test cases.
- New snapshot tests cover unengaged copies/moves, repeated invalidation, replacement of active
  and inactive caches, and empty derived-value transfers. Existing tests cover cached errors,
  stable digest views, binary content and requested derived-cache transfers.
- Independent raw-sample replay checks all 1,440 exported values and 480 unavailable positions
  for the candidate/control population; maximum arithmetic difference is 1.12e-16.
  The targeted repeat adds 192 checked values and 1,248 genuinely unavailable positions.
- Source bundles, production executables, build/link evidence, raw rounds, host observations,
  configuration reconciliation, construction assembly and instruction counters are retained under
  `/home/marcus/Documents/xff-chart-campaign-20261107/lazy-content` on drogon. A verified local
  copy lives under the original checkout's `bazel-extended-history-20261107/lazy-content` directory.
- The historical measurement population and its production binaries remain unchanged. macOS
  whole-command speed has not been measured for this candidate. The timing suite exercises active
  cache reads through regex matching, but does not qualify hash-heavy, similarity or diff workloads.
