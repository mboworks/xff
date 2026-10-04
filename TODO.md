# TODO

## Prepared expression execution and optimizer

Detailed scope, data-structure audit, benchmark matrix and decision gates:
[`docs/design-expression-program.md`](docs/design-expression-program.md).

- [ ] EP01: Inventory evaluator operands/effects; add source-node identities, conservative
      optimization properties, an isolated equivalence oracle and preparation/execution baselines.
      Initial contract/oracle and 72-case in-memory baseline are implemented; CI evidence and
      expanded selectivity/error/state coverage remain before the stage is complete.
- [ ] EP02: Bind predicate handlers once through registry-derived semantic IDs; measure a bound-tree
      executor against current name-dispatched evaluation before changing control flow.
- [ ] EP03: Prepare invariant operands and indexed worker matcher state; preserve validation timing,
      conditional metadata, dynamic values and errors; report remaining per-entry parsing.
- [ ] EP04: Compare contiguous opcode and bound-function programs; lower audited boolean control flow,
      reuse scratch and preserve source mapping, with explicit whole-expression fallback coverage.
- [ ] EP05: Complete fuzzy/unknown/error/action/deferred semantics; preserve exactly-once effects,
      VFS safety, coordinator ownership and bounded suspended-entry memory.
- [ ] EP06: Measure deterministic constant folding, jump simplification, selected fusion and proven
      value reuse independently; keep reordering behind a stronger equivalence proof.
- [ ] EP07: Qualify the winning executor on native macOS ARM64 and Linux x86-64; update --explain,
      self-documentation and performance evidence; enable only measured, semantically complete paths.
- [ ] EP-S01: Evaluate a reusable constexpr frozen/perfect-hash string map/set in MBO for startup
      lookup. Existing LimitedMap/Set and constexpr hashes already exist; preserve exact miss checks,
      per-mode aliases and bounded construction. This does not block EP01-EP07.
- [ ] EP-S02: Revisit experimental MBO interning for dynamic names only when a concrete consumer and
      measurements justify a dependency update; prefer existing strong IDs and indexed execution slots.

## Pages deployment size

- [x] Redirect byte-identical stable coverage HTML to preserved run archives in all publishers;
      check the complete staged payload before upload and verify the served benchmark index.
- [ ] Decide long-term coverage history hosting or retention before immutable reports alone
      approach the deployment ceiling. Keep raw measurements and report provenance.

## Benchmark runner capacity

- [x] Publish a versioned measurement-dataset catalog and human-readable methods overview; let local
      backfill discover compatible recipes, select among them, and inherit their settings while keeping
      machine identities and exact observation provenance separate.
- [x] Add automatic local machine-series detection and `help` / `list` / `backfill` commands;
      validate saved and published observations across batches, fill historical gaps and new merges,
      and require execution confirmation unless `-Y` / `--yes` is supplied.
- [x] Publish PR benchmark artifacts as soon as both platform comparisons finish, using a trusted
      publisher and explicit branch-head/tested-merge provenance. Offer PR previews alongside
      merged history in the chart without adding previews to merged normalization windows.
- [x] Add a compact reference-adjusted overview with compatible-case counts and largest regressions;
      update one PR comment with the current preview and 15% advisory warnings. Keep gating deferred.
- [ ] Verify the first trusted PR-preview deployment and bot comment after the publisher lands on main.
- [x] Accept flat single-artifact and nested multi-artifact shard downloads; validate the
      complete shard set before deriving report filenames, and keep platform aggregation independent.
- [x] Reject undersized runners before measurement; use the same 1/3 grid on both hosted
      platforms, pinned on Linux and requested workers on macOS. Leave one Linux vCPU
      outside the benchmark mask for background work. Preserve strict shard identity checks.
- [x] Use three complete-matrix sample shards on both hosted platforms: three measured rounds
      per shard for PR and main; pool raw samples before selecting the fastest seven of nine.
      Share compiled binaries, rotate participant order, and preserve per-sample shard provenance.
