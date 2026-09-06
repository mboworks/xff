# TODO

Actionable roadmap and deliberately deferred ideas. Completed implementation records live in
[`docs/history-roadmap.md`](docs/history-roadmap.md); other resolved design records and investigations
live in [`docs/history.md`](docs/history.md).

## Project constraints

### Minimum Bazel version

The supported minimum is **Bazel 9.1.1**. Development and CI use the newer release pinned in
`.bazelversion`; the minimum is a support floor, not a second CI configuration. Bazel 7 is no longer
supported, and no Bazel 8 patch release is part of the support contract. Lowering the minimum requires
an explicit decision.

### Coverage policy

Every current coverage group meets the shared high boundaries. Category overrides may make enforcement
stricter; there are no lowered onboarding overrides. The JSON policy remains the single source of truth
for enforcement and presentation.

## Active engineering priorities

1. **Fuzzing.** The parser-through-evaluator, matcher, template, PHAR, and ASAR targets now have
   committed seed corpora, semantic invariants, ordinary-CI replay, and automatically discovered
   bounded CI campaigns. Continue with shard, stronger matcher/template, and regex targets; every
   new target is picked up by the campaign driver. Configuration coverage
   includes both file grammars, policy gating, precedence resolution, and arming invariants.
   Expression-evaluation harnesses must exclude safety-classified descriptors and use a mutation-
   refusing in-memory filesystem; `--safe` text alone is not an isolation boundary.
2. **Performance evidence.** The parser now has a reproducible manual benchmark alongside the existing
   similarity and near-duplicate measurements. Extend the evidence with traversal and memory
   measurements before optimizing. Keep fuzz time/resource guards separate from stable performance
   measurements.
3. **Near-duplicate grouping design.** The benchmark/design spike now measures exact Jaccard
   verification behind scalable candidate generation; settle the grouping architecture, configuration,
   and core-versus-extra boundary before shipping a user-facing operation. False positives may reach
   exact verification but must never reach emitted clusters.
4. **Coverage overview ordering.** Keep `overall` first, then render the program categories
   alphabetically, followed by the extension categories alphabetically. Preserve this ordering in
   generated and published reports.
5. **Detailed coverage gaps.** Raise line and function coverage for
   `program-command-grammar/xff/registry/` and branch coverage for `program-execution/xff/exec/`
   until every range is green in the detailed LCOV report.
6. **Libarchive malformed-input boundary.** The archive fuzz campaign must keep malformed records
   from reaching known unsafe third-party parser paths. Revisit the pinned libarchive version and
   format-specific validation before broadening beyond structurally valid tar inputs; report any
   confirmed dependency defect through its private security channel when appropriate.
7. **VFS host-adapter split.** Extend the capability-oriented `vfs::FileSystem` seam for every file
   operation xff needs, then move POSIX and standard-library file primitives behind a minimal local
   host adapter. Consumers must not open or mutate host paths directly; retain annotations only at
   the immediate adapter boundary.

## Design required before implementation

1. **Reassembled shard contents (shards v2).** Decide whether the logical whole replaces or accompanies
   physical shards for matching and actions, how incomplete and duplicate sets read, and which path and
   metadata the synthetic entry owns. The shipped v1 already covers display, statistics, completeness,
   duplicates, and `-shard-status` matching; do not infer v2 semantics from it.
2. **Near-duplicate grouping.** The candidate-generation spike in
   [`docs/design-near-duplicates.md`](docs/design-near-duplicates.md) selects hashed-shingle postings
   followed by exact Jaccard verification as the first implementation. Decide the user-visible
   clustering rule for non-transitive matches and measure a memory-bounded production representation.
3. **PHAR structural exposure.** A format-defined file such as `.phar/stub.php` remains file-like, with a
   stored member winning name conflicts. Decide whether a presentation option may hide such parts.
   Aliases, serialized metadata, and signatures are not files and remain unexported unless a coherent
   separate metadata model is designed.

## Deferred refinements

- Filesystem-name behavior: Unicode normalization and Linux per-directory case-fold detection.
- Per-summary-sink modifiers.
- Histogram time buckets, explicit bucket edges/counts, and per-line template measures.
- Text-flavor mixed-line-ending policies.
- Post-1.0 behavior migration through `--unstable=NAME` if a concrete unstable spelling needs it;
  do not build the rejected general `--feature` registry speculatively.
- C++ header modules after the hermetic toolchain supplies real compile/use actions.
