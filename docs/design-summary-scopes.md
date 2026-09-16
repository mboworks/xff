<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Summary scopes

`--summary` selects statistics; `--summary-scope` selects their populations. Its default depends
on `--compare`:

- Without `--compare`, the default is `all`: combine entries across roots.
- With any `--compare` mode, the default is `compare`: one table with left and right columns,
  including all four comparison categories.
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

Comparison scopes require `--compare`. `left` and `right` alias `left-only` and `right-only`.
`diff` expands to `left-only,right-only,different`; `compare` additionally includes `identical`.
Lists expand in order and discard duplicate scopes. A repeated flag replaces the preceding list.
`all` and `root` may be mixed with comparison categories; those populations intentionally overlap.

Selected categories share one table per summary grouping. The table aligns left and right
statistics side by side. Each group (for example an extension) appears once, combining entries
from every selected category. Each side shows count, percentage of count, size, and percentage of size.
Percentages use that side's complete population across the selected categories, before `--top`.
Missing groups display `-` (JSON `null`); present empty files display zero bytes. Comparison counts
count pairs once, while ordinary combined statistics count both entries. The category filter uses the same classification as comparison output,
including directories and special file kinds. A type-changing pair remains one result and is
labelled with its left-to-right type transition. Directory equality is entry-kind equality, not a
claim about descendants. Roots themselves participate when they match the expression.
Output selection (`--compare-select`) does not filter statistics.

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

`--summary=ext --summary-scope=compare` produces one paired extension table covering all
comparison categories. `--summary=ext --summary-scope=left,right` produces one paired extension
table restricted to one-sided entries. Categories select the population; they do not split tables.
Repeating `--summary` with another grouping produces another table.

Accumulation retains the existing archive, collection, shard, and verification feeds. Combined
and root scopes aggregate during traversal. Category scopes retain per-entry contributions until
pairing assigns categories, without replaying expressions or executing actions again. Entries
without a comparison category do not enter category tables, even if a special reduction feed
(such as failed hash verification) counted them in a whole-root table.

Plain output labels scopes and roots. Flat JSONL rows carry `scope` and `root` fields when scopes are
explicit, and in comparison mode. Empty flat tables produce a zero total; empty paired sides are
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

Paired JSONL rows carry `scope` (the canonical comma-separated selection), `group`, and
`left`/`right` objects, each containing `root` and
the four statistics, or `null` for a missing group. Root and combined scopes retain flat rows.

## Markdown tables

`--format=markdown` (alias `md`) renders ordinary, comparison-result, and paired summaries as
Markdown tables, including their headers. For example:

```sh
xff --compare=summary LEFT RIGHT --summary=ext --format=md
```

This emits a comparison-result table followed by one paired extension table. Cell escaping uses
the same renderer as Markdown listings. Summary columns are determined by the grouping;
`--columns` remains a listing control and cannot be combined with Markdown summaries.
`--no-header` omits headers and Markdown separator rows when producing table fragments.
Expression actions retain their own output formats.