- [x] Verify nine-round PR artifacts retain every raw observation and the fastest-seven estimate:
      PR #952 has 440 tasks / 800 tool-case entries per platform, each with 9 raw / 7 selected samples.
      Linux jobs took 4m31s-5m17s, macOS 4m33s-7m00s, versus #951's 2m19s-3m32s one-round jobs.
      Strict host-CPU validation rejected one five-CPU macOS runner; a retry supplied three CPUs.
- [ ] Verify the first sharded PR/main runs and published results; compare measurement and
      end-to-end wall times, including the extra warm-up and fixture setup costs.
- [x] Add a CI-only historical backfill workflow for macOS and Linux replacement campaigns,
      plus a standalone tool for additional local Mac/Zen 5 series. Freeze revisions, driver,
      tools and allocations; build before measuring, resume verified records, report incomplete
      revisions, and enforce one hardware thread per physical core for local Linux pinning.
      See `docs/benchmark-backfill.md`.
- [x] Run both full CI replacement backfills on the 1/3 grid and implement publication import
      and promotion by platform/commit, retaining superseded raw observations. An incomplete
      campaign must not silently replace an entire history.
      Published campaign `36952332747/2` (19 revisions per platform) and the 17-revision local
      M5 Pro series; verified live after recovery deployment `37028502778`.
- [x] Prepare one complete-revision job per platform/commit, freeze the whole campaign, and
      validate/import it atomically. Prefer replacements in history, charts and exact PR/tag
      links while retaining original paired results and actual collection-attempt provenance.
- [x] Collect the local M5 Pro 1/3/10 batch (17 revisions) and implement publication as an additional
      machine series with selectable landscape allocation pairs and preserved raw observations.
- [ ] Run and publish the separate local Zen 5 1/3/10 series with physical-core affinity. Keep
      M5 Pro, M2 Ultra and Zen 5 distinct; macOS requests workers without claiming CPU affinity.
- [x] Implement presentation-only reference normalization using exactly five compatible reference times:
      the initial measurements share the first five; from measurement five use current plus four prior.
      Scale each individual XFF result by the reference mean divided by its own reference time.
      Preserve raw timings and ratios, expose the window and factor, and isolate worker counts,
      reference binaries, fixtures and machine series.
      See `docs/performance-analysis.md` and `docs/benchmark-comparisons.md`.

## macOS startup follow-up

Demand-driven timezone/CoreFoundation implementation and local measurements are recorded in
`docs/performance-analysis.md`: ordinary startup cases improve 25-33%; local-time formatting
regresses 3-11% across the two sessions. Keep this tradeoff visible when comparing startup paths.

- [x] Make civil-timezone resolution demand-driven using descriptor and compiled-output requirements;
      preserve explicit-zone validation, date/printf/summary behavior and config safety. The local
      investigation measured about 0.59 ms for the unnecessary default-zone query on an empty search.
- [x] Implement lazy CoreFoundation loading through Abseil/CCTZ's platform adapter; preserve macOS
      default-zone and DST semantics. The control program adds about 1.27 ms when linked to that framework.
- [ ] Measure precomputed immutable language/MIME catalogs and mode-aware flag indexes derived from
      their existing registries; keep user overlays and multi-match semantics. Do not rebuild catalogs
      for feature paths that never use them.
- [ ] Measure generated notice/extension tables and loader-friendly data layouts before adopting them.
      Linker-only chained-fixup/export experiments have no consistent startup win so far.
      See `docs/performance-analysis.md` and raw `docs/performance-startup.json`.

## Toolchain follow-up

- [ ] Complete the Clang/LLVM 23.1.2 upgrade: validate native macOS linking, Linux LTO, the
      LLVM 22 MSan compiler/runtime override, matching formatting, full CI, benchmark results,
      and stripped binary sizes before merging the separate follow-up to #936.
- [ ] Correct clang-tidy's inherited `mbo/` header filter to cover XFF-owned headers,
      including extension headers reached through Bazel's external include paths. Add a
      regression test distinguishing first-party headers from dependency headers.
