# Rg compatibility and text interpretation

`xff --rg [OPTIONS] PATTERN [PATH...]` selects rg argument parsing and matching-line
output. Invoking the executable as `rg` selects the same behavior. `--xff` switches
back to native file filters without changing the rg search patterns. Configuration,
VFS routing, safety controls and ordered result publication remain shared with XFF.
See `--help=rg` for the option reference generated directly from registry metadata.

## Modes and registered spellings

Every expression, global, and compatibility option declares a set of accepted modes:
`find`, `xff`, and/or `rg`. Lookup and generated help use the same enum/set. A shared
spelling can have different meanings in disjoint modes; duplicate compatibility
spellings in the same mode fail compilation. These are XFF's modes, not a claim that
an external find or ripgrep accepts every XFF global.

The global registry owns each flag once. Shared flags declare their rg spelling and
argument rules on the same entry; fixed-value aliases such as `--ignore-case` belong
to the owning value declaration. Rg-only options register additional globals with rg
scope. The compatibility lookup and help table are constexpr projections of those
declarations, including their summaries and translation targets. They are not a
second inventory. The rg frontend handles pattern/root ordering and typed effects.

The initial `--rg` may appear before roots or after native roots and matchers. `--xff` and
`--rg` may switch repeatedly at option boundaries. Switches only change interpretation
of subsequent tokens: they preserve the search, roots, configuration and accumulated
settings. Native roots remain roots: the first positional argument in rg segments supplies
the pattern unless `-e` or `-f` supplies explicit patterns. Additional rg paths extend the
existing roots. After `--xff`, native segments contribute file-filter expressions.
Their boolean expression continues across rg segments, including parenthesized groups.

The global-argument pass processes argv once. A registered mode selector changes the
active mode immediately; each mode uses its own index of explicitly declared spellings.
Required values and native primary operands are consumed before the next option boundary,
so values such as `--rg` remain literal. The pass retains the resolved primary descriptors
and borrowed token views; a second pass builds the native expression once. Neither pass
searches ahead for a mode selector, speculatively tries another grammar, or reconstructs
and reparses a command line. Per-file evaluation uses the resulting expression.

## Select files with XFF, then select lines with rg

XFF combines its native file and metadata predicates with rg's concise content search:

```sh
xff --sort=dir src tests -type f -name '*.cc' -size +1k --rg TODO -n -C2
```

This selects C++ source files larger than 1 KiB under both roots, then prints their `TODO`
lines with line numbers and two context lines on either side. Native content matchers
also select whole files independently of the output pattern:

```sh
xff src -name '*.cc' -rxc license --rg TODO -n
```

Only C++ source files containing `license` are searched for output; the printed lines
match `TODO`. Flags before the roots, native predicates, and trailing rg flags all retain
their own grammar. This combines file-selection expressions with rg output in one command,
without a separate filename pipeline. The initial `--rg` selects the `rg` preset at that
point in configuration order; later mode switches do not reapply it.

## Mode-specific spellings

| Spelling    | XFF/find interpretation                    | Rg interpretation                     |
| ----------- | ------------------------------------------ | ------------------------------------- |
| `-type f`   | Filesystem kind                            | Unsupported; use `--xff` first        |
| `-t cpp`    | XFF filename type; unavailable in find     | Filename-type inclusion               |
| `-o`        | Boolean OR                                 | Only matching portions                |
| `+`         | OR in XFF, rejected as an operator in find | Literal pattern/path                  |
| `-M`        | Default content-match output               | Maximum output columns; takes a value |
| `--unicode` | Valued presentation setting                | UTF-8 search interpretation           |
| `-A N`      | Unsupported; use `--context-after=N`       | N lines after each match              |
| `-B N`      | Unsupported; use `--context-before=N`      | N lines before each match             |
| `-C N`      | Unsupported; use `--context=N`             | N lines before and after each match   |

```sh
xff --rg TODO src --xff -type f --rg -tcpp -o --xff -name '*.h'
```

