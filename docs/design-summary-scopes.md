<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Summary scopes

`--summary` selects statistics; in comparison mode, `--summary-scope` collects their ordered
column groups. Outside comparison it selects root populations. Its default depends on `--compare`:

- Without `--compare`, the default is `all`: combine entries across roots.
- With any `--compare` mode, the default is `compare`: one table with `left-total` and
  `right-total` column groups, including all four comparison categories.
- An explicit `--summary-scope` overrides that default regardless of its position relative to
  `--compare`. Repeating `--summary-scope` uses the last occurrence.

For example, `xff ONE TWO --summary=ext` combines roots, while
`xff --compare=summary LEFT RIGHT --summary=ext` produces one paired extension table, plus the
comparison-result summary. Add `--summary-scope=all` to the latter to request combined statistics
instead. `--summary-scope=root` explicitly requests separate root tables.
Neither combined nor per-root statistics deduplicate files across roots.

This default selects the population only; it does not enable an ordinary summary. Bare
`--compare=summary` still requests only the comparison-result summary. Add `--summary=ext` or
`--summary=overall` to request the corresponding paired table.

Comparison scopes require `--compare` and select an ordered set of column groups:

| Scope         | Column groups                                     |
| ------------- | ------------------------------------------------- |
| `left-total`  | Every participating left entry                    |
| `right-total` | Every participating right entry                   |
| `left`        | Alias for `left-total`                            |
| `right`       | Alias for `right-total`                           |
| `compare`     | `left-total,right-total`                          |
| `left-only`   | Entries present only on the left                  |
| `right-only`  | Entries present only on the right                 |
| `different`   | Different pairs, counted once with combined bytes |
| `identical`   | Identical pairs, counted once with combined bytes |
| `diff`        | `left-only,right-only,different`                  |

`left` aliases `left-total`; `right` aliases `right-total`. Use `left-only` and `right-only`
for unmatched entries. Scope labels and column headings always use canonical names. Lists expand
in order and discard duplicate scopes. A repeated flag replaces the preceding list.
`all` and `root` may be mixed with comparison scopes and retain their separate tables.

Selected comparison scopes share one table per summary grouping. Each scope contributes four
columns: count, percentage of count, size, and percentage of size. Percentages use that column
group's complete population before `--top`. Scopes can overlap: `left-total,left-only` shows a
side total alongside its subset. Columns are never summed into a grand total.
Missing groups display `-` (JSON `null`); present empty files display zero bytes. Side totals
count only their own entries and bytes. Categories count each pair once and sum both sides' bytes.
If a pair's grouping keys differ (such as type or hash), it occupies a single `LEFT -> RIGHT` row.
Extraction streams retain their own counting units; matching keys pair by multiplicity.
The same classification drives comparison output, including directories and special file kinds.
Directory equality is entry-kind equality, not a claim about descendants. Roots themselves
participate when they match the expression. Output selection (`--compare-select`) does not filter statistics.

`--summary-scope` requires an active file summary; absence is a usage error, including when
`--summary=none` clears earlier summaries. `--compare=summary` expands to
`--compare=status --compare-select=none --summary=compare`. Its comparison-only summary
does not satisfy the requirement: add `--summary` or a grouping such as `--summary=ext`.
In comparison mode, bare `--summary` still requests
comparison counts; with an explicit scope it also requests ordinary count and size statistics.
`--summary=compare` reports whole-comparison counts, combined sizes, and their percentages.

```sh
xff ONE TWO THREE --summary=ext --summary-scope=root
xff --compare=summary LEFT RIGHT --summary=ext --summary-scope=compare
xff --compare LEFT RIGHT --summary --summary-scope=diff
```

`--summary=ext --summary-scope=compare` produces one extension table with `left-total` and
`right-total` column groups. `--summary-scope=diff` produces three column groups;
`--summary-scope=identical` produces one. The scope label lists the expanded column selection.
Repeating `--summary` with another grouping produces another table.

Accumulation retains the existing archive, collection, shard, and verification feeds. Combined
and root scopes aggregate during traversal. Category scopes retain per-entry contributions until
pairing assigns categories, without replaying expressions or executing actions again. Entries
without a comparison category do not enter category tables, even if a special reduction feed
(such as failed hash verification) counted them in a whole-root table.

