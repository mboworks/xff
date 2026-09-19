# TODO

## Completed: MSAN help-rendering timeout

- Give `//xff/cli:help_render_test` a target-specific 300-second timeout in PR #878 after
  MSAN exceeded its 60-second budget while progressing through the suite.
- Keep other test budgets unchanged; review the new MSAN runtime before deciding on profiling
  or splitting the broad help tests.

## Help follow-up: suggestions, width, and spacing

- [x] Rank fuzzy flag suggestions and retain up to three candidates (F02).
- [x] Add automatic width caps, defaulting to `auto:110`, with a 40-column minimum (F04).
- [x] Normalize help block spacing while preserving verbatim examples.
- [x] Support persistent user-INI width preferences, including help and CLI precedence (F03).

## Completed: Structured-output composition (audit S01)

- Emit typed comparison-status and built-in grep JSONL records.
- Reject unsupported comparison formats before either side can execute actions.
- Preserve authored templates and child stdout; document their output contract.
- Test complete mixed producer streams and publish the producer/format matrix.
- Preserve B05/B06 summary identities and the documented histogram schema.
- Preserve non-UTF-8 values losslessly with tagged base64 objects; verify portable virtual-path fixtures.

## Completed: Archive destination collisions (audit S02, PR #865)

- [x] Share a pure destination planner between the public API and direct writer.
- [x] Reject normalized duplicates before output creation; retain input order for first-wins.
- [x] Test existing-output preservation and first-wins payloads in tar and zip.
- [x] Connect duplicate policy to CLI, full config files, and dry-run validation.
- [x] Carry named root identity through parsing/traversal and enforce distinct root names.
- [x] Keep named roots command-line-only like positional operands; reject them in every INI source.
- [x] Validate file/directory prefix conflicts and deterministic root/traversal ordering.
- [x] Complete registry help, generated reference, integration tests, and affected-file lint.

## Completed: Compact help overview (audit S06)

- Replace the exhaustive default catalogue with command structure and task-oriented help routes.
- Preserve complete inventory and detailed reference under `--help=all` and `--help=long`.
- Validate terminal widths and executable examples; structure comparison help and stack narrow policy tables.

## Completed: Comparison exit status (audit S11)

- Explain search-style comparison exit codes in focused help and exit flags.
- Test output selection, summaries, filtered populations, and operational errors.
- Regenerate the reference and validate focused help, registry documentation, and changed-file checks.

## Completed: Order and limits guide (audit S15)

- Compare expression, listing, reduction, traversal, and sorting controls in one table.
- Verify collection placement, explicit actions, full denominators, fuzzy selection, and archive populations.
- Explain parallel ordering, retained state, and partial comparison populations.

Actionable roadmap and deliberately deferred ideas. Completed implementation records live in
[`docs/history-roadmap.md`](docs/history-roadmap.md); other resolved design records and investigations
live in [`docs/history.md`](docs/history.md).

## Completed: Unambiguous summary totals (audit B06)

- Add `is_total` to ordinary, scoped, and comparison-result JSONL rows.
- Preserve literal group keys, including total, empty strings, and (none).
- Quote ambiguous data labels in human-readable tables without adding a column.
- Validate multiple requests and comparison scopes before publication.

## Completed: JSONL summary identity (audit B05)

- Identify all ordinary, scoped, and comparison summary rows by request, grouping, and scope.
- Preserve exact template strings and request indices across filtering and output reordering.
- Include root identity and verify demultiplexing with a JSON parser.
- Update output expectations, generated help, and schema documentation; verify against merged main.

## Completed: Histogram output formats (audit B04)

- Render Markdown histograms as named bucket/value tables, with numeric alignment and shared escaping.
- Reject unsupported histogram formats before traversal or actions, including comparison mode.
- Cover repeated histograms, empty results, header suppression, and composition with summaries.

## Completed: Comparison summary request deduplication (audit S03)

- Preserve scoped totals while coalescing the comparison table requested by the shorthand and bare summary.
- Test both argument orders and retain the extension-summary composition regression.

## Completed: Install-first README (audit S07)

- Put published binaries and a four-command task guide before feature matrices.
- Keep Bazel instructions under building from source.
- Describe summary bytes as apparent sizes, not allocated disk usage.

## Completed: Release archive documentation (audit B08)

- Describe the separate lean/full archives and their matching debug symbols.
- Link release installation instructions and clarify size optimization plus ThinLTO.

## Planned: Revisit duplicate destination detection with MBO's string interner

