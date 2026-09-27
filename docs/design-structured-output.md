# Structured output

A format selects the representation of built-in records. It does not rewrite explicit printf
or grep templates, or reinterpret child-process stdout. Authored output remains responsible for
its own syntax, including when combined with JSONL summaries.

## Producer contracts

| Producer                      | Plain                         | JSONL                              | Other formats                                               |
| ----------------------------- | ----------------------------- | ---------------------------------- | ----------------------------------------------------------- |
| Default listing               | Path records                  | Objects with a path                | NUL, CSV, TSV, aligned, Markdown, tree                      |
| Comparison status             | Tab-separated status/path     | Comparison objects                 | Rejected when per-path statuses are selected                |
| Comparison patch              | Unified patch                 | Rejected when patches are selected | Rejected when patches are selected                          |
| Built-in grep                 | Match/context lines or counts | Grep objects                       | Rejected before actions run                                 |
| Summary                       | Tables                        | Grouping and metric objects        | Aligned, Markdown, and CSV/TSV summary exports              |
| Histogram                     | Bars                          | Bucket/value objects               | Aligned bars and Markdown tables; CSV/TSV/NUL/tree rejected |
| Explicit printf/grep template | Authored output               | Authored output                    | Existing action/format validation applies                   |
| Child process                 | Child stdout                  | Child stdout                       | Existing action/format validation applies                   |

Comparison restrictions concern selected per-path output. With `--compare-select=none`, the
requested reductions' format rules apply. `--compare=summary --summary=ext --format=md` can
therefore render tables without per-path records.

CSV and TSV use the listing schema for ordinary listings. With summaries, they use a single
[summary export schema](summary-export.md) across requests, with explicit grouping, scope, and
aggregate-row identity. Summary exports reject action output and per-path comparison records;
histograms reject CSV and TSV.

To consume a whole command as JSONL, use producers with JSONL representations and ensure any
authored output is valid JSONL too. Escaped newlines inside strings do not split records.

Listing records contain `path`; comparison, grep, and summary records carry their documented
`record` tag. Histogram rows contain `histogram`, `bucket`, and `value`; they retain that existing
schema. Summary rows retain `request`, grouping/scope/root identity, and `is_total`.
A built-in grep action can emit records followed by summary and histogram rows in one JSONL
stream. An ordinary summary suppresses the default listing rather than duplicating it.

## String and byte values

Text values remain JSON strings when their original bytes are valid UTF-8. Otherwise the
value is a lossless object such as `{"encoding":"base64","data":"eP8="}` (the bytes `x`
and `0xff`). Base64 uses the standard alphabet with padding and encodes the entire original
value. Consumers should handle this union for paths, roots, group labels, templates, patterns,
and grep text. No replacement characters or invalid UTF-8 are silently inserted.

This rule applies to built-in JSONL producers. Explicit templates and child stdout retain
their authored-output contract. `is_total` remains the authority for summary aggregates,
regardless of whether a group value is text or bytes.

## Comparison status

```json
{
  "record": "comparison",
  "status": "different",
  "path": "src/main.cc",
  "left_root": "LEFT",
  "right_root": "RIGHT"
}
```

`status` is `left-only`, `right-only`, `different`, or `identical`. `path` is relative to the
root operands; the root entry uses `.`. Records retain relative-path order. Selection filters
per-path records, not summary populations.

## Grep

```json
{
  "record": "grep",
  "kind": "match",
  "path": "src/main.cc",
  "root": ".",
  "pattern": "TODO",
  "line": 12,
  "group": 0,
  "text": "// TODO: document this"
}
```

`kind` is `match` or `context`. `line` is one-based. `group` is zero-based within the file and
grep evaluation, incrementing at gaps between emitted lines. JSONL never emits raw `--` group
separators. `text` excludes the line terminator. Quotes, backslashes, and controls are escaped.

With `--count`, each file with matching lines produces:

```json
{
  "record": "grep",
  "kind": "count",
  "path": "src/main.cc",
  "root": ".",
  "pattern": "TODO",
  "count": 2
}
```

