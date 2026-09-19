<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Choosing order and limits

Choose the stage you want to limit. An expression filter, a display limit, and stopping the
walk produce different inputs for actions and statistics.

| Control           | Stage                          | What it limits or orders                                                          | What it leaves intact                                                                        |
| :---------------- | :----------------------------- | :-------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------- |
| `-first N`        | Expression evaluation          | First N entries reaching this particular use; later entries evaluate false here   | Traversal, earlier actions, and other branches                                               |
| `-top N`          | Deferred expression evaluation | Best N fuzzy candidates reaching this use; resumes the expression after selection | Earlier actions; rejected candidates can still take an OR alternative                        |
| `--top=N`         | Reduction rendering            | Displayed ordinary summary groups and histogram buckets                           | Matched entries, actions, full totals, percentage denominators, comparison-result rows       |
| `--max-results=N` | Implicit listing               | At most N listed entries after expression evaluation                              | Traversal, explicit actions, summaries, histograms, collections, packing, comparison records |
| `-quit`           | Traversal control              | Stops the remaining walk when reached                                             | Actions already performed and entries already collected                                      |
| `--sort=MODE`     | Traversal order                | Root and directory-entry order, depending on the mode                             | Which entries match, absent order-dependent tests or actions                                 |
| `--sort=score`    | Implicit listing order         | Buffered listing ranked by fuzzy score                                            | The order in which expression actions already ran                                            |

`-first 0`, `-top 0`, and `--max-results=0` select or list nothing at their respective stages.
`--top=0` instead removes the reduction display limit. A limit on displayed rows is not a limit
on memory retained during traversal or deferred selection.

## Expression position controls actions

These examples use a directory `DIR` containing four one-byte regular files: `a.cc`, `ab.cc`,
`ac.h`, and `ad.h`. `--sort=tree` makes the selection reproducible within the root.

```sh
xff --sort=tree DIR -type f -first 2 -print --summary
```

Only the first two files reach `-print`, and the summary counts two. Reaching the `-first`
budget does not stop the walk. Each separate `-first` has its own counter, so an OR branch
can select another population with its own budget.

```sh
xff --sort=tree DIR -type f -collect -first 2 -print --summary
```

All four files reach `-collect`; two reach `-print`. The summary reads the collection and counts
four. Move `-collect` after `-first 2` to collect and summarize only two. The presence of a
collection action selects collection-backed reductions even if that action's branch never runs;
an unfilled collection therefore yields an empty reduction.

```sh
xff --sort=tree DIR -type f -fuzzy a -top 2 -print
```

This defers at `-top`, retains its candidates, and later runs `-print` for the best two.
A compatible fuzzy matcher must precede `-top` on every path reaching it. Ties keep traversal
order. Actions before `-top` have already happened for candidates that may later be rejected;
`-top` cannot undo them.

```sh
xff --sort=tree DIR -type f -print -quit
```

This prints the first file and stops traversal. Subsequent summaries describe the visited
population, not the complete tree.

## Display limits preserve the matched population

```sh
xff DIR -type f --max-results=2
xff DIR -type f --max-results=2 -print
xff DIR -type f --max-results=2 --summary
```

The first command lists two files. The second prints all four because `-print` is an explicit
action. The third summarizes all four; a summary replaces the implicit listing, so its output
is unaffected by `--max-results`.

```sh
xff DIR -type f --summary=ext --top=1
```

This shows one extension group plus the total. The total and percentages still use all matched
files, including hidden groups. With two equally populated extension groups, the displayed
group is 50% of the count, not 100%. `--top` does not truncate comparison-result statistics.

## Packing follows matches, not the listing or collections

Put the output archive outside `DIR` in these examples:

```sh
xff DIR -type f --max-results=1 --pack=all.tar
xff --sort=tree DIR -type f -first 2 --pack=two.tar
xff --sort=tree DIR -type f -collect -first 2 --pack=two-collected.tar --summary
```

The first archive contains all four files. The second contains two. The third also contains two,
while its collection-backed summary counts four. Collection placement changes the reduction's
source; it does not replace the final matched set used by packing. Normal archive capability,
collision, and safety rules still apply.

## Ordering, parallelism, and retained state

Ordinary expression evaluation and implicit streaming output follow the traversal coordinator's
order. `-j` permits parallel directory reads and eligible child commands; it does not make
matching callbacks race to emit entries. Child process output can interleave. Use `-j 1` when
an execution action's exit status must gate the next expression node.

`--sort=dir`, `subtree`, and `tree` differ in how they arrange directories and their descendants;
`roots` and `global` additionally sort root operands. These traversal sorts organize directory
listings rather than collecting every match. See [parallel traversal](design-parallel.md) for
the precise modes and style defaults.

`-top`, `--sort=score`, collections, summary groups, and tree-format output retain different
kinds of state. `--buffer` controls the documented output and collection budgets; neither a
small printed limit nor a small `-top` result count promises a bounded scan or candidate set.

## Comparison is a separate population

Both roots use the same expression, evaluated independently. `-first` and `-quit` can therefore
select different partial populations on each side. A path omitted by one side's expression can
appear one-sided even when it exists physically in both trees. These controls are not a
completed-directory comparison scan limit.

Comparison status and patch records have a fixed relative-path order. `--sort` still affects
evaluation within each root, including actions and order-dependent filters, but does not reorder
the final comparison records. `--max-results` does not cap those records. Use
`--compare-select` to choose categories or `--compare=summary` for statistics without a per-path
listing. [Summary scopes](design-summary-scopes.md) explain comparison populations and denominators.
