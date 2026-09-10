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

#include "xff/cli/globals.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "xff/archive/archive_backend.h"
#include "xff/fuse/fuse_backend.h"
#include "xff/matching/language/language_database_api.h"
#include "xff/matching/mime/database.h"
#include "xff/matching/regex/backend.h"
#include "xff/values/values.h"

namespace xff::cli {
namespace {

// Allowed-value tables for the flags whose synopsis collapses a long value grammar to a
// `<PLACEHOLDER>` (e.g. `--summary[=<GROUP>]`). Rendered by the detail tier as an aligned,
// wrapping `value  meaning` table, so the values stay documented off the synopsis line.
constexpr std::array kCaseValues = std::to_array<ValueDoc>({
    {.value = "sensitive", .meaning = "match exactly (-s-)"},
    {.value = "insensitive", .meaning = "fold case (-i)"},
    {.value = "smart", .meaning = "fold case unless the pattern contains ASCII uppercase (-s / -s+)"},
});
constexpr std::array kMimeConflictValues = std::to_array<ValueDoc>({
    {.value = "error", .meaning = "reject two media types claiming one extension in the same file (default)"},
    {.value = "first", .meaning = "keep the first claim in that file"},
    {.value = "last", .meaning = "keep the last claim in that file"},
});
constexpr std::array kLanguageConflictValues = std::to_array<ValueDoc>({
    {.value = "error", .meaning = "reject two languages claiming one suffix or filename in the same file (default)"},
    {.value = "first", .meaning = "keep the first claim in that file"},
    {.value = "last", .meaning = "keep the last claim in that file"},
});
constexpr std::array kRegextypeValues = std::to_array<ValueDoc>({
    {.value = "RE2", .meaning = "linear-time regular expressions (the default)"},
    {.value = "EXACT", .meaning = "a literal string; metacharacters are plain text"},
    {.value = "FNMATCH", .meaning = "flat shell wildcard; `*` matches any character including `/`"},
    {.value = "GLOB", .meaning = "path-aware shell glob; wildcards and classes are component-local"},
    {.value = "SHGLOB", .meaning = "GLOB plus `{a,b}` brace alternation, so `*.{cc,h}` matches either"},
    {.value = "PCRE2", .meaning = "Perl syntax (lookaround, backreferences); a build extra"},
    // Reserved: accepted here so the resolver's "reserved and not supported yet" error is what the
    // user sees, rather than a generic unknown-value one.
    {.value = "MATCH", .meaning = "", .hidden = true},
});
constexpr std::array kSkipVcsValues = std::to_array<ValueDoc>({
    {.value = "git", .meaning = ".git"},
    {.value = "hg", .meaning = ".hg"},
    {.value = "svn", .meaning = ".svn"},
    {.value = "jj", .meaning = ".jj"},
    {.value = "bzr", .meaning = ".bzr"},
    {.value = "darcs", .meaning = "_darcs"},
    {.value = "cvs", .meaning = "CVS"},
    {.value = "all", .meaning = "every known VCS (the bare-flag default)"},
    {.value = "none", .meaning = "off (same as --no-skip-vcs)"},
});
constexpr std::array kFormatValues = std::to_array<ValueDoc>({
    {.value = "plain", .meaning = "one path per line (the default)"},
    {.value = "nul", .meaning = "NUL-separated paths (for xargs -0)"},
    {.value = "jsonl", .meaning = "one JSON object per match"},
    {.value = "csv", .meaning = "comma-separated columns"},
    {.value = "tsv", .meaning = "tab-separated columns"},
    {.value = "aligned", .meaning = "column-aligned table"},
    {.value = "markdown", .meaning = "a Markdown table (also `md`)"},
    {.value = "tree", .meaning = "an indented directory tree"},
    {.value = "md", .meaning = "", .hidden = true},  // the alias `markdown`'s meaning already names
});
constexpr std::array kSummaryValues = std::to_array<ValueDoc>({
    {.value = "overall", .meaning = "one row aggregated over all matches"},
    {.value = "type", .meaning = "by file type"},
    {.value = "ext", .meaning = "by extension"},
    {.value = "lang", .meaning = "by programming language"},
    {.value = "mime", .meaning = "by media (MIME) type"},
    {.value = "user", .meaning = "by owner"},
    {.value = "group", .meaning = "by owning group"},
    {.value = "hash", .meaning = "by file digest (dedup: identical files share a bucket; reads every file)"},
    {.value = "hash-verification", .meaning = "verified / failed tally from exactly one reached `-hasheq`"},
    {.value = "{template}", .meaning = "by any field value, e.g. `--summary='{ext}-{type}'`"},
});
// The short sign ladders, each spelled out so IsKnownGlobal needs no literal list of its own. The
// level is the only shared part; see GlobalFlag::sign_forms.
constexpr std::array kArchiveShorts = std::to_array<std::string_view>({"-z", "-z+", "-z++", "-z-"});
constexpr std::array kArchiveWriteShorts = std::to_array<std::string_view>({"-Z", "-Z+", "-Z++", "-Z-"});
constexpr std::array kGitignoreShorts = std::to_array<std::string_view>({"-g", "-g+", "-g-"});
constexpr std::array kCaseShorts = std::to_array<std::string_view>({"-s", "-s+", "-s-"});

constexpr std::array kArchiveValues = std::to_array<ValueDoc>({
    {.value = "none", .meaning = "an archive is one plain file (find behavior; the find-style default)"},
    {.value = "roots", .meaning = "dive only when a search root is itself an archive (the xff-family default)"},
    {.value = "all", .meaning = "also dive archives found during the walk (what bare `--archive` selects)"},
    {.value = "any", .meaning = "`all`, plus offer EVERY file to the reader, not only container-looking names"},
});
constexpr std::array kColorSchemeValues = std::to_array<ValueDoc>({
    {.value = "auto",
     .meaning = "ls OR xff: the theme when $LS_COLORS / $LSCOLORS is set, else xff's scheme (the "
                "default; also spelled `ls+xff`, `ls-or-xff` or `default`)"},
    {.value = "ls", .meaning = "the theme alone ($LS_COLORS, else $LSCOLORS): what it omits prints plain, as in ls"},
    {.value = "merged",
     .meaning = "the theme where it speaks, xff's type/language colour where it does not (also `ls-and-xff`)"},
    {.value = "xff", .meaning = "xff's built-in type and language scheme, ignoring $LS_COLORS"},
    {.value = "default", .meaning = "", .hidden = true},     // spelled out in `auto`'s meaning
    {.value = "ls+xff", .meaning = "", .hidden = true},      // ditto
    {.value = "ls-or-xff", .meaning = "", .hidden = true},   // ditto
    {.value = "ls-and-xff", .meaning = "", .hidden = true},  // spelled out in `merged`'s meaning
});
constexpr std::array kArchiveAggregateValues = std::to_array<ValueDoc>({
    {.value = "members", .meaning = "count what is INSIDE a dived container, not the container (the default)"},
    {.value = "container", .meaning = "count containers as the files they are on disk, never their members"},
    {.value = "both", .meaning = "count each container AND its members - the archive plus its unpacked copy"},
});
constexpr std::array kArchivePrefixValues = std::to_array<ValueDoc>({
    {.value = "(empty)", .meaning = "no prefix - a bare path, `a.tgz!inner/x` (the default)"},
    {.value = "URI", .meaning = "the receiving ecosystem's own URL: `phar://`, `jar:file:...!/`, else `archive:`"},
    {.value = "STRING", .meaning = "any other value is used literally, e.g. `--archive-prefix=vfs:`"},
});
constexpr std::array kShardsValues = std::to_array<ValueDoc>({
    {.value = "auto", .meaning = "recognize every built-in scheme (the default when bare `--shards`)"},
    {.value = "of", .meaning = "only `<stem>-<index>-of-<total>` (TFRecord-style)"},
    {.value = "dotnum", .meaning = "only `<stem>.<NNN>` (7-Zip-style volumes)"},
    {.value = "underscore", .meaning = "only `<stem>_<NNN>`"},
});
constexpr std::array kShardsShowValues = std::to_array<ValueDoc>({
    {.value = "first", .meaning = "the representative (lowest-index) shard's path (the default)"},
    {.value = "wildcard", .meaning = "the masked-index name, e.g. `arc.???` (or `f-` idx `-of-003`)"},
    {.value = "count", .meaning = "the wildcard name plus the shard count, e.g. `arc.??? (3 shards)`"},
});
constexpr std::array kShardsDedupValues = std::to_array<ValueDoc>({
    {.value = "first", .meaning = "keep the lexicographically-first name among same-index copies (the default)"},
    {.value = "mtime", .meaning = "keep the newest by modification time (ties break on name)"},
    {.value = "error", .meaning = "treat a same-index duplicate as an error (non-zero exit)"},
});
constexpr std::array kGitignoreValues = std::to_array<ValueDoc>({
    // Listed low to high, the same order the short ladder reads in: `-g-`, `-g`, `-g+`.
    {.value = "off", .meaning = "ignore .gitignore files entirely (also `-g-`, no / false / 0)"},
    {.value = "auto", .meaning = "respect .gitignore only inside a git working tree (a bare `-g` / `--gitignore`)"},
    {.value = "on", .meaning = "respect it anywhere, git repository or not (also `-g+`, yes / true / 1)"},
});
constexpr std::array kColorValues = std::to_array<ValueDoc>({
    {.value = "auto", .meaning = "colour only when stdout is a terminal (the default; a bare --color is always)"},
    {.value = "always", .meaning = "colour even through a pipe or pager (also on / yes / true / 1)"},
    {.value = "never", .meaning = "no colour at all (also off / no / false / 0)"},
});
constexpr std::array kUnicodeValues = std::to_array<ValueDoc>({
    {.value = "auto", .meaning = "Unicode connectors when the locale is UTF-8, else ASCII (the default)"},
    {.value = "always", .meaning = "force the Unicode connectors (also on / yes / true / 1)"},
    {.value = "never", .meaning = "force the ASCII connectors (also off / no / false / 0)"},
});
constexpr std::array kHumanValues = std::to_array<ValueDoc>({
    {.value = "si", .meaning = "powers of 1000: kB, MB, GB (the default; also 1000, --si, a bare --human)"},
    {.value = "iec", .meaning = "powers of 1024: KiB, MiB, GiB (also 1024)"},
    {.value = "off", .meaning = "plain byte counts, no unit suffix"},
    {.value = "1000", .meaning = "", .hidden = true},  // named in `si`'s meaning
    {.value = "1024", .meaning = "", .hidden = true},  // named in `iec`'s meaning
});
constexpr std::array kImplicitPrintValues = std::to_array<ValueDoc>({
    {.value = "yes", .meaning = "print every match even when the expression has its own action (also on / true / 1)"},
    {.value = "no", .meaning = "never add the default print (also off / false / 0)"},
});
constexpr std::array kPathEncodingValues = std::to_array<ValueDoc>({
    {.value = "raw", .meaning = "the path's bytes verbatim, as find writes them (the default)"},
    {.value = "escape", .meaning = "C-escape control bytes, so a newline in a name cannot forge a line"},
});
constexpr std::array kHashEncodingValues = std::to_array<ValueDoc>({
    {.value = "hex", .meaning = "lower-case hex digits, as the sha256sum family prints (the default)"},
    {.value = "base64", .meaning = "standard padded base64 (RFC 4648), the Subresource-Integrity spelling"},
});
constexpr std::array kDiffFormatValues = std::to_array<ValueDoc>({
    {.value = "u", .meaning = "unified, the diff -u shape (the default; also spelled unified)"},
    {.value = "c", .meaning = "context, the diff -c shape (also context)"},
    {.value = "n", .meaning = "normal, the plain diff shape (also normal)"},
    {.value = "y", .meaning = "side by side, the diff -y shape (also side-by-side)"},
    // The long spellings each row already names.
    {.value = "unified", .meaning = "", .hidden = true},
    {.value = "context", .meaning = "", .hidden = true},
    {.value = "normal", .meaning = "", .hidden = true},
    {.value = "side-by-side", .meaning = "", .hidden = true},
});
constexpr std::array kCompareValues = std::to_array<ValueDoc>({
    {.value = "status", .meaning = "one tab-separated selected result kind and relative path per record"},
    {.value = "diff", .meaning = "a unified tree diff suitable for saving as a patch"},
});
constexpr std::array kDiffAlgorithmValues = std::to_array<ValueDoc>({
    {.value = "myers", .meaning = "minimal diff, as git computes it (the default)"},
    {.value = "direct", .meaning = "line-by-line, no alignment search"},
    {.value = "naive", .meaning = "the simple longest-common-subsequence walk"},
});
constexpr std::array kSortValues = std::to_array<ValueDoc>({
    {.value = "none", .meaning = "filesystem order, whatever the directory yields (fastest)"},
    {.value = "dir", .meaning = "sort each directory's entries (a bare --sort; also spelled name)"},
    {.value = "subtree", .meaning = "sorted entries with each subtree inlined contiguously"},
    {.value = "tree", .meaning = "path-ordered depth-first traversal within each root"},
    {.value = "roots", .meaning = "sort root operands; retain filesystem order below them"},
    {.value = "global", .meaning = "sort the roots, then walk each in tree order"},
    {.value = "score", .meaning = "best `-fuzzy` match first (buffers everything; needs `-fuzzy`)"},
    {.value = "name", .meaning = "", .hidden = true},  // `dir`'s meaning already names it
});
constexpr std::array kPagerValues = std::to_array<ValueDoc>({
    {.value = "help", .meaning = "page help, man, and Markdown on a terminal (the default)"},
    {.value = "auto", .meaning = "page every pageable output on a terminal"},
    {.value = "always", .meaning = "page every pageable output, even through a pipe"},
    {.value = "never", .meaning = "never page (same as `--no-pager`)"},
    {.value = "COMMAND", .meaning = "always page through this explicit shell command"},
});
constexpr std::array kZoneSuffixValues = std::to_array<ValueDoc>({
    {.value = "auto", .meaning = "each format's built-in default (the default)"},
    {.value = "always", .meaning = "force the offset, even on a format that omits it (also on / yes / true / 1)"},
    {.value = "never", .meaning = "drop the optional offset (also off / no / false / 0)"},
});
// The digest names, in xff/hash's sorted AlgorithmNames() order; a globals_test guard keeps
// this list identical to that SOT so it cannot drift.
constexpr std::array kHashAlgorithmValues = std::to_array<ValueDoc>({
    {.value = "blake2b", .meaning = "BLAKE2b, 512-bit"},
    {.value = "blake2b_256", .meaning = "BLAKE2b, 256-bit"},
    {.value = "blake3", .meaning = "BLAKE3 (fast, parallel)"},
    {.value = "md5", .meaning = "128-bit legacy (fast, collision-broken)"},
    {.value = "sha1", .meaning = "160-bit legacy (collision-broken)"},
    {.value = "sha224", .meaning = "SHA-2, 224-bit"},
    {.value = "sha256", .meaning = "SHA-2, 256-bit (the default)"},
    {.value = "sha384", .meaning = "SHA-2, 384-bit"},
    {.value = "sha3_224", .meaning = "SHA-3 (Keccak), 224-bit"},
    {.value = "sha3_256", .meaning = "SHA-3 (Keccak), 256-bit"},
    {.value = "sha3_384", .meaning = "SHA-3 (Keccak), 384-bit"},
    {.value = "sha3_512", .meaning = "SHA-3 (Keccak), 512-bit"},
    {.value = "sha512", .meaning = "SHA-2, 512-bit"},
    {.value = "sha512_224", .meaning = "SHA-2, 512/224 truncated"},
    {.value = "sha512_256", .meaning = "SHA-2, 512/256 truncated"},
});

// The whole-run options, in the order the --help usage page groups them. `--help` /
// `--version` and their aliases are deliberately omitted: they are special-cased in
// main.cc and self-evident, and `--help=--help` would be circular.
//
// Each element carries a trailing comma so clang-format lays the whole table out
// one field per line, uniformly (see STYLE_CPP.md "long struct-array tables").
constexpr std::array kGlobals = std::to_array<GlobalFlag>({
    {
        .name = "--config",
        .display = "--config=NAME",
        .group = "config",
        .header = "Config",
        .summary = "activate a named config or select the find, xff, or rg style; repeatable",
        .details = "A config style sets the defaults for ignore files, hidden files, sizes, sort order, and case. "
                   "find restricts the expression to find-compatible vocabulary and defaults; whole-run xff "
                   "globals remain available as explicit overrides. xff keeps find's grammar but sorts and prints "
                   "human sizes; "
                   "rg is opinionated (respect `.gitignore`, skip hidden, smart case). Every occurrence remains an "
                   "active selector, so several named config blocks can apply. Among the built-in style selectors, "
                   "the last `find`, `xff`, or `rg` occurrence chooses the baseline; custom names do not change it. "
                   "A `STYLE:EPOCH` spelling such as `xff:2` selects `STYLE` while retaining the full name as a config "
                   "selector. See `--help=styles` for the per-style defaults and `--help=config` for layering.",
        .topic = "config",
    },
    {
        .name = "--no-config",
        .display = "--no-config",
        .group = "config",
        .header = "Config",
        .summary = "disable all config-file loading",
        .details = "Runs from built-in defaults and command-line flags only. No config path is consulted: this skips "
                   "`/etc/xff.ini`, the user config, and every explicit `--xffrc=FILE`, regardless of option order. "
                   "There is no config-sourced directive to gate, so no system policy is needed in this mode. Ignore "
                   "files such as `.gitignore` and `.xffignore` are traversal inputs, not config files, and are "
                   "unaffected.",
        .topic = "config",
    },
    {
        .name = "--xffrc",
        .display = "--xffrc=FILE",
        .group = "config",
        .header = "Config",
        .summary = "also load a specific config file (a non-arming tier; see --allow-exec)",
        .details = "Loads FILE as a config tier above the user config (naming it is consent to LOAD it). It is a "
                   "NON-ARMING tier: safe directives apply, but a dangerous one - the exec family (-exec/-execdir/-ok, "
                   "-capture) or -delete - is inert unless --allow-exec is set from a trusted tier (the CLI or the "
                   "user/system config, never from an --xffrc file itself). An unarmed dangerous line is dropped with "
                   "a one-line warning. Repeatable; later files win.",
        .affects = "--allow-exec",
        .topic = "config",
    },
    {
        .name = "--allow-exec",
        .display = "--allow-exec",
        .group = "config",
        .header = "Config",
        .summary = "arm dangerous directives loaded from an --xffrc file (exec family, -delete)",
        .details = "Permits the sensitive/destructive directives (the exec family -exec/-execdir/-ok and -capture, "
                   "and the destructive -delete) carried by an --xffrc-loaded file to actually run. Honored only from "
                   "a trusted tier - typed on the CLI, or set in the user/system config - never from an --xffrc file "
                   "(so a named config cannot authorize itself). The root-owned system [policy] can hard-deny even "
                   "this. Without it, such lines are inert (dropped + warned); -delete still obeys its own "
                   "--safe/--dry-run guards.",
        .affects = "--xffrc",
        .topic = "config",
    },
    {
        .name = "--explain",
        .display = "--explain",
        .group = "config",
        .header = "Config",
        .summary = "print the resolved configuration and exit",
        .details = "Prints the active style, every config source consulted and whether it was found, resolved flags "
                   "in application order with their provenance, rejected config directives, and the style-default "
                   "table with this run's effective values. It does not walk roots or evaluate the expression. "
                   "Diagnostics about unreadable config paths are limited to the source being reported as absent.",
        .topic = "config",
    },
    {
        .name = "-H",
        .display = "-H",
        .group = "traversal",
        .header = "Traversal",
        .summary = "follow symlinks named on the command line, not while walking",
        .details = "Dereferences each symlink root operand before matching or descending, but keeps symlinks found "
                   "below that root as symlinks. A dangling root symlink falls back to the link itself. `-H`, `-L`, "
                   "and `-P` are mutually overriding leading options; the last occurrence wins.",
        .xff = false,
    },
    {
        .name = "-L",
        .display = "-L",
        .group = "traversal",
        .header = "Traversal",
        .summary = "follow symlinks everywhere during the walk",
        .details = "Dereferences symlink roots and symlinks encountered below them. Matching sees the target's type "
                   "and metadata, and directory targets are descended. Dangling links fall back to the link itself. "
                   "Filesystem loops are detected and reported instead of recursed indefinitely. `-H`, `-L`, and "
                   "`-P` are mutually overriding leading options; the last occurrence wins.",
        .xff = false,
    },
    {
        .name = "-P",
        .display = "-P",
        .group = "traversal",
        .header = "Traversal",
        .summary = "never follow symlinks (the default)",
        .details = "Matches every symlink as a link and never descends through it, including a symlink supplied as a "
                   "root operand. Predicates that explicitly inspect a target, such as `-xtype` and `-lname`, retain "
                   "their documented behavior. `-H`, `-L`, and `-P` are mutually overriding leading options; the "
                   "last occurrence wins.",
        .xff = false,
    },
    {
        .name = "--archive",
        .alias = "-z",
        .display = "--archive[=none|roots|all|any], -z[-|+|++], -Z[-|+|++]",
        .group = "archive",
        .header = "Archive traversal",
        .summary = "descend into archives: -z- none, -z roots only, -z+ / bare --archive all",
        .details = "Treats each archive (tar, gz, bzip2, xz, zstd, lz4, zip, ...) as a directory, so a member is an "
                   "ordinary entry at a member path like `foo.tar.gz!inner/x` and the expression matches it with "
                   "the same -name / -type / -size / -newer every other entry gets - and the predicates and "
                   "fields that READ an entry (-grep, -content, -hash, {hash}, {lines}) read the member out of "
                   "its container. "
                   "The modes are nested: `none` keeps find's behavior (an archive is one plain file); "
                   "`roots` dives only when a search root is itself an archive (pointing xff AT an archive "
                   "implies looking inside); `all` also dives archives discovered during the walk; `any` is "
                   "`all` without the name gate, offering every file to the reader (the older spelling is "
                   "`--archive-any`). Bare `--archive` means `all`, and the short form carries chmod-style "
                   "suffix signs (`-z-` none, `-z` roots, `-z+` all, `-z++` any). The UPPER-case family is the "
                   "same ladder with writing armed (`-Z` is `-z` plus `--archive-write`, `-Z+` is `-z+` plus "
                   "it, `-Z++` is `-z++` plus it): the case carries the capability and the signs carry the "
                   "level, so aiming at one cannot reach the other. The two axes resolve independently and "
                   "later wins, so `-Z++ -z-` arms writing with reading off, while `-Z-` is the full reset "
                   "(reading off AND writing disarmed). The find style defaults to `none`, "
                   "every xff-family style to `roots`. Members are read-only until a write spelling arms them, "
                   "so `-delete` and the exec family refuse them rather than silently skipping. "
                   "Under `all`, a file met mid-walk is offered to the reader only if its NAME looks like "
                   "a container (`any` drops that gate); one named on the command line always is. A "
                   "native phar exposes its executable stub as `.phar/stub.php`, matching tar- and zip-based "
                   "phars; it is an ordinary regular member for matching, fields, and statistics. If the manifest "
                   "stores that path, the stored member wins so one path never denotes two entries. The synthetic "
                   "stub is readable but cannot be deleted because the format requires it. "
                   "build-time extras: the stock binary is lean and omits readers; rebuild with "
                   "`--config=xff_full`, `--//xff:xff_archive` for the broad libarchive-backed set, or "
                   "one of the independent `--//xff:xff_asar` or `--//xff:xff_squashfs` readers. Asking for "
                   "archive handling without any reader is a hard error.",
        .values = kArchiveValues,
        .topic = "archive",
        .extra = "archive",
        .sign_forms = kArchiveShorts,
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--archive-depth",
        .display = "--archive-depth=N",
        .group = "archive",
        .header = "Archive traversal",
        .summary = "how many containers deep --archive dives (default 1)",
        .details = "Counted in CONTAINERS, not directory levels: the default 1 opens an archive but leaves an "
                   "archive INSIDE it a plain member, so a `.gem` shows its `data.tar.gz` without unpacking it. "
                   "`--archive-depth=2` opens that one too. Its own knob rather than part of -maxdepth because "
                   "nesting is where a decompression bomb lives - a few kilobytes can promise gigabytes per level "
                   "- while -maxdepth keeps counting member levels as the ordinary depth they are. Only `all` "
                   "nests: under `roots` a member is never a search root, so nothing inside the container is "
                   "dived whatever the value. N must be at least 1; use --archive=none / -z- to stop diving.",
        .affects = "--archive",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-aggregate",
        .display = "--archive-aggregate=<MODE>",
        .group = "archive",
        .header = "Archive traversal",
        .summary = "what --summary / --histogram count when the walk dives (default members)",
        .details = "Diving makes one byte visible twice - once as the container's own size, once as its "
                   "members' - so a total that adds `both` describes no filesystem that exists. `members` (the "
                   "default) counts a dived container's members instead of the container itself, which is what "
                   "unpacking it and measuring the result would give; `container` counts the archives and "
                   "never what is in them, which is what the disk holds; `both` counts everything, the "
                   "archive AND its unpacked copy, for when the doubling is the point. Only the REDUCTIONS "
                   "are affected: -print and every action still see every entry the walk visits, so a member "
                   "is listed under `container` and the container is listed under `members`. `members` needs "
                   "the walk to open a container before deciding, so a `-prune` on a container no longer "
                   "avoids opening it - use another mode, or no reduction, to keep that.",
        .values = kArchiveAggregateValues,
        .affects = "--archive",
        .topic = "archive",
        .extra = "archive",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--archive-delete",
        .display = "--archive-delete",
        .group = "archive",
        .header = "Archive traversal",
        .summary = "let -delete remove an archive member, rewriting its container",
        .details = "There is no such thing as removing a member in place: an archive is a stream of "
                   "header and data records, so the container is written again from the members that "
                   "survive. That is why this is opt-in and why `-delete` refuses a member without it "
                   "- an action that silently rewrites a whole archive is not one to do by default. "
                   "The rewrite happens after the walk, once per container however many of its "
                   "members matched, because the walk is reading that same container while it runs. "
                   "The new archive keeps the original's format and compression (a `.tar.gz` stays a "
                   "gzipped tar) and every surviving member keeps its name, mode, times and content; "
                   "it is written beside the original and renamed over it only when complete, so an "
                   "interrupted run leaves the container as it was. `--dry-run` lists the members "
                   "that would go and writes nothing. A NATIVE phar is rewritten too, by xff's own "
                   "writer: the manifest and data section are rebuilt from the surviving entries "
                   "verbatim (so per-member gz / bz2 compression is untouched) and the trailing "
                   "signature is recomputed (md5 / sha1 / sha256 / sha512). Refused, with the reason "
                   "named: a format this build reads but cannot write (7-Zip, RAR, ISO); a TAR-based "
                   "or ZIP-based phar, whose signature is a MEMBER computed over the rest of the "
                   "container, so a rewrite would leave it stale and PHP would reject the result; an "
                   "OpenSSL-signed phar, which cannot be re-signed without its private key; a "
                   "compressed single file, which has no member list to rewrite; and a member of a "
                   "container nested inside another one.",
        .affects = "--archive",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-extract",
        .display = "--archive-extract",
        .group = "archive",
        .header = "Archive traversal",
        .summary = "let -exec / -ok run on an archive member, via a temporary copy",
        .details = "A member is bytes inside a container, so there is no path a child process can "
                   "open and the exec family refuses one by default. With this flag the member is "
                   "written to its own temporary directory under the same name it has inside the "
                   "archive, and the child is handed THAT path: `{}` renders as the temporary file, "
                   "`-execdir` runs in the temporary directory, and `-ok` shows the copy in its prompt "
                   "before anything runs. Each copy is removed as soon as its child finishes (for a "
                   "`+` batch or a `-j` child, when the run ends), so nothing is left behind. The copy "
                   "first goes to a memory-backed directory where the platform has one "
                   "(`$XDG_RUNTIME_DIR` or `/dev/shm` on Linux, both tmpfs), so the child gets an ordinary "
                   "path without normally writing the member to disk. If no memory-backed directory is "
                   "available, or the member is larger than its reported free space, xff falls back to "
                   "`$TMPDIR` (or the platform temporary directory); extraction therefore does not guarantee "
                   "that member data never reaches disk. It is "
                   "opt-in because the child is editing a COPY: a formatter or a patch tool will "
                   "report success and change nothing in the archive. `-delete` stays refused whatever "
                   "this flag says - removing a temporary copy would be a no-op dressed as a "
                   "deletion. The container itself is an ordinary file, so an action on IT never "
                   "needed this.",
        .affects = "--archive",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-mount",
        .display = "--archive-mount",
        .group = "archive",
        .header = "Archive traversal",
        .summary = "let -exec / -ok run on an archive member by mounting its container read-only",
        .details = "The alternative to `--archive-extract`, answering the same question - what path "
                   "can a child process open for a member? - with the container itself instead of a "
                   "copy. The container is mounted read-only (once, whichever members are visited), "
                   "so `{}` names a path INSIDE the archive: a tool that only reads it (a compiler, "
                   "a checksum, `grep`) sees the real member and nothing is written anywhere. That "
                   "also removes extraction's trap, where an in-place formatter edits a temporary "
                   "copy and reports success while the archive keeps its old content: here the "
                   "mount has no write path at all, so such a tool fails honestly. Mounting is a "
                   "per-MACHINE capability (it needs the fuse extra AND a runtime FUSE library and "
                   "permission to mount), so where it cannot happen the run says so once and falls "
                   "back to extraction rather than failing - which is why this flag is safe to keep "
                   "in a config file. `--archive-extract` is still what arms writing through a "
                   "copy; this one arms reading in place.",
        .affects = "--archive,--archive-extract",
        .topic = "archive",
        .extra = "fuse",
    },
    {
        .name = "--archive-write",
        .display = "--archive-write, -Z[-|+|++]",
        .group = "archive",
        .header = "Archive traversal",
        .summary = "arm both archive write flags (--archive-extract + --archive-delete)",
        .details = "One spelling for \"let actions touch members\", because the two write flags are almost "
                   "always wanted together: `--archive-extract` so `-exec` / `-ok` can run over a member, and "
                   "`--archive-delete` so `-delete` can remove one. It is exactly those two flags and nothing "
                   "else - the dive MODE is untouched. The short form is the UPPER-case archive ladder: `-Z` is "
                   "`-z` with writing armed, `-Z+` is `-z+` with it, `-Z++` is `-z++` with it. Case carries the "
                   "capability and the signs carry the level, so a slipped shift key changes which of the two "
                   "you asked for, never both - and arming is not doing, since an action still has to ask for "
                   "the write and `--safe` / `--dry-run` still apply. The level and the arming resolve as "
                   "separate axes with later winning, so `-Z++ -z-` keeps writing armed with diving off; "
                   "`-Z-` is the full reset, disarming writing and turning diving off together.",
        .affects = "--archive-delete,--archive-extract",
        .topic = "archive",
        .extra = "archive",
        .sign_forms = kArchiveWriteShorts,
    },
    {
        .name = "--archive-any",
        .display = "--archive-any",
        .group = "archive",
        .header = "Archive traversal",
        .summary = "under --archive=all, offer EVERY file to the reader, not only likely names",
        .details = "By default `all` only opens a file the walk met whose NAME looks like a container "
                   "(`.tar`, `.tgz`, `.zip`, `.jar`, `.phar`, ... - the reader's formats plus the "
                   "packages that are one of them underneath). Without that gate, walking a source tree "
                   "would open and format-bid every `.cc` and every binary in it, so the cost of diving "
                   "would fall on runs that dive nothing. The name is only a heuristic, and this flag is "
                   "the way out of it: an archive called `blob` or `backup.dat` is found with "
                   "--archive-any and missed without. It costs a read of every candidate file, which is "
                   "why it is not the default. A file NAMED on the command line is always opened - "
                   "pointing xff at it is the request - so this flag changes nothing for `--archive=roots`.",
        .affects = "--archive",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-separator",
        .display = "--archive-separator=STRING",
        .group = "archive",
        .header = "Archive traversal",
        .summary = "string between container and member in a member path (default `!`)",
        .details = "A member path is `<container><separator><member>`, and there is no single ecosystem "
                   "convention - `!` (JAR / Java URLs), `#` (fragment style), and the multi-character `!/` or "
                   "`#/` other tools print all exist - so this is a presentation choice rather than something "
                   "hard-coded. ANY string is accepted, not a fixed menu, so xff can emit what another system "
                   "accepts. Rendering is plain concatenation and xff adds or removes no slash, so a member "
                   "stored with a leading slash keeps it: `a.tgz!/rooted` (and with `--archive-separator=!/`, "
                   "the doubled `a.tgz!//rooted`, which is why plain `!` is the better default). Parsing splits "
                   "at the FIRST occurrence and takes the remainder verbatim, so a path xff printed round-trips. "
                   "A plain `/` is allowed and composes with globs, but is lossy - a real directory named x.tar "
                   "becomes indistinguishable from an archive - so it is never the default.",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-prefix",
        .display = "--archive-prefix=[URI|STRING]",
        .group = "archive",
        .header = "Archive traversal",
        .summary = "prefix a member path: empty (default), URI, or any literal string",
        .details = "Empty (the default) prints the bare path, `a.tgz!inner/x`. `URI` renders a URL the "
                   "RECEIVING tool will accept, which means the ecosystem's own where one owns the format: a "
                   "`.phar` as PHP's `phar:///abs/a.phar/inner/x`, a `.jar` / `.war` / `.ear` as Java's "
                   "`jar:file:/abs/a.jar!/pkg/C.class`, and everything else as `archive:///abs/a.tar!x` for an "
                   "absolute container or the opaque `archive:a.tgz!x` for a relative one - `archive://a.tgz` "
                   "would be WRONG, since `//` starts the authority and would make `a.tgz` a host name. The "
                   "choice is by EXTENSION, because that is what the claim is: a jar IS a zip, and only its "
                   "name says which readers expect it. Those two spellings fix the separator as well, so "
                   "`--archive-separator` does not reach them; the BARE path keeps one separator whatever the "
                   "container is, since being re-pasteable matters more there than matching a foreign form. "
                   "Any other value is used LITERALLY (e.g. `--archive-prefix=vfs:`), the same freedom "
                   "`--archive-separator` has; `URI` is the one keyword, spelled in caps like `RE2` / `PCRE2` "
                   "/ `GLOB`. There is deliberately no `none` value: it would be indistinguishable from a "
                   "literal prefix spelled `none`, which is why empty means no prefix. Applies to PARSING too "
                   "- under a prefix, a bare path is not accepted as a member path, so the spellings never "
                   "silently interchange.",
        .values = kArchivePrefixValues,
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--jobs",
        .alias = "-j",
        .display = "-j N, -j=N, --jobs=N|all",
        .group = "scheduling",
        .header = "Concurrency and ordering",
        .summary = "directory-read and concurrent -exec workers (all = every detected core)",
        .details = "`N` is a positive integer; use `-j 4`, `-j=4`, or `--jobs=4`. The conventional attached "
                   "short form `-j4` is also accepted. Every form accepts `all` in place of `N`. `-j 1` makes "
                   "directory reads and `-exec ... ;` synchronous. At larger values, xff reads directories "
                   "on `N` worker threads and may independently keep up to `N` semicolon-form `-exec` / "
                   "`-execdir` children outstanding. Reads and children can overlap; `N` is not one shared "
                   "operation budget. The children's truth value is therefore success on launch. The "
                   "`... +` batch forms still run once after the walk and propagate a failing exit status. With "
                   "no flag, xff uses one fewer than the detected cores, capped at 15 and floored at 1; find and "
                   "rg modes use every detected core. `all` always means every detected core.",
    },
    {
        .name = "--sort",
        .display = "--sort[=<ORDER>]",
        .group = "scheduling",
        .header = "Concurrency and ordering",
        .summary = "sibling/traversal ordering (default depends on the mode)",
        .details = "Traversal order and result buffering are separate. Every mode materializes one directory "
                   "listing, and `--jobs` may read additional directory listings ahead; none of the path-order "
                   "modes buffers every match. `none` preserves each directory's filesystem order. `dir` sorts "
                   "a directory's complete child listing, emits that listing, then descends into its sorted "
                   "children. `subtree` emits sorted non-directory children first, then each sorted directory or "
                   "container subtree contiguously. `tree` visits each sorted child and its descendants before "
                   "the next child, giving lexicographic depth-first order WITHIN each root while retaining "
                   "command-line root order. `roots` stable-sorts only the root operands and otherwise behaves "
                   "like `none`. `global` stable-sorts the roots and applies `tree` below each one, producing a "
                   "total hierarchical order by root and then path. Duplicate or overlapping roots remain "
                   "separate walks and can therefore repeat paths. "
                   "`-depth` makes every mode post-order (children before their parent); `none` retains filesystem "
                   "sibling order; `roots` does too, while every other ordered mode uses sorted sibling order. "
                   "The default is per "
                   "style: xff sorts "
                   "per directory, while find and rg leave the order unspecified.\n"
                   "`score` is the odd one out: the others are TRAVERSAL orders the walk streams, while a "
                   "`-fuzzy` score only exists once an entry has been evaluated, so results are buffered and "
                   "ranked after the walk (best first, ties keeping the walk's own order). It needs `-fuzzy` "
                   "or `-ifuzzy` in the expression - ranking by a value nothing produced is a mistake, not an "
                   "empty ordering - and side-effecting actions such as `-exec` still run during the walk, so "
                   "only the printed listing is reordered.",
        .values = kSortValues,
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--block-size",
        .display = "--block-size=SIZE",
        .group = "matching",
        .header = "Matching",
        .summary = "bytes per bare/-b block for -size and -blocks (default 512)",
        .details = "A bare number is bytes. Explicit `B`/`kB`/`MB`/... units use SI powers of 1000; "
                   "`KiB`/`MiB`/... use IEC powers of 1024. Legacy `k`/`M`/`G`/... remain binary for find "
                   "compatibility. The value must be positive and fit in 64 bits. Lowercase `b` is invalid here "
                   "because defining a block in blocks is circular. This changes the comparison unit for both "
                   "`-size` (apparent bytes) and `-blocks` (allocated bytes); it does not change filesystem "
                   "metadata or the fixed units printed by `-ls`.",
    },
    {
        .name = "--exact",
        .display = "--exact",
        .group = "matching",
        .header = "Matching",
        .summary = "disable xff's filesystem-native case folding for name/path/fuzzy matching",
        .details = "In xff mode, the otherwise case-sensitive `-name`, `-path`, `-fuzzy`, and `-fuzzypath` "
                   "matchers follow the containing volume: xff folds ASCII case on a case-insensitive volume "
                   "and compares exactly on a case-sensitive one. `--exact` opts out and makes those matchers "
                   "byte-case-exact unless `--case=insensitive` or an explicitly insensitive primary (`-iname`, "
                   "`-ipath`, `-ifuzzy`, `-ifuzzypath`) requests folding. Find mode is already exact by default. "
                   "The volume probe is cached per device and safely defaults to exact matching if unavailable.",
    },
    {
        .name = "--case",
        .display = "--case=<MODE>, -i, -s[-|+]",
        .group = "matching",
        .header = "Matching",
        .summary = "letter case for matchers: -i insensitive, -s/-s+ smart, -s- sensitive (rg -> smart)",
        .details = "Controls the otherwise case-sensitive name, path, symlink-target, fuzzy, regex, and content "
                   "matchers (`-name`, `-path`, `-lname`, `-fuzzy`, `-fuzzypath`, `-regex`, `-rxc`, `-grep`). "
                   "Their `-i...` variants always fold independently. `sensitive` matches exactly; "
                   "`insensitive` (`-i`) folds case; `smart` (`-s` / `-s+`) folds only when the pattern is all "
                   "free of ASCII uppercase letters and matches exactly otherwise; `-s-` forces `sensitive`. For name, "
                   "path, and fuzzy matching, xff's filesystem-native folding can additionally apply unless "
                   "`--exact` is present. rg defaults to `smart`; xff and find default to `sensitive`.",
        .values = kCaseValues,
        .sign_forms = kCaseShorts,
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--regextype",
        .display = "--regextype=<GRAMMAR>",
        .group = "matching",
        .header = "Matching",
        .summary = "match engine: RE2, EXACT, FNMATCH, GLOB, SHGLOB (GLOB + {a,b}), or PCRE2 (a build extra)",
        .details = "Selects one grammar for every `-regex`/`-iregex`, `-rxc`/`-irxc`, and `-grep` pattern in the "
                   "run; the last occurrence wins. `RE2` "
                   "(the default) is linear-time regular expressions; `EXACT` is a literal string "
                   "(metacharacters are plain text); `FNMATCH` is a flat shell wildcard where `*` matches any "
                   "character including `/`; `GLOB` is a locale-independent path glob where `*`, `?`, and "
                   "`[...]` stay inside one component, while a complete-component `**` crosses components; "
                   "middle `foo/**/bar` permits zero or more components and trailing `foo/**` requires a "
                   "descendant. Bracket expressions support ascending ranges, leading `!` negation, and RE2 "
                   "ASCII named classes; malformed or unsupported expressions are errors. `SHGLOB` is `GLOB` "
                   "plus nested, possibly empty `{a,b}` alternatives. `PCRE2` (Perl syntax: "
                   "lookaround, backreferences) is the one build-time extra: it is present only in a full "
                   "build, and selecting it in a lean build is a hard error, never a silent fall back to `RE2`. "
                   "`RE2`/`EXACT`/`FNMATCH`/`GLOB`/`SHGLOB` are always built in; run `xff --help=extras` to "
                   "see whether THIS binary includes `PCRE2`. See `--help=grammars` for a full description of "
                   "each grammar (`GLOB`/`SHGLOB` are not POSIX glob(7)).",
        .values = kRegextypeValues,
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--exclude",
        .display = "--exclude=GLOB",
        .group = "path-filter",
        .header = "Path filters",
        .summary = "skip paths matching a gitignore-style glob (repeatable; a matched directory is pruned)",
        .topic = "ignore",
    },
    {
        .name = "--include",
        .display = "--include=GLOB",
        .group = "path-filter",
        .header = "Path filters",
        .summary = "re-include paths a --exclude would skip, matching a gitignore-style glob (repeatable)",
        .topic = "ignore",
    },
    {
        .name = "--lang-db",
        .display = "--lang-db=FILE",
        .group = "classification",
        .header = "Classification databases",
        .summary = "overlay language metadata and suffix/filename mappings from JSON; repeatable",
        .details = "Loads a JSON object keyed by canonical language name. Each value may set `type`, `color`, "
                   "`group`, and `source`, plus string arrays `aliases`, `extensions`, and `filenames`. Later files "
                   "override earlier files and compiled data. Extensions may include their leading dot and may "
                   "contain multiple parts; matching folds suffix case while exact filenames retain case. Conflicts "
                   "between two languages in ONE file follow `--lang-conflicts`.",
        .affects = "-lang",
        .topic = "content",
    },
    {
        .name = "--lang-conflicts",
        .display = "--lang-conflicts=error|first|last",
        .group = "classification",
        .header = "Classification databases",
        .summary = "resolve ambiguous suffix or filename claims within one language vocabulary file",
        .details = "Controls only ambiguity inside one `--lang-db` file. Layering remains deterministic: "
                   "a later file intentionally overrides earlier files and compiled data. `error` is the default; "
                   "`first` or `last` is an explicit compatibility escape hatch for imported databases.",
        .values = kLanguageConflictValues,
        .affects = "--lang-db",
        .topic = "content",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--mime-vocabulary",
        .display = "--mime-vocabulary=FILE",
        .group = "classification",
        .header = "Classification databases",
        .summary = "overlay media-type metadata and extension mappings from JSON; repeatable",
        .details = "Loads a JSON object keyed by canonical media type. Each value may set `description`, `source`, "
                   "`charset`, boolean `compressible`, and string arrays `aliases` and `extensions`. Later files "
                   "override earlier files and compiled data. An extension may include its leading dot; matching "
                   "folds case. Conflicts between two types in ONE file follow `--mime-conflicts`.",
        .affects = "-mime",
        .topic = "content",
    },
    {
        .name = "--mime-conflicts",
        .display = "--mime-conflicts=error|first|last",
        .group = "classification",
        .header = "Classification databases",
        .summary = "resolve ambiguous extension claims within one MIME vocabulary file",
        .details = "Controls only ambiguity inside one `--mime-vocabulary` file. Layering remains deterministic: "
                   "a later file intentionally overrides earlier files and compiled data. `error` is the default; "
                   "`first` or `last` provides an explicit compatibility escape hatch for imported databases.",
        .values = kMimeConflictValues,
        .affects = "--mime-vocabulary",
        .topic = "content",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--gitignore",
        .alias = "-g",
        .display = "--gitignore[=off|auto|on], -g[-|+]",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "respect .gitignore files: -g = auto (only in a git repo), -g+/=on always, -g-/=off never",
        .details = "Reads .gitignore rules while walking, including nested .gitignore files, .git/info/exclude, and "
                   "core.excludesFile. `-g` / `auto` activates only inside a git working tree; `-g+` / `=on` forces "
                   "it anywhere; `-g-` / `=off` disables it. Independent of `--ignore-files` (.ignore / "
                   ".xffignore).",
        .values = kGitignoreValues,
        .topic = "ignore",
        .sign_forms = kGitignoreShorts,
        .value_check = GlobalFlag::ValueCheck::kTristate,
    },
    {
        .name = "--ignore-files",
        .display = "--ignore-files",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "respect per-directory .ignore and .xffignore files (off by default)",
        .topic = "ignore",
    },
    {
        .name = "--ignore-file",
        .display = "--ignore-file=PATH",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "read an extra gitignore-format file, rooted at its own directory (repeatable)",
        .topic = "ignore",
    },
    {
        .name = "--no-ignore",
        .alias = "-u",
        .display = "--no-ignore, -u",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "disable all ignore-file processing (.gitignore/.ignore/.xffignore)",
        .topic = "ignore",
    },
    {
        .name = "--ignore-vcs",
        .display = "--ignore-vcs",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "respect version-control ignore files (.gitignore / .git/info/exclude / core.excludesFile)",
        .details = "The rg-style affirmative for the VCS ignore-file layer - today git's (.gitignore at any depth, "
                   ".git/info/exclude, core.excludesFile), the same layer -g / --gitignore auto enables. Use it to "
                   "countermand an earlier --no-ignore-vcs or a style default. Independent of --ignore-files "
                   "(.ignore / .xffignore), which keep their own switch; --no-ignore / -u still turns off every "
                   "ignore source. Last of the ignore-mode flags wins.",
        .topic = "ignore",
    },
    {
        .name = "--no-ignore-vcs",
        .display = "--no-ignore-vcs",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "do not respect version-control ignore files (keeps .ignore / .xffignore)",
        .details = "Drops the VCS ignore-file layer (git's .gitignore / .git/info/exclude / core.excludesFile) while "
                   "leaving --ignore-files (.ignore / .xffignore) untouched - that is the difference from "
                   "--no-ignore / -u, which turns off every ignore source. Today git is the only VCS ignore file xff "
                   "reads, so this is nearly --gitignore=off. Last of the ignore-mode flags wins.",
        .topic = "ignore",
    },
    {
        .name = "--hidden",
        .display = "--hidden",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "include hidden dotfiles in the walk (default: find/xff show, rg skips)",
        .topic = "ignore",
    },
    {
        .name = "--no-hidden",
        .display = "--no-hidden",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "skip hidden dotfiles (the rg default; opts find/xff out)",
        .topic = "ignore",
    },
    {
        .name = "--skip-vcs",
        .display = "--skip-vcs[=<LIST>]",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "prune VCS metadata dirs (.git, .hg, ...); bare/=all = every known VCS, =LIST a subset",
        .details = "Prunes version-control metadata directories at any depth (like ripgrep / fd), so a search "
                   "never wades into repo plumbing. Bare `--skip-vcs` (or `=all`) covers every known VCS: `git` "
                   "(.git), `hg` (.hg), `svn` (.svn), `jj` (.jj), `bzr` (.bzr), `darcs` (_darcs), `cvs` (CVS). A "
                   "comma list (`--skip-vcs=git,hg`) is an explicit, frozen subset - it never changes if a VCS "
                   "is added to the default set later. `--no-skip-vcs` (or `=none`) turns it off. Independent of "
                   "`--hidden`, so the user's own dotfiles (.bazelrc, .gitignore) still show. `-g` / gitignore "
                   "mode implies `--skip-vcs=git` (only .git); an explicit `--skip-vcs` overrides that. Default "
                   "off otherwise.",
        .values = kSkipVcsValues,
        .topic = "ignore",
    },
    {
        .name = "--no-skip-vcs",
        .display = "--no-skip-vcs",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "keep VCS metadata dirs in the walk (opts out of --skip-vcs and the -g .git default)",
        .topic = "ignore",
    },
    {
        .name = "--format",
        .display = "--format=<FORMAT>",
        .group = "format",
        .header = "Result formatting",
        .summary = "output format: plain, nul, jsonl, csv, tsv, aligned, markdown (md), tree; default plain",
        .values = kFormatValues,
        .topic = "output",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--no-header",
        .display = "--no-header",
        .group = "format",
        .header = "Result formatting",
        .summary = "omit the header row from tabular --format (csv/tsv/aligned/markdown; on by default)",
        .topic = "output",
    },
    {
        .name = "--columns",
        .display = "--columns=FIELD,...",
        .group = "format",
        .header = "Result formatting",
        .summary = "columns for tabular --format, from the {field} vocabulary (e.g. path,size,mtime)",
        .topic = "output",
    },
    {
        .name = "--compare",
        .display = "--compare[=status|diff]",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "compare two roots as selected statuses or a unified diff",
        .details = "Requires exactly two roots. Bare `--compare` and "
                   "`--compare=status` emit only discrepancies as tab-separated `left-only`, `right-only`, or "
                   "`different` records. `--compare=diff` emits one unified tree diff, suitable for redirecting to "
                   "a patch file; `--diff-context` and `--diff-algorithm` tune it. Unlike `-diff TARGET`, which is "
                   "an expression action comparing each match from one walk with a templated target and therefore "
                   "cannot discover target-only paths, `--compare` walks both roots independently and pairs the "
                   "matches by relative path. The ordinary expression, ignore, hidden-file, archive, traversal, "
                   "and `-P` / `-H` / `-L` symlink rules apply unchanged to each side; comparison itself enables "
                   "none of them. Regular files are compared byte for byte (text and binary). Unfollowed symlinks "
                   "are compared by target. Both walks complete before comparison records are emitted in bytewise "
                   "relative-path order; `--sort` affects each walk, not that final order. In status mode, "
                   "`--path-encoding=escape` makes control bytes in the relative path unambiguous.",
        .values = kCompareValues,
        .topic = "compare",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--compare-select",
        .display = "--compare-select=KIND,...",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "tree-comparison results to emit: left-only, right-only, identical, different, or all",
        .details = "Selects comma-separated result kinds for `--compare`. The default is "
                   "`left-only,right-only,different`, so equal files stay silent. `all` selects every kind. "
                   "`identical` is available with status output and is rejected with `--compare=diff`, where an "
                   "unchanged file has no patch representation.",
        .affects = "--compare",
        .topic = "compare",
    },
    {
        .name = "--diff-algorithm",
        .display = "--diff-algorithm=naive|direct|myers",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "diff engine for -diff and tree diffs: naive, direct, or myers (the default)",
        .values = kDiffAlgorithmValues,
        .affects = "-diff,--compare",
        .topic = "compare",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--diff-ignore",
        .display = "--diff-ignore=TOKEN,...",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "normalize -diff comparison: ws, change, trail, blank, case, eofnl (comma-separated)",
        .details = "Sets the normalization used by `-diff`; the last value wins. It may be saved in user config or an "
                   "explicit `--xffrc=FILE`, and a command-line value overrides the configured value. An empty value "
                   "disables configured normalization. Tokens are `ws`, `change`, `trail`, `blank`, `case`, and "
                   "`eofnl`, comma-separated.",
        .affects = "-diff",
    },
    {
        .name = "--diff-ignore-matching",
        .display = "--diff-ignore-matching=REGEX",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "-diff ignores lines matching this regex (RE2)",
        .details = "Drops matching lines before `-diff` compares the two inputs. It may be saved in user config or an "
                   "explicit `--xffrc=FILE`; the last value wins, so a command-line value overrides configuration. An "
                   "empty value disables a configured expression. The expression uses RE2.",
        .affects = "-diff",
    },
    {
        .name = "--diff-format",
        .display = "--diff-format=u|c|n|y",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "default -diff format: u/unified (default), c/context, n/normal, y/side-by-side",
        .values = kDiffFormatValues,
        .affects = "-diff",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--diff-context",
        .display = "--diff-context=N",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "default -diff context lines (3); overrides --context for -diff, and -diff:uN overrides it",
        .affects = "-diff,--compare",
        .topic = "compare",
    },
    {
        .name = "--hash-algorithm",
        .display = "--hash-algorithm=<ALGO>",
        .group = "output",
        .header = "Output values and actions",
        .summary = "default digest for -hash / {hash} (sha256 default; md5, sha512, blake3, and more)",
        .details = "Sets the default digest algorithm for the `-hash` action and the `{hash}` field. `sha256` is "
                   "the default; a `-hash:ALGO` spec or a `{hash:ALGO}` qualifier overrides it per use.",
        .values = kHashAlgorithmValues,
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--hash-encoding",
        .display = "--hash-encoding=hex|base64",
        .group = "output",
        .header = "Output values and actions",
        .summary = "default -hash / {hash} rendering: hex (default) or base64",
        .values = kHashEncodingValues,
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--path-encoding",
        .display = "--path-encoding=raw|escape",
        .group = "output",
        .header = "Output values and actions",
        .summary = "plain-output path byte encoding: raw (verbatim, default) or escape (C-escape controls)",
        .values = kPathEncodingValues,
        .topic = "output",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--template",
        .display = "--template=TEMPLATE",
        .group = "output",
        .header = "Output values and actions",
        .summary = "render each match through a field template ({path}, {name}, ...)",
        .topic = "output",
    },
    {
        .name = "--implicit-print",
        .display = "--implicit-print=yes|no",
        .group = "output",
        .header = "Output values and actions",
        .summary = "force the default -print on or off",
        .values = kImplicitPrintValues,
        .topic = "output",
        .value_check = GlobalFlag::ValueCheck::kBool,
    },
    {
        .name = "--pack",
        .display = "--pack=FILE",
        .group = "pack",
        .header = "Archive creation",
        .summary = "write every match into a new archive at FILE instead of listing them",
        .details = "The counterpart of `--archive`: instead of reading a container the walk BUILDS one, so the "
                   "member list comes from the whole expression vocabulary rather than from a shell pipeline "
                   "into `tar`. The output NAME picks the format - `--help=archive` lists exactly what this "
                   "binary writes, from the writer's own table rather than a copy kept here, and the single-word "
                   "shortcuts (`.tgz`, `.txz`, `.tbz2`, `.tzst`, `.tlz`, `.taZ`) mean what they do everywhere "
                   "else; a name "
                   "carrying no format is a usage error reported BEFORE the "
                   "walk, since finding out afterwards would waste the traversal. "
                   "Each member is stored under the entry's path relative to the search root it was found "
                   "under, in the order the walk produced it - so `--sort` decides the order inside the "
                   "archive, and nothing is renamed or re-rooted behind your back. "
                   "Like `--summary` it is a sink: it replaces the per-match listing, while explicit actions "
                   "still run, so add `-print` to watch what goes in. The archive is written after the walk "
                   "and renamed into place only when complete, so an interrupted run leaves no half archive "
                   "and an existing FILE survives a failed one. A file the walk meets that IS the output is "
                   "skipped rather than packed into itself. An archive MEMBER cannot be packed: reading files "
                   "out of one container to re-pack them into another is its own feature, and until it exists "
                   "the run is refused rather than quietly short. A build-time extra, like `--archive`.",
        .affects = "--sort",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--pack-option",
        .display = "--pack-option=NAME=VALUE|@FILE.json",
        .group = "pack",
        .header = "Archive creation",
        .summary = "tune how `--pack` writes: repeatable, last value for a NAME wins",
        .details = "The general knob behind `--pack-level`. NAME is XFF's own vocabulary, not the archive "
                   "library's: each name is translated to whatever the linked writer calls the same thing, "
                   "so an unknown name is a usage error rather than a silent no-op, the accepted set is "
                   "listed by `--help=archive` straight from the writer's table, and swapping or upgrading "
                   "that library changes a translation table instead of the flags you type. A name that "
                   "exists but does not apply to the chosen output format is refused too, naming the formats "
                   "it does apply to - `zip64` is a zip idea, `threads` is not a gzip one. Everything is "
                   "checked before the walk starts, so a typo costs no traversal and writes no file. "
                   "`--pack-option=@FILE.json` reads one JSON object whose keys are option names and whose "
                   "values are strings, integers, or booleans; booleans become `yes` or `no`. File and inline "
                   "forms may be repeated and are expanded in command-line order, so the last value for a name "
                   "wins across both forms.",
        .affects = "--pack",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--pack-level",
        .display = "--pack-level=N",
        .group = "pack",
        .header = "Archive creation",
        .summary = "compression level for `--pack` (gzip/xz/lzip/lzma/zip 0-9, bzip2/lz4 1-9, zstd 1-22)",
        .details = "How hard the compressor works, on the scale the chosen format uses; left alone it is the "
                   "format's own default. Exactly `--pack-option=level=N`, kept as its own spelling because it "
                   "is the common knob for compressors that expose a level - the same relationship `-Z` has to "
                   "`--archive-write`. On a plain `.tar` it is a usage error rather than a no-op, because "
                   "there is no compressor to set a level on and a silently ignored level reads as a smaller "
                   "archive that never arrives. Legacy Unix `compress` has no level knob, so `.tar.Z` refuses "
                   "this option too.",
        .affects = "--pack",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--summary",
        .display = "--summary[=<GROUP>]",
        .group = "stats",
        .header = "Statistics",
        .summary = "aligned count + size table (or --format=jsonl rows) instead of each match; repeatable",
        .details = "Replaces the per-match listing with an aggregate table: match count and total size per group "
                   "(overall, by type, extension, programming language, media (MIME) type, user (owner), owning "
                   "group, file digest, or hash-verification result). The categorical keys reuse the "
                   "{mime}/{user}/{group}/{hash} field "
                   "vocabulary; --summary=hash groups identical files into one bucket (a dedup count, reading every "
                   "file). `--summary=hash-verification` requires exactly one `-hasheq` and counts its `verified` or "
                   "`failed` verdict even when that verdict makes the complete expression false; an entry that "
                   "short-circuits before reaching `-hasheq` is not counted. Empty expected values and unreadable "
                   "entries are failed, matching `-hasheq` itself. A "
                   "{template} key groups by any field value (e.g. --summary='{ext}-{type}'); a single m// "
                   "extraction key (--summary='{capture.NAME:m/re/\\1/}') groups per extracted line, so a "
                   "per-file command's multi-line output tallies per key (e.g. git-blame lines per author) - the "
                   "size column is not meaningful there. Repeatable: each --summary is its own table (e.g. "
                   "--summary=ext --summary=type), printed in order. --top=N limits the rows of each, "
                   "--summary-precision sets the scaled-size digits, and --format=jsonl emits one object per group "
                   "for scripts.",
        .values = kSummaryValues,
        .topic = "stats",
        .value_check = GlobalFlag::ValueCheck::kEnumOrTemplate,
    },
    {
        .name = "--histogram",
        .display = "--histogram=BUCKET[:MEASURE]",
        .group = "stats",
        .header = "Statistics",
        .summary = "bar chart per bucket: a count or sum/mean/min/max of size|lines (repeatable)",
        .details = "A terminal reduction like --summary, drawn as bars. BUCKET groups the matches - a category "
                   "(overall, type, ext, lang, mime, user (owner), or group) or a numeric-range field "
                   "(size / lines by order of magnitude, depth per level, drawn as an ascending distribution). "
                   "The optional :MEASURE is the bar's value - "
                   "`count` (the default) or an aggregate "
                   "`sum(FIELD)` / `mean(FIELD)` / `min(FIELD)` / `max(FIELD)` over a numeric FIELD (size or lines). "
                   "A numeric metric needs an aggregator (`ext:lines` is an error; `ext:sum(lines)` is not). "
                   "Repeatable and combinable with --summary - both are fed by one walk and replace the per-match "
                   "listing. Bars scale to the tallest, use Unicode block characters on a UTF-8 locale (see "
                   "--unicode) or ASCII '#' otherwise; --top=N keeps the N tallest and --format=jsonl emits one "
                   "object per bar for scripts.",
        .topic = "stats",
    },
    {
        .name = "--shards",
        .display = "--shards[=auto|SCHEME,...]",
        .group = "shards",
        .header = "Sharded files",
        .summary = "collapse each set of sharded files (e.g. data-00000-of-00010) to one line",
        .details = "Recognizes sharded-file naming conventions and collapses each logical set to a single line "
                   "instead of listing every shard. Bare `--shards` (or `=auto`) enables all built-in schemes: "
                   "`<stem>-<index>-of-<total>` (`of`), `<stem>.<NNN>` (`dotnum`), and `<stem>_<NNN>` "
                   "(`underscore`). Restrict to specific schemes with a comma list, e.g. `--shards=of,dotnum`. "
                   "Grouping is "
                   "per-directory; files that match no scheme are listed unchanged. Off by default.",
        .values = kShardsValues,
        .topic = "stats",
    },
    {
        .name = "--shards-show",
        .display = "--shards-show=first|wildcard|count",
        .group = "shards",
        .header = "Sharded files",
        .summary = "how a collapsed shard set's line reads (default first)",
        .details = "Picks each collapsed set's display: `first` = the representative (lowest-index) shard's "
                   "path; `wildcard` = the masked-index name (the index digits shown as `???`); `count` = the "
                   "`wildcard` name plus the shard count. An incomplete set is always annotated "
                   "`(present/expected - INCOMPLETE)`. Only meaningful with `--shards`.",
        .values = kShardsShowValues,
        .topic = "stats",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--shards-dedup",
        .display = "--shards-dedup=first|mtime|error",
        .group = "shards",
        .header = "Sharded files",
        .summary = "how same-index shard duplicates are resolved (default first)",
        .details = "When two files are the same logical shard (they differ only by an opaque tail, e.g. a "
                   "regeneration id), `--shards-dedup` picks which is the representative: `first` keeps the "
                   "lexicographically-first name; `mtime` keeps the newest; `error` treats the duplicate as an "
                   "error and fails the run (non-zero exit). Also selects the representative used when "
                   "`-shard-status` classifies physical files.",
        .values = kShardsDedupValues,
        .topic = "stats",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--shard-pattern",
        .display = "--shard-pattern=REGEX",
        .group = "shards",
        .header = "Sharded files",
        .summary = "a custom shard scheme via a named-capture regex (repeatable); the escape hatch",
        .details = "Defines a custom sharded-file scheme for `--shards` and `-shard-status` when the built-ins "
                   "do not fit. REGEX is "
                   "an RE2 pattern with named groups: `(?P<stem>...)` and `(?P<index>...)` are required, "
                   "`(?P<total>...)` and `(?P<dup>...)` are optional. Repeatable; the patterns are tried in "
                   "order, before the built-in schemes.",
        .topic = "stats",
    },
    {
        .name = "--count",
        .alias = "-c",
        .display = "--count, -c",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, print a per-file matching-line count (path:count) instead of the lines",
        .affects = "-grep",
        .topic = "content",
    },
    {
        .name = "--context",
        .display = "--context=SPEC",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "-grep context lines: N both sides, or A:N,B:N,C:N for after/before/both",
        .details = "`--context=2` is grep's `-C 2` (two lines either side); the A / B / C keys inside the "
                   "value select one side (`--context=A:3,B:1`), which is what `--after-context` and "
                   "`--before-context` spell one at a time. xff has NO single-dash `-A` / `-B` / `-C`: those "
                   "letters are unclaimed for now (see TODO.md), and a single-dash flag would be an "
                   "expression primary under xff's dash-count rule rather than a whole-run option.",
        .affects = "-grep,-diff,--diff-context",
        .topic = "content",
    },
    {
        .name = "--after-context",
        .display = "--after-context=N",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, print N lines of context after each match (= --context=A:N)",
        .affects = "-grep",
    },
    {
        .name = "--before-context",
        .display = "--before-context=N",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, print N lines of context before each match (= --context=B:N)",
        .affects = "-grep",
    },
    {
        .name = "--max-results",
        .display = "--max-results=N",
        .group = "limits",
        .header = "Result limits",
        .summary = "list at most N matched entries without stopping or truncating reductions",
        .details =
            "Caps the implicit result listing after the whole expression, across every branch and every "
            "per-instance `-first` / `-top` filter. It does NOT stop traversal: `--summary`, `--histogram`, "
            "`--count`, and archive packing still see the complete matched set rather than silently reporting a "
            "partial walk. Explicit expression actions (`-print`, `-grep`, `-exec`, and friends) keep their own "
            "positional semantics and are not suppressed; use `-first` or `-top` before an action to cap the entries "
            "that reach it. With one capped filter this flag is usually redundant; its distinct use is an aggregate "
            "ceiling such as `\\( -type f -first 10 \\) -o \\( -type d -first 5 \\) --max-results=12`. Last "
            "occurrence wins. A malformed or negative count is a usage error; `0` lists none.",
    },
    {
        .name = "--top",
        .display = "--top=N",
        .group = "limits",
        .header = "Result limits",
        .summary = "with --summary or --histogram, keep only the N largest/tallest groups",
        .topic = "stats",
    },
    {
        .name = "--histogram-width",
        .display = "--histogram-width=N",
        .group = "stats-display",
        .header = "Statistics display",
        .summary = "cell width the tallest --histogram bar fills (default 40)",
        .affects = "--histogram",
        .topic = "stats",
    },
    {
        .name = "--summary-precision",
        .display = "--summary-precision=N",
        .group = "stats-display",
        .header = "Statistics display",
        .summary = "with --summary --human: fraction digits for scaled sizes (default 2; bytes stay integer)",
        .topic = "stats",
    },
    {
        .name = "--color",
        .display = "--color[=auto|always|never]",
        .group = "display",
        .header = "Terminal display",
        .summary = "colorize the plain listing by file type and language: auto (a tty), always, or never",
        .details = "Colorizes the plain listing by file type and, when the active language vocabulary supplies a "
                   "colour, programming language. auto colorizes only when stdout is a terminal and NO_COLOR is "
                   "unset; always forces color even through a pipe or pager and deliberately overrides NO_COLOR; "
                   "never disables it.",
        .values = kColorValues,
        .value_check = GlobalFlag::ValueCheck::kTristate,
    },
    {
        .name = "--color-scheme",
        .display = "--color-scheme=<SCHEME>",
        .group = "display",
        .header = "Terminal display",
        .summary = "which palette colour comes from: the terminal's ls theme, or xff's own",
        .details = "Colour is a whole-run choice, so this one palette is used by every surface that "
                   "colours - the plain listing and -ls alike; they cannot disagree. $LS_COLORS is the "
                   "variable `ls` and `dircolors` use, and xff reads the same keys: the two-letter "
                   "types (`di`, `ln`, `ex`, `pi`, `so`, `bd`, `cd`, `fi`) and the per-extension `*.tar=` entries. "
                   "Where only BSD's $LSCOLORS is set - the macOS case - that is read instead: its 11 "
                   "letter pairs carry the same types in a fixed order, with no way to say \"leave "
                   "this plain\" and no per-extension entries, so `merged` is the interesting scheme "
                   "there. $LS_COLORS wins when both are set, being the richer format. Both variables "
                   "are read on every platform rather than one per OS: which one is SET is better "
                   "evidence than which system this is (a macOS shell with GNU coreutils is themed "
                   "through $LS_COLORS, and $LSCOLORS is not macOS-only), and the fixed 22-character "
                   "shape makes the BSD one self-validating. "
                   "\"Use ls's colours\" turns out to mean three different things, so each has its "
                   "own name, spelled the way logic spells it: `+` is OR, and the merge is AND. "
                   "`auto` (the default, also `ls+xff` or `ls-or-xff`) is the theme OR xff's scheme - "
                   "a theme that is set at all is the whole answer, and with none set xff's scheme is, "
                   "so the decision is per VARIABLE; `default` is a fourth spelling of it, for a "
                   "config file that wants whatever the default currently is. `ls` is the theme "
                   "ALONE, so a type it never mentions prints uncoloured exactly as in a real ls "
                   "listing (and with no theme set, nothing is coloured). `merged` (also "
                   "`ls-and-xff`) is the theme "
                   "AND xff's scheme, merged per KEY: the theme where it speaks, xff's colour for "
                   "every key it omits - for a sparse theme you want filled in. (`ls&xff` is "
                   "deliberately not accepted: an unquoted `&` backgrounds the command.) `xff` "
                   "ignores $LS_COLORS entirely. In xff's scheme a regular non-executable file uses "
                   "the active language vocabulary's `#RRGGBB` colour when present. A theme's "
                   "matching extension or `fi` value wins; `ls` and a themed `auto` do not add "
                   "language colours, while `merged` uses one only for a key the theme omitted. "
                   "Executables and non-regular files retain their type colours. An EMPTY value in "
                   "the theme (`di=` or `fi=`) is it "
                   "saying \"leave these plain\" and is honoured as such; a malformed entry is "
                   "skipped rather than failing the run, as in ls. Whether colour is emitted at all "
                   "is --color's business, not this flag's.",
        .values = kColorSchemeValues,
        .affects = "--color",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--unicode",
        .display = "--unicode[=auto|always|never]",
        .group = "display",
        .header = "Terminal display",
        .summary = "--format=tree connectors: auto (a UTF-8 locale), always (Unicode), or never (ASCII)",
        .details = "Selects the box-drawing characters --format=tree connects nodes with. auto uses Unicode when the "
                   "locale (LC_ALL / LC_CTYPE / LANG) is UTF-8, else ASCII; always forces the Unicode connectors; "
                   "never forces the ASCII ones.",
        .values = kUnicodeValues,
        .value_check = GlobalFlag::ValueCheck::kTristate,
    },
    {
        .name = "--human",
        .display = "--human[=si|iec|off]",
        .group = "display",
        .header = "Terminal display",
        .summary = "size units for -ls / --summary: si (kB/MB, default), iec (KiB/MiB), off (bytes); xff -> si",
        .values = kHumanValues,
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--si",
        .display = "--si",
        .group = "display",
        .header = "Terminal display",
        .summary = "human sizes in SI (kB/MB, 1000^N); an alias for --human=si (the --human default)",
    },
    {
        .name = "--buffer",
        .display = "--buffer[=auto|off|all|N[kMGT]|NMB|NMiB]",
        .group = "display",
        .header = "Terminal display",
        .summary = "buffer to size columns (-ls / tables): auto, off, all, N[kMGT] rows, or NMB/NMiB bytes",
        .details = "Row windows use a bare count or decimal `k`/`M`/`G`/`T` multiplier. Byte budgets require an "
                   "explicit trailing `B`: `B`/`kB`/`MB`/.../`EB` are SI, while "
                   "`KiB`/`MiB`/.../`EiB` are IEC. The distinct suffixes keep rows and bytes unambiguous.",
    },
    {
        .name = "--width",
        .display = "--width[=auto|none|COLS]",
        .group = "display",
        .header = "Terminal display",
        .summary = "wrap column for plain --help text: auto (terminal width, else unwrapped), none, or a count",
        .details = "Wraps the flowing text of --help and --help=TOPIC (option and topic descriptions) to a "
                   "column width. auto uses the terminal width when stdout is a terminal (honoring $COLUMNS), "
                   "and leaves output unwrapped when it is not (a pipe or file); none (or 0) disables wrapping; "
                   "a positive integer sets a fixed width. Aligned vocabulary tables and example blocks keep "
                   "their own layout. Does not affect the file listing, `--man`, or formatted full help.",
    },
    {
        .name = "--pager",
        .display = "--pager[=help|auto|always|never|COMMAND]",
        .group = "display",
        .header = "Terminal display",
        .summary = "page output: help only, auto (all on a tty), always, never, or an explicit command",
        .details = "Pages every pageable output: long meta output (`--help`, `--help=TOPIC`, `--man`) and the "
                   "file listing, including action rows such as `-ls`. The default `help` "
                   "pages only those meta surfaces and only on a terminal. `auto` adds every pageable listing "
                   "when stdout is a terminal; `always` also pages through a pipe; `never` (or `--no-pager`) "
                   "disables it. Automatic command selection prefers an installed `less -FRX`, then `more`, and "
                   "only then consults `$XFF_PAGER` followed by ambient `$PAGER`. `--pager=COMMAND` always uses "
                   "COMMAND directly, so "
                   "`--pager=\"$PAGER\"` is the explicit way to request the process environment's choice. "
                   "Listings stream through one pager for the whole walk, so the first screen appears while the "
                   "walk is still running and quitting ends the run quietly. Paging steps aside for an expression "
                   "that needs the terminal itself (`-ok`, `-okdir`, `-exec`, `-execdir`, which can hand the "
                   "terminal to an editor) and for `--quiet`, which prints nothing to page; those runs are "
                   "simply unpaged.",
        .values = kPagerValues,
        .value_check = GlobalFlag::ValueCheck::kNone,
    },
    {
        .name = "--no-pager",
        .display = "--no-pager",
        .group = "display",
        .header = "Terminal display",
        .summary = "never page any output (an alias for --pager=never)",
    },
    {
        .name = "--quiet",
        .alias = "-q",
        .display = "--quiet, -q",
        .group = "exit",
        .header = "Exit code control",
        .summary = "suppress output; exit 0 if anything matched, else 1 (-q: grep-compatible)",
    },
    {
        .name = "--exit-match",
        .display = "--exit-match",
        .group = "exit",
        .header = "Exit code control",
        .summary = "keep output; exit 0 if anything matched, else 1",
    },
    {
        .name = "--safe",
        .display = "--safe",
        .group = "safety",
        .header = "Safety",
        .summary = "refuse destructive actions (-delete / -exec)",
        .details = "Rejects the run before traversal when its expression contains an armed `-delete` or exec-family "
                   "action. This is a hard guard, not a preview: use `--dry-run` when the goal is to see what a "
                   "supported write would do. `--safe` does not merely suppress the action after other expression "
                   "terms have run.",
    },
    {
        .name = "--dry-run",
        .display = "--dry-run",
        .group = "safety",
        .header = "Safety",
        .summary = "preview supported writes without changing the filesystem",
        .details = "Makes `-delete` print each path it would remove, makes `--archive-delete` list member deletions "
                   "without rewriting the container, and makes `--pack` report how many entries it would write "
                   "without creating the archive. Traversal and matching still run normally, so the preview uses "
                   "the real selected set.",
    },
    {
        .name = "--skip-unsupported",
        .display = "--skip-unsupported",
        .group = "safety",
        .header = "Safety",
        .summary = "warn and skip a predicate a filesystem cannot evaluate, not fail",
        .details = "Applies when a predicate is unsupported for an entry's filesystem, most commonly an archive "
                   "member that cannot provide an operation available on the host filesystem. Without this flag "
                   "the unsupported operation is a hard error; with it the entry is skipped and the reason is "
                   "reported. Ordinary I/O and traversal errors remain errors.",
    },
    {
        .name = "--exec-fields",
        .display = "--exec-fields",
        .group = "fields",
        .header = "Fields & Exec",
        .summary = "render -exec tokens through the field vocabulary ({name}, {path}, ...)",
    },
    {
        .name = "--define",
        .display = "--define=NAME=VALUE",
        .group = "fields",
        .header = "Fields & Exec",
        .summary = "define a value referenced as {def.NAME}",
    },
    {
        .name = "--time-format",
        .display = "--time-format=FMT",
        .group = "time",
        .header = "Time",
        .summary = "default format for time fields (a preset name or a strftime pattern)",
        .details = "Sets the default rendering for time fields ({mtime}, {atime}, -printf %t, ...) when no per-field "
                   "qualifier is given. Accepts a preset (iso, epoch, space, find) or any strftime pattern such as "
                   "%Y-%m-%d. A per-field qualifier like {mtime:%H:%M} still overrides it.",
    },
    {
        .name = "--timezone",
        .alias = "--tz",
        .display = "--timezone=ZONE, --tz=ZONE",
        .group = "time",
        .header = "Time",
        .summary = "zone for interpreting/formatting times (local, utc, an IANA name, or +HH:MM)",
        .details = "The zone used to interpret and format every time. Accepts local, utc, an IANA name like "
                   "Europe/London, or a fixed offset like +02:00. Affects time fields and -newerXt comparisons.",
    },
    {
        .name = "--time-zone-suffix",
        .display = "--time-zone-suffix[=auto|always|never]",
        .group = "time",
        .header = "Time",
        .summary = "show the zone offset on a time field: auto (per format), always, or never",
        .details = "Controls whether a time field's named preset renders its trailing zone (+0100, +01:00). "
                   "`auto` keeps each preset's default (`space` / `iso` / `rfc3339` show it, `asctime` / `epoch` "
                   "omit it); `never` drops it; `always` forces it, even on a preset that omits one. Accepts "
                   "`true` / `yes` / `on` (= `always`) and `false` / `no` / `off` (= `never`). The "
                   "inherently-zoned `zulu` / `zulu-dense` / `asn1z` always keep their mandatory Z, and a custom "
                   "strftime `--time-format` is never altered - control its zone with %z / %Ez / %Z yourself. "
                   "`asn1`'s zone is optional: `always` adds its ASN.1-style offset (+0100, no separator), "
                   "`never` / `auto` leave it bare.",
        .values = kZoneSuffixValues,
        .value_check = GlobalFlag::ValueCheck::kTristate,
    },
});

}  // namespace

absl::Span<const GlobalFlag> Globals() {
  return kGlobals;
}

mbo::types::OptionalRef<const GlobalFlag> LookupGlobal(std::string_view name) {
  for (const GlobalFlag& flag : kGlobals) {
    if (flag.name == name || (!flag.alias.empty() && flag.alias == name)) {
      return flag;
    }
  }
  return std::nullopt;
}

absl::Status ValidateGlobalValue(std::string_view arg) {
  const std::string_view::size_type equals = arg.find('=');
  if (equals == std::string_view::npos) {
    return absl::OkStatus();  // a bare or sign-suffixed form carries no value to check
  }
  const std::string_view name = arg.substr(0, equals);
  const std::string_view value = arg.substr(equals + 1);
  const mbo::types::OptionalRef<const GlobalFlag> flag = LookupGlobal(name);
  if (!flag.has_value()) {
    return absl::OkStatus();  // unknown flag: IsKnownGlobal reports it, with a better message
  }
  switch (flag->value_check) {
    case GlobalFlag::ValueCheck::kNone: return absl::OkStatus();
    case GlobalFlag::ValueCheck::kBool:
      if (values::ParseBool(value).has_value()) {
        return absl::OkStatus();
      }
      break;
    case GlobalFlag::ValueCheck::kTristate:
      if (values::ParseTristate(value).has_value()) {
        return absl::OkStatus();
      }
      break;
    case GlobalFlag::ValueCheck::kEnum:
      if (absl::c_any_of(flag->values, [value](const ValueDoc& doc) { return doc.value == value; })) {
        return absl::OkStatus();
      }
      break;
    case GlobalFlag::ValueCheck::kEnumOrTemplate:
      if (value.starts_with('{')
          || absl::c_any_of(flag->values, [value](const ValueDoc& doc) { return doc.value == value; })) {
        return absl::OkStatus();
      }
      break;
  }
  // The accepted list comes from the same table the help prints (or from the shared vocabulary),
  // so the error and the documentation cannot disagree.
  std::string accepted;
  if (flag->value_check == GlobalFlag::ValueCheck::kEnum
      || flag->value_check == GlobalFlag::ValueCheck::kEnumOrTemplate) {
    for (const ValueDoc& doc : flag->values) {
      if (doc.hidden) {
        continue;  // accepted, but not something to suggest
      }
      absl::StrAppend(&accepted, accepted.empty() ? "" : ", ", doc.value);
    }
  } else {
    accepted = flag->value_check == GlobalFlag::ValueCheck::kBool
                   ? "yes, no, on, off, true, false, 1, 0"
                   : "auto, always, never, on, off, yes, no, true, false, 1, 0";
  }
  return absl::InvalidArgumentError(
      absl::StrCat("unknown value '", value, "' for ", flag->name, " (accepted: ", accepted, ")"));
}

bool IsKnownGlobal(std::string_view arg) {
  // The sign ladders come from the flags themselves (GlobalFlag::sign_forms), so a new one is
  // recognised by declaring it rather than by also editing a list here.
  for (const GlobalFlag& flag : Globals()) {
    if (absl::c_contains(flag.sign_forms, arg)) {
      return true;
    }
  }
  // What is left are the compat aliases that carry no sign: -0 (= --format=nul) and -i
  // (= --case=insensitive).
  if (arg == "-0" || arg == "-i") {
    return true;
  }
  // Conventional attached short-option argument: -j4 / -jall. The help leads
  // with the more readable -j N and -j=N forms, but build-tool users expect this.
  if (arg.starts_with("-j") && arg.size() > 2 && !arg.starts_with("-j=")) {
    return true;
  }
  // An exact name or alias (bare flags, and valued flags used without a value).
  if (LookupGlobal(arg).has_value()) {
    return true;
  }
  // A valued form name=VALUE / alias=VALUE: the key must resolve to a flag that
  // advertises a value (its display contains '='), so `--safe=x` stays unknown while
  // `--sort=tree` / `--define=A=B` are accepted (only the key before the first '=').
  if (const std::string_view::size_type eq = arg.find('='); eq != std::string_view::npos) {
    const mbo::types::OptionalRef<const GlobalFlag> flag = LookupGlobal(arg.substr(0, eq));
    return flag.has_value() && absl::StrContains(flag->display, '=');
  }
  return false;
}

bool ExtraEnabled(std::string_view key) {
  // Answered by the LINKER, not by a parallel define: an extra registers itself into its backend slot
  // at static init, so asking the slot cannot drift from what the binary actually contains (the same
  // question `Pcre2Available()` answers for the regex slot). In the lean default build nothing
  // registers and every extra reads as off. New extras add a branch here and in ExtraBuildFlag.
  if (key == "archive") {
    return archive::ContainerSupportAvailable();
  }
  if (key == "asar") {
    return absl::c_any_of(
        archive::ContainerReadFormats(), [](const archive::ReadFormatInfo& format) { return format.name == "asar"; });
  }
  if (key == "brotli") {
    return absl::c_linear_search(archive::ContainerPackFormats(), "tar.br");
  }
  if (key == "fuse") {
    return fuse::MountSupportAvailable();
  }
  if (key == "mime-db") {
    return !mime::Databases().empty();
  }
  if (key == "language-db") {
    return !language::Databases().empty();
  }
  if (key == "squashfs") {
    return absl::c_any_of(archive::ContainerReadFormats(), [](const archive::ReadFormatInfo& format) {
      return format.name == "squashfs";
    });
  }
  if (key == "pcre2") {
    return regex::Pcre2Available();
  }
  return false;  // unknown / not-yet-wired extra
}

std::vector<std::string> EnabledExtras() {
  // The known keys live here next to ExtraBuildFlag for the same reason its spellings do: a new
  // extra adds its key to both (and a branch in ExtraEnabled), and the notice line, the extras
  // topic, and the rebuild hints all read from these three rather than keeping private copies.
  static constexpr std::array kKnownExtras = std::to_array<std::string_view>({
      "archive",
      "asar",
      "brotli",
      "fuse",
      "language-db",
      "mime-db",
      "pcre2",
      "squashfs",
  });
  std::vector<std::string> enabled;
  for (const std::string_view key : kKnownExtras) {
    if (ExtraEnabled(key)) {
      enabled.emplace_back(key);
    }
  }
  return enabled;
}

std::string_view ExtraBuildFlag(std::string_view key) {
  // The label a user must actually pass to rebuild with the extra. Spelled out per extra rather
  // than derived from the key: the Bazel flags carry an `xff_` prefix that the human-facing key
  // does not (`archive` -> `//xff:xff_archive`), and pcre2's flag is `xff_pcre`, so any derivation
  // rule would print a flag that does not exist - which is worse than useless in an error message.
  if (key == "archive") {
    return "--//xff:xff_archive";
  }
  if (key == "asar") {
    return "--//xff:xff_asar";
  }
  if (key == "brotli") {
    return "--//xff:xff_brotli";
  }
  if (key == "fuse") {
    return "--//xff:xff_fuse";
  }
  if (key == "mime-db") {
    return "--//xff:xff_mime_db";
  }
  if (key == "language-db") {
    return "--//xff:xff_language_db";
  }
  if (key == "pcre2") {
    return "--//xff:xff_pcre";
  }
  if (key == "squashfs") {
    return "--//xff:xff_squashfs";
  }
  return {};  // unknown extra: the caller omits the rebuild hint rather than inventing a flag
}

}  // namespace xff::cli