Count mode ignores context and supersedes an attached template. Files without matching lines
produce no count record. Repeating a grep action repeats its output, retaining its pattern.
An explicit `-grep:FORMAT` without `--count` retains authored output even with `--format=jsonl`.

## Validation

Unsupported built-in comparison and grep formats fail before traversal or expression actions,
including actions appearing earlier in the expression. Validation does not wait until a match
reaches the output action, so a format error cannot leave earlier deletions or commands executed.

## Grep long-option controls

The content-output controls apply to explicit `-grep PATTERN` actions and to default content
output enabled by `--match-output` (`-M`). Content tests remain file predicates. Each reached explicit action emits
immediately for its own pattern. Separate actions can emit the same line; a later false
predicate does not retract earlier action output. `-o` remains the expression OR operator.

- `--only-matching` emits each nonempty, non-overlapping matched portion, ignoring context.
  `--no-only-matching` restores whole lines. Empty matches produce no output portions.
- `--invert-match` selects nonmatching lines; `--no-invert-match` restores matching lines.
  This differs from negating a file predicate. Inverted lines have no match portions, so
  only-matching emits nothing and match counts are zero, even when the action succeeds.
- `--count` counts selected lines; with `--only-matching` it counts portions.
  `--count-matches` counts portions explicitly. Files without selected lines emit no count.
- `--files-with-matches` emits a path if any line is selected. `--files-without-match`
  (also `--files-without-matches`) emits a path if none is selected, including empty text
  files. Unreadable, binary and non-regular entries are skipped in both modes. The action's
  truth reflects whether the requested file-selection condition holds.
- Among counts and filename-only modes, the last specified mode wins; those modes supersede
  an explicit grep template. Context does not affect them.
- `--with-filename` / `--no-filename` and `--line-number` / `--no-line-number` control
  built-in plain-text prefixes, with the last setting winning. Paths and line numbers remain
  enabled by default. Filename-only output always includes the path.

JSON output retains path and line fields regardless of plain-text prefix settings. Portion
output emits one match record per portion, with `text` holding that portion. Filename modes
emit `record: grep`, `kind: file`, `path`, `root`, `pattern`, and `selected` (true for a file
with selected lines, false for one without). Count records keep their existing shape.
Explicit line templates retain their fields: in portion mode `{text}` is the complete line,
while `{match}` and `{column}` describe the current portion. Inverted/context lines have no
match or column value.

### Default content-match output

`xff -M ROOT -rxc PATTERN` replaces the default path listing with matching content lines.
The equivalent long form, `--match-output`, works anywhere a global can appear and in INI
configuration. `--no-match-output` (`-M-`) restores the path default; the last setting wins.
The short `-M` and `-M-` aliases must precede the roots. No rg argument grammar is enabled.

The file expression retains its normal truth and short-circuit rules. After a file passes,
the output searches the union of all `-rxc`, `-irxc`, `-content`, and `-icontent` patterns
present in the expression, including patterns in branches that were short-circuited.
Each selected line is emitted once. Literal patterns stay literal; each regex retains its
compiled grammar and case setting. Matched portions are non-overlapping, with the longest
portion chosen when different patterns start at the same byte.

Existing grep controls select lines, portions, counts, context, or filenames. Inversion
changes line selection inside accepted files, not the file expression. To select files that
do not contain a pattern, negate the content predicate; `--files-without-match` alone does
not undo a positive file predicate. Whole-file matches spanning newlines need not match any
individual line. Exit status and result limits continue to count accepted entries.

Explicit actions suppress default output as usual; `--implicit-print=yes` can explicitly
retain it. Summaries and other reductions retain their existing precedence. Default match
output supports plain text and JSONL, not listing columns or tabular formats. A content
predicate is required when the default output is active. Non-regular, unreadable, and binary
files retain the existing content-search treatment.

JSONL uses the existing grep records. One pattern retains the `pattern` field; a union uses
`patterns`, an array in expression order. Output modifiers do not remove structural JSON fields.