- Once MBO's string interner is available, evaluate sharing normalized pack/output destination
  paths between planned entries and the collision index instead of storing separate string copies.
- Preserve exact-duplicate and file/directory ancestor/descendant conflict detection, normalization,
  first-wins ordering, source-path diagnostics, and validation before opening output. Interned IDs
  alone do not replace the ordered or prefix-aware index needed for hierarchical conflicts.
- Verify string-view lifetime guarantees and benchmark memory and runtime on large, mostly unique
  path sets, deeply nested paths, and duplicate-heavy inputs before choosing an implementation.
- Reuse the archive collision regression tests from PR #865 to verify unchanged behavior.

## Utility and usability audit

Work through the claims in [the 0.6.0 review](docs/review-0.6.0.md), one claim per PR.
The review separates reproduced defects from design proposals and records their acceptance criteria.

## Completed: Markdown cell normalization verification (audit B07)

- Disprove the reported newline-padding defect by measuring rendered pipe positions.
- Guard normalization-before-measurement with LF, CRLF, and escaped-pipe regression cases.

## Completed: Histogram width (audit B03)

- Require positive integer widths consistently on the CLI and in complete INI files.
- Reject invalid widths before expression actions, including comparison walks.

## Completed: Buffer limits (audit B02)

- Validate row windows and byte budgets on the CLI and in complete INI files.
- Reject scaled-count overflow and invalid limits before actions execute.

## Completed: Capture consumers (audit B01)

- Declare capture binding, argument field expansion, and implicit-output behavior in registry metadata.
- Discover capture references through parsed templates in the actual consuming fields and actions.
- Apply duplicate/unused binding checks and implicit-output behavior consistently to directory captures.
- Cover printf/file-printf, grep templates, execution, chaining, comparison targets, and columns.

## Completed: Binary sniff documentation (audit B09)

- Document the binary sniff window as exactly 8,000 bytes in content and type help.
- Test NUL bytes immediately before and at the boundary, and beyond it up to 8 KiB.

## Completed: CI cache capacity after INI changes

- Raise the shared uncompressed cache limit to 1 GB and ASan to 3 GB using main-run measurements.
- Retain the compressed cleanup ceilings and test the independent budget settings.

## Completed: Comparison summary scope columns

- Make scopes collect ordered column groups; add side totals with `left`/`right` aliases and canonical output labels.
- Count category pairs once with combined sizes; preserve side totals, ordering, overlap, and percentages.
- Update plain/Markdown/JSONL output, help, design examples, and regression coverage.

## Completed: INI environment substitutions

- Add braced environment values, literal defaults, and required-value diagnostics to the shared INI lexer.
- Preserve quoting, argument boundaries, section names, and field-renderer `%{...}` syntax.
- Test complete configuration files, invalid values, and scoped mutation enforcement after root expansion.
- Document caller-controlled environment trust and literal roots for fixed administrator boundaries.

## Completed: summary and comparison audit

- Reject invalid reduction controls before traversal/actions; make top-limit reset deterministic.
- Refuse silently ignored summary formats and support aligned comparison summaries consistently.
- Document default scopes, category aliases, independent output selection, ordering, formats, and
  count/byte reconciliation; add the summary design guide to the published documentation.
- Add end-to-end reconciliation and invalid-control coverage alongside focused engine regressions.
- Measure the follow-up PR's action-cache reuse against the main caches seeded after #840.

## CI preparation profiling

- Verified #847 main: zero cache evictions, 184.7-second fuzz campaign phase, 2.69 GB cache inventory.
- Capture coverage and initial fuzz build traces to attribute the remaining four-minute preparation
  delay before changing toolchain fetching or packaging. See `docs/ci-performance.md`.

## Completed: Start long CI jobs immediately

- Remove pre-commit/Trunk scheduling dependencies from coverage and fuzz; keep every check in `done`.

## Completed: Parallel fuzz campaigns

- Build all campaigns together, prepare launch scripts sequentially, and run with CPU/RAM bounds.
- Preserve time per target, isolated corpora/crashes/logs, throughput statistics, and failure cleanup.
- Compare CI wall time and mutation throughput after main runs the parallel scheduler.

## Completed: Coverage cache capacity

- Raise coverage's cache allowance to 1.5 GB uncompressed and 1 GB compressed.
- Check the next main report for retained outputs and upload size.

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

### Audit B12: effective explain flavor values

- [x] Reproduce config flags absent from the current flavor table.
- [x] Share the fully composed command between explain and execution; verify defaults, selectors,
      explicit files, skip policy, literal operands, and no action execution.

