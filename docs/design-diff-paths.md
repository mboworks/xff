# Diff direction and output paths

## Purpose

Produce patches whose direction and destination paths are explicit and consistent across
one-sided expression actions and two-root comparison. A traversal selects physical entries;
patch paths describe where the resulting change belongs. Rewriting a patch path never changes
the selected files, their contents, traversal, expression fields, or comparison pairing.

## Direction

Left means old content; right means new content.

| Invocation                  | Left / old  | Right / new |
| --------------------------- | ----------- | ----------- |
| `-diff`                     | Absent      | Found entry |
| `-rdiff`                    | Found entry | Absent      |
| `-diff TARGET`              | TARGET      | Found entry |
| `-rdiff TARGET`             | Found entry | TARGET      |
| `--compare=diff LEFT RIGHT` | LEFT        | RIGHT       |

Both primaries accept the same optional TARGET, field templates, and `:STYLE` suffix.
Omitted TARGET and a TARGET that renders to `/dev/null` select the absent side. Optional
arguments never consume an operator or global flag. A dash-leading filename needs `./`.
Reversal exchanges content, labels, modes, hashes, and binary forward/reverse payloads;
it is not a textual substitution of `+` and `-` characters.

Equality remains true and silent; a difference remains false. Empty-file creation and
deletion are differences. Directory entries emit nothing for absent-side patches. Other
special file types are unsupported there. Regular files, executable files, and symlinks
have Git creation/deletion representations, including empty files and binary literals.
Absent-side patches use Git format regardless of STYLE, except `none`, which stays silent.
Explicit file-to-file actions retain their existing output styles, normalization options,
and binary diagnostics. An unreadable explicit TARGET retains the existing false/different result without synthesizing
a creation or deletion patch.

This intentionally reverses the previous explicit `-diff TARGET` direction. Users needing
that direction use `-rdiff TARGET`. Bare `-diff` continues to create files.

Comparison pairs regular files by bytes and executable capability (any execute bit set); all status,
summary, and patch selections use this equality. Other permission bits, ownership and timestamps are
not compared. Symlinks compare their targets without following them unless traversal requests it.

Comparison patch output uses Git headers for additions, removals, text/binary changes, executable-mode
changes and file/symlink replacements. Empty files use metadata-only creation/deletion headers.
Binary changes carry forward and reverse literals with complete blob IDs. A directory-to-file or
file-to-directory replacement combines the directory's child removals/additions with the file patch.
Git cannot represent empty directories; those remain visible in statuses and summaries. Other special
entry types report errors when selected for patch output. One-sided explicit TARGET actions retain
their text formats and binary diagnostic behavior.

Exactly two roots are required. Two file roots pair directly even when their basenames differ;
their displayed paths use their respective basenames instead of `.`. Directory roots pair by
their unmodified root-relative keys. Reverse a comparison by exchanging its root operands.

## Logical paths

Keep three concepts separate:

1. Physical paths are used exclusively for VFS reads and metadata.
2. Logical paths identify files in the patch before user transformations.
3. Output labels are transformed logical paths with literal side prefixes.

For expression actions, the logical path is the found entry relative to its search root;
a file root uses its basename. Both sides of an explicit TARGET comparison use this logical
path by default: TARGET selects comparison bytes, rather than an unrelated patch destination.
This makes `-diff '../old/{relpath}'` produce a patch for the found tree's relative names.
Independent side transformations can express intentionally different old and new names.
For directory comparison, the logical path is the existing pairing key. File-root comparison
uses each file's basename for output while retaining the shared internal pairing key.

## Options

All eight settings are whole-run globals and apply to `-diff`, `-rdiff`, and comparison
patch output. Left and right always refer to the emitted old and new sides, including
reversed actions. The defaults produce `a/` and `b/` labels.

| Option                       | Default  | Meaning                                           |
| ---------------------------- | -------- | ------------------------------------------------- |
| `--diff-left-strip=N`        | `0`      | Remove N leading old-side logical path components |
| `--diff-right-strip=N`       | `0`      | Remove N leading new-side logical path components |
| `--diff-left-root=PATH`      | Empty    | Inject an old-side output directory               |
| `--diff-right-root=PATH`     | Empty    | Inject a new-side output directory                |
| `--diff-left-rewrite=RULES`  | No rules | Rewrite the rebased old-side logical path         |
| `--diff-right-rewrite=RULES` | No rules | Rewrite the rebased new-side logical path         |
| `--diff-left-prefix=TEXT`    | `a/`     | Prepend a literal old-side label prefix           |
| `--diff-right-prefix=TEXT`   | `b/`     | Prepend a literal new-side label prefix           |