- [ ] Profile the static-analyzer cost of `xff/engine/run_test.cc` and evaluate splitting
      it into focused test targets. LLVM 23 spent 1,626 seconds on this translation unit in
      CI run 36786917255; a local run took 712 seconds, and process sampling found the
      static analyzer active. Preserve useful checks while reducing this serial bottleneck.

## Content-search output controls

- [x] Replace speculative mode scanning with one registry-driven global pass and one expression
      pass; retain resolved primary descriptors, borrow token text, and dispatch operators and
      context controls through typed effects. Generate separate constexpr indexes per mode.
- [ ] Compare the forthcoming constexpr perfect-hash map with compact trie/automaton lookup for
      registered flag spellings. Preserve exact unknown-input rejection, explicit aliases and
      per-mode tables; benchmark whole-command parsing as well as lookup and table size.

- [x] Accept native flags, roots and matchers before the first `--rg`; preserve file filters,
      literal operands, configuration order and repeated mode transitions, with execution tests
      and prominent documentation of combined file selection and matching-line output.

- [x] Verify conflict-free rg-only `-A` / `-B` / `-C` context aliases, separate/attached
      counts and mode boundaries; use canonical `--context-after` / `--context-before` names and
      accept both long spelling families in all modes and INI files through the same registry entries.

- [x] Return a style enum for invocation dispatch; keep custom config selectors separate.
- [x] Limit full-binary preset mapping to `xff_full`; preserve all other invocation names.

- [x] Implement and validate the missing grep-style long output options with explicit `-grep`.
- [x] Add `-M` / `--match-output` for default content-line output, reuse long modifiers, and validate INI selection.
- [x] Add CLI-only `--rg` parsing and the `--xff` native-filter transition, with separate search patterns.
- [x] Accept `+` as XFF-mode expression OR while preserving literal arguments and exec termination; keep it out of find mode.
- [x] Select rg grammar and match output automatically for the `rg` invocation name.
- [x] Give rg short options their rg meanings before `--xff`; retain native `-o` OR afterward.
- [x] R01: Implement rg type filters using the shared XFF language catalog, including glob precedence
      and native-filter composition. Preserve overlapping filename/extension candidates (for example,
      `.h` for C and C++) separately from the preferred language used for display and summaries.
      Keep rg type and native `-lang` membership consistent; specify overlay/conflict handling and
      test overlap, ordered inclusion/exclusion, aliases, and lean/full catalog behavior.
- [x] R02: Add multiline matching and dotall controls with counts, context, and regex-backend coverage.
- [x] R03: Add explicit UTF-8/byte search modes and concept-checked typed word classifiers; isolate Unicode fixtures.
- [x] R04: Add heading/terminal layout with explicit overrides and deterministic piped/JSON output.
- [x] R05: Stream full line/context output with bounded context retention and preserved binary/error handling.
      Implementation, compatibility boundaries, and validation: `docs/design-rg.md`.
- [ ] R06: Design explicit file decoding/transcoding (including UTF-16/32 byte order and source-offset mapping);
      the typed word classifiers are not a file decoder. Keep rg byte/UTF-8 mode explicit in the meantime.
- [ ] R07: Consider newline-capability metadata for regex backends so `-U` can retain streaming and
      ripgrep's line-count optimization for patterns that cannot match newlines; current multiline counts
      consistently count occurrences. See `docs/design-rg.md` before changing the documented count rule.

## Combined rg stack audit

- [x] Move filename-type definitions and ordered edits into a shared catalog with native/INI controls.
- [x] Preserve explicit regular-file roots through discovery filters without bypassing native predicates.
- [x] Replace origin-only flag markers with compatibility mode sets shared by lookup and help.
- [x] Support repeated rg/native transitions and preserve literal operand/command boundaries.
- [ ] Decide rg zero-length EOF/multiline occurrence rendering after the differential audit;
      preserve the documented multiline count rule until newline-capability metadata exists.

- [ ] After the R01-R05 delivery, audit the complete stacked change for configuration precedence,
      terminal/pipe defaults, encoding modes, multiline output, and native-filter interactions.
      Put any new fixes in a follow-up PR with regression tests; merge in dependency order once
      the combined work and required checks are green.

## Rg performance candidates

