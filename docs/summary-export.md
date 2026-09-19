<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CSV and TSV summary exports

Use `--summary=ext --format=csv` for a spreadsheet-ready extension summary, or select `tsv` for
xff's tab-separated, backslash-escaped format. Both contain one header for the entire export.
Repeated summary requests and multiple scopes append data rows under that same header.
`--no-header` removes that header once. There are no headings, legends, blank separators, human
size suffixes, or thousands separators in the export.

```sh
xff project-a project-b --summary=ext --summary=type --summary-scope=root --format=csv
xff --compare=summary LEFT RIGHT --summary=ext --summary-scope=diff --format=csv
```

The second command includes comparison-result rows followed by extension rows with `left-only`,
`right-only`, and `different` column groups. Their counts and byte denominators retain the normal
[summary scope semantics](design-summary-scopes.md). Each category counts pairs once; its bytes
include both sides. Side-total scopes count and size only their own side.

## Columns

| Columns                                                                   | Meaning                                                               |
| ------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| `record`                                                                  | Always `summary`                                                      |
| `request`                                                                 | Zero-based summary request identity, preserving repeated requests     |
| `summary`, `template`                                                     | Canonical grouping and its field template, when applicable            |
| `scope`                                                                   | `all`, `root`, `compare`, or the canonical selected comparison scopes |
| `root`, `left_root`, `right_root`                                         | Root identities where applicable; otherwise empty                     |
| `type`                                                                    | Entry type for comparison-result rows; otherwise empty                |
| `group`, `is_total`                                                       | Actual grouping key and explicit boolean total-row marker             |
| `count`, `count_percent`, `bytes`, `size_percent`                         | Metrics for ordinary or comparison-result rows                        |
| `SCOPE.count`, `SCOPE.count_percent`, `SCOPE.bytes`, `SCOPE.size_percent` | Metrics for each selected comparison column group                     |

Scope-prefixed columns follow the resolved selector order and appear when ordinary comparison
summaries need them. Every row has the same columns; inapplicable cells are empty. A missing
comparison cell is empty, distinct from a present metric of `0`. A summary without a byte dimension,
such as a value-stream extraction, leaves byte cells empty. `is_total=true` distinguishes the
synthetic total from a real group whose name is `total` or empty.

Counts and bytes are exact base-ten integers. Percentages use `--summary-precision`; `--human` does
not change these numeric columns. `--top` limits group rows but leaves totals and denominators
covering the complete population.

## Escaping and composition

CSV uses the existing listing encoder: commas, quotes, CR, and LF trigger quoting, and embedded
quotes are doubled. Use a CSV parser rather than splitting lines or commas: a quoted cell can
contain a newline.

TSV uses the existing listing convention, not CSV quoting: backslashes, tabs, CR, and LF are
encoded as `\\`, `\t`, `\r`, and `\n`. Split tab-separated records, then decode those escapes
once in each cell. Quotes are ordinary characters. UTF-8 content remains UTF-8.

CSV/TSV summary exports cannot include histograms or action output. Select comparison summaries
with `--compare=summary`, or suppress per-path records using `--compare-select=none`. Conflicting
output producers and applicable dry-run previews are rejected before traversal or actions.
Non-output predicates, collections, and captured command results can still feed summaries.
`--columns` remains a listing control and cannot replace this schema.