Strip, root, and prefix settings use their last supplied value. Rewrite settings accumulate
in command-line order independently for each side. Position among other option kinds never
changes the pipeline order. Empty root/prefix values are meaningful; an empty rewrite is an
invalid rule. Strip counts are nonnegative decimal integers with overflow rejected.

The pipeline is fixed:

```text
logical path
  -> strip N leading components
  -> join injected root and remaining path
  -> apply ordered regex rewrite chains
  -> validate destination path
  -> prepend literal prefix
  -> quote for the selected output format
```

Joining inserts a separator as needed; an injected root may have a trailing slash. It is a
relative directory path. Prefixes are literal: no separator is inserted and no path resolution
is performed. Empty prefixes remove `a/` and `b/`. Their spelling is preserved, but a prefix
must not make the complete output label an absolute or traversing path. `/dev/null` is an
absence sentinel and bypasses all transformations; a present file cannot map to that sentinel.
No stage follows symlinks, reads the host filesystem, or uses `..` to cancel earlier components.

Validate roots and prefixes statically where possible and transformed paths at rendering time.
Destination paths must be nonempty relative file paths without NULs, empty components, `.`,
`..`, or `.git` components. Stripping may not consume the entire logical path. Rewrites may
repair intermediate names, but the stripped path and injected root must independently have
valid structure. Logical `.git` names may be rewritten to ordinary destinations; the restriction
applies to emitted paths and injected roots. Quoting preserves whitespace, control bytes, backslashes, and non-ASCII bytes
in patch headers without changing the underlying filename.

## Regex syntax

Use the existing field substitution grammar and RE2 validation, shared with field templates:

```sh
--diff-left-rewrite='s|^legacy/|src/|'
--diff-right-rewrite='s|\.generated\.cc$|.cc|;s|^include/|public/|'
```

Each command is `s/PATTERN/REPLACEMENT/FLAGS`. Any punctuation delimiter supported by field
rewrites is accepted; choose a delimiter absent from the pattern and replacement rather than
introducing a separate escaping convention. Chains are separated by semicolons outside the
delimited arguments. Subsequent commands may omit `s`, following the field grammar. Only
substitution commands are accepted; field extraction/reducer syntax is not a path rewrite.
Replacement capture references use `\0` through `\9`, with the same validation as field
rewrites. Default replacement changes the first match, `g` changes every match, and `i` enables
case-insensitive matching. Nonmatches leave the path unchanged.

Patterns, replacement references, command structure, and flags are validated and compiled
before traversal, even when no entries match. Compiled programs own their source arguments
and can be reused without parsing per file. Path rewrites use RE2 consistently, independently
of optional filename matching engines. Rewrites run after root injection and before prefixes,
so they can modify injected roots without accidentally matching the synthetic `a/` or `b/`.

## Consistent labels and collisions

Apply one resolved mapping to every corresponding filename: `diff --git`, `---` / `+++`,
context headers, Git binary headers, one-sided binary diagnostics, and other path-bearing metadata.
Git creation/deletion headers retain both mapped names in `diff --git`, and use `/dev/null`
only for the missing side of content headers. Preserve mode and blob identity information.

Track output destinations separately for old and new sides within each patch-producing
operation. Distinct physical entries mapping to the same destination on the same side are an
error. Old and new sides may share a destination; that is the normal update case. Repeated
evaluation of the same physical pair is allowed, and separate expression actions have separate
collision domains. Comparison checks only paths selected for patch output. A collision or
runtime invalid path terminates that operation with a diagnostic and nonzero run status.
Patch output is streaming: earlier complete patches may already have been written when a later
entry fails, so callers must check the command's exit status before applying its output.

## Examples

Create and remove a selected tree:

```sh
xff src -type f -diff > additions.patch
xff src -type f -rdiff > removals.patch
```

Compare a mirror with the found contents as the new side:

```sh
xff new-tree -type f -diff '../old-tree/{relpath}' > changes.patch
```

The relative TARGET above is evaluated relative to the process working directory, just as
other TARGET templates are; use a path appropriate to that directory or an absolute template.
Expression actions visit only their search roots. They cannot discover TARGET-only paths;
use comparison when both trees must be walked.

Strip `vendor/` and inject independent output directories:

```sh
xff --compare=diff old-tree new-tree \
  --diff-left-strip=1 --diff-right-strip=1 \
  --diff-left-root=injected-left --diff-right-root=injected-right
```

For logical `vendor/src/file.cc`, headers become `a/injected-left/src/file.cc` and
`b/injected-right/src/file.cc`. Different injected roots intentionally describe different
old/new labels. Use the same injected root for ordinary updates at the same destination.

