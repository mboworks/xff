<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# xff 0.6.0 utility and usability work list

Reviewed 2026-09-17 against the published lean and full macOS binaries and the 0.6.0
implementation/reference. This covers the complete feature inventory at the feature-family level,
not only the recent summary work. It is a usability and consistency review, not a proof that every
combination is correct or that the safety boundary has no vulnerabilities.

Evidence includes 81 broad executable probes plus targeted reproductions, registry/help inspection,
and examination of implementation and existing test boundaries. All probe writes used disposable
fixtures under `/private/tmp`. Linux-specific behavior, FUSE mounting, every archive format, hostile
filesystem races, and large-scale performance were not independently exercised in this review.
Existing tests are useful evidence of intended coverage, not a substitute for those experiments.

Priority: **P1** = incorrect results, misleading automation, or an ineffective requested constraint;
**P2** = important workflow or clarity improvement; **P3** = polish. An unchecked item is proposed
work, not an authorization to settle a design decision without discussion. Merged resolutions are tracked by the checkboxes below. B01 is proposed in PR #854; B08 was merged in PR #855. The other claims remain separate changes.

## Overall assessment

The core utility is strong: one expression can filter paths and content, derive fields, execute
selected actions, aggregate results, and operate on archive members. Configuration and safety are
substantially more explicit than an informal collection of shell wrappers. The largest remaining
weakness is composition: separately understandable features do not always preserve their contracts
when combined. Output-format selection, field consumers, resource limits, and repeated reductions
need the most attention.

Keep the current dash-count distinction, explicit archive capabilities, separate block/profile/preview
model, canonical comparison scope names, and shared configuration grammar. The recommendations below
improve their implementation and discoverability rather than replacing them with another vocabulary.

## Confirmed defects and documentation errors

### B01 - P1: Capture use is rejected in valid field consumers

- [ ] Fix capture-reference discovery across all actual field-consuming expression nodes.

Reproduction with any ordinary `FILE`:

```sh
xff FILE -capture:answer /usr/bin/printf answer \; -printf '%{capture.answer}\n'
xff FILE -capture:answer /usr/bin/printf answer \; --exec-fields \
  -execdir /usr/bin/printf '%s\n' '{capture.answer}' \;
```

Both exit 2 claiming the capture is never referenced. A summary key or `--template` referencing
the same capture succeeds. `AppendCaptureRefs` only scans `-exec` and `-capture` arguments, so its
validation does not represent the shared field vocabulary.

**Acceptance:** cover printf/file-printf variants, exec/execdir and capture/capturedir, grep templates,
and other supported consumers. Derive references from parsed templates where possible; escaped
literal braces must not falsely count as use. Retain a real unused-capture diagnostic.
**Start:** `xff/engine/run.cc`, `xff/engine/run_test.cc`, CLI capture/format integration tests.

### B02 - P1: Invalid buffer limits silently leave collections unbounded

- [ ] Reject malformed `--buffer` values before traversal or actions.

```sh
xff DIR -type f -collect --summary --buffer=garbage
```

This succeeds and collects everything. With `--buffer=1` on a directory containing several files,
the collection correctly errors when its bound is exceeded. `ResolveBufferBound` silently preserves
the preceding/default bound for an unrecognized value. A typo in a requested resource limit therefore
has materially different consequences from a typo in `--top` or `--max-results`.

**Acceptance:** valid row/byte limits and `auto/off/all` retain their documented meanings; malformed,
negative, overflowing, and unsupported-unit values fail on CLI and through config validation.
No expression action runs before that failure. Test both collections and tabular buffering.
**Start:** `xff/engine/run.cc`, shared option-value validation, collection/CLI tests.

### B03 - P2: Histogram width silently accepts invalid values

- [ ] Validate `--histogram-width` with the same discipline as summary precision.

```sh
xff DIR --histogram=ext --histogram-width=garbage
xff DIR --histogram=ext --histogram-width=-1
```

Both succeed using the default width. The implementation also silently ignores zero. This is
inconsistent with other numeric output controls.

**Acceptance:** document the valid range and whether zero has any explicit meaning; otherwise reject
it. Test invalid and overflowing values and repeated occurrences in CLI and INI files.
**Start:** `ResolveHistogramWidth` and histogram CLI tests.

### B04 - P2: Histogram exports ignore unsupported format requests

- [ ] Define and enforce a histogram format-support matrix.

```sh
xff DIR --histogram=ext --format=csv
xff DIR --summary=ext --histogram=ext --format=md
```