### Audit S04: static template validation

- [x] Reject overflowing numeric capture indices without signed arithmetic overflow.
- [x] Preserve parsed syntax/name diagnostics on compiled templates, distinguishing absent values.
- [x] Validate native qualifiers through field renderers and the shared hash vocabulary.
- [x] Validate rewrite/extraction patterns, replacements, flags, and reducer syntax.
- [x] Share placeholder parsing with printf consumers, including quoted closing braces.
- [x] Check active template consumers before traversal or actions, including comparison mode and explain.
- [x] Update help and pass full-config, before-mutation, summary, and engine regression checks.
- [x] Verify generated reference and all 24 affected-header consumers; complete local commit hooks.

### Audit S19: fuzz filesystem isolation

- [x] Supply an in-memory VFS for every fuzz-generated path and abort on attempted mutation.
- [x] Assert that content fields read synthetic bytes, with absolute and traversal-path corpus cases.
- [x] Verify corpus replay and local lint; audit every mutation entrypoint for fail-fast rejection.
- [x] Make expression-evaluation fuzzing abort on all write and controlled-mutation entrypoints.
- [x] Exercise both staged release executables with a cross-feature smoke corpus before upload.
- [x] Cover NUL, escaped plain, and TSV record boundaries in the release smoke corpus.

### Audit S05: effective safety explanation

- [x] Share safety resolution between execution and inspection; retain file/line/section origins.
- [x] Show final capabilities, mandatory and profile causes, modes, categories, and directory roots.
- [x] Exercise system blocks, user overrides, skipped sections, expansions, and nested roots with full INI fixtures.
- [x] Complete changed-file lint, affected-header-consumer checks, and generated-reference verification.

### Audit S17: configuration inspection

- [x] Preserve named declarations and structured validation reasons for explain output.
- [x] Show skip/selection state, declared behavior, and physical application origins.
- [x] Add personal, project, mandatory-policy, and environment-root recipes.
- [x] Verify complete CLI configurations, generated reference, and changed-header consumers.

### Audit S18: spelling and placement diagnostics

- [x] Derive conservative spelling hints and short-global placement hints from registries.
- [x] Verify parser boundaries, CLI errors before actions, normal hooks, and changed-file clang-tidy.

### Audit S08: compact comparison summaries

- [x] Group console scope headings and retain one table at narrow widths.
- [x] Inject width from parsed CLI globals and preserve JSON accounting.
- [x] Test exact 80/120/160-column layouts, Unicode wrapping, controls, missing values, and headers.
- [x] Complete generated reference/notices and Linux Unicode-width portability checks.
- [x] Complete normal commit hooks and affected-source lint.

### Audit S09: summary grouping and population labels

- [x] Label console summary groupings and explain full-population denominators when top-limited.
- [x] Add truncated JSONL group counts and count comparison union keys once.
- [x] Verify empty, zero-size, repeated, scoped, and top-limited summaries; complete docs and lint.

### Audit S10: delimited summary exports

- [x] Define a wide schema with request, scope, root, and total identity; reuse listing encoders.
- [x] Parse CSV/TSV exports and compare metrics with JSONL across repeated requests and scopes.
- [x] Verify output-producer metadata, full-config composition, generated help, and changed-header consumers.
- [x] Reconcile B06 total-row identity and S09 truncation metadata before publication.

### Audit B10: physical comparison populations with shard flags

- [x] Reproduce a mixed identical/different set being assigned wholly to its representative's category.
- [x] Keep comparison results and reductions physical; preserve scheme selection for `-shard-status`.
- [x] Add regressions for category accounting, side/combined totals, extra listings, and ordinary logical summaries.
- [x] Complete generated documentation, targeted tests, and changed-file checks before publication.

A two-shard set with one identical pair and one different pair produced two physical comparison
results, but `--shards --summary=ext --summary-scope=identical,different` placed all eight bytes under
`identical` and none under `different`. Comparison without ordinary summaries could also emit extra
collapsed paths. Whole-set comparison semantics remain a separate design question; this fix keeps
all comparison accounting at the existing physical-entry level.

### Audit B11: collect and shard reductions must use one population

- [x] Reproduce double counting when `-collect` and `--shards` feed a summary together.
- [x] Feed reductions from the collected population once, with logical shard grouping applied consistently.
- [x] Test collection placement before/after truncation, named collections, duplicate/incomplete/custom sets,
      histograms, summaries, root/category scopes, zero-byte files, and ordinary behavior without collections.