Produce labels without the conventional prefixes:

```sh
xff --compare=diff old-tree new-tree --diff-left-prefix= --diff-right-prefix=
```

## Implementation and verification

The presentation patch package owns compiled side transformations, validation, output quoting,
and collision tracking. The field package exposes its compiled substitution program so both
clients share the grammar and validation. The engine resolves one configuration before each
walk, borrows it into evaluation contexts, and keeps collision state in the coordinator.
Collision tracking retains output and source names in memory proportional to the number of
unique emitted paths per operation. Comparison already retains its matched inventories.
Comparison keeps physical pairing keys and worker input unchanged; only rendering consumes
the transformations. Registry/global descriptors carry the complete user-facing help.

Verification covers direction and boolean behavior; both optional argument spellings and
styles; field TARGETs; file and directory roots; each stage and its fixed ordering; repeated
rewrites, captures, flags, and malformed input before an empty walk; both sides and empty
prefixes; every header/diagnostic representation; invalid paths and collisions; and actual
Git application of creation/deletion patches containing text, empty files, binary data,
executable files, symlinks, unusual names, and missing final newlines. Existing comparison,
parser, expression executor, help, and conformance suites must continue to pass with direction
goldens updated intentionally. Run repository formatting/policy checks and the full Bazel suite.

## Explicit comparison ignore files

`--compare-ignore-file=PATH` reads an explicit gitignore-format file for a two-root comparison.
Absolute paths are accepted; relative file paths resolve from the working directory. Patterns
are anchored at each comparison root, not the rules file's directory. For example:

```sh
xff -g+ --compare=diff old-tree new-tree --compare-ignore-file=/path/to/shared.ignore
```

A `/build/` rule prunes the top-level `build` directory in both trees. Unanchored patterns match
at any depth. Negations follow ordinary gitignore behavior: excluded directories are pruned,
so including a child requires keeping its parents traversable.

Choose which roots receive each file's rules:

| Flag                               | Apply patterns relative to |
| ---------------------------------- | -------------------------- |
| `--compare-ignore-file=PATH`       | Both roots                 |
| `--compare-left-ignore-file=PATH`  | Left root only             |
| `--compare-right-ignore-file=PATH` | Right root only            |

For all three flags, distinguish the rule file's location from the patterns' scope:

- **File location:** absolute, or relative to the process working directory. Comparison roots and
  the INI file containing the option do not change that base.
- **Pattern scope:** relative to both comparison roots, the left root, or the right root.

For example, when run from a directory containing `old` and `new`:

```sh
xff --compare old new --compare-left-ignore-file=old/.gitignore
```

This reads `old/.gitignore` from the working directory. A `/build/` rule inside it excludes
`old/build/`. A bare `.gitignore` file argument would instead read the working directory's
`.gitignore`, regardless of which side receives its patterns.

Shared and side-specific files accumulate in argument order. On each side, the last matching applicable rule wins; a
side-specific file has no implicit priority over a shared file. For example:

```sh
xff --compare LEFT RIGHT --compare-ignore-file=/rules/common.ignore \
  --compare-left-ignore-file=/rules/left.ignore --compare-right-ignore-file=/rules/right.ignore
```

Shared files are the recommended default. Asymmetric exclusions can make an existing file appear
left-only or right-only. These explicit scopes are independent of `--compare-ignore`: selecting
`left`, `right`, or `union` for auto-discovered rules does not change an explicit file's chosen side.
Precedence is:

1. CLI `--include` / `--exclude` patterns.
2. Applicable shared and side-specific comparison ignore files, in argument order.
3. Ordinary `--ignore-file` sources, still anchored at their own directory.
4. Automatically discovered rules chosen by `--compare-ignore`.

These flags supplement every `--compare-ignore` policy, including `none`, and do not enable
Git ignore discovery by themselves. `--no-ignore` disables application of comparison ignore files.
Explicit file requests are still validated: empty paths and missing or unreadable files are hard
errors before traversal, including when `--no-ignore` is set. Each option requires `--compare` and
works with status, summary, and patch output. It changes selection, not patch destination paths.

## Complete comparison patch verification

A Git-application round trip compares file bytes, symlink targets and executable capability after
forward and reverse application. The matrix includes empty and nonempty additions/removals,
large multi-block binary literals, text/binary transitions, text without a final newline, unusual
ASCII names, symlink updates, regular/symlink replacements, directory/file replacements, and mode-only
or combined mode/content updates. It runs with one and four comparison workers, and also generates
the inverse comparison by exchanging roots. Pure renderer tests validate paths, modes and unchanged
inputs; worker tests distinguish executable capability from other permission bits.