The first succeeds but emits terminal bars, not CSV. The second mixes a Markdown summary table
with unstructured terminal bars. Summary tables already reject unsupported formats, while
histograms bypass that validation.

**Acceptance:** render the requested format or reject the combination before traversal. Decide
whether Markdown histograms should be tables or explicitly fenced text; test combinations with
summaries, not just histogram-only output.
**Start:** `ValidateSummaryOptions`, histogram rendering, `xff/cli/histogram_test.sh`.

### B05 - P1: Summary JSONL does not identify its originating summary

- [ ] Add an explicit summary identity to every summary record.

```sh
xff DIR -type f --summary=ext --summary=type --format=jsonl
xff --compare=summary LEFT RIGHT --summary=ext --summary=type --format=jsonl
```

Ordinary rows lack a grouping/table discriminator; comparison-scope rows identify their scope but
not whether they belong to the extension or type summary. Consumers must infer table boundaries
from row values and order. Histograms already include a `histogram` identity, demonstrating the
inconsistency. Multiple template summaries make inference even less reliable.

**Acceptance:** every row identifies its record kind, grouping/request, and scope. Preserve original
request identity when distinct templates or repeated groupings coexist. Document root attribution
for standalone comparison JSONL. Add tests that demultiplex records without interpreting group names.
**Start:** summary JSON emitters in `xff/engine/run.cc`; summary schema documentation.

### B06 - P1: A real group named `total` collides with the total row

- [x] Distinguish aggregate-total rows structurally from ordinary group keys.

Create a file named `total`, then run:

```sh
xff DIR -type f '--summary={name}' --format=jsonl
```

The file's bucket and the aggregate row both have `"group":"total"`. The same occurs in
comparison-scope JSONL. A consumer indexing records by group can overwrite a real bucket or
mistake it for the aggregate.

**Acceptance:** add a dedicated row kind or total marker; literal keys remain unchanged. Cover
`total`, `(none)`, empty template values, and multiple summary requests. Coordinate with B05 rather
than making two incompatible schema revisions. Text tables should also distinguish data from totals.
**Start:** summary emitters and CLI JSONL regressions.

### B07 - Closed: Markdown newline-padding diagnosis was incorrect

- [x] Recheck the published output and add a regression for cell normalization.

The original visual inspection reported mismatched final pipes for newline-containing filenames.
Direct measurement of the published binary's output disproves that claim: every header and data
row has the same final-pipe offset. `TableStream::Add` already normalizes Markdown cells before
measuring their widths. A regression now covers LF, CRLF, and escaped pipes alongside ordinary cells.
No production padding change is needed for this report.

The existing renderer measures byte lengths, not Unicode display cells, and passes tabs through.
Those separate source-readability limitations need their own evidence and design; this result does
not claim universal visual alignment for Unicode or control characters.

### B08 - P2: README describes the wrong release archive layout