These are separate from R01-R05. Evidence and implementation order are in
`docs/performance-analysis.md`, "Applying the worker model elsewhere".

- [x] P01: Give native content predicates worker-private RE2 state; measurements in `docs/performance-analysis.md`.
- [x] P02: Reuse PCRE2 match scratch per worker while sharing compiled code; lifetime, reentrancy and limit tests included.
- [x] P03: Decouple metadata demand from parallel-filter eligibility for summaries and presentation.
- [x] P04: Reuse or stream per-entry hashes, line counts, and text checks; prebind field rewrite programs.
- [x] P05: Parallelize independent comparison pairs; retain measured cursor and bounded patch-input reuse.
- [x] P06: Measure and retain bounded eager-stat chunks for broad directories; preserve lazy/owned-source paths.
- [x] P07: Measure archive-member matching; retain serial admission after format and shared-budget experiments.
- [x] P08: Measure heterogeneous batches, startup, buffering and overlap; retain smaller chunks and pool growth.

Measured follow-ups (see the P07/P08 sections of `docs/performance-analysis.md`):

- [ ] Add archive read admission or serial extraction into byte-bounded matcher queues before enabling member workers.
- [ ] Investigate ZIP member restart/indexing cost separately from compressed TAR decoding.
- [ ] Design a byte-bounded ordered output queue and shared CPU allowance before enabling an overlapping pipeline.

## Rg integration review (PRs 916-919)

- [x] D01: Preserve VFS sources when probing stdin and other virtual roots for archives.
- [x] D02: Preserve rg pattern ownership in native subcommand diagnostics.
- [x] D03: Compose only-matching output with maximum columns.
- [x] D04: Enforce rg native-filter action eligibility independently of implicit output.
- [x] D05: Preserve regex grammar semantics for whole-line rg matching.
- [x] D06: Include archive-member identity in automatic filename prefixes.
- [x] D07: Apply positive rg globs before hidden-entry pruning.
- [x] D08: Preserve named roots in rg commands.
- [x] D09: Reject simultaneous stdin pattern and content consumption.
- [x] D10: Translate rg automatic worker selection.
- [x] D11: Explain active rg selection independently of line rendering.
- [x] D12: Remove the unrelated rg example from comparison help.
- [x] D13: Preserve field-aware metadata and bounded reads through the rg input adapter.
- [x] Benchmark and parallelize rg content selection, independently of traversal workers.
- [x] Reuse per-entry content for native `-M` and rg native filters, including native matcher batches.
- [x] Stream filename/count/quiet selection without collecting whole contents or selected lines.
- [x] Generate rg option help from the parser metadata, with completeness, arity, and uniqueness checks.
      See the Rg integration review in `docs/performance-analysis.md`.

## Release publication repair

