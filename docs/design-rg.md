# Rg compatibility and text interpretation

`xff --rg [OPTIONS] PATTERN [PATH...]` selects rg argument parsing and matching-line
output. Invoking the executable as `rg` selects the same behavior. `--xff` switches
back to native file filters without changing the rg search patterns. Configuration,
VFS routing, safety controls and ordered result publication remain shared with XFF.
See `--help=rg` for the option reference generated directly from parser metadata.

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

These are rg search options, parsed before `--xff`. Native filters keep their
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

`-t TYPE` includes a type and `-T TYPE` excludes one. Repeated selections are ordered:
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
fail before traversal. These invocation-local edits do not modify `-lang` or the catalog.

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