- [x] Correct the installation/distribution description (PR #855).

README says each platform has one `xff-PLATFORM-ARCH.tar.zst` containing both executables. The
release script and published 0.6.0 assets instead provide separate lean and full archives, each
containing its executable and corresponding symbols. The sentence about release optimization also
reads “combines the hermetic Clang toolchain with size”.

**Acceptance:** document the actual artifact names and contents, link installation instructions,
and check the description against `stage_release_artifacts.sh` and the published manifest.
**Start:** `README.md`.

### B09 - P3: Binary-content documentation gives conflicting sniff lengths

- [ ] Use the exact shared 8,000-byte threshold throughout help and comments.

`-text` documents 8,000 bytes; `-binary` and content help say 8 KiB. The implementation uses
`kBinaryNulSniffBytes = 8'000`. A file with its first NUL at offset 8,050 is classified as text,
consistent with the implementation but inconsistent with an 8,192-byte reading of the help.

**Acceptance:** update registry prose and generated documentation together. Keep boundary cases
at 7,999/8,000/8,050/8,191 explicit in tests; do not change the classification policy accidentally.
**Start:** `xff/content/line_match.h`, `xff/registry/registry.cc`.

## Design and usability improvements

These are observed limitations or deliberate current behaviors, not claims of implementation bugs.

### S01 - P1: Make machine-output composition predictable

- [x] Define comparison/grep JSONL records, authored-output exceptions, producer format rules, and lossless byte values.
- [x] Verify integrated stream regressions and generated help on current main; enforce affected-header checks before commit.

`xff --compare LEFT RIGHT --format=jsonl` emits TSV status records. Adding `--summary=ext` produces
TSV followed by JSONL. Likewise `-grep PATTERN --format=jsonl` retains its normal text output.
The action exception is documented, but the spelling of a whole-run output format still leads users
to expect a consumable machine stream.

**Recommendation:** support structured comparison status records. For content search, offer an
explicit supported structured path or a diagnostic when the request has no effect. Preserve
intentional explicit-action output semantics; do not casually reinterpret `-printf` or child stdout.
**Acceptance:** publish and test a producer × format matrix covering listing, status, patch, grep,
summary, histogram, explicit actions, and combinations. Machine-format success must have a clear
contract about which records can appear.

### S02 - P1: Decide archive member-name collision policy

- [ ] Define behavior when multiple inputs map to the same packed member name.

With `LEFT/same.txt` containing `left` and `RIGHT/same.txt` containing `right`,
`xff LEFT RIGHT -type f --pack=out.tar` succeeds and writes two members both named `same.txt`.
Both payloads exist in the tar, but normal extraction cannot preserve both at that path.
This is an observed ambiguity, not evidence that packing itself discarded a payload.

**Recommendation:** reject duplicate normalized member names by default before publication; permit
an explicit alternative only after deciding its meaning. Do not silently invent root prefixes.
**Acceptance:** test overlapping roots, duplicate roots, distinct roots with equal relative names,
and relevant writer formats. Failure must preserve an existing destination.

### S03 - P2: Remove duplicate comparison tables from the obvious summary workflow

- [ ] Reconcile the shorthand, bare summary, and scope-driver rules.

`--compare=summary --summary-scope=compare` errors and recommends adding `--summary`.
Doing so works but prints the comparison-result table twice before the ordinary table.
Repeatable summaries explain the implementation, but not a useful user intention here.

**Recommendation:** make the shorthand's comparison summary idempotent with a following bare summary,
or recommend `--summary=overall` in the diagnostic and examples. Decide explicitly whether deliberate
repetition of identical explicit summary requests remains supported.
**Acceptance:** a small truth table covers ordinary traversal, status/diff/summary comparison, bare
summary, explicit groupings, explicit scopes, repeated flags, and `--summary=none`.

### S04 - P1: Offer consistent validation of static field names

- [ ] Decide strictness for templates, printf field escapes, and derived grouping keys.

`--template='{nmae}'` succeeds with empty output, whereas `--format=csv --columns=nmae` rejects the
unknown field. The empty-template behavior is documented, but a typo can silently erase an important
report column or collapse a summary into one bucket.

**Recommendation:** reject unknown static fields during template compilation, or provide an explicit
strict mode if literal/permissive templating must remain. Distinguish unknown fields from valid fields
whose value is absent, such as an unset environment value or a capture not reached by the expression.
**Acceptance:** define behavior for unknown qualifiers/algorithms, escaped braces, optional dynamic
namespaces, and malformed syntax. Validate before an action consumes a mistaken template.

### S05 - P1: Add an effective safety-policy view to `--explain`

- [ ] Show final capability decisions, their cause, and their provenance.

Current explain output lists applied arguments and style defaults. For example, one ordinary
safe-profile write setting expands into temp/output/archive settings, but the user must mentally
combine those with active safe mode and unconditional blocks.

**Recommendation:** append a table of effective allow/block decisions, unconditional versus profile
cause, source file/line or CLI origin, category interpretation, and effective directory roots.
Show shadowed decisions where useful. This is an inspection feature, not a new policy language.
**Acceptance:** examples cover an administrator block, user profile override, skipped named sections,
category expansion, overlapping directory roots, and environment-expanded roots.

### S06 - P2: Shorten entry help without losing complete reference coverage

- [ ] Improve the overview and the largest focused help pages.

The published lean binary's default help is 1,029 lines. Focused `--help=summary` with `--width=80`
is 288 lines; `--help=safe` is 571. An expanded topic can dominate the requested flag's explanation.
Compare help includes a roughly 2,600-character paragraph in unwrapped output.

**Recommendation:** start default help with a compact task-oriented guide and clear routes to the full
inventory. Keep focused entries structured as syntax, values/defaults, interaction notes, examples,
then context. Preserve helpful grammar expansion and the no-recursive-expansion rule for long help.
**Acceptance:** readability checks at realistic terminal widths; examples stay copyable; long help
contains each topic once. Review prose/table width independently of verbatim examples.

### S07 - P2: Bring the README to the user's first successful command sooner

- [ ] Put release installation and a small task guide before the large feature matrices.

The Quick Start currently begins with Bazel despite published binaries. The capability matrices
precede installation and can obscure the most useful everyday workflows.

**Recommendation:** lead with lean versus full downloads, a few commands for find/search/compare/report,
then advanced capabilities. Clarify that “disk use” summary examples report apparent entry sizes,
not allocated blocks or recursive directory size. Retain detailed feature comparisons farther down.
**Acceptance:** a user without Bazel can install and run a useful query directly from the README.

### S08 - P2: Make paired console tables readable at normal widths

- [ ] Reduce repeated scope text in comparison-summary headers.

The two-total table already has eight numeric columns; `diff` has twelve, each repeating the scope
name. The reproduced two-total example is roughly 150 columns and the three-category example exceeds
200, before long group names.

**Recommendation:** use grouped headers on the console, concise count/size subheadings, and explicit
behavior when the terminal is narrow. Keep canonical identities in JSON and retain numeric alignment.
Do not change accounting to solve a presentation problem.
**Acceptance:** golden outputs at 80/120/160 columns, long Unicode group labels, and four-plus scopes.

### S09 - P2: Explain denominators, omitted groups, and grouping identity at the table

- [ ] Make ordinary console tables identify their grouping, and make top-limited output self-explanatory.

Repeated plain summaries use a generic `Group` header, without the grouping-specific heading available
in Markdown. Totals and percentages intentionally include rows hidden by `--top`, which is useful
but can look like an arithmetic error.

**Recommendation:** label each table with its grouping; when truncated, state how many groups were
shown out of the complete set. Do not silently switch percentage denominators to visible rows.
**Acceptance:** multiple summaries, zero groups, zero-byte files, top limits, and overlapping scopes
remain distinguishable in plain, Markdown, and JSONL.

### S10 - P2: Extend summaries to CSV/TSV deliberately

- [ ] Decide and document a flat export schema for ordinary and comparison summaries.

These formats are currently rejected for summaries. That is preferable to silent fallback, but it
leaves a common spreadsheet/reporting workflow unsupported.

**Recommendation:** fixed scalar columns for ordinary summaries; canonical prefixed columns for
comparison scopes. Include grouping/scope/row identity and correct quoting of group values.
**Acceptance:** parse exported CSV with a real CSV reader and reconstruct the corresponding JSONL
statistics; test multiple tables and control characters before choosing a concatenation convention.

### S11 - P2: Clarify comparison exit status for automation

- [ ] Add a comparison-specific exit-status example and focused-help explanation.

Observed behavior is useful: with `--exit-match` or `--quiet`, identical trees return 1 and trees
with discrepancies return 0; operational errors return 2. `--compare-select=all` or `none` does not
change that result. This is search-style success, not the usual `diff` convention, and the generic
“something matched” explanation does not convey it.

**Recommendation:** document the existing behavior first. Do not add another exit flag unless a
specific workflow cannot be expressed. Include empty trees, filtered populations, and summary mode.

### S12 - P2: Make potentially ineffective modifiers visible

- [ ] Define a consistent policy for modifiers whose consuming feature is absent.

Examples that currently succeed without effect include `--count` without `-grep`,
`--shards-show=count` without `--shards`, and `--diff-format=y` in tree-diff mode (which remains unified).
Defaults in reusable profiles can justify dormant modifiers; typos and mistaken CLI requests do not.

**Recommendation:** distinguish a dormant configured default from an explicitly ineffective CLI
request. At minimum expose the latter in `--explain`; use errors only where the contract is unambiguous.
**Acceptance:** dependency metadata and tests cover modifiers, cleared consumers, and config reuse.

### S13 - P2: Make expensive operations and retained state inspectable

- [ ] Describe buffering, file reads, and concurrency as separate resources.

Directory reads and eligible child commands are parallel; expression evaluation is coordinated.
Tree comparison retains matched inventories until both walks finish. `--buffer` is not a global
memory cap and does not bound those inventories. Fields such as hash/lines and content predicates
also cause reads, unlike filename/metadata predicates.

**Recommendation:** add execution-cost notes to help and an explain view of buffering/reads. Measure
before optimizing. Investigate the previously intended directory-at-a-time comparison separately,
including ordering, filtered populations, archives, and error handling.
**Acceptance:** benchmarks report time to first output, peak memory, bytes read, and behavior with
large directories, deep trees, network filesystems, and repeated content consumers.

### S14 - P2: Make logical shards versus physical entries explicit

- [ ] Document a small matrix for shard collapsing, status predicates, actions, and reductions.

`--shards` changes logical presentation, while `-shard-status` selects physical files and classifies
only entries reaching that node. This is powerful but easy to misuse with a filter before the status
predicate or an action before deferred classification.

**Acceptance:** examples cover complete/incomplete/duplicate sets, `-size`/fields, summaries, actions,
comparison, and custom schemes. Label whether a count describes physical files or logical sets.
No observed shard correctness defect is asserted here.

### S15 - P2: Publish a concise order-and-limits guide

- [ ] Put `-first`, `-top`, `--top`, `--max-results`, `-quit`, and `--sort` in one comparison table.

These controls intentionally act at different stages: expression filtering, deferred fuzzy selection,
reduction-row selection, implicit listing limits, traversal termination, and ordering. The probe
confirmed that `--max-results=1 -print` does not limit the explicit action, as documented.

**Acceptance:** examples show what reaches actions, summaries, collections, and packing. Explain
which operations stream, which defer, and what `-j` does not reorder. Retain existing distinctions.

### S16 - P2: Make archive capability and safety explanations easier to apply

- [ ] Provide task-based recipes alongside the full operation tables.

The full binary exposes many readers and fewer writers/rewriters. Reading, extraction/mounting,
member deletion, container creation, category controls, and scoped destinations are different axes.
The present tables are accurate in structure but demand substantial study.

**Recommendation:** recipes for read-only inspection, new archive creation, member deletion, and
writing only under a declared output root; show the minimum capabilities each needs. Keep explicit
limitations for unsupported mutation formats and source/archive-member repacking.
**Acceptance:** execute the recipes against isolated fixtures, including blocked variants. Do not
claim a policy sandboxes arbitrary child programs; pager policy remains a separate deferred discussion.

### S17 - P2: Improve configuration inspection and failure navigation

- [ ] Show named-profile availability and disabled-profile reasons clearly in explain output.

The shared grammar, fixed user-config location, ordered composition, global policy, and opt-in rc
discovery form a coherent design. Invalid selected sections and unsanctioned rc globals produced
clear failures in the probes. Their interaction remains hard to predict from a long precedence list.

**Recommendation:** show a short source/application timeline, why a profile is available or disabled,
and whether it contributes globals, predicates, or actions. Add one tiny recipe for each common
case: personal defaults, project profile, mandatory system policy, and environment-derived output root.
**Acceptance:** preserve selector order, atomic disablement, system authority, and non-arming rc files.
Never describe environment substitution as validating trust in the caller's chosen path.

### S18 - P3: Add useful spelling suggestions and position hints

- [ ] Improve unknown-option/predicate diagnostics without accepting approximate spellings.

`--sumary=ext` receives generic help; `-naem` is an unknown predicate. A trailing `-L` or `-g+`
is also reported as an unknown predicate even though it is a recognized leading-only global.
Leading placement works, so this is not a parsing defect.

**Recommendation:** suggest close registered names, or explicitly say “place this short global before
the roots; the double-dash form is position-independent” where such a form exists. Do not invent
`--L` or another unsupported alias. Keep `--` and primary argument runs protected from hoisting.

### S19 - P2: Strengthen cross-feature tests rather than only individual flags

- [ ] Turn this backlog's reproductions into durable tests when their fixes land.

The repository already has parser, matching, config, archive, safety, help, CLI, fuzz, and platform
conformance tests. The uncovered failures cluster at boundaries those individual tests do not prove.

**Acceptance:** cover every field consumer with captures; producer × format combinations; repeated
summary identity; literal total keys; numeric controls in CLI and complete INIs; multi-root archive
collisions; and unusual filenames in every renderer. Add a focused release-artifact smoke corpus.
Keep race/safety adapter tests and platform integration tests distinct from usability probes.

## Feature-family coverage and disposition

| Family                                       | Assessment / evidence                                                                                                                                                     | Follow-up                   |
| :------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | :-------------------------- |
| Expression grammar and styles                | Inventory and docs reviewed; predicates, operators, leading short globals, hoisted long globals, and rg-style behavior sampled. Keep compatibility distinctions explicit. | S15, S18, S19               |
| Name/path/regex matching                     | Glob, case, RE2, exact, and full-build PCRE2 probes behaved coherently; invalid enum values rejected.                                                                     | S06, S18                    |
| Fuzzy and similarity matching                | Subsequence/fzf ranking and reference similarity sampled. Models and thresholds answer different questions; do not imply universal score comparability.                   | S13, S15                    |
| Metadata, ownership, permissions, size, time | Representative predicates and invalid timezone tested; vocabulary/docs inspected. No general defect established.                                                          | S07, S19                    |
| Traversal, depth, links, pruning             | Representative depth, empty, symlink and prune cases passed. Do not confuse entry equality with subtree equality.                                                         | S13, S15                    |
| Ignore rules, hidden files, VCS              | Gitignore, ignore-file, include/exclude, and rg defaults sampled. Independent axes are useful but need task examples.                                                     | S07, S17                    |
| Content search and text classification       | Grep/context/count, content/rxc, text/binary and line endings sampled. Structured search output and exact binary threshold need attention.                                | B09, S01                    |
| Language and MIME classification             | Full-binary matching sampled; overlay/conflict metadata and integration-test boundaries inspected. Extension inference must not be presented as content verification.     | S06, S19                    |
| Hashing, equality, verification              | Hash, cmp, hasheq tally and similarity sampled. Failed hash predicates obey expression/exit conventions rather than automatically making the run an operational error.    | S11, S19                    |
| Fields, templates, printf, defines           | Scalar and rewrite probes passed; unknown-field and capture-consumer inconsistencies reproduced.                                                                          | B01, S04                    |
| Execution, capture, prompting                | Benign exec and capture consumers sampled; dry-run behavior inspected. Interactive prompting and concurrent-child stress were not exercised.                              | B01, S05, S15               |
| File writes, deletion, safety                | Disposable output creation and overwrite blocking tested; dry-run/block precedence and scoped-policy design/tests inspected. No broad safety-certification claim.         | B02, S05, S16, S19          |
| Configuration, environment, rc               | Explicit composition, invalid section, environment default, root rc discovery and rejection of unsanctioned globals tested.                                               | S05, S17                    |
| Listing formats and path handling            | All eight listing formats sampled with unusual filenames; structured escaping is generally sound.                                                                         | B07, S01                    |
| Tree comparison and diff                     | Status, patch, scopes, accounting, formats, filtering and exit behavior sampled.                                                                                          | B05, B06, S01, S03, S08–S13 |
| Summaries, histograms, collections           | Broad reductions sampled; invalid control handling and multi-table schemas need work.                                                                                     | B02–B06, S03, S08–S10       |
| Sharded data                                 | Incomplete-set collapsing and status selection sampled; physical/logical distinction reviewed.                                                                            | S14                         |
| Archive read/write and extras                | Full-build tar.gz packing, virtual walk, content search, dry-run and multi-root collision tested; other formats/mounting reviewed from capability docs/tests.             | S02, S16                    |
| Ordering, concurrency, memory                | Sort/ranking/limits sampled; concurrency architecture and inventory retention inspected, not benchmarked.                                                                 | B02, S13, S15               |
| Terminal display, pager, help                | Explicit width and help navigation sampled; color/pager semantics inspected. Interactive rendering not comprehensively exercised.                                         | S06, S08                    |
| Diagnostics and exit status                  | Invalid values, missing dependencies, config errors, quiet and compare exit behavior sampled.                                                                             | B02, B03, S11, S12, S18     |
| Installation, releases, documentation        | Published artifact layout and version checked; README contradiction confirmed.                                                                                            | B08, S07                    |

## Suggested work sequence

1. **Correctness fixes:** B01, B02, B03, B07, B08, B09. Keep code fixes and documentation in the same
   respective change; no new flag-design decisions are needed beyond valid numeric ranges.
2. **Machine-output contract:** design B05/B06 with S01; implement each claim in a separate PR, coordinating shared renderer/validation boundaries. Decide CSV/TSV support (S10) separately if larger.
3. **Archive collision policy:** decide S02, then implement publication-safe validation and tests.
4. **Everyday usability:** S03, S06–S09, S11, S12, S15, S18, with executable examples.
5. **Policy and operational insight:** S04, S05, S13, S14, S16, S17. Discuss policy choices first;
   benchmark resource changes before adopting an optimization.
6. **Regression discipline:** S19 accompanies every implementation batch rather than waiting until
   the end.

There is no recommendation to expand the feature vocabulary broadly. Making existing combinations
predictable and inspectable will produce more immediate value than adding unrelated flags.
