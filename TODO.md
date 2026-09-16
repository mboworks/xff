# TODO

Actionable roadmap and deliberately deferred ideas. Completed implementation records live in
[`docs/history-roadmap.md`](docs/history-roadmap.md); other resolved design records and investigations
live in [`docs/history.md`](docs/history.md).

## Completed: summary and comparison audit

- Reject invalid reduction controls before traversal/actions; make top-limit reset deterministic.
- Refuse silently ignored summary formats and support aligned comparison summaries consistently.
- Document default scopes, category aliases, independent output selection, ordering, formats, and
  count/byte reconciliation; add the summary design guide to the published documentation.
- Add end-to-end reconciliation and invalid-control coverage alongside focused engine regressions.
- Measure the follow-up PR's action-cache reuse against the main caches seeded after #840.

## Completed: Cache storage reporting and MSan allowance

- Retain up to 4 GB uncompressed / 1.2 GB compressed for MSan.
- Record per-upload measurements and report compressed/uncompressed sizes, eviction counts,
  and repository-wide usage against 10 GB in main's final job.

## Completed: Immediate main cache retirement

- After upload, verify the exact replacement and compressed size before deleting older main
  generations in the same OS/architecture/configuration. Keep concurrent newer generations.
- Retain workflow-completion and scheduled cleanup as fallbacks; monitor #842 main uploads for
  actual compressed sanitizer sizes and total repository storage.

## Completed: CI compiled-output cache lifecycle

- Replace frozen cache keys with fresh main-run generations and configuration-specific restoration.
- Preserve the cache budget: bound uploads, retain the newest usable generation, and remove
  closed-PR and tag-scoped entries. PR and tag runs are restore-only.
- Add GCC output caching, share release/main build caches, and test retention and upload behavior.
- Measured PR #841: normal Linux Clang had 3,768 disk hits, GCC 2,724, and documentation 718.
  Sanitizers restored the correct namespaces but age-based eviction discarded most early outputs.
- Follow-up: evict largest blobs first, allow 2.6 GB uncompressed for sanitizer jobs, and cover deep-fuzz and release
  reference-generation cache routing. Verify sanitizer reuse after main seeds the revised caches;
  consider per-configuration budgets using compressed upload sizes if retention remains inadequate.

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

## Safety follow-up sequence

1. Discuss whether pager-command allowlisting is useful and define its trust and configuration
   model before implementing it. Pager commands remain separate from execution blocks and dry-run.

The configuration audit findings and verification map are recorded in
[the audit implementation record](docs/history.md#configuration-validation-and-documentation-audit).
PR #821 carries the fixes; final CI and release-artifact verification gate release readiness.

The safety model and directory-scoped permissions are tracked in
[the implementation record](docs/history.md#directory-scoped-safety-controls).

## Active engineering priorities

1. **Fuzzing.** The parser-through-evaluator, matcher, template, PHAR, and ASAR targets now have
   committed seed corpora, semantic invariants, ordinary-CI replay, and automatically discovered
   bounded CI campaigns. Continue with shard, stronger matcher/template, and regex targets; every
   new target is picked up by the campaign driver. Configuration coverage
   includes the shared INI/xffrc argument grammar, quoting and comments, policy gating, precedence
   resolution, and arming invariants.
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

## Coverage report ordering

- [x] Pin `main`, then interleave PRs and releases newest-first by merge/tag position on main.
- [x] Refresh retained PR metadata after merges, preserve report replacement identity, and test
      non-numeric ordering, annotated/lightweight tags, unmerged reports, and delayed CI runs.

## Summary accounting follow-up

- [x] Add count and byte percentages to summary tables, using full pre-`--top` totals.
- [x] Include directories, symlinks, special files, and type changes in comparison accounting;
      distinguish entry equality from subtree equality and test empty-directory differences.
- [x] Add comparison combined sizes and size percentages: each result counts once, while bytes
      include both sides. Use entry metadata sizes, never recursive directory sizes.
- [x] Combine selected categories into one table with left/right column groups, including missing sides
      and zero sizes;
      use each side's selected population as the denominator and preserve JSONL attribution.
- [x] Default ordinary summaries to paired comparison scope under `--compare`; retain `all` otherwise
      and document explicit overrides, option-order independence, and summary-driver requirements.
- [x] Render ordinary and comparison summaries as Markdown for `--format=md|markdown`, reusing
      listing cell escaping and testing validation, fixed schemas, and table boundaries.
- [x] Add count/size reconciliation tests, update registry/topic/generated help, and prepare a PR.

## Summary presentation follow-up

- [x] Separate each summary note from the following scope in console and Markdown output.
- [x] Add grouping-specific Markdown headings and bullet lists for scope/root labels and notes;
      preserve headerless fragments.
- [x] Align exact byte values with scaled integer digits using fixed SI/IEC unit widths.
- [x] Test mixed/repeated/scoped summaries and size alignment at zero and nonzero precision;
      update help, generated reference, and changelog, then prepare a PR.

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