- [x] Publish stable benchmark references (PR #890); both repair workflows completed successfully.
- [x] Verified v0.7.0 navigation links to its versioned benchmark page and retained run
      35471800839/1 matches tagged commit `0bcb461dbb08a1d3b14d1d73938a8b905aed81ea`.

## Original audit and review completion

- Original audit implementation and review fixups are merged, including F09 in #887.
- v0.7.0 is released. F10 competitor comparisons resumed; see `docs/benchmark-comparisons.md`.

## Performance optimization follow-up

- [ ] Benchmark collection arena block policies: 4 KiB -> 64 KiB (current), 64 KiB -> 1 MiB,
      and 4 KiB -> 1 MiB. Compare allocation counts, CPU time, unused capacity and peak memory
      for small/large collections on Linux and macOS before choosing new defaults.

- [x] Evaluate MBO SegmentedVector and Arena at pinned Git head; adopt measured collection storage
      improvements with lifetime tests and allocation/memory tradeoffs in `docs/performance-analysis.md`.
- [ ] Confirm collection storage gains on native Linux and in traversal-inclusive workloads; measure
      deferred result candidates, listing API changes and matcher-buffer reuse before extending adoption.

- [ ] Profile optimized production binaries across 10/100/1,000/10,000-file broad and deep trees;
      separate startup/configuration, traversal/metadata, matching, and output costs. Run both one- and
      four-CPU allocations on verified tmpfs storage.
- [ ] Measure default and representative configured safety policies without bypassing the VFS or
      weakening protections. Read-only `--safe` enumeration is included in competitor benchmarks.
- [x] Derive metadata demand (NEVER / ONDEMAND / ALWAYS) from descriptor requirements and expression
      control flow; cache on-demand values/errors and retain eager prefetch where metadata is always needed.
      Investigate eager metadata collection for name/type-only queries; preserve symlink, error,
      safety, archive, and ordering semantics in any metadata-laziness optimization.
- [ ] Record before/after timing, throughput, memory, and correctness for each optimization; add
      representative larger-content and realistic directory-fanout workloads before broad claims.

## Regex engine performance investigation

- [x] Verify the pinned PCRE2 build and production dispatch: JIT is disabled at both layers;
      JIT SIMD scanning is unavailable. Document match-time anchoring and resource-limit constraints.
- [x] Add correctness-checked RE2/PCRE2/rg-PCRE2 end-to-end workloads and a separate in-memory
      production-backend benchmark; retain compile and matching measurements separately.
- [ ] Repeat on native Linux/x86 with verified tmpfs storage and CPU affinity.
- [x] Enable PCRE2 JIT with explicit dispatch verification, fallback and resource-limit tests;
      retain measured gains, startup cost, and binary size in `docs/performance-analysis.md`.

## Benchmark visualization follow-up

- [x] Keep the hover card in a stable panel below the version and performance-scale cards,
      instead of following the pointer.
- [x] Replace benchmark versions in place while preserving chart space and camera state during loading.
- [x] Add percentage/logarithmic range presets and bright capped outliers or cut-off gaps.
- [x] Expose every measured CPU pair and link selected versions to their PR/releases where available.
- [x] Place the version slider and compact provenance table above the performance scale inside
      the chart, with separate outlined panels and usable controls after loading errors.
- [x] Track hovered points on the legend and preview a minimum-performance threshold while hovering the scale,
      with selectable 50%/75% transparency or hiding below it and an optional threshold plane.

- [x] Add a benchmark overview landscape with platform selection and a slider over retained measured commits.

- [x] Compare Three.js with Plotly using the same benchmark JSON; record startup/update and bundle
      size measurements in `docs/benchmark-renderer-evaluation.md`. Integrate the approved renderer,
      plane-mounted titles, unrestricted orbit, outward ticks, hover guides and offline publishing.
- [ ] Measure production rotation/hover frame times and test Firefox/Safari on native hosts;
      Chromium browser tests cover rendering and interaction in the normal CI workflow.

## Benchmark publication follow-up

- [ ] Retain separate macOS and Linux HTML reports plus raw JSON, with commit, build,
      hardware/runner and measurement-contract identities. Local temporary previews are not release assets.
- [ ] Publish post-merge reports as official results and pre-merge reports as labeled PR previews.
      Release pages should link results for the exact tagged commit per platform; reuse compatible
      post-merge results, otherwise measure the tag and show pending until publication completes.

## Completed: F09 benchmark history

- [x] Paired immediate-base/head measurement with raw observations and a versioned contract.
- [x] Publish bounded PR/release history through the shared site queue; test ordering and retention.
- [x] Validate the first hosted optimized run and record observed runner noise and workflow cost.
      PR #887 post-merge results are live; see `docs/benchmark-history.md` for the initial observations.
- [x] Add correctness-checked competitor tasks, pipeline accounting, and retained reports (F10).

## Completed: F08/F11 Python tooling ownership

- Use Aspect Python rules and a pinned Python 3.13 toolchain for tooling tests and benchmarks.
- Declare Python libraries, executables, tests, and fixture inputs; retain direct script usage.
- Keep fast checker tests in pre-commit and run repository-wide ownership/discovery checks through `bazel run`.

## Completed: Fuzzy help-selector diagnostics

- Share ranked fuzzy matching across unknown CLI flags and unknown help selectors.
- Include registered flags, primaries, topics, and aliases; retain advisory-only behavior.
- Test dashless and dashed selectors, config-only help, case folding, bounded input, and action isolation.

## Range-list style review

- [x] Permit short inline literal ranges when the complete formatted loop header fits one line.
- [x] Separate naming/readability from explicit element typing; check typed wrappers as well.
- [x] Review all C++ sources and headers, name long/reused/unclear collections,
      remove the temporary baseline, and verify the whole codebase against the clarified rule.

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

## Apple application distribution readers

- [x] Delivery 1 (PR #896): XAR/PKG/XIP, owned streaming sources, contextual payload discovery,
      and the removable PBZX extension in the existing full binary on Linux and macOS.
- [x] Verify #896 post-merge main CI and coverage publication (run 35529346705, published
      as the PR 896 post-merge row for `ced00ac`; benchmark publication also succeeded).
- [x] Add native `pkgbuild`/`productbuild` fixtures from original payload text, including XAR seeks.
- [x] Keep isolated PBZX fuzzing enabled; add XZ and decoder-limit corpus seeds.
- [x] Add reproducible 16/64/256 MiB package resource measurements.
- [x] Add shared buffer/decoder reservations and bounded range reads; native sources seek directly,
      while sequential source replay is bounded across related cursors.
- [ ] Verify production package variants with redistributable provenance; extend PBZX framing only
      with explicit fixtures. Third-party application data and signature validation are not covered.
- [ ] Before disk-reader integration, add format-aware block indexes and cache eviction; source ranges
      are bounded, but reopening sequential archive members can still rescan parent streams.
- [ ] Delivery 2: evaluate a separate permissively licensed UDIF/HFS+/APFS extension. No GPL/LGPL,
      installer execution, implicit host mounts, or new Apple-specific binary.

### Traversal and matcher performance follow-up to PR 900

- [x] Derive never/on-demand/always metadata demand from descriptors and consumers.
- [x] Preserve metadata required for traversal, unknown entry types, symlinks and safety.
- [x] Remove deferred-expression memoization when no deferred consumer exists.
- [x] Avoid fuzzy score computation when no output or ranking consumer requires it.
- [x] Add bounded parallel matching for audited independent content predicates with ordered output.
- [x] Compare release optimization levels and system/TCMalloc/mimalloc allocators on available hosts;
      select measured winners and document size/startup tradeoffs in `docs/performance-analysis.md`.
- [x] Audit traversal, content reads, metadata demand, output and safety costs; retain unproven
      directory scheduling changes locally and document bulk-metadata/ISA follow-ups.
- [x] Remove unnecessary content read-buffer initialization with empty/full/partial chunk tests.
- [x] Complete sanitizer, coverage and changed-file lint validation and compare the standard scaling matrix.
- [x] Land performance changes in PR #902 and rebase measurement PR #900 onto main.

## Git creation patches and directory deletion

- [x] Verify empty/nonempty directory deletion and child-before-parent order against find semantics.
- [x] Document why `-prune` cannot prevent descent with `-delete`.
- [x] Default `-diff [TARGET]` to `/dev/null` using registry-driven optional argument parsing in CLI and INI.
- [x] Emit Git creation patches for text, binary, empty, executable and symlink entries through VFS reads.
- [x] Validate generated patches with Git (text, empty files, modes, symlinks, quoted names and multi-block binary literals).

## Published site storage review

- [ ] Revisit total Pages size when `storage-report.json` reaches 200 MB. Follow the options in
      [docs/site-storage.md](docs/site-storage.md): data-driven shared table rendering, numeric chart
      matrices, column-oriented benchmark observations, and additional coverage/source deduplication.
      Preserve coverage aggregate history and all release/merged-PR performance measurements.

## Benchmark baseline and progress repairs

- [x] Put worker and file counts first in measurement/backfill progress, preserving throttling.
- [x] Load compatible earlier main measurements in both aggregate workflows; select by main
      ancestry before run time so reruns cannot compare with themselves or future merges.
- [x] Reproduce the missing-baseline report using run 37196735470 artifacts without new measurements:
      the repaired selection finds all 468 reference comparisons on each platform against #957.