Two 2-byte files `data-00000-of-00002` and `data-00001-of-00002` produce count 1 / bytes 4 with
`xff ROOT -type f --shards --summary --format=jsonl`. Adding `-collect` before `--shards` produces
count 3 / bytes 8. The post-walk shard pass feeds one logical set, then `FinishCollections` feeds
the two physical members again. Merely suppressing one feed is insufficient unless the selected
collection population and logical grouping semantics remain correct. S14's guide must not present
this as intentional accounting.

### Audit S14: shard population guide

- [x] Explain physical predicates/actions, logical reductions, status-cohort order, collections,
      missing members, duplicate selection, custom schemes, and physical comparison with executable examples.
- [x] Publish with the B10 comparison and B11 collection-accounting corrections in PR #871.

### Audit B13: archive mode ordering

- [x] Reproduce stale filename sniffing after a later all-mode selection.
- [x] Resolve traversal and sniffing together; test CLI aliases and full INI selection order.
- [x] Verify generated help, archive regression tests, and changed-source lint.

### Audit S16: task-based archive safety recipes

- [x] Document minimal capability profiles and format/destination limitations.
- [x] Execute permitted and blocked recipe variants against isolated fixtures.

- [x] **B14: Preserve hash-summary context.** Fix archive member grouping in `--summary=hash`
      and honor configured digest/encoding like `{hash}`; verify real archive and ordinary-file
      regressions. See `docs/review-0.6.0.md`.

### Audit S12: inactive modifier inspection

- [x] Add typed consumer metadata and reuse runtime resolvers for effective output state.
- [x] Diagnose effective CLI requests while preserving dormant config defaults and operand literals.
- [x] Verify full configs, cleared consumers, generated help, and every changed-header consumer.
- [x] Add archive depth, member-path, and aggregate dependencies using resolved flavor and backend availability.
- [x] Verify archive diagnostics and all affected header consumers.
- [x] Track algorithm/encoding defaults through parsed hash actions and field templates, including
      printf/exec syntax, suppressed listings, grep counts, summaries, and configured consumers.
- [x] Verify hash diagnostics, integrated prerequisites, generated reference, and every affected header consumer.

### Audit S13: execution resource inspection

- [x] Document retained state, content reads, concurrency, and measurement definitions separately.
- [x] Add an effective-command resource view without presenting static estimates as measured usage.
- [ ] Measure first output, elapsed time, peak memory, and logical/backend bytes across broad and deep
      trees, network storage, and repeated content consumers.
- [x] Investigate directory-at-a-time comparison barriers, ordering, errors, actions, and VFS lifetimes; record the prototype and acceptance criteria in `docs/execution-resources.md`.
- [ ] Prototype and measure paired-directory comparison after settling its output-order and partial-error contracts.

### Audit S13: resource measurement harness

- [x] Measure first stdout, elapsed/CPU time, byte counts, and normalized peak child RSS.
- [x] Preserve failure status and bounded stderr diagnostics without pipe deadlocks.
- [x] Exercise eight real xff workloads on generated broad/deep trees.
- [x] Measure logical whole-file reads for broad/deep synthetic trees, including repeated content consumers and paired comparison.
- [x] Inspect effective worker limits, active content fields, and retained-state consumers without traversal.
- [ ] Integrate general runtime read accounting.
- [x] Add a read-only benchmark observer for whole-file/range calls on real broad/deep fixtures,
      with failed/empty/concurrent-read tests and separate invocation provenance.
- [ ] Extend measurement coverage to archive/member backends and supplied network fixtures.

- [x] Record S13 one/eight-worker observations on broad/deep fixtures, with raw runs and timing
      ranges; keep optimized branching/network scaling and storage-traffic claims unproven.

### Registry capability audit

- [x] Derive primary operand grammar, smart case, traversal effects, deferred controls,
      execution behavior, and help relationships from descriptor metadata.
- [x] Preserve literal token lookup, evaluator dispatch, and config-layer directive parsing.
- [x] Add renamed-descriptor regressions so shared behavior follows capabilities rather than spelling.
- [x] Resolve explicit archive mode and presence together instead of maintaining a second alias list.
- [x] Complete integrated tests and changed-header lint before publication with audit group six.

### Audit B15: filename controls in display output

- [x] Escape controls and backslashes before table width calculation and tree rendering.
- [x] Preserve raw and machine-output encoding; document the display distinction.
- [x] Verify buffered/streaming tables, tree labels, and actual filenames.
- [x] Regenerate and verify the flag reference.
