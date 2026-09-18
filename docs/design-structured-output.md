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
| Summary                       | Tables                        | Grouping and metric objects        | Aligned and Markdown; others rejected                       |
| Histogram                     | Bars                          | Bucket/value objects               | Aligned bars and Markdown tables; CSV/TSV/NUL/tree rejected |
| Explicit printf/grep template | Authored output               | Authored output                    | Existing action/format validation applies                   |
| Child process                 | Child stdout                  | Child stdout                       | Existing action/format validation applies                   |

Comparison restrictions concern selected per-path output. With `--compare-select=none`, the
requested reductions' format rules apply. `--compare=summary --summary=ext --format=md` can
therefore render tables without per-path records.

CSV and TSV describe one listing schema, not concatenated tables with unrelated columns.
Summary and histogram requests reject those formats rather than emit a mixed-schema stream.

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
