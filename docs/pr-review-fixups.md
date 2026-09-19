# PR review fixups

Track changes identified while reviewing the current open PRs. Collect the notes here and apply
them afterwards in follow-up changes. Each entry must identify its originating PR and a concrete
action. Keep entries pending until the change and its verification are complete; then record the
implementing PR.

## Agreed integration and follow-up sequence

1. Merge #869 through #875 into #868's branch with merge commits, preserving individual PR history.
   Completed: #868 merged before the local integration fixes were published.
   Carry F01, F05-F07, and F12 in the immediate follow-up PR.
2. Follow-up PR: F02-F04, fuzzy suggestions, persistent/capped width, and help spacing.
3. Follow-up PR: F08 and F11, Python testing ownership and proper Bazel Python targets.
4. Follow-up PR: F09, benchmark history across PRs and releases.
5. Separate final PR: F10, comparison benchmarks. Explicitly defer this work until the original
   goal has been resumed and actually completed. It must not hold up the preceding PRs or expand
   that goal's completion requirements.

## F01: Make safety provenance field names explicit

- Origin: [PR #868](https://github.com/mboworks/xff/pull/868).
- Location: `xff/config/safety.cc`, `SafetyResolution`.
- Status: merged in PR #876.
- Finding: `std::optional<std::size_t> dry_run` holds an index into the resolved flag application
  sequence, not the boolean dry-run value. The actual value is `policy.dry_run`. The current name
  makes those two roles easy to confuse.
- Action: rename the provenance member to `dry_run_source_index`. Apply consistent source-index
  naming to the adjacent provenance members (`activation`, `temp_root`, `output_root`, `mandatory`,
  and `profile`), using plural names for arrays. Keep the boolean policy fields and index semantics
  unchanged; update all consumers.
- Verification: run the affected safety-policy and explain tests; effective decisions and rendered
  provenance must remain unchanged.
- Implemented in: immediate follow-up to #868 (CI pending).

## F02: Broaden and rank flag spelling suggestions

- Origin: [PR #869](https://github.com/mboworks/xff/pull/869).
- Location: `xff/cli/diagnostics.cc`, `IsNearby`, `RenderSuggestions`, and their callers/tests.
- Status: implemented; all 78 CLI test targets and changed-file clang-tidy pass; publication pending.
- Finding: suggestions exist, but matching accepts only one insertion, deletion, substitution, or
  adjacent transposition. Names shorter than four characters and cross-dash-family matches are
  excluded. Candidates are alphabetized rather than ranked, and more than three candidates suppress
  the entire suggestion. Global candidates come from the flag registry and aliases, excluding
  config-only flags; predicate candidates come from the expression registry. Value errors have
  separate accepted-value diagnostics rather than this spelling matcher.
- Reproduction: the staged executable suggests `--summary` for `--sumary=ext` and `-name` for
  `-naem`, but gives no candidate for `--summary-scp=all`. `-time` produces no suggestion because
  the one-edit candidates are too numerous. Each command exits with usage status 2. The diagnostic
  implementation and unit tests are unchanged between the #869 head and the tested staged stack.
- Action: replace the one-edit matcher with fuzzy matching for flag and predicate suggestions.
  Reuse the existing fuzzy-matching facilities where suitable, and rank candidates by match quality,
  including useful multi-edit mistakes in longer names. Retain a short best-candidate list
  instead of suppressing all candidates solely because there are more than three. Keep registry
  metadata as the candidate source, deterministic tie handling, and advisory-only output; never
  accept or execute a corrected command automatically.
- Implementation details: choose and test sensible score thresholds and deterministic handling of
  ambiguous candidates. Preserve the current distinction between globals and primaries; value
  suggestions remain a separate scope decision.
- Verification: cover multi-edit long names, ranking/ties, more than three candidates, unrelated
  inputs, aliases, config-only exclusions, bounded input, `=value` separation, and argument literals.
  Retain CLI tests proving a usage error occurs before actions execute.
- Implementation: `tools/benchmark_history.py`, dedicated measurement/publication workflows, and
  [measurement contract and retention](benchmark-history.md). Hosted-run noise/cost calibration remains
  observational; no regression threshold is enforced.

## F03: Support and explain persistent width preferences

- Origin: [PR #870](https://github.com/mboworks/xff/pull/870).
- Location: `xff/cli/globals.cc` (`--width`), `xff/cli/main.cc`, configuration handling, and width help.
- Status: implemented; all 78 CLI test targets and changed-file clang-tidy pass; publication pending.
- Finding: `--width=auto` explains detection but not how to persist a narrower personal preference.
  More importantly, `--width` is currently marked CLI-only, and help resolves width before loading
  configuration. Adding an INI example alone would therefore give unusable advice.
- Action: support a width preference in the user INI and make it effective for the documented
  width-controlled outputs, including help. Preserve CLI override precedence and argument-run
  isolation. Explain that users who find automatic output too wide can put their preferred width
  in the unsectioned user config at `<OS account home>/.config/xff/config`, with a tiny example:

  ```ini
  --width=100
  ```

  Review early help dispatch carefully: reading a display preference must not execute configured
  actions. Keep the registry help, environment documentation, and generated reference consistent
  about `$COLUMNS` precedence, terminal detection, and unknown-width behavior.

- Verification: full INI tests for the preference, CLI override, help and comparison summaries;
  ensure child-command width arguments remain literals and help never executes configured actions.
- Implemented in: not yet assigned.

## F04: Offer capped automatic width

- Origin: [PR #870](https://github.com/mboworks/xff/pull/870).
- Location: `xff/cli/help_width.cc`, width validation, registry help, and width-controlled renderers.
- Status: capped CLI/INI width and default implemented; all 78 CLI test targets and changed-file clang-tidy pass; publication pending.
- Proposal: extend the existing value set with `--width=auto:COLS`, for example `--width=auto:120`.
  Use the automatic width up to the cap; use the cap when width cannot be detected. Preserve the
  existing `$COLUMNS` then terminal detection order. This avoids adding a separate interacting flag
  and supports a portable personal default through F03.
- Action: parse and validate the cap, document its behavior with small and unknown
  terminal widths, and support it through the same INI and CLI paths as other width values.
- Agreed default: `auto:110`; explicit `auto` remains uncapped. Existing widths have a 40-column minimum; reject caps below 40 rather
  than silently exceeding the requested cap. The cap bounds layout selection/wrapping, not every
  emitted line: numeric cells, unbreakable content, and outputs outside `--width`'s scope retain
  their documented behavior.
- Verification: detected widths below/at/above the cap, unavailable detection, `$COLUMNS`, malformed
  and overflowing caps, zero/subminimum caps, INI defaults and CLI overrides, plus unchanged `auto`,
  `none`, `0`, and fixed-width behavior.
- Implemented in: not yet assigned.

## F05: Clarify and enforce struct-initializer formatting across the codebase

- Origin: [PR #870](https://github.com/mboworks/xff/pull/870).
- Location: `STYLE_CPP.md` and all project-owned C++ sources, headers, tests, and extensions.
  Initial examples are in `xff/engine/run.cc`, in `emit(exporter.Row(...))` calls.
- Status: merged in PR #876.
- Finding: multiple struct initializers omit the trailing comma needed for the expected layout.
  The final `.metrics = ExportMetrics(...)` member in the summary export calls is one example.
  A comma after the closing brace only separates function arguments. The style guide already
  requires inner trailing commas for long/complex aggregates and every struct element in long
  registry-style tables, but compliance is inconsistent.
- Action: revisit the style guide to make the required cases explicit, with examples of nested
  initializers and structs passed directly as function arguments. Audit the full project-owned
  codebase, not just #870, and add missing commas inside the relevant struct initializers. Review
  nested aggregates independently; a comma on the outer initializer does not fix an inner one.
  Preserve the documented exception for simple single-line aggregates unless the revised guide
  explicitly changes it. Run the pinned clang-format version after correction.
- Prevention: evaluate a focused policy check for objectively defined cases so formatter success
  alone cannot hide omissions. Do not use a blanket textual replacement that confuses struct
  initializers with other brace-delimited constructs or edits external/generated code.
- Verification: check all project-owned C++ areas, run formatting and any added policy-check tests,
  and review the diff to confirm this remains a formatting-only change. Record the audited scope
  and any explicit exceptions in the implementing PR.
- Implemented in: immediate follow-up to #868 (CI pending).

## F06: Remove literal Chinese text from scoped-table tests

- Origin: [PR #870](https://github.com/mboworks/xff/pull/870).
- Location: `xff/presentation/render/scoped_table_test.cc`, the two multiline layout goldens.
- Status: merged in PR #876.
- Finding: the layout goldens contain literal Chinese characters. The fixture and other assertions
  already spell the same code points using ASCII `\u` escapes. These characters exercise terminal
  column width; the tests do not need Chinese-language wording.
- Action: remove the literal Chinese text from the source. Use ordinary English/ASCII labels for
  general layout examples and explicit, named, escaped Unicode data for dedicated column-width
  coverage, with English comments explaining the tested property. Preserve meaningful wide-character
  and combining-character tests. Do not put `\u` escapes directly in raw strings expecting decoding;
  construct the Unicode expectations explicitly while retaining readable multiline goldens.
- Verification: scoped-table tests pass, the source contains no literal Chinese characters, and
  dedicated tests still distinguish display-column width from byte or code-point counts.
- Implemented in: immediate follow-up to #868 (CI pending).

## F07: Remove unwanted non-ASCII source text across the repository

- Origin: review of [PR #870](https://github.com/mboworks/xff/pull/870), extended to the full tree.
  Additional findings originate in [PR #873](https://github.com/mboworks/xff/pull/873) and
  [PR #874](https://github.com/mboworks/xff/pull/874); most affected lines predate this stack.
- Status: merged in PR #876.
- Evidence: [the non-ASCII audit](non-ascii-audit.md) records every affected line, code point, escaped
  excerpt, and originating PR where applicable. The complete stack has 114 affected lines across
  25 text files; the user's tracked working files independently have 78 lines across 20 files.
- Finding: literal Chinese characters also appear in `tools/release_smoke.py`, `xff/cli/csv_test.sh`,
  and `xff/presentation/render/render_test.cc`, beyond F06's scoped-table goldens. Other findings
  include typographic punctuation, README symbols, HTML separators, Unicode filenames, intentional
  tree/histogram output glyphs, and the literal character in the em-dash prohibition hook.
- Action: remove unwanted literal non-ASCII source text. Use plain ASCII prose and comments;
  preserve necessary Unicode runtime behavior through explicit escapes or HTML entities, with
  readable English explanations and dedicated test data. Review README symbols for ASCII-word
  replacements. Extend F06's cleanup to all affected tests without removing Unicode coverage.
  Leave binary assets, compressed data, and archive fixtures intact.
- Prevention: define and enforce an ASCII source-text policy with explicit handling of binary
  fixtures and generated/external files; the checker itself must use ASCII spelling for code points.
- Verification: repeat the complete tracked-text scan after cleanup; verify unchanged Unicode
  rendering, unusual-filename round trips, and column-width behavior in affected tests. Review
  generated documentation and the existing em-dash check as well.
- Implemented in: immediate follow-up to #868 (CI pending).

## F08: Define ownership of Python tooling tests and pre-commit checks

- Origin: [PR #873](https://github.com/mboworks/xff/pull/873).
- Status: implemented with F11 in the Python tooling follow-up.
- Bazel owns 33 tooling suites under `//tools:python_tests`; `//...` runs them too.
  All Python hooks use managed Python 3.13, matching the explicit standalone Bazel toolchain.
- Pre-commit retains narrowly triggered checker tests. Process-measurement and fuzz scheduler
  tests move to Bazel; commits never recursively launch Bazel to test hooks.
- `bazel run //tools:repository_tooling_test` checks all live BUILD files, registered-extra coverage
  mapping, and test ownership via `BUILD_WORKSPACE_DIRECTORY`. Main and release CI run it explicitly;
  no maintained list of BUILD exports or sandbox fixtures restricts repository discovery.
- Repository fixtures are declared runfiles; grouping tests create temporary source files. Git and
  shell integration suites use temporary repositories and are marked `requires-host-tools`.
- Validation: all 33 tooling targets and the benchmark integration passed through Bazel; the three
  live repository checks and changed-file pre-commit checks passed directly.
- Implemented in: `tooling/python-bazel-targets` follow-up.

## F09: Publish benchmark history across PRs and releases

- Origin: [PR #873](https://github.com/mboworks/xff/pull/873).
- Location: resource benchmark harness, benchmark reports, CI, and the published project site.
- Status: completed in PR #887; optimized hosted runs and live publication verified.
- Finding: committed measurement reports provide reproducible snapshots but no continuous history
  or PR-to-baseline comparison like the coverage overview.
- Action: retain machine-readable benchmark records and publish an indexed history for main, PRs,
  and releases. Link each result to the exact measured commit, baseline commit, workflow run, and
  raw observations. Use explicit pre-/post-merge phases, with PR merge times and tagged-commit times newest first. Keep reruns distinguishable and avoid presenting duplicate runs
  as separate source changes. Use the immediate base for stacked PR comparisons and identify any
  additional main/release baseline explicitly.
- Measurement contract: version the scenarios and fixture generator; record fixture identity,
  optimized build settings, executable identity, platform/runner class, tool versions, worker
  limits, cache preparation, and repetition policy. Compare compatible runs only. Publish elapsed
  time, time to first output, CPU time, and peak memory with repetitions and variability, plus
  output counts/bytes and read counters where available. Distinguish logical reads from storage I/O.
- CI design: separate small harness correctness tests from performance runs. Establish runner
  noise and cost before choosing regression thresholds or making performance a merge gate. Measure
  base and head under comparable conditions; label historical comparisons that use different hosts.
  Define artifact retention and index cleanup without storing every raw run in the source tree.
- Verification: index ordering, commit/baseline association, schema compatibility, missing/failed
  runs, reruns, retention, and rendering all have tests; published summaries reconcile with raw data.
- Implemented in: not yet assigned.

## F10: Add task-specific comparisons with find, rg, and fzf

- Origin: [PR #873](https://github.com/mboworks/xff/pull/873).
- Location: benchmark scenario definitions, fixtures, correctness checks, and published reports.
- Status: agreed requirement; deferred to a separate final PR after the original goal is resumed
  and completed. Do not include comparison benchmark implementation in F09's history PR.
- Action: extend the benchmark suite with explicit comparison tasks for `find`, `rg`, and `fzf`.
  Start with traversal/name/type selection for `find`, file enumeration and content search for
  `rg`, and noninteractive fuzzy selection over the same input list for `fzf`. Record exact
  implementations (including GNU versus BSD find), versions, commands, and options. Do not force
  unsupported tasks such as xff-specific summaries into misleading standalone-tool comparisons.
- Equivalence: specify roots, input population, hidden/ignored files, symlinks, case/regex/fuzzy
  semantics, ordering, output encoding, result limits, and output sink per task. Verify expected
  results independently before comparing timings. Different fuzzy models/rankings require explicit
  labels and quality checks rather than an assumption that scores or result order are equivalent.
- Boundaries: report fuzzy matching of precomputed candidate lists separately from end-to-end
  discovery plus matching. For pipelines, report the full command and account for all processes;
  do not compare a single child's peak RSS with another tool's process-tree total. Keep startup and
  steady workload costs visible, and distinguish interactive behavior from batch throughput.
- Method: use optimized binaries, repeat and interleave runs to reduce order bias, record cache
  conditions honestly, and include broad/deep trees and varied content/query workloads. Reuse F09's
  history and raw-data format so both xff regressions and relative performance can be tracked.
- Verification: scenario equivalence, version capture, exit-status interpretation, unavailable-tool
  reporting, pipeline accounting, and per-task report generation. Missing tools must be explicit
  skips or errors, never successful empty measurements.
- Implemented in: not yet assigned.

## F11: Model benchmark Python programs as Python targets

- Origin: [PR #875](https://github.com/mboworks/xff/pull/875).
- Status: implemented with F08 in the Python tooling follow-up.
- Use stable `aspect_rules_py` rules and an explicit standalone Python 3.13 toolchain. MBO has no
  current first-party Python target convention to copy.
- Replace `resource_benchmark_scripts` with `py_binary` executables and imported Python libraries.
  The benchmark driver resolves its declared measurement executable through runfiles, preserving a
  fresh measurement process per invocation. Direct script execution remains supported.
- Replace the shell-embedded benchmark test with a `py_test` and declared xff, counter, and driver
  executables. Retain workload, invalid-input, traversal-failure, provenance, and measurement checks.
- Validation: the benchmark integration passed under the Aspect launcher and hermetic interpreter.
- Implemented in: `tooling/python-bazel-targets` follow-up.

## F12: Preserve detailed coverage for aggregated PRs and individual runs

- Origin: aggregation of PRs #868-875 into #868.
- Status: merged in PR #876; timestamp ordering and closed-PR filtering merged in PR #877; live publication verified.
- Finding: all eight original reports already had retained summaries, run metadata, and detailed
  LCOV pages. However, the aggregation's next run would replace #868's original per-PR report, and
  nested PR merge commits were absent from main's first-parent ordering.
- Action: preserve immutable run/attempt snapshots with a run-history index; archive before replacing
  per-target reports. Position nested PRs at the first main commit that includes them without
  changing their measured source commits, run identities, statistics, or detail pages.
- Backfill: snapshot the eight verified existing report trees before the integration's next coverage
  publication, reusing existing data rather than rerunning coverage.
- Verification: tests cover real nested Git merges, pre-integration/unmerged/missing commits,
  retained source/run identity and detail files, run attempts, immutable snapshots, and interrupted
  archive-copy recovery. Verify publication after the integration merges.
- Implemented in: immediate follow-up to #868.

## Adding review notes

Use a new `Fnn` identifier for each independently actionable finding, with its origin PR, code or
document location, finding, action, verification, status, and eventual implementing PR. If a decision
is still needed, record the question explicitly instead of treating a proposed solution as agreed.

## F13: Show histogram block glyphs alongside their code points

- Origin: PR #876 review of `docs/history-roadmap.md`.
- Action: show the full and fractional Unicode blocks beside their code points; preserve the
  ASCII `#` fallback. Permit only these eight glyphs in this documentation file, with a scoped
  pre-commit check; C++ source continues to use escapes.
- Implemented in: #876.

## F14: Isolate Unicode test coverage

- Origin: PR #876 review of the release smoke filename fixture.
- Action: remove Unicode from ordinary smoke, control-escaping, and summary fixtures. Group
  Unicode coverage in labeled sections with dedicated fixtures or functions. Use documented
  Latin/fullwidth Latin examples for multibyte and wide text; keep grapheme cases explicit.
- Implemented in: #876.

## F15: Preserve compact README capability markers

- Origin: PR #876 review of the feature comparison matrix.
- Action: use full/half-filled circles for supported/partial and blank cells for unsupported,
  with a short legend. Keep columns compact and avoid repeated Markdown footnote references,
  which generate a return-link list for every referencing cell.
- Implemented in: #876.

## F16: Separate Unicode tests into dedicated files

- Origin: PR #876 review of `tools/release_smoke.py`.
- Action: move Unicode-handling smoke, rendering, text-width, and CSV cases into separate
  files. Keep approved histogram/tree glyph expectations in their functional tests. Exempt only
  the dedicated Unicode files from ASCII-only checking, retaining the em-dash prohibition. Use visible Latin samples literally; keep invisible encoding details escaped.
- Implemented in: #876.

## F17: Give Unicode C++ tests independent Bazel targets

- Origin: PR #876 review of `xff/presentation/render/BUILD.bazel`.
- Action: one source per test rule for render, scoped-table, and text-width tests, with independent
  Unicode targets and only their required dependencies. Record the rule in `AGENTS.md`.
- Implemented in: #876.
