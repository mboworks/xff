# Expression execution qualification audit

Audit baseline: #973 head `4b2eef55c84adcba223fc8c90267a9961a960391`, including #960-972.
This is an evidence inventory for [EP01-EP07](design-expression-program.md), not a declaration
that the complete plan is finished. It records the original requirements even where the current
implementation or measurements cover less. Runtime semantics and the measured production choice
can be ready to merge while further qualification remains open.

## Verified production candidate

#971 head `4dd9aa724fa4a71b370fc2d051b852c37879faa3` passed every required check in
[run 37235761322](https://github.com/mboworks/xff/actions/runs/37235761322), including both
native platforms, coverage, GCC, sanitizers, clang-tidy and the aggregate `done` check.
The production factory selects recursive prepared execution. Qualification-only iterative programs
and optimizers remain outside shipping dependencies. Independent Linux/macOS measurements support
large-search gains; small-search preparation costs and the lean macOS size increase remain explicit
tradeoffs in [the performance analysis](performance-analysis.md).

## Requirements and evidence

| Requirement                                                      | Inspected evidence                                                                                                                 | What remains                                                                                                                                                      |
| :--------------------------------------------------------------- | :--------------------------------------------------------------------------------------------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Parse once; bind handlers outside entry evaluation               | `parser::Parse`, `PrepareExpressionExecution`, constexpr engine dispatch table                                                     | No repeated spelling lookup is needed in prepared dispatch. Do not confuse this with eliminating dynamic work in every callback.                                  |
| Operand/effect inventory and conservative barriers               | Design inventory; `EveryRegisteredPrimaryRetainsAnOptimizationBarrier`; `EveryRegisteredPrimaryHasAnExplicitBindingDisposition`    | Audit remaining template/hash/diff invariants and preserve observation timing.                                                                                    |
| Stable source identities and ownership                           | Contract move tests; `MovingPlanAndWorkerPreservesBorrowedStorage`; production coordinator/worker capture test                     | Keep source ownership and sparse replay identities stable in subsequent preparation work.                                                                         |
| Boolean, scored, unknown and deferred semantics                  | Operator truth tables; seeded/exhaustive program tests; two-frontier replay; metadata/deletion failures; dry-run unknown results   | These targeted tests do not prove every integration combination in the plan.                                                                                      |
| Whole-run configuration and virtual sources                      | Layered INI/CLI/mode test; mandatory deletion blocks; mounted member output/prune/quit/replay; serial/pooled ordered output        | Explicitly map remaining mutation/read invalidation, confirmation/batched execution, cancellation, symlink/unknown-type/birth-time combinations to coverage.      |
| Prepared numeric/type/permission/time operands and matcher slots | `PreparedOperandTest` boundary tests; original/indexed/lazy/coordinator benchmark variants                                         | Context-dependent account/reference/calendar observations stay dynamic. Finish invariant-family audit without moving their reads.                                 |
| Contiguous switch/function programs and optimizer passes         | `expression_program_test`; separate switch/function/constants/jumps/fusion/combined benchmarks                                     | Record a final per-pass retain/reject table. Keeping a slower candidate test-only is a valid measured rejection, not a production requirement.                    |
| Preparation and execution separated                              | `expression_benchmark` separately times preparation and hot execution; whole-engine benchmark excludes parser/fixture construction | Whole configuration application is not isolated as a timed phase. Measure remaining long-expression startup cases and crossover.                                  |
| Expression lengths and selectivity                               | Kernel registers 1/16/64 predicates; all-hit and early-miss AND/OR cases; correctness tests include 4/256 and seeded trees         | Benchmark cheap 4/256-predicate cases, balanced/nested control flow, and 1/50/99% selectivity with clustered/interleaved ordering. Tests are not timing evidence. |
| File/CPU scaling and work families                               | Whole engine covers 0/1/10/1k/10k entries and requested 1/3 workers; published cross-tool grid supplies broader file counts        | Extended in-memory stress through 100k and actual worker activation counts are missing. Requested worker counts do not establish concurrency.                     |
| Nine-round native repeat qualification                           | Retained raw Linux/macOS rounds; fastest-seven, median and spread; repeated production and first/MIME sessions                     | Several tiny cases remain noisy. Do not claim operand-specific improvement from the common prepared/tree gain.                                                    |
| No steady dispatch allocations; memory bounded                   | Owned-record capacities; sparse replay storage; ASan phase diagnostic passed 162 cases and calibration                             | Native allocation attribution, suspended-entry scaling, all-thread/peak-memory accounting remain open. ASan counts do not close these requirements.               |
| Shipping size and dependencies                                   | Downloaded staged lean/full Linux/macOS artifacts; qualification targets are `testonly`; symbol inspection                         | Lean macOS grows 49,968 bytes / 1.587%, above the review budget. Keep that tradeoff explicit; account for later additions separately.                             |
| Explain/help and user behavior                                   | Production preparation records in `--explain`; generated help snapshot; actual CLI suites                                          | Keep the final decision and remaining dynamic callbacks documented. No public executor switch is introduced.                                                      |
| Durable data and decision publication                            | CI uploads `expression-baseline-*` JSON with 30-day retention; compact result tables in performance analysis                       | Current Pages publisher downloads `benchmark-report*`, not expression artifacts. Add durable raw-data retention and a rendered decision view with provenance.     |
| Historical baseline repair                                       | #974 locally reconstructs run 37196735470's baseline from retained ancestor reports without changing raw data                      | Merge, publish and verify both served platform pages. A local HTML preview is not the published repair.                                                           |
| Worker/file progress prefix                                      | Merged #959 implementation and tests                                                                                               | No additional work identified for the requested `Workers: ... Files: ... Run: ...` ordering.                                                                      |

The complete plan's broader correctness list remains authoritative. A row citing a representative
suite does not imply that every listed interaction is covered. For example, archive replay plus
safe deletion is not equivalent to testing a successful mutation followed by another read, and
worker reuse is not a substitute for cancellation during an active batch.

## Next acceptance work

1. Complete the callback audit for peer/expected-hash templates, hash specification, diff style,
   algorithm and ignore-regex configuration. Compile invariant syntax once; render fields, observe
   files and report conditional errors only when reached. Add direct and whole-run differential
   cases and preparation/kernel measurements before adopting each family.
2. Expand the named cheap-expression matrix instead of multiplying every dimension. Add missing
   lengths, branch shapes/selectivities and extended stress as bounded case groups. Record actual
   reached predicates and actual worker creation separately from requests.
3. Qualify native allocation and suspended-memory behavior with a separate diagnostic build that
   preserves the native allocator. State exactly which allocation routes it observes; retain any
   unobserved C/third-party allocations as a limitation. No diagnostic instrumentation belongs in
   release timing measurements. ASan remains a useful, separate regression test.
4. Preserve raw kernel/engine results beyond expiring Actions artifacts, with tested revision,
   case contract, platform, compiler/LTO identity and sampling metadata. Render absolute values,
   paired ratios, variation, storage and the per-pass decision. Do not manufacture external-tool
   normalization for in-process kernels.
5. Finish the test-to-requirement mapping, per-family/per-pass decisions and startup crossover
   analysis; then update the EP01-EP07 checkboxes from evidence. Verify merged main CI and the
   historical published baseline repair before declaring the full goal complete.

FrozenMap/Set lookup experiments, dynamic interning and adaptive worker activation remain separately
tracked opportunities. They do not justify delaying the already qualified production stack or
silently expanding the acceptance gate. The toolchains_llvm 1.11.1 evaluation is also isolated from
these changes, so its build/performance effects remain attributable to the version pin.