Context counts can be separate (`-A 2`, `-B 2`, `-C 2`) or attached (`-A2`, `-B2`,
`-C2`), including at the end of a short-option bundle (`-nC2`). Rg options may
precede or follow the pattern and paths. These aliases are available only in rg
segments. The canonical names `--context`, `--context-after`, and `--context-before`
group the output function before its direction and work in all modes and INI files.
The familiar `--after-context` and `--before-context` spellings are aliases in every
mode and in INI files, mapped to the same registry entries. After `--xff`, use either
long spelling or switch back with `--rg` for the short forms. A zero count requests no context on the selected side.

```sh
xff --rg -n -B 1 -A2 TODO src
```

A required option value, native primary operand, or command argument is consumed
before mode switching. Thus `-e --xff`, `-name --rg`, and `-exec echo --rg \;`
contain literal values. Bare `--` in rg mode ends option parsing, including switches.
The execution action remains disallowed in an rg search, independently of parsing.
Configuration presets such as `--config=rg` change defaults without selecting argv
grammar. INI files use native spellings and reject CLI-only grammar switches.

## Input encoding and the C++ contract

Rg search defaults to `--unicode`: content is interpreted as UTF-8. ASCII input is
already valid UTF-8. `--no-unicode` selects arbitrary 8-bit bytes and ASCII word
boundaries. This setting reaches RE2 and PCRE2, including worker-local copies;
changing only word classification would leave dot and character matching inconsistent.
The last encoding option wins. There is no automatic detection or transcoding.
For a legacy 8-bit file, use `--no-unicode`; this preserves its bytes but does not
provide language-aware Latin-1 or Windows-1252 case folding.

```sh
xff --rg -w TODO src
xff --rg --no-unicode -w TODO legacy-data
```

These are rg search options, parsed in rg segments. Native filters keep their
selected regex grammar's semantics. Native XFF's valued `--unicode=auto|always|never`
is a separate presentation control. No native file predicate silently changes its
encoding because an rg content search is active.

`xff::content::text::WordClassifier` checks the common interface:

```cpp
template<typename Classifier>
concept WordClassifier = requires(typename Classifier::View text, std::size_t offset) {
  requires std::same_as<typename Classifier::View,
                        std::basic_string_view<typename Classifier::Char>>;
  { Classifier::WordBefore(text, offset) } -> std::same_as<bool>;
  { Classifier::WordAfter(text, offset) } -> std::same_as<bool>;
};
```

`HasWordBoundaries<Classifier>` is a small constrained consumer. Each implementation
is named for its interpretation: `Ascii`, `Latin1`, `Utf8`, `Utf16`, or `Utf32`.
Offsets count code units, not displayed characters. UTF-8 uses `char` byte views;
UTF-16 and UTF-32 use `char16_t` and `char32_t` views. Each operation inspects the
complete scalar next to the offset; malformed or partial scalars are non-word.
ASCII and arbitrary bytes classify only ASCII letters, digits and underscore.
Latin-1 maps every byte to its ISO-8859-1 scalar, not Windows-1252.

The typed UTF-16/32 implementations operate on already-decoded strings. They do not
make UTF-16/32 files searchable: a file-decoding layer, byte-order policy and offset
mapping remain separate future work. Tests use the same constrained consumer for
all five implementations and reject incomplete or wrongly typed interfaces at
compile time. Unicode fixtures live in dedicated Unicode test files.

