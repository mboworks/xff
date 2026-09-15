<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Summary scopes

`--summary` selects statistics; `--summary-scope` selects their populations. The default
`all` combines entries across roots. `root` emits a labelled set of tables for each root.
Neither setting deduplicates files across roots.

Comparison scopes require `--compare`. `left` and `right` alias `left-only` and `right-only`.
`diff` expands to `left-only,right-only,different`; `compare` additionally includes `identical`.
Lists expand in order and discard duplicate scopes. A repeated flag replaces the preceding list.
`all` and `root` may be mixed with comparison categories; those populations intentionally overlap.

Paired categories produce separate tables for the left and right root, so different sizes and
metadata remain attributable. Comparison counts count pairs once, while ordinary combined
statistics count both entries. Category tables use the same classification as comparison output,
including its directory exclusions. Output selection (`--compare-select`) does not filter statistics.

`--summary-scope` never enables a summary. In comparison mode, bare `--summary` still requests
comparison counts; with an explicit scope it also requests ordinary count and size statistics.
`--summary=compare` itself remains a whole-comparison count and percentage table.

```sh
xff ONE TWO THREE --summary=ext --summary-scope=root
xff --compare=summary LEFT RIGHT --summary=ext --summary-scope=compare
xff --compare LEFT RIGHT --summary --summary-scope=diff
```

Accumulation retains the existing archive, collection, shard, and verification feeds. Combined
and root scopes aggregate during traversal. Category scopes retain per-entry contributions until
pairing assigns categories, without replaying expressions or executing actions again. Entries
without a comparison category do not enter category tables, even if a special reduction feed
(such as failed hash verification) counted them in a whole-root table.

Plain output labels scopes and roots. JSONL rows carry `scope` and `root` fields when scopes are
explicit, and in comparison mode. Empty populations produce a zero total. Sorting, top limits,
precision, templates, and size formatting apply separately to each table.