Plain output labels scopes and roots. Flat JSONL rows carry `scope` and `root` fields when scopes are
explicit, and in comparison mode. Empty flat tables produce a zero total; empty comparison column groups are
absent. Sorting, top limits, precision, templates, and size formatting apply separately to each table.

## Counts, sizes, and reconciliation

Ordinary tables show count, percentage of count, size, and percentage of size. Table totals are
computed before top filtering; zero denominators produce zero percent. JSONL uses `count`,
`count_percent`, `bytes`, and `size_percent`; extraction-only summaries omit byte fields because
line extraction has no byte dimension. Sizes sum entry metadata, including directory and symlink
metadata sizes, never recursive directory sizes or allocated disk space.

Comparison tables group by entry type and status. Counts represent comparison results; sizes are
combined left-plus-right bytes. Thus a 100-byte identical pair contributes one result and 200 bytes;
a 100/150-byte different pair contributes one result and 250 bytes. Min/max/average are not used.
Type-changing pairs have one transition bucket rather than being counted twice.

For the same matched population, combined entry count is left-only plus right-only plus twice the
paired result count. Combined bytes equal left bytes plus right bytes. Special reduction feeds
(collections, archive aggregation, extraction, verification, and shards) can intentionally select
or represent a different population; reconciliation only applies when the populations agree.

Comparison-scope JSONL rows carry `scope` (the canonical comma-separated selection), `group`,
and one object per selected scope, named `left-total`, `different`, etc. Each contains the four
statistics, or is `null` for a missing group. Root and combined scopes retain flat rows.

## Markdown tables

`--format=markdown` (alias `md`) renders ordinary, comparison-result, and paired summaries as
Markdown tables, including their headers. For example:

```sh
xff --compare=summary LEFT RIGHT --summary=ext --format=md
```

This emits a comparison-result table followed by one paired extension table. Cell escaping uses
the same renderer as Markdown listings. All rows are buffered so column separators align
vertically in the source. Labels align left; numeric headers and values align right, with matching
Markdown alignment markers. Exact byte values reserve the missing fraction digits and use the same
unit width as scaled SI (`kB`) or IEC (`KiB`) sizes, so integer digits align at the decimal boundary.

Each table has a descriptive heading, such as `## Comparison summary` or `## Summary by extension`.
Scope and left/right root labels are Markdown bullet items below the heading and before the table,
so renderers keep each label on its own line. Explanatory notes below a table are bullet items too. Repeated summary groupings get their own
headings and scope labels. Accounting notes follow their table; a blank line separates them from
the next summary, including in console output. `--no-header` also suppresses Markdown headings.

Summary columns are determined by the grouping;
`--columns` remains a listing control and cannot be combined with Markdown summaries.
`--no-header` omits headers and Markdown separator rows when producing table fragments.
Expression actions retain their own output formats.

## Choosing a command

| Command suffix after `--compare LEFT RIGHT`                            | Per-path output | Summary tables                                                                  |
| :--------------------------------------------------------------------- | :-------------- | :------------------------------------------------------------------------------ |
| (none)                                                                 | Discrepancies   | None                                                                            |
| `--summary`                                                            | Discrepancies   | Comparison results                                                              |
| `--summary=ext`                                                        | Discrepancies   | Paired extension statistics for all categories                                  |
| `--summary --summary-scope=compare`                                    | Discrepancies   | Comparison results, then paired overall statistics                              |
| `--compare=summary`                                                    | None            | Comparison results                                                              |
| `--compare=summary --summary=ext`                                      | None            | Comparison results, then paired extension statistics                            |
| `--compare=summary --summary=ext --summary-scope=left-only,right-only` | None            | Comparison results, then paired extension statistics for one-sided entries only |

Use `compare` for complete side-total columns or `root` for separate whole-root tables. The comparison-result table always describes the full comparison. Neither
`--summary-scope` nor `--compare-select` filters it; only the ordinary summary population changes
with scope. Conversely, `--compare-select` changes only per-path records and requires `--compare`.

For a small example, suppose there is one left-only file of 3 bytes, one right-only file of 5 bytes,
one identical pair of 2-byte files, and one different pair of 7 and 11 bytes. With `-type f`:

- Comparison total: 4 results, 30 combined bytes.
- Paired `compare` total: left 3 entries / 12 bytes, right 3 entries / 18 bytes.
- Combined `all` total: 6 entries / 30 bytes.
- `left-only,right-only` totals: left-only 1 entry / 3 bytes, right-only 1 entry / 5 bytes.
- `diff` totals: left-only 1 / 3 bytes, right-only 1 / 5 bytes, different 1 pair / 18 bytes.
- `identical` total: 1 pair / 4 combined bytes.

Expression filters run independently on each side **before pairing**. A path can be classified
as one-sided when its counterpart exists but fails the expression or is excluded by traversal
settings. For example, `-type f` excludes a directory opposite a regular file, so that path becomes
one-sided rather than a file-to-directory transition. Omit the type filter to compare empty
directories and other entry kinds as well.

Comparison-result tables appear first, regardless of where their summary flags occur. Ordinary
summary requests then appear in request order within each scope. Selected comparison scopes
share one table, emitted at the first comparison scope's position in the scope list. Repeating
`--summary=compare` requests repeated comparison tables; `--summary=none` clears earlier requests.

## Formats and controls

Summary tables support `plain`, `aligned`, `jsonl`, and `markdown` (alias `md`). `aligned` has the
same summary layout as `plain`. CSV, TSV, NUL, and tree formats are listing formats and are rejected
while a summary remains active, rather than silently producing plain text. Fixed summary columns
cannot be replaced by `--columns` in aligned or Markdown mode.

`--format` does not reformat explicit expression actions or per-path comparison records. Status
records remain tab-separated, and diff mode still emits a patch. Use `--compare=summary` (or
`--compare-select=none`) without printing actions when exporting summary-only JSONL or Markdown.
Repeated JSONL tables follow the same order as text tables; they are not wrapped in a JSON array.

`--summary-precision=N` accepts `0` through `9` and controls all summary percentages and scaled
sizes, including JSON percentage fields. Invalid values are errors. `--top=N` accepts a
non-negative integer; `0` removes the limit, and the last occurrence wins. Ordinary groups rank
by size, then count, then name; comparison-scope groups rank by the sum of their displayed column-group sizes (including overlap). Extraction groups
have no byte dimension and rank by count. Totals and percentage denominators include every
group, even those hidden by `--top`. Comparison-result rows are never truncated by `--top`.

## JSONL summary identity

Every summary row carries `record: "summary"`, a zero-based `request` index, the canonical
`summary` grouping, and `scope`. The index refers to the active resolved summary-request list:
`--summary=none` clears that list, and indices restart at zero. An implicit request, including the
comparison summary shorthand or the additional totals requested by bare summary with an explicit
scope, occupies its own index. Filtering requests for a particular output stage preserves indices.
Comparison tables can therefore appear before an earlier ordinary request without losing identity.

Repeated groupings have distinct indices. Aliases use their canonical grouping (`owner` becomes
`user`); templates use `summary: "template"` and carry the exact template string in `template`.
Demultiplex tables by request and scope, adding root identity for per-root tables. Do not infer
which summary produced a row from its group value or its position in the output stream.

Ordinary tables include `root`: the selected root for `scope: "root"`, or an empty string for
combined populations. Comparison-result tables use `scope: "compare"`; comparison-category tables
use their canonical comma-separated scope list. Both comparison table forms include `left_root`
and `right_root`, in argument order. Category and side-total metrics retain their existing nested
objects and `null` for an absent category/group combination.

These identity fields describe summary records only. Explicit printing actions and per-path
comparison output retain the format restrictions described above.

## Aggregate identity

Every summary JSONL row has an `is_total` boolean. It is `true` only for the aggregate row,
including comparison-result and comparison-scope totals. All data rows have `is_total: false`.
A data key named `total` remains `group: "total"`; consumers must use the marker rather than
interpreting the key or assuming a particular row position. Empty and `(none)` data keys are
likewise preserved.

Plain and Markdown tables retain the aggregate label `total`. A data label equal to `total`,
an empty label, or a label beginning with a double quote is shown as a JSON-quoted string.
Quoting leading quotes prevents a real filename containing quotes from imitating an escaped label.
This affects presentation only: JSONL group values keep their original contents.

See [structured output](design-structured-output.md) for comparison and grep records,
producer/format compatibility, and authored-output exceptions.
