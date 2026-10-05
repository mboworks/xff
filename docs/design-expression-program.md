# Prepared expression programs and optimization plan

Status: implementation and measurement plan, not a claim that the proposed executor exists or
has measured speedups. The implementation sequence below is intended for separately reviewable
changes. Benchmark publication and parser-lookup experiments remain independent workstreams.

## Objective and inspected baseline

Move invariant expression work out of the per-entry path. Parse and validate once, resolve
configuration, prepare operands and execution state, then execute a compact program with bound
operations. Preserve observable behavior, VFS routing, safety enforcement and existing worker
eligibility. Do not promise zero overhead: matching, indirect calls, branches, memory access and
output still cost time.

Inspected on 2026-10-04 against XFF main `6f9c05aa15` (#956), refreshed before planning:

- [`parser::Parse`](../xff/parser/parser.cc) calls `ParseArguments` and then `ExprParser` once.
  Per-entry execution does not repeat either parser pass.
- [`ApplyResolvedConfig`](../xff/cli/config_validation.cc) parses configuration expressions and
  combines them with the CLI tree. Final case/grammar binding happens after configuration resolution.
- [`EvaluateResult`](../xff/engine/evaluate.cc) recursively interprets expression nodes.
  `EvaluatePredicate` looks up the handler by descriptor name in a constexpr `LimitedMap` for
  every reached predicate. Some constant operands are decoded again: `MatchesSize` calls
  `ParseSizeSpec`, numeric tests decode comparisons, and `EvalMime` lowercases its pattern.
- Regex matchers and field transformations already have preparation paths; reuse these rather
  than creating a second compiler for them.
- [`evaluate.h`](../xff/engine/evaluate.h) indexes worker matchers, deferred decisions, memoized
  results and counters by `ExprIdentity` in maps. Deferred evaluation carries truth, fuzzy score,
  suspension and unknown results; it is not ordinary two-valued boolean evaluation.
- [`Descriptor`](../xff/registry/descriptor.h) already supplies cost, purity, metadata, safety,
  binding, control and worker eligibility. Its `pure` comment currently suggests reorderability;
  that must be clarified before an optimizer relies on it. Purity alone is insufficient.

Related evidence: [performance analysis](performance-analysis.md), especially the registered
mode parser experiments, worker-state work and metadata-demand sections.

## Data-structure audit

XFF pins MBO commit `c854476fb9bf22b6dc372a756d80831f7655d60e` through a git override, despite
the dependency's declared release version. The audit also inspected the newer local MBO checkout
at `df11721e7b`. MBO [PR #550](https://github.com/mboworks/mbo/pull/550) subsequently merged
experimental `FrozenMap` / `FrozenSet` at `bf65c21c495fc789feab79f4516d1ae7215cb292`.
The API and implementation audit below includes that commit. These newer APIs are not present in
XFF's pin; no MBO files or dependency versions were changed for this plan.

| Need                                     | Available now                                                                              | Decision / missing work                                                                                       |
| ---------------------------------------- | ------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------- |
| Compile-time maps and sets               | MBO `LimitedMap` / `LimitedSet` are constexpr-capable ordered, fixed-capacity containers   | Already available; they are not hash tables. Keep as the small-table baseline.                                |
| Exact mode-dependent flag lookup         | XFF `SpellingIndex` constructs sorted per-mode arrays at compile time                      | Already available; preserves aliases and exact unknown-token rejection.                                       |
| Compile-time string hashing              | MBO hash algorithms expose constexpr hashing, including tested FNV-1a and other algorithms | Already available; choose using short-key measurements, not a new ad hoc hash.                                |
| Frozen/perfect-hash string map and set   | Upstream MBO experimental `FrozenMap` / `FrozenSet` from PR #550; absent from XFF's pin    | Available for a measured dependency-update experiment in startup lookup; not a prerequisite for the executor. |
| Typed node, instruction and slot IDs     | MBO `ConstStrongId` / `ConstStrongOrdinal` are in the pinned revision                      | Reuse distinct tags; XFF still needs the ID assignment and slot-layout design.                                |
| Contiguous immutable instruction storage | `std::vector` with reserve and standard arrays/spans                                       | Sufficient initially. Count nodes, reserve once, then freeze; do not add a container.                         |
| Stable growing records and owned text    | Pinned MBO `SegmentedVector`, `SegmentedDeque` and `Arena`                                 | Available when lifetime/address stability requires them; benchmark against reserved vectors.                  |
| Runtime associative lookup               | Abseil flat hash maps/sets and existing ordered maps                                       | Available for preparation. Do not replace ordered observable output with hash iteration.                      |
| Dynamic string-to-ID/value mapping       | Newer MBO has experimental `StringInterner` / `StringInternerMap`; absent from the XFF pin | Candidate for captures, definitions and dynamic catalogs after a separate dependency decision.                |
| Persistent/shared maps                   | Newer MBO has experimental HAMT map/set variants; absent from the XFF pin                  | No demonstrated need for persistent snapshots in the immutable execution program.                             |
| Per-worker and suspended-entry state     | Existing maps, but no prepared dense execution-slot layout                                 | XFF-specific work: indexed slots, validity tracking and compact suspended-state records.                      |
| Optimization effects and dependencies    | Existing descriptor fields cover only part of the contract                                 | XFF-specific work: totality, observations, state reads/writes, invalidation and explicit barriers.            |

The core executor requires no new general-purpose container or dependency upgrade. The missing
pieces are its representation and semantic contracts. A constexpr hash table improves name-to-ID
resolution during preparation; the prepared per-entry path should perform neither hashing nor
flag-name lookup. A HAMT or interner in that path would retain work that binding can remove.
Registry tables can be built at C++ compile time; the user's particular program is prepared at
runtime once its CLI and configuration are known. The two preparation times must not be confused.

### Optional frozen hash map/set contract

MBO's experimental containers now provide the planned facility. The requirements for adopting
them in XFF remain:

1. Build immutable, statically stored tables from constexpr entries; set and map share key-index
   construction. Use length-aware borrowed keys with static lifetime and typed mapped values.
2. Reject conflicting duplicate keys during constant evaluation. Construct each mode from the
   registry's declared spellings; permit the same spelling to mean something else in another mode.
3. For the perfect-hash variant, verify unique occupied slots for the registered key set at
   compile time. A unique full hash does not prove collision-free bucket placement. Bound seed or
   displacement search and issue a clear construction failure instead of exhausting compiler resources.
4. Always compare the candidate's full spelling on lookup. Unknown input can hit an occupied slot
   even when all registered keys are collision-free. Handle empty, embedded-NUL, non-ASCII byte and
   long unknown inputs safely; never index by a signed character.
5. Require constexpr/runtime hash equivalence, allocation-free lookup, no dynamic initialization,
   explicit absent results and checked construction bounds. Use XFF's optional-reference/value
   conventions at its interface. Preserve deterministic generation across supported compilers.
6. Test zero/one/many keys, duplicate diagnostics, unsuccessful construction, adversarial misses,
   aliases and all modes. Measure build time, constexpr evaluation cost, object/rodata size and
   whole-command startup alongside lookup latency. A non-minimal sparse perfect table may beat a
   minimal one; neither should be selected from asymptotic complexity alone.

Compare the existing sorted index and `LimitedMap`, the bounded-probing prototype, a frozen perfect
table, and the compact/direct trie experiments behind the same exact-lookup interface. A trie may
help token scanning as well as spelling lookup, but would need an explicit operand-boundary design.
Do not turn failure to find a fast frozen table into a blocker for expression execution work.

### FrozenMap/Set adoption check after MBO #550

The merged [API documentation](https://github.com/mboworks/mbo/blob/bf65c21c495fc789feab79f4516d1ae7215cb292/mbo/container/experimental/FROZEN.md)
and headers satisfy the structural requirements: constexpr inline immutable storage, shared map/set
index construction, bounded displacement search, occupied-slot verification and exact candidate
equality on lookup. `FrozenMap::lookup` returns `OptionalRef<const Value>`, so XFF does not need a
raw-pointer adapter. Distinct aliases can share a mapped ID; build each mode separately to preserve
different meanings for the same spelling. Conflicting duplicate mappings fail construction.

Use registry-generated `std::string_view` to entry-ID pairs with static backing storage. Preserve
the existing exact miss result and mode switching. `FrozenSet` is useful only for membership-only
tables; flag dispatch needs the map's associated entry. Do not introduce a second spelling catalog
or hash flag names inside the prepared per-entry executor.

The first experiment should compare default sparse placement (twice capacity) and minimal placement
against the current sorted index on the actual XFF/find/rg vocabularies. Include hits, misses,
aliases, full parser commands, compile time and binary/read-only storage cost. Verify Clang and GCC
constant evaluation within their normal budgets. The default limits are 4,096 entries, 65,536 total
key bytes and 65,536 construction work units; actual registry construction must fit, or justify an
explicit bounded option change. A distinct-key full-hash collision is rejected, not silently handled
by a runtime fallback. The experimental API and container measurements alone do not establish an
XFF startup speedup. Pin the inspected MBO commit in the experiment and retain the current production
index until those application measurements justify replacement.

The user reports that the merged Frozen containers are substantially slower than
`std::unordered_map` / `std::unordered_set` in their measurements. No raw measurements from that
comparison have been imported into this XFF analysis. Treat performance as an unresolved adoption
gate, not as a benefit of constexpr construction. Include the standard unordered container and
the existing bounded-probing prototype in the application comparison. A dependency update or
production replacement is not justified by API availability alone. The prepared executor removes
per-entry name lookup regardless of which container eventually serves startup lookup.

## Program architecture

### Preparation boundary and ownership

The logical pipeline is:

```text
argv / INI tokens -> mode-aware argument pass -> parsed expression
    -> resolved configuration and whole-command validation
    -> bound operands and effects -> optional optimization -> execution program
    -> immutable shared program + worker-local scratch -> entry results / effects
```

The expression pass creates the intermediate representation. Final lowering must wait for resolved
configuration, case/grammar, block units, timezone/reference time, output defaults and safety policy.
Do not push engine dependencies or VFS operations into the CLI parser merely to combine passes.
Measure preparation as a whole before trying to fuse its internal passes.

A run-owned object owns the syntax/provenance needed for diagnostics, prepared operand pools and
the frozen program. Workers and suspended entries cannot outlive it. Assign stable source-node IDs
before rewriting; preserve a mapping from optimized instructions to original expressions. Keep
configuration/CLI origin information where available, and add an explicit source record if current
AST data is insufficient. IDs are process-local implementation details, not a serialized ABI.

### Instructions, operands and outcomes

Begin with a bound-tree implementation to isolate handler-binding gains. Then compare a contiguous
instruction array with a small opcode-switch executor against bound function-pointer instructions.
Avoid allocating `std::function` closures per node. Do not depend on recursive tail-call elimination,
computed-goto extensions or machine-code generation for the first implementation.

An instruction needs an operation, a typed operand/operand-pool index, source-node identity and
explicit control-flow destinations. Keep cold diagnostic strings outside frequently visited records.
Use separate tagged IDs for instruction positions, source nodes and state slots; check overflow and
invalid targets during construction. Select index widths from measured program sizes, not assumptions.

For the unscored, non-deferred subset, a program can look like:

```text
0  test_file_type(regular)       true -> 1, false -> done(false)
1  test_size(prepared_spec)      true -> 2, false -> done(false)
2  emit_path(prepared_output)   -> done(true)
```

The complete executor must retain unknown/error/suspend outcomes and fuzzy composition. AND/OR,
NOT, NAND/NOR, XOR/XNOR and comma cannot all be encoded as the same simple branch. Scored OR may
need both sides. Compile explicit score-combination and continuation instructions, and retain
left-to-right execution of side effects. A tiny expression may use a cheaper bound-tree path if
linear-program preparation does not amortize.

Prepare numeric comparison operators, units, type sets, permission syntax, invariant text flavor,
pattern normalization, output options and existing regex/template objects once. Keep field-expanded
values, filesystem-dependent case behavior, mutable capture values and per-entry facts dynamic.
Distinguish parsing a reference pathname from reading the referenced file: moving a filesystem or
user/group lookup earlier can change errors or observe different state. Audit those cases before
caching them. Preserve the existing validation timing and invalid-input behavior; changes to those
semantics require separate decisions, not silent optimizer side effects.

### State and storage

- Assign worker matcher/counter/scratch slots at preparation. Reuse existing backend-specific worker
  state; immutable code is shared, mutable RE2/PCRE2 scratch stays private. Binding should remove
  repeated `ExprIdentity` map lookups, not just replace them with a different map.
- Prepare reusable scratch once per worker. For ordinary filtering, target zero allocations by the
  dispatch/control-flow machinery per entry. Distinguish allocations inside content, rendering and
  actions in measurements instead of claiming all evaluation is allocation-free.
- Avoid clearing every possible slot for every entry. Evaluate compact live-slot layouts, touched-slot
  lists or generation tags; explicitly test generation wrap and stale-state reuse.
- A suspended entry needs its own continuation, completed effects, scores and required state. Do not
  retain an entire dense worker array per file: measure memory versus live deferred slots and preserve
  result-set buffering limits. Keep sparse snapshots where the density does not justify arrays.
- Keep coordinator-owned actions, output ordering, cancellation and existing worker eligibility.
  This project does not make stateful expressions parallel merely because they are compiled.
- Reserve contiguous program/operand storage from counted requirements. Use existing arenas or
  segmented storage only where measurements or stable-lifetime requirements justify them. Include
  construction, move, teardown and peak retained memory; larger blocks are not automatically better.

## Optimization contract

Extend declarative semantic metadata rather than adding flag-name conditionals. Engine preparation
callbacks remain engine-owned; registry metadata stays dependency-light. Add a stable semantic ID
or generated index binding between them, with completeness and alias-consistency checks. Do not
duplicate help, mode, safety or operand rules in an optimizer registry.

For each operation, describe inputs, produced values, observable effects, possible failure or
suspension, metadata/content needs, state invalidation, and whether repeated evaluation is equivalent.
Default unaudited operations to barriers. Reuse existing fields where their meaning is adequate;
clarify `pure` without treating it as permission to reorder all metadata/content predicates.

| Transformation                                | Initial scope                                                           | Required guard                                                                                                    |
| --------------------------------------------- | ----------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Bound handler and operand specialization      | All audited predicates; adapters for remaining handlers                 | Same validation, error and value semantics; no per-entry string dispatch                                          |
| Constant folding / unreachable branch removal | Proven constant boolean subexpressions                                  | Preserve whole-command validation, traversal effects and implicit-output decisions made from the original command |
| Jump simplification and instruction fusion    | Adjacent equivalent control operations; measured common predicate pairs | Preserve every failure edge, action order, score and metadata-on-demand boundary                                  |
| Reuse of computed values                      | Invariant preparation data; audited entry-local derivations             | Compatible parameters and lifetime; invalidate across mutations or unknown effects                                |
| Predicate reordering                          | Later experiment within independent, total, unscored conjunctions       | No observable errors, reads whose timing matters, captures, counters, actions, traversal effects or suspension    |
| Adaptive costs / profile-guided ordering      | Deferred                                                                | Static optimizer must first prove useful; runtime profiling must justify its own overhead                         |

Examples of required distinctions:

- `false AND X` need not execute X, but X's presence may already affect default printing, traversal
  mode, safety preflight or diagnostics. Elimination cannot undo those decisions.
- `X AND false` cannot drop X when it emits output, changes state or reports a reachable error.
- `P AND P` is not generally reducible merely because P is marked pure: content may change,
  observations may fail, and fuzzy scoring or bookkeeping may be relevant.
- Moving a name test before a metadata test can suppress a metadata error. Do not call that an
  equivalent reordering without an explicit language-level decision.
- Never move `-delete`, execution, output, captures, `-prune`, `-quit`, `-first`, `-top` or collection
  operations across effects. Mutation policy remains enforced at the VFS control surface.

Keep optimizer passes deterministic and bounded. No exponential normal-form expansion or unlimited
fusion generation. Record pass time, instruction counts and skipped transformations for `--explain`.
The initial comparison selector belongs in test/benchmark APIs, not a provisional user flag or
environment variable. Extend existing `--explain` help and tests when program explanations ship.

## Implementation sequence and exit decisions

Each stage is a reviewable PR on the preceding required stage. Independent lookup/container work
does not belong in this stack. Infrastructure stages need correctness and bounded cost, not invented
speedup claims; enabling a production optimization requires its measured benefit.

| Stage                              | Implementation and deliverables                                                                                                                   | Exit decision                                                                                                                                  |
| ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| EP01: contract and baseline        | Inventory every evaluator/operand/effect; trace-based oracle; preparation/execution benchmarks; typed node IDs and conservative property schema   | Baseline reproduces current behavior and separates startup from per-entry cost; no production algorithm change                                 |
| EP02: bind dispatch                | Resolve handlers once through generated semantic bindings; bound-tree executor; registry coverage checks; keep syntax for diagnostics             | Retain if lookup work disappears and cheap-expression measurements improve without material startup regression                                 |
| EP03: prepare operands             | Typed numeric/type/permission operands and audited string/template preparation; indexed worker matcher slots; remaining-dynamic-work inventory    | Retain each family separately; prove invariant parsing happens once and preserve conditional metadata/errors                                   |
| EP04: lower control flow           | Contiguous instruction representation; switch versus function-pointer pilot; explicit boolean control flow and reusable scratch; source mapping   | Choose measured layout; enable only audited semantic subset, with whole-expression fallback reported explicitly                                |
| EP05: complete execution semantics | Scores, unknown/error outcomes, action ordering, deferred continuations and compact snapshots; integrate serial/parallel paths and existing sinks | Differential equivalence, exactly-once effects and bounded memory; migrate remaining fallbacks or document why they remain                     |
| EP06: deterministic optimizer      | Constant folding with whole-command semantics preserved, jump simplification, selected fusion and proven value reuse; per-pass explain details    | Keep transformations individually by evidence; reordering stays disabled until its stronger proof and benchmarks pass                          |
| EP07: production decision          | Cross-platform comparison, ordinary CLI/regression workloads, binary/preparation/memory accounting, generated help and performance report         | Enable the winning path; retain unoptimized-program and reference test coverage, or leave unsupported/unhelpful cases on the existing executor |

EP04's fallback is transitional, not completion of the objective. Report its coverage by operation
and workload, not merely a percentage of tests. Avoid routing back through a name lookup inside every
prepared instruction. Only remove a reference implementation after the differential suite has an
equally strong replacement; do not retain two independently evolving public execution modes.

## Correctness and integration verification

Compare the tree oracle, unoptimized program and optimized program on the same isolated fixture.
Record truth, fuzzy value, unknown/error status and diagnostics, output bytes/order, prune/quit,
captures/counters, deferred decisions, action events and final filesystem state. For proven read
reuse, compare observable results while separately checking allowed read-count reductions. Event
traces must distinguish permissible internal cache changes from suppressed failures or effects.

Required cases include:

- Every operator, nested/long chains, empty/default expressions and every registered predicate family.
  Exhaust small boolean trees; use seeded generated larger trees with reproducible failure fixtures.
- All-match, no-match and mixed selectivity; short-circuited failures and unreachable malformed
  arguments; implicit printing and traversal effects contributed by unreachable actions.
- INI globals/named configurations, composition, CLI overrides, find/XFF/rg switches, native filters
  before `--rg`, aliases, final case/grammar and block/timezone settings.
- Metadata never/on-demand/always, basic versus birth-time demand, unknown entry types, symlinks,
  unsupported operations, races and read failures. Never turn an error into an ordinary false result.
- Fuzzy scores and ordering; counters, collections and captures; suspend/resume at multiple nodes;
  completed actions must run exactly once through replay and cancellation.
- Safe/block/dry-run combinations; writes, deletion and execution through isolated recording sinks.
  Read-after-mutation invalidation, output ordering, explicit confirmation, batched exec and failures.
- Plain paths, formatted/content output, `-M`, rg context/count/quiet/multiline behavior, summaries,
  comparison, archives and virtual sources; lean and extension builds.
- Plan moves/lifetime, worker creation/reuse, slot reset, cancellation/teardown and deep expressions.
  Separate Unicode fixtures as required by repository policy.

Use existing VFS seams and in-memory fixtures. Fuzz only registered `Safety::kNone` operations in
an isolated filesystem with mutations denied and execution/output sinks disconnected. Effectful
cases belong in controlled tests, not arbitrary-input fuzzing. Use one C++ test source per Bazel
target, repository status/optional matchers, changed-file clang-tidy, sanitizer CI and existing
coverage thresholds. No threshold weakening to accommodate the new executor.

## Benchmark design

### Separate four costs

| Layer                           | Timed work                                                                                                                    | Purpose                                                                  |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| Preparation                     | Parse, configuration application, operand binding, optimization, worker preparation and teardown; also time phases separately | Catch regressions for tiny searches and long expressions                 |
| Execution kernel                | Reuse a prepared plan over prebuilt entries with a checked result sink                                                        | Isolate dispatch/control flow; exclude construction and filesystem speed |
| In-memory engine                | Real VFS walk, filtering, workers and bounded output over deterministic trees                                                 | Detect allocation, metadata, synchronization and scheduling interactions |
| Whole process / competing tools | Same executable configuration, fixtures and exact output semantics as published comparisons                                   | Verify user-visible wins, particularly `files / rg` and `name-txt / rg`  |

Extend the existing `parser_benchmark`, `rg_benchmark`, `read_benchmark` and comparison tooling
where appropriate; add a focused engine program benchmark for the first two layers. Proposed new
target names must be marked as proposed until implemented. The current harnesses do not by
themselves establish performance of this design.

Use an identical frozen fixture description for candidate and baseline. Reuse the manifest format
(`=:`, `=t:`, `=b:`, `=l:`) and archive inputs for larger cases; no sidecars. Execution-kernel entries
and in-memory VFS data exclude host filesystem latency. External tools cannot use XFF's VFS: use
the existing host-fixture comparison tier and record storage/cache policy separately.

Core matrix:

- Entries: 10, 20, 50, 100, 200, 500, 1,000, 2,000, 5,000 and 10,000 for PR comparisons;
  add 20,000, 50,000 and 100,000 for full/main qualification. Add zero/one entry for startup.
- CPU allowance: 1/3 in CI; 1/3/10 locally where capacity permits. Record actual worker count,
  eligibility, scheduling and affinity. Cheap serial predicates remaining serial is not a failed
  concurrency test; separately measure eligible content-worker cases and do not force production
  parallelism just to populate a chart.
- Expressions: 1, 4, 16, 64 and 256 predicates; AND/OR chains, balanced trees, repeated predicates,
  nested negation and mixed operators. Vary selectivity (0/1/50/99/100 percent) and early/late failure,
  with clustered and interleaved entry ordering to expose branch-prediction effects.
- Work families: file/type listing; name/path exact and glob selection; size/permission/time tests;
  content literal and equivalent RE2/PCRE2 workloads; metadata/content reuse; output-heavy actions;
  fuzzy and deferred controls. Include controls without explicit expressions and rg-only searches
  that may barely use the native evaluator. Improvements cannot be assumed for those paths.

Do not multiply every dimension into a prohibitively large campaign. EP01 checks in a named core
case manifest covering every semantic family and interaction, plus an extended stress matrix.
Run cheap dispatch/operand cases across all expression lengths; use representative 1/16/64-node
cases for expensive content and effect tests. Record the exact case set, not only its range.

### Sampling, resources and publication

Build all variants before timing with identical compiler, optimization, LTO, allocator and extras.
Measure these variants independently: current tree; bound tree; prepared operands; unoptimized
linear program; each optimizer pass; final combination. Do not attribute combined gains to one pass.
An internal harness selector may compare executors in one binary; also compare final shipping
binaries to catch code-layout and size effects.

Use a correctness-checked discarded warm-up, then rotate/interleave baseline and candidate order.
Keep nine raw measured rounds and report the fastest-seven mean, matching the existing PR/main
policy. For microbenchmarks, batch enough entries for reliable clock resolution and report time per
entry and per reached predicate; do not count a never-reached 256-node chain as 256 evaluations.
Also retain medians, all-round spread and paired comparisons so fastest-sample selection cannot hide
tail regressions. Repeat candidate decisions in two independent CI sessions. Use small focused local checks only;
local timing campaigns require a specific need because this work prioritizes low local resource use.

For CI publication use the existing three shards with three rounds each and pool before selecting
seven of nine. Keep local machine series separate; Linux uses recorded core affinity, macOS worker
requests are not hard pinning. Run native macOS ARM64 and Linux x86-64; GCC remains a portability
check unless separately benchmarked. Sanitizer timings are not release performance evidence.

Collect wall/CPU time, throughput, preparation/optimization time, instruction bytes/count, branch
targets, allocation counts/bytes by phase, peak RSS, worker scratch and suspended-entry memory,
VFS calls/bytes and stripped lean/full binary size. Hardware counters for branches/misses and
instruction/cache behavior are optional platform diagnostics, not a prerequisite. Keep intrusive
allocation/trace instrumentation out of timed release binaries; use a separate diagnostic run.

Retain raw JSON with revisions, binaries, fixture/case hashes, driver, compiler/LTO commands,
platform/CPU/allocation, reference-tool versions and sampling contract. Publish HTML and a compact
decision table with absolute times, paired percentage changes, reference-adjusted changes, noise
and applicability. Preserve incomplete or incompatible results as such, never as zero.

For whole-process comparisons, show both raw timings and the existing presentation-only reference
normalization. If baseline is X1/R1 and candidate X2/R2, compare X1 against X2\*R1/R2; a common
reference-window target cancels in that pairwise ratio. Use the established first-five window for
initial history and backward five-measurement windows thereafter, only within compatible series.
Microbenchmarks without external references use interleaved paired results; do not manufacture a
reference normalization for them. Keep the existing 15% CI advisory non-blocking for now.

## Decision gates and rollout

Correctness and safety are mandatory. Performance thresholds below are initial review budgets,
not new CI gates or claimed benefits; record deviations and their rationale before adoption.

| Decision                 | Initial acceptance / rejection rule                                                                                                                                          |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Correctness              | Zero unexplained oracle differences, no additional mutation route, no repeated effects, all required tests and existing coverage gates green                                 |
| Bind/lower/optimize      | Prefer at least 5% repeatable improvement in its targeted cheap-entry cases, exceeding measured noise; require supporting whole-engine results rather than kernel-only wins  |
| Ordinary workloads       | Investigate any repeatable slowdown over 2% outside noise; do not hide a slow case inside a geometric mean. Keep a simpler path or reject the change if the tradeoff is poor |
| Preparation              | Report zero/one/ten-entry latency and long-expression cost. Compute the break-even entry count; do not accept large setup costs based only on 100,000-entry wins             |
| Dispatch allocations     | Zero steady-state dispatch/control-flow allocations for the ordinary prepared path after worker initialization; separately account for payload operations                    |
| Memory                   | No per-entry full-program clone and no unbounded dense deferred snapshot. Review any worker/RSS growth above 5% against absolute bytes and workload size                     |
| Code size and build cost | Review stripped binary growth above 1% or 64 KiB, whichever is smaller; disclose compile-time/constexpr costs and limit specialization/fusion growth                         |
| Architecture consistency | Require evidence on native ARM64/macOS and x86-64/Linux before a universal default; state unsupported or unmeasured cases explicitly                                         |
| Optional containers      | Replace the existing structure only when total preparation or execution improves after including construction, memory, teardown and dependency cost                          |

Let preparation increase by `delta_prepare` and per-entry time decrease by `delta_entry`.
The simple amortization estimate is `ceil(delta_prepare / delta_entry)` when both are positive;
otherwise report immediate benefit or no measured crossover, as applicable. Treat it as an estimate:
selectivity, output, worker startup and cache behavior can make scaling non-linear.

Each stage appends evidence and a retain/revise/reject decision to
[`performance-analysis.md`](performance-analysis.md). Record unsuccessful experiments, remaining
dynamic work, fallback coverage and the next action. The final decision must answer which workload
benefits, where overhead remains, how much memory/setup it costs, and whether the simpler alternative
is sufficient. Reject an unhelpful optimization even when its infrastructure already exists.

### Open decisions with defaults

- Executor layout: measure switch and function-pointer forms; start with a reserved contiguous array.
- Operand layout: use small typed records and separate pools; avoid a maximal variant in every
  instruction until its cache footprint is known.
- State layout: typed indexed worker slots; compact/sparse state for suspended entries unless density
  measurements favor another representation.
- General reordering: off until totality, observations and score semantics permit it. Preserve user
  order everywhere else, including error-producing reads.
- Frozen hash table: separate optional MBO work; keep the exact sorted index until a replacement wins.
- Experimental MBO interning/HAMT: no dependency upgrade in the core executor work without a concrete
  consumer, API/lifetime audit and measurements. Existing strong IDs, arrays and arenas are enough
  to start.

## EP01 implementation record

The first implementation provides `DescribeExpression`, dense preorder source IDs from MBO
`ConstStrongId`, and conservative registry-derived requirements. The original tree owns the nodes;
the table borrows them and survives moving the owning `unique_ptr`, but not replacement of nodes.
All operations remain optimization barriers. `Descriptor::pure` is explicitly insufficient to
establish reorderability. Neither table construction nor lookup is on the production per-entry path.

The isolated contract suite records output and VFS observations and checks every binary operator's
truth table and short-circuit order, NOT's score behavior, deferred prefix replay, dry-run unknown
results, and prune/quit effects. This is the initial oracle corpus; EP02-EP05 extend it to compare
candidate executors and cover the full integration matrix above.

`//xff/engine:expression_benchmark` separates parse/bind/contract preparation from tree execution
on immutable in-memory entries. Its named core cases are `type`, `name`, `size`, `permission`,
`content`, `regex`, `fuzzy` and `output`, each at 1/16/64 predicates; kernels process batches of
10/1,000 entries. All cases match and therefore reach every predicate. Preparation includes
teardown; kernel construction and its correctness check are untimed. Cached content is invalidated
between entries. This initial matrix establishes dispatch/operand costs; it is not the extended
selectivity/parallel/integration qualification matrix.

The existing CI benchmark build jobs collect nine raw rounds after warmup and report fastest-seven,
median and spread statistics in `expression-baseline-linux` and `expression-baseline-macos` JSON
artifacts. These kernel timings have no external reference and are not reference-normalized. They
are baseline evidence, not a claimed speedup, and are separate from the unchanged competitor grid.
No new benchmark job or blocking performance threshold is introduced. CI compilation, sanitizers,
coverage and linting validate the C++ additions; local checks stay limited to formatting and policy.

## EP02 implementation record

`BoundExpression` is an experimental bound tree in the engine library. Preparation validates shape,
resolves the existing engine dispatch table once and stores direct handlers beside preorder source
references and typed child indices. Fuzzy-only subtree classification is prepared in a reverse pass.
There are no per-entry dispatch lookups or preparation allocations. Payload matchers may still
allocate, and invariant operand parsing is unchanged until EP03.

The tree must outlive the bound expression. Moving its owning `unique_ptr` preserves references;
replacement or mutation of the AST is not permitted after binding. Evaluation uses the existing
boolean/error/score/deferred implementation specialized on tree or bound cursors, with original
source identities for per-run counters and replay memoization. Conditional metadata acquisition and
content invalidation stay in their original order. This experiment introduces no CLI selector and
does not change which executor production uses.

Every registered non-operator descriptor must have an engine handler or an explicit preparation/
traversal/control disposition. `evaluation_noop` marks grammar-only predicates whose effects were
consumed by parsing. Missing handlers without such a disposition fail preparation. The contract
tests run as named `Tree` and `Bound` parameterizations with the same expected observations.

The benchmark doubles the original kernel case matrix to compare both executors, randomly
interleaving nine repetitions in one binary. `prepare/tree` measures the actual parse/bind/teardown
path without EP01's diagnostic contract table; `prepare/bound` also includes binding and teardown.
The discarded correctness check and kernel preparation are untimed. All raw rounds, fastest-seven,
median and spread remain available; the CI report includes revision and sampling provenance.
This implementation is a candidate awaiting measurements, not an accepted performance improvement.

## EP03 operand preparation boundaries

The first operand candidate covers type lists, size specifications, unsigned numeric tests
and permission modes. Preparation must retain the existing raw spelling for diagnostics and bind
its decoder from the same engine dispatch entry as the evaluator. Do not introduce another
spelling switch or map. Preserve the EP02 bound-only variant in the same benchmark binary to
separate operand savings from dispatch savings.

| Family                                   | Prepare once                                                                                                | Preserve at execution / validation boundary                                                                                                       |
| :--------------------------------------- | :---------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------ |
| `-type`, `-xtype`                        | Decode the complete type list into a small set; invalid trailing items must invalidate the complete operand | Entry type; `-xtype`'s conditional target stat and broken-link fallback                                                                           |
| `-size`, `-blocks`                       | Parse comparison, count and explicit byte unit                                                              | Whole-command `ValidateSizeArgs` still runs; bare and `b` units use the resolved block size; apparent versus allocated byte source stays distinct |
| `-links`, `-inum`, `-uid`, `-gid`        | Decode comparison and unsigned count                                                                        | Conditional metadata; preserve malformed-value behavior and explicitly test integer boundaries before replacing the existing decoder              |
| `-perm`                                  | Resolve exact/all/any mode and octal or symbolic mask from a zero base                                      | Mask the entry mode to `07777`; preserve zero-mask any-of, BSD `+octal`, and symbolic `+r` semantics                                              |
| `-used` and numeric age tests            | Comparison/count and fixed unit after signed boundary tests                                                 | Signed differences, truncation toward zero, birth-time availability and error policy                                                              |
| Word/calendar ages                       | Parse relative time only after the run's time and timezone are final                                        | Do not change day-start/calendar semantics or initialize timezones for unrelated predicates                                                       |
| `-newer*`, `-samefile`                   | Spelling/selector decoding only initially                                                                   | Reference-file reads remain conditional; preparation must not observe a skipped reference or hide changes made by earlier actions                 |
| `-user`, `-group`, `-nouser`, `-nogroup` | No account-database snapshot in the first candidate                                                         | Existing name-service lookup/failure behavior remains dynamic until its semantics are separately decided                                          |

Current numeric helpers have different overflow handling: `ParseSizeSpec` uses checked
`SimpleAtoi`, while the plain numeric, signed numeric and permission helpers accumulate digits
directly. This is an audit finding, not evidence that all boundary forms reach those helpers from
every command-line path. Add boundary cases before extracting the decoders; do not introduce new
signed-overflow behavior in the prepared path or silently treat an arbitrary input as zero.

For matcher state, the current `WorkerMatchers` owns a map keyed by `ExprIdentity`; `MatcherFor`
looks into that map per reached regex predicate. The prepared program can assign dense matcher
slots during preparation and let each worker populate its own vector once. Preserve
`ForkForWorker` failure's current fallback to the validated immutable matcher, and keep mutable
backend state worker-owned. Original expression identities must still remain available for
captures, counters and deferred replay. Slot indices belong to one program and cannot be reused
against a different expression's worker vector.

Evaluate compact operand pools before inflating every boolean node with a maximum-sized payload.
Record node bytes, pool bytes and preparation allocations alongside per-entry cost; unsupported
families should retain their existing callback rather than reparsing an already prepared family.
`PreparedExpression` implements the scalar candidate with a typed pool, source-node references
and the same recursive control semantics as EP02. Factories are registered with the existing
engine handler table, and numeric/permission parsing is shared with the reference evaluator.
No production switch is made. The trace oracle runs all three executors; boundary tests additionally
reuse prepared scalars across changed entry metadata and block sizes. Nine-round interleaved CI
benchmarks compare tree, bound-only and prepared execution in 270 cases and record owned storage
capacity. Indexed worker matchers and the remaining families above are still open; this is a
partial EP03 implementation, not the stage's completion.

### On-demand indexed worker matcher candidate

The first whole-engine session exposed eager regex-fork work even for empty searches and
unreached predicates. Each prepared worker now allocates its slot vector once, then resolves
each slot on its first reached regex predicate. An initialized bit distinguishes a never-used
slot from an absent or failed fork; subsequent files never retry it. Successful mutable backend
state belongs to that worker, while the immutable original matcher remains the failure fallback.
No shared lazy state or locking is introduced. The original validated matcher still exists before
the walk, so malformed-pattern validation does not become conditional.

`MatcherInitialization::kEager` remains an internal comparison option. Tests check partial
short-circuit initialization, independent workers, reuse across files, moves, captures and
unbound matchers. Ordinary, iterative and instruction callbacks share slot resolution. Report
initialized slot counts separately from allocated slot capacity. The bit may increase each slot's
size through alignment, which is included in the existing storage-byte accounting.

Compare both initialization policies in the native kernel and complete-engine harnesses, including
zero/one files and a name filter that prevents every regex from being reached. Kernel warmup
includes the first match, so it excludes initialization; complete-engine timings include it.
The eager comparator uses the same new slot bookkeeping and isolates initialization timing.
Compare both against the unchanged tree and the previous candidate results to assess total
hot-loop/storage overhead. Moving cost into the first match is not by itself a speedup for a
reached predicate. Production remains the tree until this tradeoff is measured.

### Evaluator operand/effect inventory

The registry remains authoritative for spelling, aliases, traversal, metadata, safety and worker
eligibility. The following groups partition the current dispatcher by the work they perform; they
are an implementation audit, not a second runtime vocabulary. `-top` and `-shard-status` bypass the
ordinary dispatcher; parser/traversal-only primaries preserve their existing true/no-op behavior.

| Family                                                         | Current invariant work                          | Dynamic work and barriers                                                      |
| :------------------------------------------------------------- | :---------------------------------------------- | :----------------------------------------------------------------------------- |
| `-true`, `-false`                                              | None                                            | Whole-command effects of surrounding syntax still apply                        |
| `-type`, `-xtype`                                              | Type-list decoding                              | Entry type; `-xtype` may observe the link target and fail                      |
| `-name`, `-iname`, path/wholename variants                     | Pattern bytes; case selection                   | Entry name/path and filesystem case policy                                     |
| `-lname`, `-ilname`                                            | Link glob and case policy                       | VFS link read and failure                                                      |
| `-regex`, `-iregex`, `-rxc`, `-irxc`                           | Matcher already compiled                        | Entry bytes, optional captures, backend worker scratch                         |
| `-content`, `-icontent`, `-text`, `-binary`, `-eof*`           | Literal/flavor interpretation                   | Content reads, cache invalidation, unsupported/error outcomes                  |
| `-size`, `-blocks`, `-links`, `-inum`, `-uid`, `-gid`, `-used` | Numeric comparison, units, block size           | Conditional metadata fetch; signed age arithmetic                              |
| `-perm`                                                        | Numeric/symbolic permission parsing             | Conditional mode fetch; preserve invalid-input behavior                        |
| `-*time`, `-*min`, `-newer*`, `-anewer`, `-cnewer`             | Duration/time-string decoding                   | Clock/timezone, birth-time support and reference-file observations             |
| `-user`, `-group`, `-nouser`, `-nogroup`                       | Numeric-ID parsing                              | User/group database lookup is observable; no eager caching yet                 |
| `-empty`, `-sparse`, `-fstype`, `-samefile`                    | Reference pathname only                         | Metadata, directory reads, filesystem/reference observations                   |
| `-readable`, `-writable`, `-executable`                        | Access mode                                     | VFS access checks remain dynamic                                               |
| `-lang`, `-mime`                                               | Catalog selection / MIME pattern normalization  | Filename classification; final configured catalog                              |
| Fuzzy variants, `-similar`                                     | Fzf query already compiled; score configuration | Content/path scoring, incoming score, result-set state                         |
| `-first`, `-top`, `-shard-status`                              | Limit/selection decoding                        | Counters, suspension, replay decisions and fuzzy score                         |
| `-print*`, `-printf*`, `-fprint*`, `-ls`, `-fls`               | Output format/cells                             | Output order, files, fields and conditional reads/errors                       |
| `-grep`, `-diff`, `-cmp`                                       | Matcher/template/style/diff options             | Content, context output, comparison/reference reads                            |
| `-hash`, `-hasheq`                                             | Algorithm/encoding interpretation               | Content reads, output and verification bookkeeping                             |
| `-collect`                                                     | Collection name                                 | Coordinator-owned retained entries and limits                                  |
| `-exec*`, `-ok*`, `-capture`, `-capturedir`                    | Static argv/template portions                   | Dynamic fields, confirmation, execution, capture, batching and dry-run unknown |
| `-delete`                                                      | None                                            | Controlled mutation, failure reporting, archive-member deferral                |
| `-prune`, `-quit`                                              | None                                            | Traversal control even if a later predicate is false                           |

Further preparation must preserve when failed observations occur. In particular, username lookup,
reference-file stat, field expansion and conditional content reads cannot silently move to startup.

### Indexed worker matcher candidate

`PreparedExpression::MakeWorker` assigns one dense slot per path/content regex node. A worker
owns its forked mutable regex state and borrows its prepared expression; the prepared expression
and source AST must outlive it. Moving either owner preserves allocated storage identities.
The API binds each worker to its expression, so callers cannot accidentally evaluate a slot vector
against another program. Worker evaluation calls indexed regex callbacks directly, without
`WorkerMatchers`' per-predicate map search. Serial prepared evaluation retains the immutable
compiled matcher; absent matchers remain false, and a failed fork retains the original fallback.

The existing tree/map path stays available. Regex kernel benchmarks compare it with the indexed
worker path, including worker construction in preparation timings. Content and path regexes are
measured separately; the full matrix has 333 cases. Owned-slot capacity is reported separately
from backend allocations, which are not estimated. Named oracle cases include the worker path,
plus persistent independent workers, captures, source/worker moves, and unbound matcher cases.
No production parallel driver has switched yet. Match-output/grep's separate worker program and
remaining dynamic operand families retain their current handling pending their own audit.

## EP04 contiguous program pilot

`ExpressionProgram` lowers every boolean operator into a contiguous stream of predicate calls,
conditional jumps, negation, and saved-value XOR operations. Each instruction retains its original
preorder source ID. Lowering uses an explicit stack, and each worker reserves its maximum boolean
scratch once; repeated entries do not grow that storage. Predicate instructions call the prepared
worker directly and share the reference evaluator's metadata, safety, invalidation, and control
handling. No per-instruction flag-name lookup is introduced.

The pilot compares an opcode switch with bound instruction-function dispatch using the same
instruction layout. Holding layout constant isolates dispatch cost; the table currently stores both
opcodes and function pointers, so it is not yet a minimal representation for either final choice.
Worker regex state remains independent. Source/program/worker storage ownership is explicit and
move-safe under the documented lifetime order.

An evaluation context with fuzzy-score collection or deferred replay uses the prepared recursive
executor for the whole expression and reports `used_fallback`. Tests assert that ordinary boolean,
metadata-failure, dry-run, mutation-denial, regex, long-chain and nested-XOR cases actually use the
program. Separate tests verify the reported fallback. This is the planned EP04 transitional
boundary; EP05 must implement or justify the remaining contexts and integrate the driver.

Native CI benchmarks retain every prior variant, add both program dispatch choices, and include
AND early misses plus OR early hits and complete misses. The expanded core matrix has 666 cases.
Reached-predicate counts account for short-circuit cases, and program timing rejects unexpected
fallback during its untimed check. Instruction/storage counters and preparation timings remain
separate from execution time. No production selection or speedup claim is made before measurement.

## EP05 stateful execution candidate

Scored and deferred contexts now use an iterative continuation program over the same dense
prepared nodes. A worker reuses enter/left/right frames, carrying left outcomes and AND input
scores without recursive expression calls. The ordinary boolean instruction stream remains
separate so optional replay machinery does not tax every cheap predicate. This is an execution
representation choice, not a new user-visible mode.

Child outcomes are memoized under their original source identities only after completion.
Unknown or suspended outcomes unwind worker scratch without caching unresolved ancestors;
completed effects remain in the existing sparse per-entry memo. Replay therefore preserves
exactly-once actions, counters and source frontiers. Fuzzy-only OR still evaluates both branches
when a score consumer exists. AND combines incoming scores, NOT/NAND/NOR/XNOR discard scores as
before, and XOR preserves the selected branch's score. The caller's score observer and incoming
score are restored on every exit.

The state stack reserves only on its first use and is reused thereafter; ordinary boolean
contexts do not allocate it. It is bounded by the prepared node count and is never cloned into
suspended entries. A future compact depth-based reservation can reduce this conservative bound,
but requires measurement. The full evaluator suite and observation oracle include iterative and
program variants, with persistent-worker replay, storage stability, unknown cleanup, and all
operator score combinations. Two score-consuming benchmark families extend the matrix to
756 cases. CI must validate behavior and measure costs before production integration, which
remains a separate EP05 step. EP06 optimization and EP07 whole-engine qualification remain open.

## Implementation qualification boundary

The internal `RunFind` executor argument now permits direct whole-engine comparisons of the tree,
bound, prepared and both program layouts. It is unavailable in CLI/configuration. Preparation
runs after the original whole-command validation and traversal/output decisions. Each matcher
worker owns its execution state; deferred replay uses the coordinator worker and preserves sparse
per-entry memoization. `//xff/engine:expression_execution_test` checks the integrated paths against
the reference, and `//xff/engine:expression_run_benchmark` includes preparation and teardown for
0 through 10,000 files. The default remains the reference until qualification selects a winner;
this integration does not complete EP06 or the production decision.

### EP06 candidate: independently measurable instruction rewrites

`ExpressionProgram::Prepare` now accepts internal `ProgramOptimizations` for constants, jump
threading and branch fusion. None is enabled by default. The test suite runs both dispatch forms
with each pass independently and their combination. The whole-engine qualification selector adds
`kProgramOptimized`; it is not a CLI/config flag or the production default.

- Registry `constant_truth` describes audited total, effect-free literal truth. A compile-time
  guard rejects contradictory effect/metadata/control declarations. Only `-true` and `-false`
  declare it. Bottom-up facts retain original preorder source IDs. A constant left side may remove
  an unreachable right side; an unknown left side is never discarded because the right is constant.
- Jump threading follows an existing jump directly to another jump testing the same truth value.
  Compiler-generated edges go forward, so a reverse pass is bounded and needs no iterative search.
- Fusion combines a predicate with its immediately following conditional jump only when no other
  edge enters that jump. Unknown/error/suspension exits still precede the conditional. Compaction
  remaps every destination, including the terminal position, and preserves predicate source IDs.
- The original AST remains available to whole-command validation, safety preflight, implicit output,
  traversal policy, scoring and deferred replay. The latter two still use the separately qualified
  stateful executor; these boolean rewrites do not claim to improve that path.
- `OptimizationStats` records each pass's input/output instruction counts and rewrite count;
  optional timing is off for ordinary preparation. Connecting these records to the selected
  production executor's `--explain` output remains part of EP06/EP07, not a completed deliverable.

The native kernel matrix includes each pass and the full combination beside every earlier executor.
The whole-engine matrix adds the combined candidate. Keep passes individually only after current
Linux/macOS measurements and differential tests establish their cost and semantic equivalence.
No predicate reordering, speculative metadata reads or common-subexpression elimination is enabled.

The program oracle also exhausts the 902 syntax trees containing up to two logical operators
(all seven binary operators and unary NOT) over literal truth values. A fixed `mt19937_64` seed
generates 32 larger mixed trees with constants, scalar/name/regex/fuzzy predicates and recording
output. Every optimizer/dispatch combination runs both scored and unscored over three entry
sizes. Failures include the exact token sequence and sample index for reproduction. These
generated cases complement explicit failure, replay and effect-order tests; they do not replace
the remaining full-run configuration/archive qualification.
