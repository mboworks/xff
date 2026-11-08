# Performance candidate completion audit, 2026-11-07

The performance goal is not complete. A completed experiment, an accepted private
handoff, a published PR, a successful PR run and a successful merged-main run are
different evidence states. Preserve all candidates and rejected implementations;
do not replace unfinished parent requirements with whichever child has passed.

## Audit boundary

GitHub's main ref was read directly for this audit and is
`77aa9ef9bc9475adcbdd37cf3004143a96f03060` (#1129). Its actual Test workflow
`42359504322` succeeds. The table reconciles that tree's canonical TODO and
performance analysis with the frozen, separately published follow-ups through
#1140. Linked reports document the original populations and their limits. This
audit does not rerun every historical benchmark, independently reprove every past
performance estimate, or turn old green checks into current PR-context checks.

The dedicated orchestration ledger and GitHub remain authoritative for the moving
PR graph. A snapshot here is not a merge schedule, check substitution or readiness
claim. Required current-head integration and resulting main health are still
required for every published implementation.

## Original candidate families

The original O01-O22 inventory is retained. A selected subset in a partially
completed family is useful progress, not completion of the full parent design.
An evaluated, rejected concrete implementation need not be reimplemented just to
obtain a favorable result; a new mechanism needs a new hypothesis and population.

| Candidate | Documented decision                                                         | Evidence                                                                                                                                             | Remaining boundary                                                                         |
| :-------- | :-------------------------------------------------------------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------- | :----------------------------------------------------------------------------------------- |
| O01       | Selected: path-owned collection text                                        | [Report](benchmarks/collection-text-sharing-20261101.md)                                                                                             | Owned paths, fallback copies and budgets remain part of the contract.                      |
| O02       | Selected: prepared owned/simple/folded glob paths                           | [Report](benchmarks/folded-ascii-globs-20261105.md)                                                                                                  | Native/complex/locale fallbacks and their measured costs remain.                           |
| O03       | Selected: fuzzy ceiling and worker scratch                                  | [Report](benchmarks/fuzzy-score-workspace-20261102.md)                                                                                               | Initial exact capacity chosen; the 64-character minimum was rejected.                      |
| O04       | Selected: lexical field views                                               | [Report](benchmarks/lexical-field-views-20261102.md)                                                                                                 | Preserve native spelling and lifetime/fallback behavior.                                   |
| O05       | Evaluated: generic field lookup changes rejected                            | [Report](benchmarks/field-lookup-controls-20261102.md)                                                                                               | Future prepared bindings need a different measured mechanism.                              |
| O06/O07   | Selected: range overwrite and cursor-owned reuse                            | [Report](benchmarks/cursor-owned-read-buffers-20261102.md)                                                                                           | One initial clear remains selected; O32 is a distinct legacy API.                          |
| O08       | Selected: typed atomic task slots; cleanup variant rejected                 | [Report](benchmarks/directory-task-slots-20261102.md)                                                                                                | No new admission threshold or global executor is implied.                                  |
| O09       | Selected: packed owned snapshots                                            | [Report](benchmarks/packed-visit-snapshots-20261102.md)                                                                                              | No dangling views into movable short strings.                                              |
| O10       | Selected: bounded retained result capacity                                  | [Report](benchmarks/bounded-match-results-20261102.md)                                                                                               | A retention limit is not an in-flight output-byte limit.                                   |
| O11       | Selected: useful warm participation                                         | [Report](benchmarks/useful-match-workers-20261102.md)                                                                                                | Worker creation and failed growth-throttling policies stay separate.                       |
| O12       | Evaluated: direct evaluator storage rejected                                | [Report](benchmarks/direct-evaluator-workers-20261102.md)                                                                                            | A different typed plan remains an EP design, not an adopted win.                           |
| O13       | Selected: requested derived-value handoff                                   | [Report](benchmarks/derived-content-handoff-20261102.md)                                                                                             | Only requested existing data crosses the stage boundary.                                   |
| O14       | Partial: default snapshot reuse selected                                    | [Report](benchmarks/immutable-default-catalogs-20261102.md)                                                                                          | First-parse generated flat/offset tables and per-run MIME binding are not qualified.       |
| O15       | Partial: diff/hash invariant operands selected                              | [Report](benchmarks/prepared-hash-operands-20261102.md)                                                                                              | Remaining EP03/EP07 operand/template acceptance is not proved by these subsets.            |
| O16       | Evaluated: compact retention selected; paired-frontier prototype rejected   | [Report](benchmarks/comparison-frontier-rejection-20261106.md)                                                                                       | A completed-frontier redesign remains open; both production inventories are retained.      |
| O17       | Evaluated: all six shared-stage executor variants rejected                  | [Report](benchmarks/shared-stage-workers-20261107.md)                                                                                                | Keep specialized persistent pools; old draft PRs are not qualified replacements.           |
| O18       | Open: full byte-bounded stage overlap not implemented/qualified             | [Report](https://github.com/mboworks/xff/blob/3e50f36bde466ef576e03510780c3515c2c474ca/docs/benchmarks/queued-read-cancellation-20261107.md)         | Queued-read cancellation trials are separate and do not close pipeline overlap.            |
| O19       | Partial: buffers/ranges/observations selected; seek/locator trials rejected | [Report](https://github.com/mboworks/xff/blob/912386016f1e42874b2060537bc1e92bea03e611/docs/benchmarks/immutable-zip-locator-evaluation-20261107.md) | Indexed/serial decoder admission and streamed ZIP correctness remain open.                 |
| O20       | Selected: optional backend bulk metadata and native observations            | [Report](benchmarks/native-metadata-followups-20261105.md)                                                                                           | Direct-vector/scan/cleanup trials rejected; further demand planning requires new evidence. |
| O21       | Partial: literal-reference and exact shingle verification selected          | [Report](benchmarks/similarity-reference-reuse-20261103.md)                                                                                          | Dynamic source identity, invalidation and exact candidate indexes remain unqualified.      |
| O22       | Partial: record/cell/template/rg transaction reuse selected                 | [Report](benchmarks/transactional-rg-records-20261107.md)                                                                                            | Remaining reusable structured storage and streaming machine representations stay open.     |

## Later concrete candidates and integration

- O23's native PGO experiment is retained as research. Portable training,
  source/compiler/architecture provenance, trust, regeneration and source-only
  release fallback are required before adoption.
- O24's actual-library stream synchronization audit establishes no useful runtime
  switch for the currently pinned libc++; do not add a placebo call.
- O25 direct final-string language decoding is merged in #1128. This does not remove
  first-language JSON parsing or build the O14 generated-table representation.
- O26 empty deferred time formats, O28 observation-mode specialization, O29 borrowed
  immediate archive keys and O30 owned archive input have separate documented
  decisions. O28 is rejected; O29/O30 ownership changes do not license a global
  unbudgeted archive index.
- O27's three insertion proposals are measured and unselected, including the hinted
  insertion report merged in #1129. Do not describe the hinted form as unmeasured
  after that report or treat an older empty-map microbenchmark as full parsing cost.
- O31's full policy is unselected. The template-only subset is published in #1134
  and still requires current-base integration and eventual main verification.
- O32's legacy extraction-buffer proposal is published in #1140. Its one-line
  change, both-host focused release/sanitizer qualification and raw matrices are
  preserved. Mac gzip's repeatable 64 KiB cost remains explicit. This audit does
  not count an open PR as merged or a legacy API result as a general CLI gain.
- The exact known-length JSON list reservation is also measured on both hosts and
  unselected. Keep [both harness versions and all observations](benchmarks/language-list-reserve-20261107.md).
  Its six focused release suites and final 16-case matrix do not establish first-load
  generated catalogs, allocation counts, a full suite or whole-command gains.

## Requirements that remain genuinely open

1. **O18 pipeline ownership and output admission.** The shipping matcher waits for
   an entire batch; O22c2h transfers a validated per-file rg transaction but does not
   cap in-flight output or overlap the next batch. Implement an ordered completion
   protocol and actual admission before output allocation. A delayed head must not
   lose credit to later entries. Define one explicitly oversized head transaction
   or another transactional storage contract; do not truncate results, spill through
   an unauthorized write path, or count worker-held strings as free memory.
2. **O19 archive admission and correctness.** The seek and cached-locator prototypes
   were actually implemented and measured; they are not unfinished trial runs.
   Retain their repeatable first-read costs. A different immutable-identity/index
   design must preserve all visible ZIP members, methods, exact sizes, shared
   budgets and sequential-source fallbacks. The streamed descriptor/bzip2 defects
   in #1139 are still defects; a decoder-to-count shortcut fails method/corruption
   coverage and is not a completed fix.
3. **O14 first construction and O15 remaining preparation.** Default restoration and
   direct Brotli string output reduce different costs. Generated immutable tables
   must preserve append-only registration, user layers, aliases/overlaps, errors,
   old views, binary size and loader relocation costs. The original EP03/EP07
   acceptance inventory remains authoritative for operands not covered by the
   selected diff/hash implementations.
4. **O21 dynamic representations.** A fresh source observation and an immutable
   literal reference have different contracts. Caches require explicit source
   identity/invalidation and retained-byte ownership; indexes must preserve exact
   verification and the requested grouping semantics.
5. **O22 and metadata follow-ups.** Existing output buffers, callback lifetimes and
   cheap metadata paths are real implementations. Further storage or bulk-read
   changes need an observed cost, with reentrant callbacks, view lifetimes, directory,
   followed-link and mount behavior preserved. Do not repeat rejected vector or
   batching wrappers without a changed mechanism.
6. **Publication and merged qualification.** Current public implementations must
   still finish their own checks, synchronize and merge in the authorized sequence.
   Verify resulting main jobs and actual hosted results. Queue retention is bounded;
   already canceled main campaigns need explicit reconciliation/backfill, and the
   publication snapshot/compaction fixes still need hosted evidence. Preserve all
   release/main benchmark data and coverage aggregate history.

## Next O18 implementation and decision gates

The source audit identifies `ParallelMatch::Match`, its owned entry/result banks,
`RenderRgOutput`'s validated transaction, and the coordinator's `flush_matches` as
the concrete boundaries. Replacing the existing pools with another generic executor
is not a prerequisite; O17's tested regression must not be reintroduced.

- Separate batch submission from ordered entry completion. Keep persistent leaf
  workers and coordinator publication. Two explicitly owned banks can let traversal
  prepare a bounded next input while workers finish the prior one; callbacks must
  not observe a result or filesystem owner after its bank is recycled.
- Account for retained input text, entry/result slots and output storage before
  admitting work. Bounded entry counts or capacity retained between batches are not
  a byte-bound proof. Track the peak and oversized-head contract independently of
  active decoder/read budgets.
- Preserve per-file late read/binary suppression. A completed unvalidated partial
  string cannot be emitted just because it is first. Any chunked transaction must
  retain that validation contract and avoid a compulsory full-size flattening copy.
- Drain before incompatible archive/mount work, effects, deferred/capture barriers,
  traversal error publication and owner destruction. Early result limits and stops
  need exact read/effect traces; do not speculate beyond the old behavior silently.
- Wake blocked producers and consumers on failure/cancellation before joining.
  A leaf worker must never await work on its own saturated executor. Test delayed
  heads, full later records, giant/empty/no-output files, preparation and read errors,
  tiny tails, root/mount lifetime and repeated reuse.
- Measure unchanged output and worker-only controls on both hosts first, then
  native/in-memory small/large one/three-worker workloads, first output, elapsed,
  actual queue bytes and peak memory. Preserve every slower case and identical-image
  controls. Reject a design that merely shifts retention or adds dispatch overhead;
  do not hide costs with arbitrary count/time thresholds.

These are next implementation gates, not claims that a pipeline exists. The goal
remains active until every required candidate disposition and promised integration
is proved against current source and external state.
