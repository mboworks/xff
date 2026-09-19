<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Physical files and logical shard sets

`--shards` groups sharded filenames within each directory for listing and ordinary reductions.
It does not concatenate their contents or turn a logical set into a filesystem entry. Without it,
counts describe physical entries. Files outside the selected naming schemes remain individual entries.

| Operation                                                 | Population                                                                             | Size or content used                                                       |
| --------------------------------------------------------- | -------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| Predicates such as `-name`, `-size`, and content tests    | Physical entries reaching the predicate                                                | Each physical file                                                         |
| Explicit actions such as `-print`, `-printf`, and `-exec` | Physical entries reaching the action                                                   | Each physical file                                                         |
| `-shard-status`                                           | Physical files reaching this particular node, grouped per directory for classification | Names and declared indices; it does not verify payload integrity           |
| Ordinary listing with `--shards`                          | One representative per logical set, plus ungrouped entries                             | The representative path; incomplete sets are annotated                     |
| Ordinary summaries and histograms with `--shards`         | One logical set, plus ungrouped entries                                                | Size sums selected members; content-derived fields read the representative |
| Reductions with `-collect` and `--shards`                 | Each named collection grouped independently                                            | Each collection is counted once; collecting into two names is additive     |
| Comparison and its summaries                              | Physical relative-path entries and pairs                                               | Each side's actual file; shard collapsing does not change comparison units |

## Two files, one logical set

Create a small fixture:

```sh
mkdir data
printf aa > data/data-00000-of-00002
printf bb > data/data-00001-of-00002
```

These commands each report four bytes, but their counts differ:

```sh
xff data -type f --summary                         # 2 physical files
xff data -type f --shards --summary                # 1 logical set
xff data -type f -collect --shards --summary        # 1 logical set, counted once
xff data -type f -shard-status complete --summary   # 2 physical representatives
```

`-size 2c` matches both physical files. Combining it with `--shards --summary` still yields one
logical set and four bytes. `-size 4c` matches neither: the predicate runs before logical grouping.
In a grouped summary, `{size}` is four and `{shard}` is two. Content-dependent fields such as a
hash or line count read the representative file, not a concatenation of both members.

Explicit actions remain physical even with `--shards`:

```sh
xff data -type f -printf '%f:%s\n' --shards
```

This prints two action lines, each with size two, followed by the collapsed listing line. Omit
`--shards` when only the physical action output is wanted. Place destructive actions after status
selection if they should act only on the selected files; shard grouping is not a safety control.

## Completeness depends on expression order

```sh
xff data -type f -name '*00000*' -shard-status incomplete -print
xff data -type f -shard-status complete -name '*00000*' -print
```

Both print the first file. The first command supplies only that file to classification, so the set
appears incomplete. The second classifies both members as complete before selecting one for output.
Actions before `-shard-status` have already run and cannot be undone by later classification.

Likewise, `-collect` records entries where it appears. With sorted traversal, collecting before
`-first 1` retains both members for reduction; collecting after it retains only one:

```sh
xff data --sort=name -type f -collect -first 1 --shards --summary  # 1 set, 4 bytes
xff data --sort=name -type f -first 1 -collect --shards --summary  # 1 set, 2 bytes
```

## Missing members and duplicate copies

For an `of` set, the declared total defines the expected indices. A missing index makes the set
incomplete; it remains one logical summary unit, with only present selected members contributing
bytes. `--shards-show=count` makes the listing's member count visible. Schemes without a declared
total cannot establish that an unknown final member is missing.

An opaque tail can identify multiple copies of the same index, for example:

```text
data-00000-of-00002.aaaaaaaa
data-00000-of-00002.bbbbbbbb
data-00001-of-00002
```

`--shards-dedup=first` chooses the lexicographically first copy; `mtime` chooses the newest;
`error` fails instead of accepting duplicates. Only the chosen copy contributes to a logical
summary. `-shard-status superfluous` selects duplicate copies not chosen as representatives and
indices outside a declared total. Inspect these physical files before deciding what to remove.

## Naming schemes and comparison

The built-in schemes are `of`, `dotnum`, and `underscore`. Restrict them with `--shards=of`, or
supply a custom RE2 pattern with required `stem` and `index` captures and optional `total` and
`dup` captures:

```sh
xff data -type f --shards \
  '--shard-pattern=(?P<stem>img)_(?P<index>[0-9]+)_v(?P<dup>[0-9]+)\.raw' --summary
```

Custom patterns are tried before built-ins and also apply to `-shard-status`. Grouping never merges
members across different directories.

Comparison keeps physical units so that one changed member cannot disappear behind a representative:

```sh
xff LEFT RIGHT --compare=summary -type f --shards --summary=ext --summary-scope=compare
```

If each side has two members and only one differs, the comparison has one identical pair and one
different pair; each side's ordinary summary counts two files. Category counts count a paired result
once; category bytes include both sides. See [summary scopes](design-summary-scopes.md) for side totals
and category accounting.

The examples are exercised by `//xff/cli:shard_populations_test`; collection and comparison edge
cases also have focused regression suites.
