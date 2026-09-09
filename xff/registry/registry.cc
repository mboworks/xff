// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xff/registry/registry.h"

#include <array>
#include <string_view>

#include "absl/types/span.h"
#include "xff/registry/descriptor.h"

namespace xff::registry {
namespace {

// The find / xff expression vocabulary. The parser, --help, `xff help <name>`,
// --explain and the cost-warning all read from here; `summary` is the one-line
// synopsis surfaced by the help system.
constexpr std::array kDescriptors = std::to_array<Descriptor>({
    {
        .name = "-name",
        .alias = "-n",
        .summary = "match the basename against a shell glob",
        .details = "Globs the entry's basename (last path component): `*` matches any run including none, `?` one "
                   "character, `[...]` a class. Unlike the shell a leading dot is matched literally. Case follows "
                   "`--case` - the xff default folds when the volume does (APFS / HFS+ / NTFS), while `--exact` or "
                   "`--config=find` forces a byte-exact compare; `-iname` always folds. Contrast `-path` (whole "
                   "path) and `-regex` (anchored pattern). Example: `xff . -name '*.log'`.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-iname",
        .summary = "match the basename against a shell glob, case-insensitively",
        .details = "The always-case-insensitive `-name`: folds case regardless of `--case` or the volume.",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
    },
    {
        .name = "-path",
        .alias = "-p",
        .summary = "match the whole path against a shell glob",
        .details = "Globs the whole path as printed (from the start point down), not just the basename. Unlike the "
                   "shell, `*` and `?` DO match `/`, so `-path '*/build/*'` matches a build directory at any depth. "
                   "Wildcards and case handling are `-name`'s. GNU spells this `-wholename`.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-ipath",
        .summary = "match the whole path against a shell glob, case-insensitively",
        .details = "The always-case-insensitive `-path` (whole-path glob).",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
    },
    {
        .name = "-wholename",
        .summary = "GNU synonym for -path",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-iwholename",
        .summary = "GNU synonym for -ipath",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
    },
    {
        .name = "-lname",
        .summary = "match the symlink target against a shell glob",
        .details = "Globs the symlink's target text - the path the link points AT, never the resolved destination - "
                   "so a link matches even when its target is missing. Only a symbolic link can match, and with the "
                   "default `-P` (or `-H`) a symlink is seen as itself. Wildcards and case handling are `-name`'s; "
                   "`-ilname` always folds.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-ilname",
        .summary = "match the symlink target against a shell glob, case-insensitively",
        .details = "The always-case-insensitive `-lname` (symlink-target glob).",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
    },
    {
        .name = "-regex",
        .summary = "match the whole path against a regular expression",
        .details = "Matches when the pattern matches the WHOLE path (anchored both ends, like find), not just a "
                   "substring - use `.*` to match anywhere. Dialect is chosen by `-regextype` (RE2 by default); "
                   "capture groups become `{1}`..`{N}` for a following `-exec` / `-printf`. Example: `xff . -regex "
                   "'.*/[0-9]+\\.log'`.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-iregex",
        .summary = "match the whole path against a regular expression, case-insensitively",
        .details = "The case-insensitive `-regex`: same whole-path anchoring and capture-group binding, matching "
                   "without regard to case.",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
    },
    {
        .name = "-regextype",
        .summary = "select the regex dialect for the following -regex/-iregex",
        .kind = Kind::kTest,
        .arity = 1,
    },
    // xff content-search predicates: match the file's CONTENT, not its path. The
    // literal pair (-content/-icontent) sidesteps grep's regex-flavor ambiguity; the
    // regex pair (-rxc/-irxc) is the RE2 counterpart. All read the file (skipping
    // binaries), so they are Cost::kExpensive and the strict find style rejects them.
    {
        .name = "-content",
        .summary = "match a literal substring in the file's content (xff)",
        .details = "Matches when the file contains SUBSTRING literally (no regex metacharacters - the literal pair "
                   "sidesteps grep's flavor ambiguity). Reads the file, so it is expensive; a non-regular, "
                   "unreadable, or binary file (a NUL byte in the first 8 KiB) never matches. `-icontent` folds "
                   "ASCII case. Use `-rxc` for a pattern. This is an xff extension `--config=find` rejects.",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
        .topic = "content",
    },
    {
        .name = "-icontent",
        .summary = "match a literal substring in the file's content, case-insensitively (xff)",
        .details = "The case-insensitive `-content`: folds ASCII case on the literal substring search.",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
        .topic = "content",
    },
    {
        .name = "-rxc",
        .summary = "match the file's content against a regular expression (xff)",
        .details = "The regex counterpart of `-content`: matches when the RE2 pattern is found ANYWHERE in the "
                   "content (unanchored, like grep - use `^` / `$` to anchor), not the whole-file anchoring `-regex` "
                   "applies to the path. Same expensive read and non-regular / unreadable / binary skip; `-irxc` "
                   "folds case. An xff extension `--config=find` rejects.",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
        .topic = "content",
    },
    {
        .name = "-irxc",
        .summary = "match the file's content against a regular expression, case-insensitively (xff)",
        .details = "The case-insensitive `-rxc`: folds case on the content regex search.",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
        .topic = "content",
    },
    // xff content-type predicates: is the file's CONTENT text or binary? Both read the file and use
    // the same NUL-in-first-8-KiB heuristic as the content search above, so they classify a file the
    // same way -content / -grep skip it. Both are file-only: a non-regular or unreadable entry is
    // neither text nor binary (so -text and -binary are NOT complements - `! -text` also matches
    // directories, symlinks and unreadable files).
    {
        .name = "-text",
        .summary = "match a regular text file; -text[=git|posix|windows|apple] picks the definition (xff)",
        .details =
            "TRUE for a regular, readable file whose content is text. Bare `-text` (or `=git`) is the default "
            "heuristic: no NUL byte in the first 8000 bytes (git's buffer_is_binary, also grep/ripgrep), "
            "line-ending-agnostic. One leading UTF-8 BOM is transparent. The strict flavors forbid a NUL ANYWHERE "
            "after that BOM and pin the line ending, "
            "requiring a final terminator (an empty file is vacuously complete): `=posix` = LF only, ends "
            "with a newline; `=windows` = CRLF only; `=apple` = CR only. Reads the file (expensive). A directory, "
            "symlink, device or unreadable file is not text (nor binary), so it never matches - `! -text` is "
            "NOT `-binary`. An xff extension `--config=find` rejects.",
        .kind = Kind::kTest,
        .arity = 0,
        .binding = Binding::kText,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
        .topic = "content",
    },
    {
        .name = "-binary",
        .summary = "match a regular file whose content is binary (a NUL in the first 8 KiB) (xff)",
        .details = "TRUE for a regular, readable file whose content is binary - a NUL byte in the first 8 KiB. The "
                   "precise complement of `-text` WITHIN regular files: a directory, symlink, device or unreadable "
                   "file is neither, so `-binary` is not `! -text`. Reads the file (expensive). An xff extension "
                   "`--config=find` rejects.",
        .kind = Kind::kTest,
        .arity = 0,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    {
        .name = "-eofnl",
        .summary = "match a regular file whose content ends with a newline (LF), or is empty (xff)",
        .details = "TRUE for a regular, readable file whose content ends with a newline / LF (or is empty - a "
                   "zero-line file is complete). Tests ONLY the final terminator, the other axis from "
                   "`-text`/-binary: compose `-text` `-eofnl` for a well-formed (POSIX-style) text file, or `-text` "
                   "! `-eofnl` for the common lint 'a text file missing its final newline'. A CRLF file ends with LF "
                   "too, so it also matches `-eofnl`; `-eofcrlf` is the strict CRLF form. Reads the file "
                   "(expensive). An xff extension `--config=find` rejects.",
        .kind = Kind::kTest,
        .arity = 0,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    {
        .name = "-eofcr",
        .summary = "match a regular file whose content ends with a bare CR, or is empty (xff)",
        .details = "TRUE for a regular, readable file whose content ends with a bare carriage return / CR (or is "
                   "empty). The classic-Mac / `-text:apple` final terminator, and the CR analogue of `-eofnl`: "
                   "compose `-text:apple` `-eofcr` for a well-formed CR-terminated file, or `-text:apple` ! `-eofcr` "
                   "for the missing final CR. A CRLF file ends with LF (not a bare CR), so it does NOT match "
                   "`-eofcr`. Reads the file (expensive). An xff extension `--config=find` rejects.",
        .kind = Kind::kTest,
        .arity = 0,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
        .topic = "content",
    },
    {
        .name = "-eofcrlf",
        .summary = "match a regular file whose content ends with CRLF, or is empty (xff)",
        .details = "TRUE for a regular, readable file whose content ends with CRLF (or is empty). The Windows / "
                   "`-text:windows` final terminator, and the CRLF analogue of `-eofnl`: compose `-text:windows` "
                   "`-eofcrlf` for a well-formed CRLF-terminated file, or `-text:windows` ! `-eofcrlf` for the "
                   "missing final CRLF. Stricter than `-eofnl` (which any LF-ending file, including CRLF, "
                   "satisfies). Reads the file (expensive). An xff extension `--config=find` rejects.",
        .kind = Kind::kTest,
        .arity = 0,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
        .topic = "content",
    },
    // xff -collect: the other half of result-set shaping - an ACTION that holds entries back for a
    // post-walk sink, so a truncating test can narrow what is SHOWN without also narrowing what is
    // summarised. The first user of Binding::kLabel.
    {
        .name = "-collect",
        .summary = "add the entry to a named collection for --summary to reduce (xff)",
        .details =
            "xff extension: an ACTION that adds the entry to a collection instead of printing it, and makes "
            "`--summary` reduce THAT collection rather than what matched. This is what a truncating test cannot "
            "do on its own: a FALSE test removes the entry from every sink, so `-first 10 --summary` summarises "
            "ten entries, never \"all of them, showing ten\". ORDER selects the reading, because these are "
            "primaries rather than position-independent globals: `-collect -first 10 -ls --summary` collects "
            "everything, lists ten, and summarises ALL of them, while `-first 10 -collect --summary` collects "
            "only the ten and summarises those. The second prints no listing because `-collect` is an action, "
            "so the implicit `-print` is suppressed - find's own rule, not a new one. `-collect:NAME` uses a "
            "second collection; a bare `-collect` uses the one named `default`. A NAME is an identifier "
            "(`[A-Za-z_][A-Za-z0-9_]*`), which is what reserves punctuation for modifiers. Two nodes MAY "
            "share a collection, but the later one must SAY so with `!`: "
            "`\\( -type f -collect:all \\) -o \\( -type d -collect:!all \\)` gathers both branches into one "
            "collection, while an unmarked repeat is a usage error - a silently shared collection shows up "
            "only as a doubled total. The modifier is per node, so it cannot loosen the other `-collect` in a "
            "long command the way a whole-run flag would. A collection holds every match until the walk ends, so "
            "`--buffer` bounds it (a row count or a byte budget); exceeding it is an ERROR rather than a "
            "silent truncation, because a summary over part of the walk is indistinguishable from a correct "
            "one. Without `--buffer` there is no cap. Presence is SYNTACTIC, like the implicit print: "
            "a `-collect` in a branch that never runs still switches the summary's source, and the summary is "
            "then empty. Example: `xff . -type f -collect -first 3 -ls --summary`.",
        .kind = Kind::kAction,
        .arity = 0,
        .binding = Binding::kLabel,
        .style = Style::kXff,
    },
    // xff -first: the streaming half of result-set shaping (see TODO.md's pinned design). A TEST
    // that keeps a counter: nothing about a test forbids state, it just owes a truth value.
    {
        .name = "-first",
        .summary = "true for the first N entries this instance sees, false after (xff)",
        .details = "Caps a result set as it streams: TRUE for the first N entries reaching it, FALSE from then "
                   "on. The count is PER USE, not per run, so each `-first` keeps its own budget - "
                   "`\\( -type f -first 10 \\) -o \\( -type d -first 5 \\)` yields ten files AND five "
                   "directories, which no whole-run flag could express. Because a FALSE test removes the entry "
                   "from everything downstream, `-first` genuinely narrows the result set (the summary sees only "
                   "those N); use `-collect` before it when you want the full set summarised and only a few "
                   "shown. Which N you get follows `--sort`, like any other order-dependent behaviour. An xff "
                   "extension `--config=find` rejects. A count that cannot be read is a usage error rather than an "
                   "empty result set - `-first nope` is a typo, and returning nothing would be "
                   "indistinguishable from a tree with no matches; `-first 0` IS valid and means none. "
                   "Example: `xff . -type f -first 20`.",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
        .pure = false,
    },
    // xff -top: the exact ranked half of result-set shaping. Unlike --sort=score this is a TEST in
    // the expression: entries that do not make this instance's N best become false here, and actions
    // to its right therefore run only for the survivors. The evaluator defers at this node and
    // resumes after the walk has selected the exact set.
    {
        .name = "-top",
        .summary = "true for the N best fuzzy matches reaching this instance (xff)",
        .details =
            "Keeps exactly the N entries with the best normalized fuzzy score reaching THIS use, then resumes "
            "the expression for those survivors. It is a TEST, not an output limit: everything to its left has "
            "already happened, while tests and actions to its right run after the walk only for entries that make "
            "the cut. Thus `-collect -top 10 -ls --summary` collects every good match, lists the ten best, and "
            "summarises all of them; `-top 10 -collect --summary` collects and summarises only the ten and prints "
            "no implicit listing. Each `-top` instance owns an independent candidate set, retained until the "
            "post-walk decision so a rejected entry can still take an `-o` alternative to the right. A tie keeps "
            "traversal "
            "order, making the result deterministic under a deterministic `--sort`. A fuzzy matcher must precede "
            "the node on every path that reaches it. All contributing fuzzy matchers must use the same model and "
            "threshold: scores from different models or differently strict predicates do not describe one ordering. "
            "A bare fuzzy matcher has a `0%` threshold. `-fuzzy:fzf:80% foo -top 10` therefore means the ten best "
            "good matches. An xff extension `--config=find` rejects. A malformed or negative count is a usage error; "
            "`-top 0` is valid and keeps none.",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
        .pure = false,
    },
    {
        .name = "-shard-status",
        .summary = "match complete, incomplete, or superfluous physical shards (xff)",
        .details = "Classifies the physical shard files reaching THIS primary after the traversal, then resumes the "
                   "expression. `complete` matches representatives belonging to a set with every expected index; "
                   "`incomplete` matches representatives in a set with a missing expected index; `superfluous` matches "
                   "same-index duplicate copies and indices outside a declared total. A non-shard file matches none. "
                   "Completeness is computed per directory and only from entries that reach this node, so a predicate "
                   "to its left intentionally narrows the cohort being validated; put ordinary actions to its right "
                   "when they should run only for the selected status. This is a physical-file diagnostic independent "
                   "of `--shards`: without `--shards` every selected file is listed, while `--shards` still controls "
                   "logical-set collapsing. Custom `--shard-pattern` definitions and any scheme restriction from "
                   "`--shards=SCHEME,...` apply. Example: `xff data -type f -shard-status incomplete -print`.",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
        .pure = false,
        .topic = "stats",
    },
    // xff -fuzzy: approximate NAME matching, the fzf/fd style of "type a few letters and find it".
    {
        .name = "-fuzzy",
        .summary = "match the basename loosely, optionally requiring a normalized score (xff)",
        .details = "TRUE when PATTERN matches the entry's basename under the selected MODEL. The forms are "
                   "`-fuzzy PATTERN`, `-fuzzy:MODEL PATTERN`, `-fuzzy:PCT% PATTERN`, and "
                   "`-fuzzy:MODEL:PCT% PATTERN`; the default model is `fzf`, and `edit` aliases `levenshtein`. "
                   "The models answer different questions. `sequence` is a literal ordered subsequence: "
                   "`tmh` finds `the_main_header.h`. `fzf` adds fzf EXTENDED-SEARCH expressions: spaces AND terms, "
                   "`|` joins OR alternatives, `'` requests exact matching, `^` and `$` anchor, `!` excludes, and "
                   "`\\ ` embeds a literal space. Quote a query containing spaces for the shell, for example "
                   "`-fuzzy:fzf '^core go$ | rb$ | py$'`. As in fzf, anchors ignore whitespace at the corresponding "
                   "candidate edge. An OR applies only inside its adjacent group: in that example `^core` remains a "
                   "required AND term while `go$`, `rb$`, and `py$` are alternatives. Prefix an exact term with `!'` "
                   "to exclude that fuzzy subsequence; `!^foo` excludes a prefix and `!foo$` a suffix. Backslash only "
                   "escapes the next query character, so shell quoting and fzf query escaping are separate layers. "
                   "`levenshtein` is normalized edit similarity (insert, "
                   "delete, and substitute cost one), while `shingles` is unique character-bigram Jaccard "
                   "similarity. The first two reject a candidate that is not a subsequence/query match; the latter "
                   "two score every candidate. Case follows `--case` like `-name` does; `-ifuzzy` always folds. "
                   "PCT requires normalized quality from 0 through 100, and `{fuzzy}` renders it. Multiple fuzzy "
                   "tests compose through the expression: AND keeps the weakest required "
                   "score and OR the best successful alternative, independent of predicate order. Ranking "
                   "requires every fuzzy test to use the same MODEL and threshold (a bare test means `0%`): "
                   "different domains are valid filters, but do not define one unambiguous ordering. In `fzf`, the "
                   "percentage is the best alignment relative to an exact self-match: characters at a word start, "
                   "matched consecutively, and matched early score higher. Thus nearby candidates can all match while "
                   "receiving different scores; inspect them with `--format=tsv --columns=fuzzy,path`, rank them with "
                   "`--sort=score`, or keep the best N with `-top N`. Use `-name` for a "
                   "glob and `-regex` for a pattern. An xff extension `--config=find` rejects. Example: "
                   "`xff . -fuzzy rdme --columns=fuzzy,path`.",
        .kind = Kind::kTest,
        .arity = 1,
        .binding = Binding::kFuzzy,
        .style = Style::kXff,
    },
    {
        .name = "-fuzzypath",
        .summary = "match the whole path loosely, optionally requiring a normalized score (xff)",
        .details = "`-fuzzy` for the whole PATH instead of the basename - the `-path` to its `-name`. It accepts "
                   "the same `:MODEL[:PCT%]` syntax and scoring, so `-fuzzypath:sequence eng/wlk` finds "
                   "`xff/engine/walk.cc`, which "
                   "no basename match could. It matches far more than `-fuzzy` does (every path shares its "
                   "directories), so it is most useful RANKED: `--sort=score` puts the best match first, and "
                   "`{fuzzy}` renders the score. Case follows `--case`; `-ifuzzypath` always folds. An xff "
                   "extension `--config=find` rejects. Example: `xff . -fuzzypath eng/wlk --sort=score`.",
        .kind = Kind::kTest,
        .arity = 1,
        .binding = Binding::kFuzzy,
        .style = Style::kXff,
    },
    {
        .name = "-ifuzzy",
        .summary = "match the basename loosely, case-insensitively (xff)",
        .details = "The always-case-insensitive `-fuzzy`: accepts the same `:MODEL[:PCT%]` syntax and folds ASCII "
                   "case regardless of `--case` or the volume.",
        .kind = Kind::kTest,
        .arity = 1,
        .binding = Binding::kFuzzy,
        .fold_case = true,
        .style = Style::kXff,
    },
    {
        .name = "-ifuzzypath",
        .summary = "match the whole path loosely, case-insensitively (xff)",
        .details = "The always-case-insensitive `-fuzzypath`: accepts the same `:MODEL[:PCT%]` syntax and folds "
                   "ASCII case regardless of `--case` or the volume.",
        .kind = Kind::kTest,
        .arity = 1,
        .binding = Binding::kFuzzy,
        .fold_case = true,
        .style = Style::kXff,
    },
    // xff -cmp: content comparison. TRUE when the file is byte-for-byte identical to
    // TARGET (a field template rendered per entry, e.g. '{def.B}/{relpath}'); byte-exact
    // and binary-safe, so `! -cmp` selects files that differ from a parallel tree.
    {
        .name = "-cmp",
        .summary = "true when the file's content is byte-identical to TARGET (a field template) (xff)",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    {
        .name = "-similar",
        .summary = "match text whose word-shingle Jaccard similarity to TARGET reaches a threshold (xff)",
        .details = "Compares a regular text file with TARGET, which is a {field} template evaluated per entry, "
                   "using Jaccard overlap of unique contiguous word shingles. Bare `-similar TARGET` uses five-word "
                   "shingles and requires `80%`; qualify it as `-similar:PCT%`, `-similar:WIDTH`, or "
                   "`-similar:WIDTH:PCT%` to override either default. Words are case-folded ASCII alphanumeric or "
                   "UTF-8 byte runs; punctuation and whitespace separate them. A short non-empty file contributes "
                   "one shingle containing all its words. Non-regular, unreadable, or binary files do not match. "
                   "This v1 answers whether each file resembles one reference; whole-tree clustering is deferred.",
        .kind = Kind::kTest,
        .arity = 1,
        .binding = Binding::kSimilarity,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
        .topic = "content",
    },
    // xff -diff[:STYLE]: emit a diff of each match against TARGET (a field template). An
    // ACTION whose truth is TRUE = same (like cmp/diff): silent when equal, prints the diff
    // and is false on a difference. STYLE (u3 default / c / n / y / none) picks the output.
    {
        .name = "-diff",
        .summary = "diff the file against TARGET (a field template); true when equal (xff)",
        .details = "Compares the matched file against TARGET - a {field} template evaluated per entry, so it can name "
                   "a mirror path like `../b/{relpath}` - and is true when they are equal, false on a difference. The "
                   "optional :STYLE picks the output: unified `u3` (default; 3 lines of context), context `c`, "
                   "normal `n`, side-by-side `y`, or `none` for just the boolean. This is a one-sided expression "
                   "action: it visits only the search roots, so it cannot report paths that exist only under TARGET. "
                   "Use `--compare[=status|diff] LEFT RIGHT` to walk two roots with the same options and expression, "
                   "then compare their matches symmetrically. Text files only; expensive.",
        .kind = Kind::kAction,
        .arity = 1,
        .binding = Binding::kStyle,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    {
        .name = "-hash",
        .summary = "print the file digest and path; -hash:ALGO[/ENCODING], sha256 hex default (xff)",
        .details = "Prints `DIGEST  PATH` for each match (an action). `-hash:ALGO[/ENCODING]` picks the algorithm "
                   "(sha256 default; also sha1/sha512/...) and encoding (hex default, or base64). Reads the whole "
                   "file, so it is expensive; the same digest is available as the {hash} field.",
        .kind = Kind::kAction,
        .arity = 0,
        .binding = Binding::kHash,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    // xff -hasheq EXPECTED: true when the file's digest EQUALS EXPECTED (a field template rendered
    // per entry, so it can name a sidecar value like `{def.SUMS}` or a capture). `-hasheq:ALGO
    // [/ENCODING]` picks the algorithm / encoding, like -hash. `! -hasheq` selects files that drift.
    {
        .name = "-hasheq",
        .summary = "true when the digest equals EXPECTED (a field template); -hasheq:ALGO[/ENC] (xff)",
        .details = "Computes the file's digest and is true when it equals EXPECTED - a {field} template evaluated "
                   "per entry, so it can name a sidecar value like `{def.SUMS}` or a capture. "
                   "`-hasheq:ALGO[/ENCODING]` picks the algorithm (sha256 default; also sha1/sha512/...) and "
                   "encoding (hex default, or base64); the same grammar as `-hash` / {hash}. It is a strict equality "
                   "test (hex folds case). `! -hasheq` selects files whose digest differs (drift / corruption). "
                   "Reads the whole file, so it is expensive.",
        .kind = Kind::kTest,
        .arity = 1,
        .binding = Binding::kHash,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    {
        .name = "-type",
        .summary = "match the file type (f, d, l, b, c, p, s)",
        .details = "Matches the entry's type by letter: `f`=regular file, `d`=directory, `l`=symlink, `b`/`c`=block "
                   "/ char "
                   "device, `p`=FIFO, `s`=socket. A GNU-style comma list is any-of, so `-type f,l` matches regular "
                   "files "
                   "or symlinks. Under the default `-P` a symlink is type `l`; `-xtype` tests its target's type "
                   "instead.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-xtype",
        .summary = "match the file type of a symlink's target",
        .details = "Like `-type`, but for a symlink it tests the type of the link's TARGET (the link is followed). A "
                   "broken symlink has no target, so it reports as a symlink and `-xtype l` matches it, matching GNU "
                   "find under the default `-P`. On a non-symlink it is identical to `-type`.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        // xff: match the media (MIME) type derived from the extension, glob-style.
        .name = "-mime",
        .summary = "match the media type by extension against a glob, e.g. -mime 'image/*' (xff)",
        .details = "xff extension: matches the media (MIME) type derived from the filename extension against a "
                   "shell glob, so `image/*` matches png/jpg/... and `text/plain` is exact. The lean binary has a "
                   "curated common-type table; the removable `mime-db` build extra supplies thousands of types, "
                   "and repeatable `--mime-vocabulary=FILE` JSON layers override mappings and metadata. This is "
                   "fast name classification, not content sniffing. The same value is the `{mime}` field; "
                   "`{mime-category}`, `{mime-description}`, `{mime-charset}`, `{mime-compressible}`, and "
                   "`{mime-source}` "
                   "expose its metadata. Matching is always "
                   "case-insensitive (MIME names are case-insensitive per RFC 2045/6838), so `IMAGE/*` behaves like "
                   "`image/*`; `--case` / -i / -s do not affect it. See `--help=content` for the overlay schema and "
                   "conflict policy.",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
    },
    {
        .name = "-lang",
        .summary = "match the language by extension/filename against a glob, e.g. -lang 'C*' (xff)",
        .details = "xff extension: matches the programming language inferred from the extension/filename "
                   "against a shell glob, so `C*` matches C / C++ / C#. The lean binary has a curated common table; "
                   "the removable GitHub Linguist build extra supplies hundreds of canonical records, and repeatable "
                   "`--lang-db=FILE` JSON layers override mappings and metadata. Exact filenames win over "
                   "the longest matching suffix. The same canonical value is `{lang}`; `{lang-type}`, `{lang-color}`, "
                   "`{lang-group}`, and `{lang-source}` expose metadata. A pattern may also match an alias (`cpp` "
                   "matches canonical `C++`). Matching is always case-insensitive and unaffected by `--case` / -i / "
                   "-s. This is fast name classification, not Linguist's content/shebang heuristic classifier.",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
    },
    {
        .name = "-size",
        .summary = "match apparent size with legacy, explicit SI (MB), or IEC (MiB) units",
        .details = "Compares the file's apparent size. A bare number counts 512-byte blocks (find default); a unit "
                   "suffix sets the scale: find's `c`/`w`/`k`/`M`/`G`/`T`/`P`/`E` are retained as legacy binary "
                   "units; explicit "
                   "`B`/`kB`/`MB`/... are SI powers of 1000, and `KiB`/`MiB`/... are IEC powers of 1024. A leading "
                   "+ / - means greater / less than. The size is rounded up to whole units, so `-size +100M` means "
                   "larger than `100 MiB`, while `-size +100MB` means larger than `100 MB`. See `--help=size` and "
                   "`-blocks` for allocated space.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        // xff extension: -size but over ALLOCATED space (st_blocks), not apparent size.
        .name = "-blocks",
        .summary = "match the allocated size (st_blocks); xff's disk-occupancy counterpart to -size",
        .details = "Uses the same `[+|-]N[unit]` grammar as `-size`, but compares allocated disk space rather than "
                   "apparent length. See `--help=size` for legacy, SI, and IEC units.",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
    },
    {
        .name = "-links",
        .summary = "match the hard-link count",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-inum",
        .summary = "match the inode number",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-samefile",
        .summary = "match files that share an inode with FILE",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-fstype",
        .summary = "match the filesystem type (statfs)",
        .details = "Matches when the filesystem holding the entry has the given type name (e.g. `apfs`, "
                   "`ext2/ext3`, `tmpfs`, `nfs`). The recognized names are platform-specific - macOS / BSD report "
                   "`f_fstypename` verbatim, Linux maps the statfs magic to a find-compatible name - so a portable "
                   "expression usually cannot assume one name across OSes.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-uid",
        .summary = "match the numeric owner id",
        .details = "Matches the owner's numeric user id. Like find's numeric tests it accepts `+N` (greater than), "
                   "`-N` (less than), or a bare N (exact). Match by login name with `-user` instead.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-gid",
        .summary = "match the numeric group id",
        .details = "The group counterpart of `-uid`: the numeric group id, with `+N` / `-N` / bare-N. Match by group "
                   "name with `-group` instead.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-user",
        .summary = "match the owner by name",
        .details = "Matches the owner by login name, resolved through the passwd database. A name with no passwd "
                   "entry never matches, but a bare numeric argument is taken as a uid, so `-user 0` behaves like "
                   "`-uid 0`. Exact match only (no `+` / `-`).",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-group",
        .summary = "match the group by name",
        .details = "The group counterpart of `-user`: matches by group name (via the group database), falling back "
                   "to a numeric gid. Exact match only.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-nouser",
        .summary = "match when the owner uid has no passwd entry",
        .details = "Matches when the entry's owner uid has NO entry in the passwd database - an orphaned owner, e.g. "
                   "from a deleted account or an archive unpacked with foreign ids. Takes no argument. See "
                   "`-nogroup` for the group side.",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-nogroup",
        .summary = "match when the group gid has no group entry",
        .details = "Matches when the entry's group gid has no entry in the group database (the group side of "
                   "`-nouser`).",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-newer",
        .summary = "match when mtime is newer than the reference file's mtime",
        .details = "Matches when the entry's mtime is strictly newer than reference FILE's mtime. FILE is stat'd "
                   "following symlinks; a missing or unreadable reference makes it false. This is the base of the "
                   "-newerXY family: `-newerXY FILE` compares the entry's X time against the reference's Y time, "
                   "where each of X and Y is a=access, c=status-change, m=modification, or B=birth - so `-newerac` "
                   "is the entry's atime vs the reference's ctime. `-anewer` / `-cnewer` are the classic aliases. "
                   "When Y is `t` the operand is a TIME STRING, not a file (see `-newermt`). A birth time the "
                   "filesystem never recorded makes an X=B test a hard error and a Y=B reference a silent no-match.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-anewer",
        .summary = "match when atime is newer than the reference file's mtime (== -neweram)",
        .details = "find's classic spelling of `-neweram`: the entry's access time is newer than the reference "
                   "file's modification time. See `-newer` for the -newerXY family.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-cnewer",
        .summary = "match when ctime is newer than the reference file's mtime (== -newercm)",
        .details = "find's classic spelling of `-newercm`: the entry's status-change time is newer than the "
                   "reference file's modification time. See `-newer` for the -newerXY family.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-neweraa",
        .summary = "match when atime is newer than the reference file's atime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerac",
        .summary = "match when atime is newer than the reference file's ctime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-neweram",
        .summary = "match when atime is newer than the reference file's mtime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerca",
        .summary = "match when ctime is newer than the reference file's atime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newercc",
        .summary = "match when ctime is newer than the reference file's ctime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newercm",
        .summary = "match when ctime is newer than the reference file's mtime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerma",
        .summary = "match when mtime is newer than the reference file's atime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newermc",
        .summary = "match when mtime is newer than the reference file's ctime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newermm",
        .summary = "match when mtime is newer than the reference file's mtime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerat",
        .summary = "match when atime is newer than a time string",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerct",
        .summary = "match when ctime is newer than a time string",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newermt",
        .summary = "match when mtime is newer than a time string",
        .details = "The `-newerXt` time-string form: matches when the entry's mtime is newer than TIME - a timestamp "
                   "xff parses (an ISO date / date-time, @epoch, or a relative span), interpreted in `--timezone` - "
                   "rather than a reference file. `-newerat` / `-newerct` / `-newerBt` are the access / "
                   "status-change / birth-time counterparts; the file-reference forms are -newerXY (see `-newer`).",
        .kind = Kind::kTest,
        .arity = 1,
    },
    // Birthtime -newerXY combos (BSD-compat): X or Y = B (birth/creation time).
    {
        .name = "-newerBa",
        .summary = "match when birth time is newer than the reference file's atime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerBc",
        .summary = "match when birth time is newer than the reference file's ctime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerBm",
        .summary = "match when birth time is newer than the reference file's mtime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerBB",
        .summary = "match when birth time is newer than the reference file's birth time",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerBt",
        .summary = "match when birth time is newer than a time string",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-neweraB",
        .summary = "match when atime is newer than the reference file's birth time",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newercB",
        .summary = "match when ctime is newer than the reference file's birth time",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newermB",
        .summary = "match when mtime is newer than the reference file's birth time",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-mtime",
        .summary = "match the data-modification age in days",
        .details = "Matches the data-modification age. A bare integer N counts 24-hour periods with any fraction "
                   "floored (a 2.9-day file is 2); `+N` matches strictly older than N units, `-N` strictly younger. "
                   "A trailing s/m/h/d/w overrides the unit BSD-style (`-mtime -1h` = under an hour old). The "
                   "xff-only word/compound span (`-mtime \"-3 weeks 3 hours\"`, sign required) reaches back a full "
                   "relative duration and is rejected by `--config=find`. See `-mmin` for the minute scale, `-atime` "
                   "/ `-ctime` / `-Btime` for the other time axes.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-mmin",
        .summary = "match the data-modification age in minutes",
        .details = "The minute-scale `-mtime`: N counts whole minutes (floored), `+N` / `-N` for older / younger. "
                   "Integer only - no unit suffix and no compound span (use `-mtime` for those).",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-atime",
        .summary = "match the access age in days",
        .details = "`-mtime` measured on the access time (atime): same N-day scale, `+N` / `-N` polarity, BSD unit "
                   "suffix, and xff compound span. Note atime is often unreliable - many mounts use relatime or "
                   "noatime, so a read may not update it.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-amin",
        .summary = "match the access age in minutes",
        .details = "The minute-scale `-atime` (access time): integer minutes, `+N` / `-N`, no suffix. See `-mmin`.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-ctime",
        .summary = "match the status-change age in days",
        .details = "`-mtime` measured on the status-change time (ctime) - when the inode metadata last changed "
                   "(permissions, ownership, link count, rename), which a content edit also bumps. Same N-day scale, "
                   "`+N` / `-N` polarity, BSD unit suffix, and xff compound span. This is not a creation time; see "
                   "`-Btime` for that.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-cmin",
        .summary = "match the status-change age in minutes",
        .details = "The minute-scale `-ctime` (status-change time): integer minutes, `+N` / `-N`, no suffix. See "
                   "`-mmin`.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-Btime",
        .summary = "match the birth (creation) age in days",
        .details = "`-mtime` measured on the birth (creation) time: same N-day scale, `+N` / `-N` polarity, BSD unit "
                   "suffix, and xff compound span. Birth time is not recorded on every filesystem or kernel - where "
                   "it is absent the test cannot be evaluated and is a hard error (exit 2); `--skip-unsupported` "
                   "downgrades that to a warning and skips the entry.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-Bmin",
        .summary = "match the birth (creation) age in minutes",
        .details = "The minute-scale `-Btime` (birth time): integer minutes, `+N` / `-N`, no suffix. Same "
                   "unrecorded-birth-time handling as `-Btime` (hard error, or a skip under `--skip-unsupported`).",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-used",
        .summary = "match the whole days between atime and ctime",
        .details = "Matches the whole days between an entry's last status change and its last access (atime minus "
                   "ctime) - roughly how long after its metadata changed it was next read. `+N` / `-N` for more / "
                   "fewer days. Shares atime's relatime / noatime caveat (see `-atime`).",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-perm",
        .summary = "match the permission bits (octal or symbolic mode)",
        .details = "Matches the permission (and setuid / setgid / sticky) bits. MODE is octal (`644`, `0755`) or a "
                   "chmod-style symbolic mode (`u+w`, `go=r`, comma-separated clauses). A bare MODE matches exactly; "
                   "`-MODE` matches when ALL the listed bits are set; `/MODE` (GNU) when ANY are. BSD `+octal` is "
                   "any-of like `/`, while a symbolic `+r` stays exact. Example: `-perm -u+x` = owner-executable. "
                   "Contrast `-readable` / `-writable` / `-executable`, which probe the effective user's real "
                   "access.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-maxdepth",
        .summary = "descend at most N directory levels below each start",
        .details = "Limits traversal to at most N levels below each start point: level 0 is a start point itself, 1 "
                   "its immediate children. Like find this is a global positional option - it applies to the whole "
                   "run wherever it sits in the expression, not just to what follows it. Pair with `-mindepth` to "
                   "bound both ends.",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-mindepth",
        .summary = "skip entries fewer than N levels below each start",
        .details = "Skips entries fewer than N levels below a start point, so `-mindepth` 1 excludes the start "
                   "points themselves. A global positional option like `-maxdepth` (applies run-wide).",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-depth",
        .summary = "process a directory's contents before the directory",
        .details = "Visits a directory's contents BEFORE the directory itself (post-order), so a directory is acted "
                   "on only after everything within it - what `-delete` needs, and `-delete` turns this on for you. "
                   "A global positional option; `-d` is the BSD/GNU short spelling.",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-d",
        .summary = "BSD/GNU short spelling of -depth",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-xdev",
        .summary = "do not descend into other filesystems",
        .details = "Confines the walk to the filesystem of each start point: it will not descend into a directory "
                   "that lives on a different mounted device. A global positional option; `-mount` and `-x` are "
                   "synonyms.",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-mount",
        .summary = "GNU/BSD synonym for -xdev",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-x",
        .summary = "BSD synonym for -xdev",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-daystart",
        .summary = "measure age tests from today's local midnight",
        .details = "Measures the day- and minute-scale age tests (`-mtime` / `-atime` / `-ctime` / `-Btime` and "
                   "their -min forms) from the start of today (local midnight) instead of from the exact current "
                   "instant, matching GNU find's `-daystart`. Unlike find, where it only affects tests to its right, "
                   "in xff it applies run-wide regardless of where it appears in the expression.",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-ignore_readdir_race",
        .summary = "skip entries that vanish during the walk (ENOENT)",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-noignore_readdir_race",
        .summary = "report vanished entries as errors (default)",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-empty",
        .summary = "match an empty regular file or empty directory",
        .details = "Matches an empty regular file (size 0) or a directory with no entries; other types never match. "
                   "The directory case reads the directory to check, so it costs a syscall.",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-sparse",
        .summary = "match a file with holes (allocated blocks < apparent size)",
        .details = "Matches a file stored sparsely - fewer 512-byte blocks are allocated than its apparent size "
                   "would need (`st_blocks * 512 < st_size`), i.e. it has holes. A zero-size file is never sparse. "
                   "Compare `-blocks` (allocated space) against `-size` (apparent size).",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-readable",
        .summary = "match entries the current user can read",
        .details = "Matches when the entry is readable by the CURRENT (effective) user, via a real access(2) probe "
                   "rather than a guess from the mode bits - so it reflects ownership and ACLs and can differ from "
                   "`-perm`. See `-writable` / `-executable` for the other access modes.",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-writable",
        .summary = "match entries the current user can write",
        .details = "The write-mode `-readable`: a real access(2) probe for the effective user (see `-readable`).",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-executable",
        .summary = "match entries the current user can execute",
        .details = "The execute/search-mode `-readable`: a real access(2) probe for the effective user. On a "
                   "directory this means search (traverse) permission. See `-readable`.",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-true",
        .summary = "always match",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-false",
        .summary = "never match",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-ls",
        .summary = "print an `ls -dils` style line per entry",
        .details = "Prints one `ls -dils`-style line per match: inode, blocks, mode, links, owner, group, size, "
                   "time, name (find's `-ls`). Columns align to ls/BSD width defaults. For a custom layout use "
                   "`-printf`; for aligned columns of {field}s use `--format=aligned`.",
        .kind = Kind::kAction,
        .arity = 0,
    },
    {
        .name = "-print",
        .summary = "print the path followed by a newline",
        .details = "Prints the path then a newline. This is the DEFAULT action: with no action anywhere in the "
                   "expression xff prints each match, exactly as if `-print` were appended. Naming any action "
                   "(including `-print` itself) suppresses that implicit default; `--implicit-print=yes`|no forces "
                   "it on or off.",
        .kind = Kind::kAction,
        .arity = 0,
    },
    {
        .name = "-print0",
        .summary = "print the path followed by a NUL",
        .details = "Prints the path then a NUL byte instead of a newline, so paths containing spaces or newlines "
                   "survive a pipe into `xargs -0`. The machine-readable counterpart of `-print`; see also "
                   "`--format=jsonl`.",
        .kind = Kind::kAction,
        .arity = 0,
    },
    {
        .name = "-printf",
        .summary = "print a custom format string (%{field} expands the xff field vocabulary)",
        .details = "Prints FORMAT for each match, expanding find's `%` directives (%p path, %f name, %s size, %t/%Ak "
                   "times, ...) and C escapes (\\n, \\t). xff adds `%{NAME}` to reach the full {field} vocabulary "
                   "and its qualifiers (see --help=fields, --help=printf). No trailing newline unless you write one; "
                   "`-printfln` adds the OS line ending. Example: `xff . -printf '%s\\t%p\\n'`.",
        .kind = Kind::kAction,
        .arity = 1,
    },
    {
        // xff: -print with the OS line ending
        .name = "-println",
        .summary = "print the path with the OS line ending (xff)",
        .details = "`-print` but terminated with the OS-native line ending (CRLF on Windows, LF elsewhere) rather "
                   "than always LF. An xff extension `--config=find` rejects.",
        .kind = Kind::kAction,
        .arity = 0,
        .style = Style::kXff,
    },
    {
        // xff: -printf + the OS line ending
        .name = "-printfln",
        .summary = "print a custom format with the OS line ending (xff)",
        .details = "`-printf` plus the OS line ending appended, so you write FORMAT without a trailing `\\n`. An xff "
                   "extension `--config=find` rejects; see `-printf` for the directive vocabulary.",
        .kind = Kind::kAction,
        .arity = 1,
        .style = Style::kXff,
    },
    {
        // xff: the line-output companion of the -rxc content predicate. Bare -grep
        // prints path:line:text; -grep:FORMAT renders a {line}/{text}/... template.
        .name = "-grep",
        .summary = "print each content line matching a regex; -grep:FORMAT for a template (xff)",
        .details = "The line-output companion of `-rxc`: `-grep PATTERN` prints every content line matching the RE2 "
                   "PATTERN as `path:lineno:text` (grep's piped form; a literal substring under "
                   "`--regextype=EXACT`). `-grep:FORMAT PATTERN` renders a {line}/{text}/{match}/{column} template "
                   "instead. Honors `-c` / `--count` (one `path:count` per file) and -A / -B / `--context` "
                   "(surrounding lines, grep-style). Reads the file (expensive); non-regular / unreadable / binary "
                   "files yield nothing. Its truth is \"matched a line\", so it composes with `-o` / `-q`. An xff "
                   "extension `--config=find` rejects.",
        .kind = Kind::kAction,
        .arity = 1,
        .binding = Binding::kFormat,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
        .topic = "content",
    },
    {
        .name = "-fprint",
        .summary = "write -print output to a named file",
        .details = "Writes what `-print` would emit to FILE instead of stdout. FILE is opened once (truncating any "
                   "existing content) and held open for the whole walk, so matches append to it in visit order. This "
                   "is the anchor of the -f* family - each mirrors a stdout action: `-fprint0`, `-fprintf`, `-fls`, "
                   "and the xff `-fprintln` / `-fprintfln`.",
        .kind = Kind::kAction,
        .arity = 1,
    },
    {
        // xff: -fprint with the OS line ending (the file form of -println)
        .name = "-fprintln",
        .summary = "write -println output to a named file (xff)",
        .details = "The file form of `-println` (`-fprint` with the OS line ending). See `-fprint` for the file "
                   "handling; an xff extension `--config=find` rejects.",
        .kind = Kind::kAction,
        .arity = 1,
        .style = Style::kXff,
    },
    {
        .name = "-fprint0",
        .summary = "write -print0 output to a named file",
        .details = "The file form of `-print0` (NUL-terminated paths). See `-fprint` for the file handling.",
        .kind = Kind::kAction,
        .arity = 1,
    },
    {
        .name = "-fprintf",
        .summary = "write -printf output to a named file",
        .details = "The file form of `-printf`: `-fprintf FILE FORMAT` (FILE first, then the format). See `-printf` "
                   "for the directive vocabulary and `-fprint` for the file handling.",
        .kind = Kind::kAction,
        .arity = 2,
    },
    {
        // xff: -fprintf with the OS line ending (the file form of -printfln)
        .name = "-fprintfln",
        .summary = "write -printfln output to a named file (xff)",
        .details = "The file form of `-printfln`: `-fprintfln FILE FORMAT` with the OS line ending appended. An xff "
                   "extension `--config=find` rejects; see `-fprint` for the file handling.",
        .kind = Kind::kAction,
        .arity = 2,
        .style = Style::kXff,
    },
    {
        .name = "-fls",
        .summary = "write -ls output to a named file",
        .details = "The file form of `-ls` (the `ls -dils` line). See `-fprint` for the file handling.",
        .kind = Kind::kAction,
        .arity = 1,
    },
    {
        .name = "-delete",
        .summary = "delete the matched entry",
        .details = "Deletes the matched file or (empty) directory, and implies `-depth` so a directory's contents "
                   "are removed before the directory itself. Destructive, so it is guarded: `--dry-run` previews "
                   "(prints what would be deleted, removes nothing) and `--safe` refuses risky targets. Example: "
                   "`xff . -name '*.tmp' -delete`.",
        .kind = Kind::kAction,
        .arity = 0,
        .safety = Safety::kSafety,
    },
    {
        .name = "-prune",
        .summary = "do not descend into the matched directory",
        .details = "When the matched entry is a directory, do not descend into it (evaluates true). Usually paired "
                   "with `-o` to skip a subtree while still processing everything else: `xff . -name .git -prune -o "
                   "-print`.",
        .kind = Kind::kAction,
        .arity = 0,
    },
    {
        .name = "-quit",
        .summary = "stop the search immediately",
        .details = "Stops the whole search as soon as it is reached (after actions on the current entry have run). "
                   "Handy to emit just the first match: `xff . -name target -print -quit`.",
        .kind = Kind::kAction,
        .arity = 0,
    },
    {
        .name = "-exec",
        .summary = "run a command per match (;) or batched (+)",
        .details = "Runs the command up to a terminator: `;` runs it once per match, `+` batches as many paths as "
                   "fit per invocation (like xargs). `{}` expands to the path; xff also binds `{1}`..`{N}` from "
                   "`-regex` capture groups and the whole {field} vocabulary. The `;` form is synchronous at "
                   "`-j 1`; larger job counts permit up to `N` children concurrently, and their direct stdout / "
                   "stderr may interleave. The `+` form remains an end-of-walk batch. Sensitive: loaded from an "
                   "`--xffrc` file it needs `--allow-exec`. Example: `xff . -name '*.o' -exec rm {} +`.",
        .kind = Kind::kAction,
        .arity = -1,
        .safety = Safety::kSecurity,
        .terminal = true,
    },
    {
        .name = "-execdir",
        .summary = "run a command in the matched entry's directory",
        .details = "Like `-exec`, but each command runs with its working directory set to the matched entry's parent "
                   "and `{}` is the basename - safer against path injection and directory races. `;` per match or "
                   "`+` batched (a batch shares one directory). Example: `xff . -name '*.log' -execdir gzip {} ;`.",
        .kind = Kind::kAction,
        .arity = -1,
        .safety = Safety::kSecurity,
        .terminal = true,
    },
    {
        .name = "-ok",
        .summary = "like -exec, but prompt before each command",
        .details = "Like `-exec` but prompts on stderr before each command and runs it only when the reply begins "
                   "with 'y'; a declined or EOF answer skips that entry. `;`-terminated only (no `+` batching, since "
                   "each run needs its own prompt).",
        .kind = Kind::kAction,
        .arity = -1,
        .safety = Safety::kSecurity,
        .terminal = true,
    },
    {
        .name = "-okdir",
        .summary = "like -execdir, but prompt before each command",
        .details = "Like `-execdir` (runs in the matched entry's directory, `{}` is the basename) but prompts before "
                   "each command, exactly as `-ok` does.",
        .kind = Kind::kAction,
        .arity = -1,
        .safety = Safety::kSecurity,
        .terminal = true,
    },
    {
        // -capture:NAME[=REGEX] cmd... ;
        .name = "-capture",
        .summary = "run a command and bind its output to {capture.NAME} (xff)",
        .details = "xff extension: runs the `;`-terminated command and binds its stdout to `{capture.NAME}` for a "
                   "later `-printf` / `--format` field; `-capture:NAME=REGEX` keeps only REGEX's first capture "
                   "group. A NAME must be an identifier (`[A-Za-z_][A-Za-z0-9_]*`), because it is referenced as "
                   "`{capture.NAME}`; binding one NAME twice is an error, and `-capture:!NAME` on the LATER node "
                   "says the re-bind is meant (per node, so it cannot loosen the other captures in the command). "
                   "Sensitive: from an `--xffrc` file it needs `--allow-exec`. Example: `-capture:branch git "
                   "rev-parse --abbrev-ref HEAD ; -printf '{relpath}\\t{capture.branch}\\n'`.",
        .kind = Kind::kAction,
        .arity = -1,
        .binding = Binding::kLabelRegex,
        .safety = Safety::kSecurity,
        .style = Style::kXff,
    },
    {
        // -capture run in the matched entry's directory
        .name = "-capturedir",
        .summary = "run -capture in the matched entry's directory (xff)",
        .details = "The `-execdir` counterpart of `-capture`: runs the command in the matched entry's directory and "
                   "binds its stdout to `{capture.NAME}`. Same `NAME[=REGEX]` binding and `--allow-exec` gating.",
        .kind = Kind::kAction,
        .arity = -1,
        .binding = Binding::kLabelRegex,
        .safety = Safety::kSecurity,
        .style = Style::kXff,
    },
    {
        .name = "-a",
        .summary = "logical AND (implicit between predicates)",
        .details = "Logical AND of two predicates (`-and` is the long spelling). It is also IMPLICIT between "
                   "juxtaposed predicates, so `-type f -name '*.c'` means `-type f -a -name '*.c'`. Precedence, "
                   "tightest to loosest: `-not`, then `-a`, then (xff) `-xor`, then `-o`, then the `,` comma "
                   "operator; parentheses `( ... )` override it. Evaluation short-circuits.",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    {
        .name = "-and",
        .summary = "logical AND (implicit between predicates)",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    {
        .name = "-o",
        .summary = "logical OR",
        .details = "Logical OR of two predicates (`-or` is the long spelling); binds looser than `-a`, so `A -o B -a "
                   "C` is `A -o (B -a C)`. Short-circuits: the right side is skipped when the left already matched. "
                   "See `-a` for the full precedence order.",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    {
        .name = "-or",
        .summary = "logical OR",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    {
        .name = "-not",
        .summary = "logical negation",
        .details = "Negates the predicate that follows (`!` is the synonym). Binds tightest of the operators, so "
                   "`-not -type d -o -name x` is `(-not -type d) -o -name x`. See `-a` for the full precedence "
                   "order.",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    {
        .name = "!",
        .summary = "logical negation",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    // xff logical operators (no find has them): precedence NOT > AND/-nand > XOR/-xnor > OR/-nor.
    {
        .name = "-xor",
        .summary = "logical XOR; matches exactly one side (xff)",
        .details = "Matches when exactly ONE side is true (never both). One of four xff-only operators find lacks: "
                   "`-xor`, and the negations `-nand` (not both), `-nor` (neither), `-xnor` (both agree). They sit "
                   "between `-a` and `-o` in precedence (`-not` > `-a` / `-nand` > `-xor` / `-xnor` > `-o` / `-nor`) "
                   "and, like all xff-only operators, are rejected by `--config=find`.",
        .kind = Kind::kOperator,
        .arity = 0,
        .style = Style::kXff,
    },
    {
        .name = "-nand",
        .summary = "logical NAND; ! (lhs -a rhs) (xff)",
        .kind = Kind::kOperator,
        .arity = 0,
        .style = Style::kXff,
    },
    {
        .name = "-nor",
        .summary = "logical NOR; ! (lhs -o rhs) (xff)",
        .kind = Kind::kOperator,
        .arity = 0,
        .style = Style::kXff,
    },
    {
        .name = "-xnor",
        .summary = "logical XNOR; matches when both sides agree (xff)",
        .kind = Kind::kOperator,
        .arity = 0,
        .style = Style::kXff,
    },
});

}  // namespace

mbo::types::OptionalRef<const Descriptor> Lookup(std::string_view name) {
  for (const Descriptor& descriptor : kDescriptors) {
    if (descriptor.name == name || (!descriptor.alias.empty() && descriptor.alias == name)) {
      return descriptor;
    }
  }
  return std::nullopt;
}

absl::Span<const Descriptor> All() {
  return kDescriptors;
}

}  // namespace xff::registry