Unicode word characters are Alphabetic, Mark, Decimal_Number, Connector_Punctuation
and Join_Control. utf8proc supplies categories; the additional Alphabetic symbol
ranges come from [Unicode 16 `PropList.txt`](https://www.unicode.org/Public/16.0.0/ucd/PropList.txt). Regex syntax and character classes still
belong to the chosen engine: for example, RE2's `\w` is ASCII, while PCRE2's UCP
mode uses Unicode properties. This helper gives `-w` explicit Unicode semantics
without claiming that the engines implement identical regex languages.

PCRE2 uses `PCRE2_MATCH_INVALID_UTF` in UTF-8 mode so malformed bytes form barriers
while valid text elsewhere remains searchable. Byte mode can match those bytes.
See [PCRE2's invalid-UTF contract](https://pcre2project.github.io/pcre2/doc/pcre2unicode/).

## File types and overlapping language candidates

`--file-type=TYPE` includes a filename type and `--file-type-not=TYPE` excludes one.
XFF and rg both accept `-t` and `-T`, with separate or attached values (`-t cpp`,
`-tcpp`, or `-t=cpp`). Native short forms must precede roots; the long forms are
position-independent. Find mode rejects both short aliases. `-type f` retains its
filesystem-kind meaning. Rg also spells the long forms `--type` and `--type-not`.
INI files accept the same native aliases and long forms. Repeated selections are ordered:
the last matching rule wins. If any positive type selection exists, files outside
all selected types are excluded. `all` means every known type; `-Tall` selects
unrecognized file types. Explicit rg `-g` decisions take precedence; native filters
after `--xff` still apply. A file is searched and emitted once even when it belongs
to several selected types.

Built-in types come from XFF's existing language catalog: the curated lean vocabulary,
the optional GitHub Linguist database, and configured `--lang-db=FILE` overlays. No
ripgrep catalog is copied or maintained separately. The vocabularies differ from
ripgrep, and linking the larger database makes additional types available. Canonical
language names, declared aliases, and unambiguous suffix aliases select the same type
case-insensitively: `C++`, `cpp`, and `cc` select C++. An ambiguous alias fails with a
diagnostic; use the canonical name instead. Suffix matching is case-insensitive,
exact filename matching is case-sensitive, and exact names win over the longest suffix.

The catalog preserves **candidates for filtering** separately from the **preferred
label for display**. `.h` belongs to C, C++, and Objective-C; native `-lang` and rg
`-t` check those same candidates. `{lang}`, language metadata fields, colors, and
summary grouping retain the preferred C label, so totals do not count the file three
times. If an ambiguous name has candidates but no preferred label, filters can still
select it and its display label remains empty. This is filename classification,
without content inspection or a claim that a header actually uses all three languages.

```sh
xff --rg -tc -tcpp TODO src       # A shared header is searched once.
xff --rg -tcpp -Tc TODO src       # C excludes shared .h headers as the later rule.
xff --rg -Tc -tcpp TODO src       # C++ includes them again as the later rule.
xff src -lang C++                # Native filtering includes shared headers too.
```

`--type-add=NAME:GLOB` appends a case-sensitive basename glob;
`--type-add=NAME:include:TYPE,...` imports existing definitions; `--type-clear=NAME`
removes a definition, including access through its aliases. Definitions are processed
in order before selection. New custom names contain letters and numbers; existing
canonical names can contain punctuation. `all` is reserved. `--type-list` prints
sorted names and definitions (`language NAME` membership and any custom globs)
without reading search input. Unknown selected/imported types and invalid definitions
fail before traversal. One immutable filename-filter catalog is shared by native and
rg selection; there is no rg-private definition set. Apply JSON language overlays
first, then these edits in resolved system/user/named-section/CLI order, then resolve
selections. Imports copy definitions as they exist at the import point. These
invocation-local edits do not change language classification (`-lang`, `{lang}`),
MIME classification, colors or summary buckets. Use `--lang-db` to change those
language memberships and labels.

Explicit regular-file roots bypass filename-type and rg glob discovery filters;
native predicates and safety controls still apply. Discovered archive members remain
filtered. In native listings the explicit archive file itself remains selected.
Excluded directories are still traversed so matching descendants remain reachable.

All five shared long options work in INI files as well as native commands. For example:

```ini
--type-add=project:*.{cc,h}
[sources]
--file-type=project
```

`xff --config=sources src` lists those files; `xff --rg --config=sources TODO src`
searches their contents. An rg-only alias such as `-tproject` does not belong in INI.

```sh
xff --rg --type-add='local:*.{one,two}' -tlocal TODO src
xff --rg --type-clear=cpp --type-add='cpp:*.cc' --type-list
```

### JSON overlays and deliberate overlap

Ordinary `extensions` and `filenames` choose the preferred label. The arrays
`shared_extensions` and `shared_filenames` add candidates without changing that label:

```json
{
  "ScriptA": { "extensions": ["task"] },
  "ScriptB": { "shared_extensions": ["task"] }
}
```

Both filters match `work.task`; `{lang}` is `ScriptA`. Shared claims can overlap under
any `--lang-conflicts` setting. That flag still resolves conflicting **preferred**
claims inside one JSON file; choosing `first` or `last` does not preserve losing
preferred claims as shared candidates.

Layers apply in order. A new preferred claim replaces older preferred and shared
memberships for that key, then explicit shared claims from the new layer are added.
Thus a project can assign `.h` exclusively or deliberately reintroduce overlap.
Providing a shared list replaces that language's previous shared list of that kind;
`[]` removes it. An omitted list preserves it unless another preferred claim replaces
that key. Preferred extension and filename lists retain their existing independent
replacement behavior. Metadata-only overlays do not change membership.

## Multiline selection

`-U` / `--multiline` lets matches cross line boundaries. `--multiline-dotall` additionally
makes dot match newlines; it has no effect without multiline mode. The `--no-` forms
undo each setting independently. RE2, PCRE2 and fixed strings support multiline.
Other grammars reject multiline and explicit text-mode options rather than silently
ignoring them. `-w` and `-x` override each other in argument order.

Multiline selection retains the complete subject. Full-line output prints each selected
line once. `-o` splits a multiline match into its per-line portions; a newline alone
has no printable portion. Context remains line-based. In multiline mode XFF counts
match occurrences for `-c` and `--count-matches`, including multiple matches within
one line; with `-v`, `-c` counts selected nonmatching lines. Unlike ripgrep's optimization
for patterns proven unable to match newlines, XFF does not switch this explicit multiline
count rule back to counting lines based on pattern analysis.

## Terminal layout and streaming

Headings and line numbers default on for terminal output and off for piped output.
`--heading` / `--no-heading`, `-n` / `-N`, and `--column` / `--no-column` provide explicit
controls. Columns are one-based byte offsets; context lines have no match column.
`--column` also enables line numbers. `-p` / `--pretty` enables headings, line numbers
and color through pipes. With filename output enabled, headings separate files with a
blank line. JSON keeps XFF's existing record schema without headings or ANSI color.

Ordinary line, portion, count, filename and quiet searches read through VFS streams.
Line/context selection retains one incomplete line and at most the requested number
of before-context lines. A preceding native content predicate can supply its existing
per-entry snapshot instead of rereading the file. Selected output is buffered until
EOF so a late binary marker or read failure cannot publish a partial file result.
Therefore this bounds retained _source/context_, not total output memory: long lines,
large before-context windows, selected records and parallel result batches still use
proportional memory. Multiline and staged stdin retain their whole subject.

## Validation and remaining work

R01-R05 in `TODO.md` track this delivery. Tests cover type precedence/composition,
multiline patterns and counts, typed encoding interfaces, UTF-8/byte behavior in RE2
and PCRE2, layout overrides, streaming context across chunk boundaries, late failures,
and binary suppression. Existing native grep tests protect shared rendering behavior.
The separate P01-P08 performance candidates remain in `docs/performance-analysis.md`.

## Combined-stack audit

The follow-up review covers PRs 916-922 together. Regression tests exercise INI
system/user/named-section/CLI ordering, native and rg catalog selection, terminal
versus pipe defaults, UTF-8/byte interpretation, multiline output, native predicates,
mode boundaries, explicit files, and discovered archive members. Serial and four-worker
runs agree for thirteen output/filter combinations over 300 files.

A 208-case differential check against ripgrep 15.2.0 found fifteen output differences.
They concern documented multiline occurrence counting, zero-length matches, and byte
columns. XFF reports the actual start column for multiline matches. Ripgrep can avoid
multiline processing for newline-incapable patterns, affecting both counts and empty
match output; XFF consistently uses the requested multiline path. Zero-length output
at unterminated EOF needs a separate compatibility decision and remains tracked in
TODO.md. This review does not claim complete ripgrep equivalence.
