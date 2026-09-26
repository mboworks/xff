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

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/types/span.h"
#include "xff/archive/archive_backend.h"
#include "xff/cli/help_width.h"
#include "xff/fuse/fuse_backend.h"
#include "xff/matching/language/language_database_api.h"
#include "xff/matching/mime/database.h"
#include "xff/matching/regex/backend.h"
#include "xff/presentation/format/format.h"
#include "xff/registry/consumers.h"
#include "xff/values/values.h"

namespace xff::cli {
namespace {

// Allowed-value tables for the flags whose synopsis collapses a long value grammar to a
// `<PLACEHOLDER>` (e.g. `--summary[=<GROUP>]`). Rendered by the detail tier as an aligned,
// wrapping `value  meaning` table, so the values stay documented off the synopsis line.
constexpr std::array kHelpFormatValues = std::to_array<ValueDoc>({
    {.value = "plain", .meaning = "terminal text; respects width, color, and paging"},
    {.value = "markdown", .meaning = "Markdown document; no terminal wrapping or colors"},
    {.value = "md", .meaning = "alias for `markdown`"},
    {.value = "html", .meaning = "standalone HTML document; no terminal wrapping or colors"},
    {.value = "roff", .meaning = "man-page source; terminal formatting and paging follow `--man`"},
});

constexpr std::array kCaseValues = std::to_array<ValueDoc>({
    {.value = "sensitive", .meaning = "match exactly (-s-)"},
    {.value = "insensitive", .meaning = "fold case (-i)"},
    {.value = "smart", .meaning = "fold case unless the pattern contains ASCII uppercase (-s / -s+)"},
});
constexpr std::array kDetailedBlockPolicyValues = std::to_array<ValueDoc>({
    {.value = "archive", .meaning = "use dedicated controls for archive output and member edits"},
    {.value = "temp", .meaning = "use dedicated controls within the declared temporary root"},
    {.value = "output", .meaning = "use dedicated controls within the declared output root"},
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
    {.value = "ERE", .meaning = "platform POSIX extended regular expressions via regcomp(3)"},
    {.value = "EXACT", .meaning = "a literal string; metacharacters are plain text"},
    {.value = "FNMATCH", .meaning = "flat shell wildcard; `*` matches any character including `/`"},
    {.value = "GLOB", .meaning = "path-aware shell glob; wildcards and classes are component-local"},
    // Reserved: keep the resolver's unsupported-grammar diagnostic.
    {.value = "MATCH", .meaning = "", .hidden = true},
    {.value = "PCRE2", .meaning = "Perl syntax (lookaround, backreferences); a build extra"},
    {.value = "RE2", .meaning = "linear-time regular expressions (the default)"},
    {.value = "SHGLOB", .meaning = "GLOB plus `{a,b}` brace alternation, so `*.{cc,h}` matches either"},
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
    {.value = "jsonl", .meaning = "JSON objects for built-in listing, comparison, grep, and reduction records"},
    {.value = "csv", .meaning = "comma-separated columns"},
    {.value = "tsv", .meaning = "tab-separated columns"},
    {.value = "aligned", .meaning = "column-aligned table"},
    {.value = "markdown", .meaning = "a Markdown table (also `md`)"},
    {.value = "tree", .meaning = "an indented directory tree"},
    {.value = "md", .meaning = "", .hidden = true},  // the alias `markdown`'s meaning already names
});
constexpr std::array kSummaryScopeValues = std::to_array<ValueDoc>({
    {.value = "all", .meaning = "combine all input roots"},
    {.value = "root", .meaning = "separate input roots"},
    {.value = "left-only", .meaning = "comparison entries present only on the left"},
    {.value = "right-only", .meaning = "comparison entries present only on the right"},
    {.value = "different", .meaning = "different pairs counted once, with combined sizes"},
    {.value = "identical", .meaning = "identical pairs counted once, with combined sizes"},
    {.value = "left-total", .meaning = "all participating left entries"},
    {.value = "right-total", .meaning = "all participating right entries"},
    {.value = "left", .meaning = "alias for `left-total`"},
    {.value = "right", .meaning = "alias for `right-total`"},
    {.value = "diff", .meaning = "`left-only,right-only,different`"},
    {.value = "compare", .meaning = "`left-total,right-total`"},
});

constexpr std::array kSummaryValues = std::to_array<ValueDoc>({
    {.value = "none", .meaning = "clear all previously requested summaries"},
    {
        .value = "compare",
        .meaning = "comparison counts, combined sizes, and percentages by type and status; requires `--compare`",
    },
    {.value = "overall", .meaning = "one row aggregated over all matches"},
    {.value = "type", .meaning = "by file type"},
    {.value = "ext", .meaning = "by extension"},
    {.value = "lang", .meaning = "by programming language"},
    {.value = "mime", .meaning = "by media (MIME) type"},
    {.value = "user", .meaning = "by owner"},
    {.value = "owner", .meaning = "alias for `user`"},
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
constexpr std::array kRcForms = std::to_array<std::string_view>({"--rc-", "--rc+"});
constexpr std::array kCaseShorts = std::to_array<std::string_view>({"-s", "-s+", "-s-"});

constexpr std::array kArchiveValues = std::to_array<ValueDoc>({
    {.value = "none", .meaning = "an archive is one plain file (find behavior; the find-style default)"},
    {.value = "roots", .meaning = "dive only when a search root is itself an archive (the xff-family default)"},
    {.value = "all", .meaning = "also dive archives found during the walk (what bare `--archive` selects)"},
    {.value = "any", .meaning = "`all`, plus offer EVERY file to the reader, not only container-looking names"},
});
constexpr std::array kColorSchemeValues = std::to_array<ValueDoc>({
    {
        .value = "auto",
        .meaning = "ls OR xff: the theme when $LS_COLORS / $LSCOLORS is set, else xff's scheme (the "
                   "default; also spelled `ls+xff`, `ls-or-xff` or `default`)",
    },
    {.value = "ls", .meaning = "the theme alone ($LS_COLORS, else $LSCOLORS): what it omits prints plain, as in ls"},
    {
        .value = "merged",
        .meaning = "the theme where it speaks, xff's type/language colour where it does not (also `ls-and-xff`)",
    },
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
constexpr std::array kPackDuplicateValues = std::to_array<ValueDoc>({
    {.value = "error", .meaning = "reject duplicate normalized member destinations (the default)"},
    {.value = "first", .meaning = "keep the first input for each destination and skip later duplicates"},
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
    {.value = "summary", .meaning = "shorthand for `--compare=status --compare-select=none --summary=compare`"},
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
        .details =
            "A config style sets the defaults for ignore files, hidden files, sizes, sort order, and case. "
            "find restricts the expression to find-compatible vocabulary and defaults; whole-run xff "
            "globals remain available as explicit overrides. xff keeps find's grammar but sorts and prints "
            "human sizes; "
            "rg is opinionated (respect `.gitignore`, skip hidden, smart case). Every occurrence remains an "
            "active selector, so several named config blocks can apply. All config files use INI sections; each name "
            "is declared once per file and may be refined in other files. `--config=NAME` inside a section "
            "composes it with another config. Among the built-in style selectors, "
            "the last `find`, `xff`, or `rg` occurrence chooses the baseline; custom names do not change it. "
            "A `STYLE:EPOCH` spelling such as `xff:2` selects `STYLE` while retaining the full name as a config "
            "selector, which must be declared in an active config file. Only plain `find`, `xff`, and `rg` need no "
            "declaration. See `--help=styles` for the per-style defaults and `--help=config` for layering.",
        .topic = "config",
        .repetition = GlobalFlag::Repetition::kAccumulate,
    },
    {
        .name = "--require-system-globals",
        .display = "--require-system-globals",
        .group = "config",
        .header = "Config",
        .summary = "keep existing system globals active when named configs are skipped",
        .details = "Config-only: unsectioned system config only, once per positive/negative pair. System globals "
                   "remain active with `--no-system-config` or `--no-config`. This is the default; missing files "
                   "remain normal. Named sections can still be excluded.",
        .topic = "config",
        .config_only = true,
    },
    {
        .name = "--no-require-system-globals",
        .display = "--no-require-system-globals",
        .group = "config",
        .header = "Config",
        .summary = "permit skipping system globals along with named configs",
        .details = "Config-only: unsectioned system config only, once per pair. `--no-system-config` or `--no-config` "
                   "then excludes globals as well as named sections. Without a skip request, globals still apply. "
                   "Authoritative system admission and requirement controls are still inspected.",
        .topic = "config",
        .config_only = true,
    },
    {
        .name = "--require-user-globals",
        .display = "--require-user-globals",
        .group = "config",
        .header = "Config",
        .summary = "keep existing user globals active when named configs are skipped",
        .details = "Config-only: unsectioned system or user config, once per pair per file. The system decision wins. "
                   "User globals remain active with `--no-user-config` or `--no-config`. This is the default; missing "
                   "files remain normal. Named sections can still be excluded.",
        .topic = "config",
        .config_only = true,
    },
    {
        .name = "--no-require-user-globals",
        .display = "--no-require-user-globals",
        .group = "config",
        .header = "Config",
        .summary = "permit skipping user globals along with named configs",
        .details = "Config-only: unsectioned system or user config, once per pair per file. The system decision wins. "
                   "`--no-user-config` or `--no-config` then excludes user globals as well as named sections. Without "
                   "a skip request, globals still apply.",
        .topic = "config",
        .config_only = true,
    },
    {
        .name = "--allow-xffrc",
        .display = "--allow-xffrc",
        .group = "config",
        .header = "Config",
        .summary = "permit explicit and automatic .xffrc loading",
        .details = "Config-only: unsectioned system config or any user section. User decisions follow configuration "
                   "application order, including composed sections. A system denial remains authoritative. Neither the "
                   "CLI nor an .xffrc file may grant admission.",
        .topic = "config",
        .config_only = true,
    },
    {
        .name = "--no-allow-xffrc",
        .display = "--no-allow-xffrc",
        .group = "config",
        .header = "Config",
        .summary = "reject explicit and automatic .xffrc loading",
        .details =
            "Config-only: unsectioned system config or any user section. A system denial cannot be overridden by user "
            "or .xffrc content, or by skipping system defaults. Admission is checked before files are opened.",
        .topic = "config",
        .config_only = true,
    },
    {
        .name = "--no-config",
        .display = "--no-config",
        .group = "config",
        .header = "Config",
        .summary = "exclude named system and user configs and disable rc autoloading",
        .details = "Excludes named system and user sections and disables `.xffrc` autoloading regardless of flag "
                   "order. Existing globals remain active by default; the applicable `--no-require-system-globals` or "
                   "`--no-require-user-globals` permits excluding the corresponding globals too. Both files are still "
                   "read and validated. Missing automatic files remain normal. Explicit `--xffrc=FILE` inputs and "
                   "ignore files remain active.",
        .topic = "config",
        .cli_only = true,
    },
    {
        .name = "--no-system-config",
        .display = "--no-system-config",
        .group = "config",
        .header = "Config",
        .summary = "exclude named system config sections while retaining required globals",
        .details = "Excludes named sections in `/etc/xff.ini`. Unsectioned globals remain active by default or with "
                   "`--require-system-globals`; `--no-require-system-globals` allows excluding them too. The file is "
                   "still read and validated. User and explicit `.xffrc` configurations remain active.",
        .topic = "config",
        .cli_only = true,
    },
    {
        .name = "--no-user-config",
        .display = "--no-user-config",
        .group = "config",
        .header = "Config",
        .summary = "exclude named user config sections while retaining required globals",
        .details = "Excludes named user sections. Unsectioned globals remain active by default or with "
                   "`--require-user-globals`; `--no-require-user-globals` allows excluding them too. The system "
                   "decision takes precedence over the user declaration. The file is still read and validated. System "
                   "and explicit `.xffrc` configurations remain active.",
        .topic = "config",
        .cli_only = true,
    },
    {
        .name = "--rc",
        .display = "--rc[-|+]",
        .group = "config",
        .header = "Config",
        .summary = "discover .xffrc in argument roots; minus disables, plus includes descendants",
        .details = "Defaults to `--rc-` (off). `--rc` loads `.xffrc` in each directory search root; "
                   "`--rc+` also searches descendant directories. With no roots, the default root is `.`. "
                   "Discovery finishes before execution, in argument order, parent before children and "
                   "lexicographically among siblings. Files contribute to the whole invocation, after trusted "
                   "defaults and before CLI flags. Discovery ignores search filters and never follows directory "
                   "symlinks or enters archives. Only system/user configuration and the CLI may set this mode. "
                   "`--no-config` disables discovery. Unsectioned content requires a trusted "
                   "`--allow-rc-globals` grant; otherwise loading fails. Explicit `--xffrc=FILE` remains independent.",
        .affects = "--xffrc,--explain",
        .topic = "config",
        .sign_forms = kRcForms,
    },
    {
        .name = "--allow-rc-globals",
        .display = "--allow-rc-globals",
        .group = "config",
        .header = "Config",
        .summary = "permit unsectioned content in automatically discovered .xffrc files",
        .details = "Config-only: once per pair in unsectioned system/user configuration. A system "
                   "`--no-allow-rc-globals` decision cannot be overridden. Without a grant, an autoloaded file "
                   "containing unsectioned content fails before execution. Named sections remain available; "
                   "explicit `--xffrc=FILE` is unaffected. This permission does not arm dangerous actions.",
        .affects = "--rc",
        .topic = "config",
        .config_only = true,
    },
    {
        .name = "--no-allow-rc-globals",
        .display = "--no-allow-rc-globals",
        .group = "config",
        .header = "Config",
        .summary = "reject unsectioned content in automatically discovered .xffrc files",
        .details = "The default without an explicit grant. Config-only: once per pair before all sections "
                   "in system/user configuration. A system denial is authoritative. Rejection includes "
                   "unsectioned expressions and `--config=NAME`; nothing is silently dropped. "
                   "Explicit `--xffrc=FILE` keeps its existing global and named-section behavior.",
        .affects = "--rc",
        .topic = "config",
        .config_only = true,
    },
    {
        .name = "--xffrc",
        .display = "--xffrc=FILE",
        .group = "config",
        .header = "Config",
        .summary = "also load a specific config file (a non-arming tier; see --allow-exec)",
        .details = "Loads FILE as a config tier above the user config (naming it is consent to LOAD it). It is a "
                   "NON-ARMING tier: execution and deletion actions - the exec family (-exec/-execdir/-ok, "
                   "-capture) or -delete - are inert unless --allow-exec is set from a trusted tier (the CLI or the "
                   "user/system config, never from an --xffrc file itself). An unarmed dangerous line is dropped with "
                   "a one-line warning. Repeatable; later files win.",
        .affects = "--allow-exec",
        .topic = "config",
        .repetition = GlobalFlag::Repetition::kAccumulate,
        .cli_only = true,
    },
    {
        .name = "--allow-exec",
        .display = "--allow-exec",
        .group = "config",
        .header = "Config",
        .summary = "arm dangerous directives from explicit or autoloaded .xffrc files",
        .details = "Permits the sensitive/destructive directives (the exec family -exec/-execdir/-ok and -capture, "
                   "and the destructive -delete) carried by an explicit or autoloaded `.xffrc` file to actually run. "
                   "Honored only from "
                   "a trusted tier - typed on the CLI, or set in the user/system config - never from an --xffrc file "
                   "(so a named config cannot authorize itself). Arming cannot bypass unconditional blocks or "
                   "the active safe profile; see `--help=safety`.",
        .affects = "--xffrc",
        .topic = "config",
    },
    {
        .name = "--explain",
        .display = "--explain",
        .group = "config",
        .header = "Config",
        .summary = "inspect resolved configuration and execution resources",
        .details = "Prints the active style, every config source consulted and whether it was found, resolved flags "
                   "in application order with their provenance, rejected config directives, and the style-default "
                   "table with this run's effective values. The effective safety table shows unconditional blocks, "
                   "the stored safe profile, active decisions, and source file/line/section or CLI origins. "
                   "It also shows per-file category translation, resolved temp/output roots, and dry-run state. "
                   "Profile origins remain visible when an unconditional block wins. Named declarations list their "
                   "source, selection, availability or skip/validation reason, and whether they declare globals, "
                   "predicates, or actions; availability never authorizes a gated action. "
                   "The resource view reports worker limits, potential retained state, active hash/line-count "
                   "fields, and advisory expression costs. It does not predict bytes read, peak memory, or latency. "
                   "It performs enabled `.xffrc` discovery but does not "
                   "evaluate the expression. "
                   "Existing unreadable config files and missing explicit `--xffrc` files are errors. "
                   "For modifiers with registered dependencies, `inactive-modifier` notes identify effective CLI "
                   "settings with no consumer. Dormant config defaults and superseded settings stay quiet; "
                   "the notes do not reject commands or prove that a conditional action will run.",
        .topic = "config",
        .cli_only = true,
    },
    {
        .name = "-E",
        .display = "-E",
        .group = "matching",
        .header = "Matching",
        .summary = "use the configured extended-regex grammar (RE2 by default)",
        .details = "Accepts BSD/macOS find's leading extended-regex switch. In xff the extended grammar is "
                   "selected by `--regextype`; it defaults to `RE2`, and command-line `--re2` or `--pcre` "
                   "override a configured choice. This compatibility flag does not itself replace that choice.",
        .affects = "-regex,-iregex,-rxc,-irxc,-grep,-capture,-capturedir",
        .see_also = "regex,grammars",
        .xff = false,
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
        .see_also = "config,ignore,archive",
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
        .see_also = "config,ignore,archive",
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
        .see_also = "config,ignore,archive",
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
                   "(reading off AND writing disarmed). Filename sniffing follows that final mode too: "
                   "`--archive=any --archive=all` restores the filename gate, including when the modes come "
                   "from named configurations applied in that order. The find style defaults to `none`, "
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
        .required_consumer = registry::ModifierConsumer::kArchiveNestedTraversal,
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
        .required_consumer = registry::ModifierConsumer::kArchiveReduction,
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
        .summary = "alias for --archive=any: traverse all archives without the filename gate",
        .details = "Selects the same traversal mode as `--archive=any` or `-z++`: open archives discovered "
                   "during the walk, even when their names lack a known archive suffix. This may read every "
                   "candidate file to identify its format. A root file is always offered to the reader when "
                   "archive traversal is enabled. Like the other archive mode selectors, a later selector "
                   "can replace this mode, including `--archive=roots` or `--archive=none`.",
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
        .required_consumer = registry::ModifierConsumer::kArchiveTraversal,
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
        .required_consumer = registry::ModifierConsumer::kArchiveTraversal,
    },
    {
        .name = "--jobs",
        .alias = "-j",
        .display = "-j N, -j=N, --jobs=N|all",
        .group = "scheduling",
        .header = "Concurrency and ordering",
        .summary = "directory-read, eligible content-match and -exec workers",
        .details = "`N` is a positive integer; use `-j 4`, `-j=4`, or `--jobs=4`. The conventional attached "
                   "short form `-j4` is also accepted. Every form accepts `all` in place of `N`. `-j 1` makes "
                   "directory reads and `-exec ... ;` synchronous. At larger values, xff reads directories "
                   "on `N` worker threads. Independent content predicates also match in bounded parallel batches, "
                   "with output kept in traversal order; stateful expressions remain on the coordinator. "
                   "Xff may independently keep up to `N` semicolon-form `-exec` / "
                   "`-execdir` children outstanding. Reads and children can overlap; `N` is not one shared "
                   "operation budget. The children's truth value is therefore success on launch. The "
                   "`... +` batch forms still run once after the walk and propagate a failing exit status. With "
                   "no flag, xff uses one fewer than the detected cores, capped at 15 and floored at 1; find and "
                   "rg modes use every detected core. `all` always means every detected core.",
        .see_also = "output,stats",
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
        .see_also = "output,stats",
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
        .see_also = "size,output",
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
        .see_also = "regex,grammars",
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
        .affects = "-name,-path,-lname,-fuzzy,-fuzzypath,-regex,-rxc,-grep,-content",
        .see_also = "regex,grammars",
        .sign_forms = kCaseShorts,
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--regextype",
        .display = "--regextype=<GRAMMAR>",
        .group = "matching",
        .header = "Matching",
        .summary = "match engine: RE2, ERE, EXACT, FNMATCH, GLOB, SHGLOB, or PCRE2 (a build extra)",
        .details = "Selects one grammar for every `-regex`/`-iregex`, `-rxc`/`-irxc`, and `-grep` pattern in the "
                   "run; the last occurrence wins. `RE2` is the default. Selecting `PCRE2` in a build without "
                   "that extra is a hard error; see `--help=extras` for availability.",
        .values = kRegextypeValues,
        .affects = "-regex,-iregex,-rxc,-irxc,-grep,-capture,-capturedir",
        .primary_expansion_topic = "grammars",
        .see_also = "regex,grammars",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--re2",
        .display = "--re2",
        .group = "matching",
        .header = "Matching",
        .summary = "select the fast, linear-time RE2 grammar",
        .details = "A convenient command-line spelling of `--regextype=RE2`. It overrides a grammar selected "
                   "by configuration; among grammar selectors, the last occurrence wins.",
        .affects = "--regextype,-regex,-iregex,-rxc,-irxc,-grep,-capture,-capturedir",
        .see_also = "regex,grammars",
    },
    {
        .name = "--pcre",
        .display = "--pcre",
        .group = "matching",
        .header = "Matching",
        .summary = "select the PCRE2 grammar (a build extra)",
        .details = "A convenient command-line spelling of `--regextype=PCRE2`. It overrides a grammar selected "
                   "by configuration; among grammar selectors, the last occurrence wins. PCRE2 is available "
                   "only in a full build, and selecting it in a lean build is a usage error.",
        .affects = "--regextype,-regex,-iregex,-rxc,-irxc,-grep,-capture,-capturedir",
        .see_also = "regex,grammars",
        .extra = "pcre2",
    },
    {
        .name = "--exclude",
        .display = "--exclude=GLOB",
        .group = "path-filter",
        .header = "Path filters",
        .summary = "skip paths matching a gitignore-style glob (repeatable; a matched directory is pruned)",
        .topic = "ignore",
        .repetition = GlobalFlag::Repetition::kAccumulate,
    },
    {
        .name = "--include",
        .display = "--include=GLOB",
        .group = "path-filter",
        .header = "Path filters",
        .summary = "re-include paths a --exclude would skip, matching a gitignore-style glob (repeatable)",
        .topic = "ignore",
        .repetition = GlobalFlag::Repetition::kAccumulate,
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
        .repetition = GlobalFlag::Repetition::kAccumulate,
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
        .repetition = GlobalFlag::Repetition::kAccumulate,
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
        .repetition = GlobalFlag::Repetition::kAccumulate,
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
        .details =
            "`markdown` (alias `md`) also renders ordinary, comparison-result, and paired summary tables "
            "as Markdown, with vertically aligned separators and right-aligned numeric headers and values "
            "in both source and rendered tables. Aligned and Markdown cells and tree labels C-escape "
            "control bytes and literal backslashes; Markdown additionally escapes that spelling for "
            "its renderer. Widths include the escaped text. This does not alter raw plain output or "
            "CSV, TSV, NUL, and JSONL encoding; `--path-encoding` controls plain listing paths. "
            "Summary schemas come from `--summary`; `--columns` selects "
            "listing fields and "
            "cannot change summary columns. Markdown summaries have descriptive headings above their scope "
            "and table. `--no-header` omits those headings, table headers, and Markdown separator rows "
            "when producing fragments. Summaries support `plain`, `aligned`, `jsonl`, `markdown` (alias `md`), "
            "`csv`, and `tsv`; `nul` and `tree` are listing-only formats. CSV/TSV summary exports have one "
            "header for all requests, explicit row identity, raw numeric values, and canonical scope-prefixed "
            "columns. Missing metrics are empty; `--no-header` removes the single header. TSV uses the same "
            "backslash escaping as listings. Histograms, action output, and dry-run previews cannot be mixed "
            "into these exports. In comparison mode use `--compare=summary` or `--compare-select=none`. "
            "Comparison status records support `plain` and `jsonl`; comparison patches require `plain`. "
            "Use `--compare-select=none` to format only comparison summaries. Explicit expression actions "
            "retain their own output formats. Built-in `-grep` uses JSON match/context/count records with "
            "`jsonl` and rejects formats other than `plain` or `jsonl`; an explicit `-grep:FORMAT` remains authored "
            "output. JSONL textual values stay strings when valid UTF-8; other byte sequences use "
            "`{\"encoding\":\"base64\",\"data\":\"...\"}` with standard padded base64, preserving every byte.",
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
        .display = "--compare[=status|diff|summary]",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "compare two roots as selected statuses, a unified diff, or a summary",
        .details =
            "Requires exactly two roots. `--compare=summary` expands at its position to "
            "`--compare=status --compare-select=none --summary=compare`; later flags may override its settings. "
            "Combining the shorthand with bare `--summary` emits the comparison summary once. "
            "With an explicit `--summary-scope`, bare `--summary` also adds scoped totals; "
            "explicit groupings such as `--summary=ext` add their own tables. "
            "Bare `--compare` and "
            "`--compare=status` emit only discrepancies as tab-separated `left-only`, `right-only`, or "
            "`different` records. With `--format=jsonl`, each status is a JSON object containing "
            "`record: comparison`, `status`, relative `path`, `left_root`, and `right_root`. "
            "`--compare=diff` emits one unified tree diff, suitable for redirecting to "
            "a patch file; `--diff-context` and `--diff-algorithm` tune it. Unlike `-diff TARGET`, which is "
            "an expression action comparing each match from one walk with a templated target and therefore "
            "cannot discover target-only paths, `--compare` walks both roots independently and pairs the "
            "matches by relative path. The ordinary expression, ignore, hidden-file, archive, traversal, "
            "and `-P` / `-H` / `-L` symlink rules apply unchanged to each side; comparison itself enables "
            "none of them. Regular files are compared byte for byte (text and binary). Unfollowed symlinks "
            "are compared by target. Both walks complete before comparison records are emitted in bytewise "
            "relative-path order; `--sort` affects each walk, not that final order. In status mode, "
            "`--path-encoding=escape` makes control bytes in the relative path unambiguous. "
            "`--summary` / `--summary=compare` append results and combined bytes by entry type and status. "
            "Results count each pair once; bytes include both sides, including two copies of identical files. "
            "Directories and special entries are included; directory equality compares the entry kind, not its "
            "subtree. "
            "Ordinary summary groupings default to `--summary-scope=compare`: one table with left and right "
            "columns across all comparison categories. Explicit `--summary-scope=all` combines both sides; "
            "`--summary-scope=root` separates roots. `--compare` changes this default regardless of option order.",
        .values = kCompareValues,
        .topic = "compare",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--compare-select",
        .display = "--compare-select=KIND,...",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "tree-comparison results to emit: left-only, right-only, identical, different, all, or none",
        .details = "Selects comma-separated result kinds for `--compare`. The default is "
                   "`left-only,right-only,different`, so equal files stay silent. `all` selects every kind. "
                   "`none` or an empty value suppresses per-path output. Requires `--compare`; "
                   "neither comparison-result nor ordinary summaries are filtered by this selection. "
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
        .required_consumer = registry::ModifierConsumer::kDiffComputation,
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
        .see_also = "compare,content",
        .required_consumer = registry::ModifierConsumer::kFileDiff,
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
        .see_also = "compare,content",
        .required_consumer = registry::ModifierConsumer::kFileDiff,
    },
    {
        .name = "--diff-format",
        .display = "--diff-format=u|c|n|y",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "default -diff format: u/unified (default), c/context, n/normal, y/side-by-side",
        .values = kDiffFormatValues,
        .affects = "-diff",
        .see_also = "compare,content",
        .value_check = GlobalFlag::ValueCheck::kEnum,
        .required_consumer = registry::ModifierConsumer::kDefaultDiffFormat,
    },
    {
        .name = "--diff-context",
        .display = "--diff-context=N",
        .group = "diff",
        .header = "Tree comparison and diffs",
        .summary = "default -diff context lines (3); overrides --context for -diff, and -diff:uN overrides it",
        .affects = "-diff,--compare",
        .topic = "compare",
        .required_consumer = registry::ModifierConsumer::kDiffContext,
    },
    {
        .name = "--hash-algorithm",
        .display = "--hash-algorithm=<ALGO>",
        .group = "output",
        .header = "Output values and actions",
        .summary = "default digest for -hash / {hash} / --summary=hash (sha256 default)",
        .details = "Sets the default digest algorithm for the `-hash` action, the `{hash}` field, and "
                   "`--summary=hash`. `sha256` is "
                   "the default; a `-hash:ALGO` spec or a `{hash:ALGO}` qualifier overrides it per use. "
                   "`--explain` identifies a CLI default with no active consumer; suppressed output does not count.",
        .values = kHashAlgorithmValues,
        .see_also = "fields,output",
        .value_check = GlobalFlag::ValueCheck::kEnum,
        .required_consumer = registry::ModifierConsumer::kHashAlgorithm,
    },
    {
        .name = "--hash-encoding",
        .display = "--hash-encoding=hex|base64",
        .group = "output",
        .header = "Output values and actions",
        .summary = "default -hash / {hash} / --summary=hash rendering: hex (default) or base64",
        .details = "Used by hash actions, fields, and `--summary=hash` unless that use specifies its own encoding. "
                   "`--explain` identifies a CLI default with no active consumer; suppressed output does not count.",
        .values = kHashEncodingValues,
        .see_also = "fields,output",
        .value_check = GlobalFlag::ValueCheck::kEnum,
        .required_consumer = registry::ModifierConsumer::kHashEncoding,
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
        .primary_expansion_topic = "fields",
        .see_also = "fields",
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
        .name = "--root",
        .display = "--root=NAME=PATH",
        .group = "pack",
        .header = "Archive creation",
        .summary = "add a named search root; use NAME as its archive destination directory",
        .details = "Command-line only, like positional root operands. Repeatable. Each name must be a distinct single "
                   "directory component; `.` and `..`, "
                   "path separators, and control characters are rejected. A directory root contributes "
                   "`NAME/relative/path`; a file root contributes `NAME/basename`. The same physical path may "
                   "have different names. Named and positional roots retain their argument order, except "
                   "when `--sort` explicitly sorts roots. Names affect archive destinations; ordinary path "
                   "output still uses the input paths. Duplicate root names are errors even with "
                   "`--pack-duplicates=first`.",
        .affects = "--pack,--pack-duplicates,--sort",
        .topic = "archive",
        .repetition = GlobalFlag::Repetition::kAccumulate,
        .cli_only = true,
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
                   "archive. Member destinations are normalized; `--pack-duplicates` controls collisions. "
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
        .name = "--pack-duplicates",
        .display = "--pack-duplicates=error|first",
        .group = "pack",
        .header = "Archive creation",
        .summary = "reject duplicate archive destinations or keep the first input",
        .details = "Defaults to `error`. Member destinations are normalized before comparison: leading and "
                   "interior `./`, repeated separators, and trailing separators do not distinguish names. "
                   "`first` keeps the earliest collected input and skips later copies; it does not rename them. "
                   "Duplicate validation runs before archive output creation and also applies to `--dry-run`. "
                   "An error reports both source paths and preserves any existing destination archive.",
        .values = kPackDuplicateValues,
        .affects = "--pack",
        .topic = "archive",
        .extra = "archive",
        .value_check = GlobalFlag::ValueCheck::kEnum,
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
        .repetition = GlobalFlag::Repetition::kKeyed,
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
        .name = "--summary-scope",
        .display = "--summary-scope=SCOPE,...",
        .group = "stats",
        .header = "Statistics",
        .summary = "summarize roots or collect comparison column groups",
        .details =
            "Selects root populations or collects ordered comparison column groups for each ordinary "
            "`--summary` grouping. "
            "`all` combines every root; `root` separates input roots. Without an explicit scope, the default is "
            "`all` outside comparison and `compare` when `--compare` is active, regardless of option order. "
            "An explicit scope overrides that conditional default; repeated scopes use the last occurrence. "
            "Comparison scopes require `--compare`. `compare` expands to `left-total,right-total`; "
            "`diff` expands to `left-only,right-only,different`. `left` and `right` alias `left-total` and "
            "`right-total`; use `left-only` and `right-only` for unmatched entries. Output uses canonical names. "
            "Aliases expand in order and duplicate scopes are removed. Repeating the flag replaces "
            "the preceding list. Selection is independent of `--compare-select`. "
            "Selected comparison scopes share one table, with a column group per scope: count, "
            "count percentage, size, and size percentage. Side totals count their own entries and bytes. "
            "Categories count pairs once and sum both sides' bytes. Different grouping keys form a "
            "left-to-right transition row. Percentages use each column group's full population before "
            "`--top`; missing entries display a dash. Overlapping scopes are not added into a grand total. "
            "`all` counts both copies. Requires an active file summary such as `--summary` or `--summary=ext`; "
            "otherwise it is an error. `--compare=summary` expands to `--summary=compare`, which only counts "
            "comparison results and does not satisfy this requirement.",
        .values = kSummaryScopeValues,
        .affects = "--summary",
        .topic = "stats",
        .see_also = "compare",
        .value_check = GlobalFlag::ValueCheck::kEnumList,
    },
    {
        .name = "--summary",
        .display = "--summary[=<GROUP>]",
        .group = "stats",
        .header = "Statistics",
        .summary = "count, count percentage, size, and size percentage table; repeatable",
        .details =
            "With `--compare`, bare `--summary` or `--summary=compare` appends result counts, combined "
            "left-plus-right sizes, and percentages by type and status, plus a total after the comparison "
            "output. With explicit `--summary-scope`, bare `--summary` "
            "also adds ordinary count and size statistics. Other groupings default to one left/right table with "
            "`--compare` (`--summary-scope=compare`), "
            "and combine all roots otherwise (`--summary-scope=all`). Explicit scope selection overrides "
            "this conditional default regardless of option order. "
            "`--summary-scope` selects combined, per-root, or comparison-category tables. "
            "Plain and Markdown tables identify their grouping unless `--no-header` is set. "
            "JSONL summary rows carry `record=summary`, a zero-based `request` index, the canonical "
            "`summary` grouping, and `scope`. Template groupings also retain their exact `template`. "
            "Comparison tables identify `left_root` and `right_root`; ordinary tables identify `root` "
            "(empty when combined). Request indices distinguish repeated groupings and survive table reordering. "
            "JSONL rows distinguish aggregate totals with `is_total=true`; data rows have `is_total=false`, "
            "even when their group is named `total`. Text and Markdown quote data labels that are empty, "
            "equal to `total`, or begin with a quote, keeping the aggregate label unambiguous. "
            "Outside comparison, replaces the per-match listing with an aggregate table: match count and total "
            "size per group "
            "(overall, by type, extension, programming language, media (MIME) type, user (owner), owning "
            "group, file digest, or hash-verification result). The categorical keys reuse the "
            "{mime}/{user}/{group}/{hash} field "
            "vocabulary; --summary=hash groups identical files into one bucket (a dedup count, reading every "
            "file through its active filesystem, including archive members). It uses `--hash-algorithm` and "
            "`--hash-encoding`, just like `{hash}`. `--summary=hash-verification` requires exactly one `-hasheq` and "
            "counts its `verified` or "
            "`failed` verdict even when that verdict makes the complete expression false; an entry that "
            "short-circuits before reaching `-hasheq` is not counted. Empty expected values and unreadable "
            "entries are failed, matching `-hasheq` itself. A "
            "{template} key groups by any field value (e.g. --summary='{ext}-{type}'); a single m// "
            "extraction key (--summary='{capture.NAME:m/re/\\1/}') groups per extracted line, so a "
            "per-file command's multi-line output tallies per key (e.g. git-blame lines per author) - the "
            "size column is not meaningful there. Explicit groupings are repeatable: each request adds its own "
            "table (e.g. `--summary=ext --summary=type`), printed in request order within each scope. "
            "In comparison mode, bare `--summary` and `--compare=summary` reuse an existing comparison-results "
            "table; explicit `--summary=compare` requests remain repeatable. Comparison-result "
            "tables precede ordinary tables regardless of request order. Percentages use the complete table totals "
            "before "
            "`--top`; a zero denominator yields zero percent. "
            "Sizes sum entry metadata sizes, including directory metadata, never recursive subtree sizes. "
            "`--format=markdown` (or `md`) renders these tables with grouping-specific headings above their "
            "scopes and fixed summary columns. Scope/root labels and explanatory notes are Markdown bullet "
            "items so each stays on its own rendered line. Each accounting note stays with its table, "
            "separated from the next summary by a blank line. Exact bytes align with scaled sizes at the "
            "decimal boundary. "
            "`--format=jsonl` includes `count_percent` and `size_percent` alongside `count` and `bytes`. "
            "--top=N limits the rows of each, "
            "`--summary-precision` sets all summary percentage and scaled-size digits, and --format=jsonl emits one "
            "object per group "
            "for scripts. Supported summary formats are `plain`, `aligned`, `jsonl`, `markdown` (alias `md`), "
            "`csv`, and `tsv`; listing-only formats `nul` and `tree` are rejected. CSV/TSV exports use one "
            "header, explicit `is_total` identity, raw numeric metrics, and canonical scope-prefixed columns "
            "for comparison groups. They reject histograms, action output, and applicable dry-run previews. "
            "Per-path comparison records and expression actions retain their own output; use "
            "`--compare-select=none` or `--compare=summary` for summary-only exports.",
        .values = kSummaryValues,
        .topic = "stats",
        .repetition = GlobalFlag::Repetition::kAccumulate,
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
                   "object per bar for scripts. `--format=markdown` renders bucket/value tables with numeric "
                   "values right-aligned, including when combined with summaries. `--no-header` omits histogram "
                   "headings and column headers. `plain` and `aligned` retain text bars; `csv`, `tsv`, `nul`, and "
                   "`tree` are unsupported for histograms and fail before traversal or actions.",
        .topic = "stats",
        .repetition = GlobalFlag::Repetition::kAccumulate,
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
                   "per-directory; files that match no scheme are listed unchanged. Off by default. "
                   "In `--compare` mode, entries and reductions remain physical files; `--shards` does not "
                   "collapse them or infer a whole-set comparison result. Its scheme selection still applies "
                   "to `-shard-status`.",
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
        .required_consumer = registry::ModifierConsumer::kShardListing,
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
        .required_consumer = registry::ModifierConsumer::kShardGrouping,
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
        .repetition = GlobalFlag::Repetition::kAccumulate,
        .required_consumer = registry::ModifierConsumer::kShardGrouping,
    },
    {
        .name = "--only-matching",
        .display = "--only-matching",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "print each nonempty matched portion on its own line",
        .details =
            "With `-grep`, emit nonempty, non-overlapping matches instead of complete lines. Context is ignored. With "
            "`--count`, count individual matches. Inverted selection has no matching portions to print. "
            "`FNMATCH` treats the complete matching line as its matched portion.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--no-only-matching",
        .display = "--no-only-matching",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "print complete selected lines instead of matched portions",
        .details = "Restores complete-line output for `-grep`. Last setting wins.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--files-with-matches",
        .display = "--files-with-matches",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, print paths of files containing selected lines",
        .details = "Suppresses line and count output. Selection includes `--invert-match`. Each reached `-grep` action "
                   "reports its own result; this does not defer actions until the full expression succeeds.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--files-without-match",
        .alias = "--files-without-matches",
        .display = "--files-without-match, --files-without-matches",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, print paths of readable text files without selected lines",
        .details = "Empty text files qualify. Binary, unreadable and non-regular files do not. This changes the truth "
                   "of the `-grep` action to whether the file has no selected lines.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--count-matches",
        .display = "--count-matches",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, count nonempty matching portions per file",
        .details = "Counts non-overlapping matched portions instead of selected lines. Context and explicit grep "
                   "templates are superseded. Inverted selection has no matching portions to count.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--invert-match",
        .display = "--invert-match",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, select lines that do not match its pattern",
        .details = "Inverts line selection, not the file-level expression. `! -rxc PATTERN` instead selects files "
                   "whose content does not match. Does not invert other predicates.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--no-invert-match",
        .display = "--no-invert-match",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, select lines that match its pattern",
        .details = "Restores positive line selection. Last setting wins.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--line-number",
        .display = "--line-number",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "include line numbers in built-in plain grep output (default)",
        .details = "Prefix selected and context lines with one-based line numbers. Explicit templates and JSON records "
                   "retain their own fields.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--no-line-number",
        .display = "--no-line-number",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "omit line numbers from built-in plain grep output",
        .details = "Does not remove line fields from JSON records or explicit templates. Last setting wins.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--with-filename",
        .display = "--with-filename",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "include paths in built-in plain grep output (default)",
        .details = "Applies to line and count prefixes. Filename-only modes always print paths. Explicit templates and "
                   "JSON records retain their own fields.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--no-filename",
        .display = "--no-filename",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "omit paths from built-in plain grep line and count output",
        .details = "Filename-only modes always print paths. Explicit templates and JSON records retain their own "
                   "fields. Last setting wins.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
    },
    {
        .name = "--count",
        .alias = "-c",
        .display = "--count, -c",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, print a per-file matching-line count (path:count) instead of the lines",
        .details = "Counts selected lines, or nonempty matched portions with `--only-matching`. Files without selected "
                   "lines emit no count. Context and templates are superseded. The last count/filename mode wins.",
        .affects = "-grep",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kGrep,
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
                   "expression primary under xff's dash-count rule rather than a whole-run option. "
                   "A final symmetric before/after context also supplies the default for contextual `-diff` output, "
                   "unless `--diff-context` or a per-action count overrides it.",
        .affects = "-grep,-diff,--diff-context",
        .topic = "content",
        .required_consumer = registry::ModifierConsumer::kSharedContext,
    },
    {
        .name = "--after-context",
        .display = "--after-context=N",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, print N lines of context after each match (= --context=A:N)",
        .details = "Together with the other context settings, a final symmetric context also supplies the "
                   "default for contextual `-diff` output unless `--diff-context` or a per-action count overrides it.",
        .affects = "-grep,-diff",
        .see_also = "content,regex",
        .required_consumer = registry::ModifierConsumer::kSharedContext,
    },
    {
        .name = "--before-context",
        .display = "--before-context=N",
        .group = "grep-output",
        .header = "Content-match output",
        .summary = "with -grep, print N lines of context before each match (= --context=B:N)",
        .details = "Together with the other context settings, a final symmetric context also supplies the "
                   "default for contextual `-diff` output unless `--diff-context` or a per-action count overrides it.",
        .affects = "-grep,-diff",
        .see_also = "content,regex",
        .required_consumer = registry::ModifierConsumer::kSharedContext,
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
        .see_also = "output,safety",
    },
    {
        .name = "--top",
        .display = "--top=N",
        .group = "limits",
        .header = "Result limits",
        .summary = "with --summary or --histogram, keep only the N largest/tallest groups",
        .details = "Requires a non-negative integer; `0` removes the limit. Last occurrence wins. "
                   "Ordinary summary groups rank by bytes, then count, then name; comparison-scope tables rank by "
                   "the sum of displayed column-group bytes, including overlap. Extraction summaries rank by count "
                   "because they have no byte "
                   "dimension. Totals and percentage denominators include groups omitted by the limit. "
                   "When groups are omitted, summary tables state how many are shown out of the complete set; "
                   "JSONL rows add `groups_shown` and `groups_total`. Comparison scopes count distinct group "
                   "keys across the selected columns, including overlapping scopes only once. "
                   "Comparison-result summaries always show every type and status; `--top` does not truncate them. "
                   "Numeric-range histograms retain every range; only categorical histogram buckets are top-limited.",
        .affects = "--summary,--histogram",
        .topic = "stats",
        .required_consumer = registry::ModifierConsumer::kRankedReduction,
    },
    {
        .name = "--histogram-width",
        .display = "--histogram-width=N",
        .group = "stats-display",
        .header = "Statistics display",
        .summary = "cell width the tallest --histogram bar fills (default 40)",
        .details = "Requires a positive integer; zero, negative, malformed, and overflowing values are errors. "
                   "Last occurrence wins.",
        .affects = "--histogram",
        .topic = "stats",
        .value_check = GlobalFlag::ValueCheck::kPositiveInteger,
        .required_consumer = registry::ModifierConsumer::kHistogramBars,
    },
    {
        .name = "--summary-precision",
        .display = "--summary-precision=N",
        .group = "stats-display",
        .header = "Statistics display",
        .summary = "fraction digits for summary percentages and human-readable sizes (default 2)",
        .details =
            "Accepts integers from `0` through `9`; other values are errors. Last occurrence wins. "
            "Applies to ordinary and comparison summaries, including JSON percentage fields, and histogram means. "
            "Exact byte counts stay integers.",
        .affects = "--summary,--compare,--histogram",
        .topic = "stats",
        .required_consumer = registry::ModifierConsumer::kPrecisionReduction,
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
        .see_also = "output,environment",
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
        .see_also = "output,environment",
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
        .see_also = "output,environment",
        .value_check = GlobalFlag::ValueCheck::kTristate,
    },
    {
        .name = "--human",
        .display = "--human[=si|iec|off]",
        .group = "display",
        .header = "Terminal display",
        .summary = "size units for -ls / --summary: si (kB/MB, default), iec (KiB/MiB), off (bytes); xff -> si",
        .values = kHumanValues,
        .see_also = "output,environment",
        .value_check = GlobalFlag::ValueCheck::kEnum,
    },
    {
        .name = "--si",
        .display = "--si",
        .group = "display",
        .header = "Terminal display",
        .summary = "human sizes in SI (kB/MB, 1000^N); an alias for --human=si (the --human default)",
        .see_also = "output,environment",
    },
    {
        .name = "--buffer",
        .display = "--buffer[=auto|off|all|N[kMGT]|NMB|NMiB]",
        .group = "display",
        .header = "Terminal display",
        .summary = "buffer to size columns (-ls / tables): auto, off, all, N[kMGT] rows, or NMB/NMiB bytes",
        .details = "Row windows use a bare count or decimal `k`/`M`/`G`/`T` multiplier. Byte budgets require an "
                   "explicit trailing `B`: `B`/`kB`/`MB`/.../`EB` are SI, while "
                   "`KiB`/`MiB`/.../`EiB` are IEC. The distinct suffixes keep rows and bytes unambiguous. "
                   "Malformed, negative, and overflowing limits are errors before traversal. "
                   "`off` or `0` disables column buffering; `all` requests the complete row set.",
        .see_also = "output,environment",
        .value_check = GlobalFlag::ValueCheck::kBuffer,
    },
    {
        .name = "--help-format",
        .display = "--help-format=plain|markdown|html|roff",
        .group = "display",
        .header = "Terminal display",
        .summary = "renderer for any help target: plain (default), markdown (md), html, or roff",
        .details = "Requires help output, including a topic, flag, index, or the full reference. "
                   "For example, `--help=notice --help-format=markdown`. "
                   "Markup output ignores terminal `--width` and `--color`; Markdown and HTML disable "
                   "automatic paging, while explicit `--pager=always` or a pager command still applies. "
                   "Roff uses the same terminal formatting and paging as `--man`. "
                   "Conflicting format selections "
                   "are errors, including conflicts with `--man` (which selects `roff`).",
        .values = kHelpFormatValues,
        .see_also = "output,environment",
        .value_check = GlobalFlag::ValueCheck::kEnum,
        .cli_only = true,
    },
    {
        .name = "--width",
        .display = "--width[=auto|auto:COLS|none|COLS]",
        .group = "display",
        .header = "Terminal display",
        .summary = "width for plain help and comparison summaries: capped auto, auto, none, or a column count",
        .details = "Wraps the flowing text of `--help` and `--help=TOPIC` to a column width. "
                   "Also bounds plain/aligned comparison-summary tables: scopes use grouped column headers "
                   "when they fit, or labelled rows in one table when they do not. Numeric cells are never "
                   "truncated; a width below one metric row may overflow. The default is `auto:110`. "
                   "`auto:COLS` caps automatic width at `COLS`, using the cap when detection is unavailable; "
                   "caps below 40 are errors. Explicit `auto` (also bare `--width`) is uncapped and uses "
                   "`$COLUMNS` when set, "
                   "otherwise the terminal width when stdout is a terminal, otherwise unlimited width. "
                   "`none` (or `0`) disables wrapping; a positive integer sets a fixed width (at least "
                   "40 columns); 60 or more is recommended for readability. Aligned help vocabulary tables "
                   "and example blocks keep their own layout. "
                   "Does not affect the file listing, comparison-results table, summary legends or path "
                   "headings, `--man`, or formatted full help. Save a personal preference such as "
                   "`--width=auto:100` in the unsectioned user INI at "
                   "`<OS account home>/.config/xff/config`. Explicit CLI width overrides the preference. "
                   "Help reads only automatic system/user preferences, including selected named sections; "
                   "it never loads `.xffrc` files or executes configured actions. Unavailable or invalid "
                   "automatic preferences are ignored for help so configuration remains repairable.",
        .see_also = "output,environment",
        .value_check = GlobalFlag::ValueCheck::kWidth,
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
        .see_also = "output,environment",
        .value_check = GlobalFlag::ValueCheck::kNone,
    },
    {
        .name = "--no-pager",
        .display = "--no-pager",
        .group = "display",
        .header = "Terminal display",
        .summary = "never page any output (an alias for --pager=never)",
        .see_also = "output,environment",
    },
    {
        .name = "--quiet",
        .alias = "-q",
        .display = "--quiet, -q",
        .group = "exit",
        .header = "Exit code control",
        .summary = "suppress output; exit 0 if anything matched, else 1 (-q: grep-compatible)",
        .details = "In comparison mode, a match means a left-only, right-only, or different entry in the "
                   "matched population: `0` means discrepancies, `1` means none, and `2` means an error. "
                   "`--compare-select` and summary output do not change this status.",
        .see_also = "output",
    },
    {
        .name = "--exit-match",
        .display = "--exit-match",
        .group = "exit",
        .header = "Exit code control",
        .summary = "keep output; exit 0 if anything matched, else 1",
        .details = "In comparison mode, a match means a left-only, right-only, or different entry in the "
                   "matched population: `0` means discrepancies, `1` means none, and `2` means an error. "
                   "`--compare-select` and summary output do not change this status.",
        .see_also = "output",
    },
    {
        .name = "--block-policy-categories",
        .display = "--block-policy-categories=LIST",
        .group = "safety",
        .header = "Safety",
        .summary = "select categories with dedicated blocking controls (config only)",
        .details = "Selects separate controls; this directive itself neither blocks nor allows operations. "
                   "A comma-separated category list; empty means ordinary file controls for all categories (default). "
                   "`archive`, `temp`, and `output` are supported. Allowed once before all sections in system or user "
                   "configuration. Each file chooses its own "
                   "policy, including for its named sections. "
                   "Neither named sections, explicit `.xffrc` files, nor the CLI may set it. "
                   "See `--help=safety` for the operation table and archive replacement tradeoff.",
        .values = kDetailedBlockPolicyValues,
        .topic = "safety",
        .value_check = GlobalFlag::ValueCheck::kEnumList,
        .config_only = true,
    },
    {
        .name = "--temp-root",
        .display = "--temp-root=PATH",
        .group = "safety",
        .header = "Safety",
        .summary = "declare an existing absolute temp root (config only)",
        .details = "Allowed once in unsectioned system or user INI; a system declaration wins. "
                   "Permissions cover descendants recursively; the root itself remains protected. "
                   "Roots and descendant traversal must not contain symlinks. INI `${NAME}` substitutions are "
                   "allowed, but trust the caller-controlled environment to choose the root; use literal paths "
                   "for fixed administrator boundaries. See `--help=config` and `--help=safety`.",
        .topic = "safety",
        .config_only = true,
    },
    {
        .name = "--output-root",
        .display = "--output-root=PATH",
        .group = "safety",
        .header = "Safety",
        .summary = "declare an existing absolute output root (config only)",
        .details = "Allowed once in unsectioned system or user INI; a system declaration wins. "
                   "Permissions cover descendants recursively; the root itself remains protected. "
                   "Roots and descendant traversal must not contain symlinks. INI `${NAME}` substitutions are "
                   "allowed, but trust the caller-controlled environment to choose the root; use literal paths "
                   "for fixed administrator boundaries. See `--help=config` and `--help=safety`.",
        .topic = "safety",
        .config_only = true,
    },
    {
        .name = "--block-directory-creation",
        .display = "--block-directory-creation",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit directory creation; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-directory-creation",
        .display = "--safe-block-directory-creation",
        .group = "safety",
        .header = "Safety",
        .summary = "include directory creation in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-directory-creation",
        .display = "--no-safe-block-directory-creation",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude directory creation from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-directory-deletion",
        .display = "--block-directory-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit directory deletion; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-directory-deletion",
        .display = "--safe-block-directory-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "include directory deletion in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-directory-deletion",
        .display = "--no-safe-block-directory-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude directory deletion from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-temp-file-writing",
        .display = "--block-temp-file-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit temp file writing; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-temp-file-writing",
        .display = "--safe-block-temp-file-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "include temp file writing in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-temp-file-writing",
        .display = "--no-safe-block-temp-file-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude temp file writing from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-temp-file-overwrite",
        .display = "--block-temp-file-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit temp file overwrite; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-temp-file-overwrite",
        .display = "--safe-block-temp-file-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "include temp file overwrite in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-temp-file-overwrite",
        .display = "--no-safe-block-temp-file-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude temp file overwrite from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-temp-file-deletion",
        .display = "--block-temp-file-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit temp file deletion; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-temp-file-deletion",
        .display = "--safe-block-temp-file-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "include temp file deletion in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-temp-file-deletion",
        .display = "--no-safe-block-temp-file-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude temp file deletion from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-temp-directory-creation",
        .display = "--block-temp-directory-creation",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit temp directory creation; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-temp-directory-creation",
        .display = "--safe-block-temp-directory-creation",
        .group = "safety",
        .header = "Safety",
        .summary = "include temp directory creation in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-temp-directory-creation",
        .display = "--no-safe-block-temp-directory-creation",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude temp directory creation from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-temp-directory-deletion",
        .display = "--block-temp-directory-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit temp directory deletion; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-temp-directory-deletion",
        .display = "--safe-block-temp-directory-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "include temp directory deletion in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-temp-directory-deletion",
        .display = "--no-safe-block-temp-directory-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude temp directory deletion from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-output-file-writing",
        .display = "--block-output-file-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit output file writing; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-output-file-writing",
        .display = "--safe-block-output-file-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "include output file writing in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-output-file-writing",
        .display = "--no-safe-block-output-file-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude output file writing from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-output-file-overwrite",
        .display = "--block-output-file-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit output file overwrite; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-output-file-overwrite",
        .display = "--safe-block-output-file-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "include output file overwrite in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-output-file-overwrite",
        .display = "--no-safe-block-output-file-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude output file overwrite from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-output-file-deletion",
        .display = "--block-output-file-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit output file deletion; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-output-file-deletion",
        .display = "--safe-block-output-file-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "include output file deletion in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-output-file-deletion",
        .display = "--no-safe-block-output-file-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude output file deletion from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-output-directory-creation",
        .display = "--block-output-directory-creation",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit output directory creation; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-output-directory-creation",
        .display = "--safe-block-output-directory-creation",
        .group = "safety",
        .header = "Safety",
        .summary = "include output directory creation in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-output-directory-creation",
        .display = "--no-safe-block-output-directory-creation",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude output directory creation from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-output-directory-deletion",
        .display = "--block-output-directory-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit output directory deletion; later settings cannot clear it",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-output-directory-deletion",
        .display = "--safe-block-output-directory-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "include output directory deletion in the active safe-mode restrictions",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-output-directory-deletion",
        .display = "--no-safe-block-output-directory-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude output directory deletion from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for directory scope, capability composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-file-deletion",
        .display = "--block-file-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit deletion; later flags cannot remove this block",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-file-deletion",
        .display = "--safe-block-file-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "include deletion in the active safe-mode restrictions",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-file-deletion",
        .display = "--no-safe-block-file-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude deletion from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-execution",
        .display = "--block-execution",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit execution; later flags cannot remove this block",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-execution",
        .display = "--safe-block-execution",
        .group = "safety",
        .header = "Safety",
        .summary = "include execution in the active safe-mode restrictions",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-execution",
        .display = "--no-safe-block-execution",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude execution from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-file-writing",
        .display = "--block-file-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit writing; later flags cannot remove this block",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-file-writing",
        .display = "--safe-block-file-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "include writing in the active safe-mode restrictions",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-file-writing",
        .display = "--no-safe-block-file-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude writing from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-file-overwrite",
        .display = "--block-file-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit overwrite; later flags cannot remove this block",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-file-overwrite",
        .display = "--safe-block-file-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "include overwrite in the active safe-mode restrictions",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-file-overwrite",
        .display = "--no-safe-block-file-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude overwrite from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-archive-writing",
        .display = "--block-archive-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit archive writing; later flags cannot remove this block",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-archive-writing",
        .display = "--safe-block-archive-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "include archive writing in the active safe-mode restrictions",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-archive-writing",
        .display = "--no-safe-block-archive-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude archive writing from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-archive-overwrite",
        .display = "--block-archive-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit archive overwrite; later flags cannot remove this block",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-archive-overwrite",
        .display = "--safe-block-archive-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "include archive overwrite in the active safe-mode restrictions",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-archive-overwrite",
        .display = "--no-safe-block-archive-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude archive overwrite from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--block-archive-content-writing",
        .display = "--block-archive-content-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally block archive content writing",
        .details = "Applies to member edits of existing archives under `--block-policy-categories=archive`. "
                   "See `--help=safety` for the operation table and whole-archive replacement tradeoff.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-archive-content-writing",
        .display = "--safe-block-archive-content-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "include in the safe profile: archive content writing",
        .details = "Applies to member edits of existing archives under `--block-policy-categories=archive`. "
                   "See `--help=safety` for the operation table and whole-archive replacement tradeoff.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-archive-content-writing",
        .display = "--no-safe-block-archive-content-writing",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude from the safe profile: archive content writing",
        .details = "Applies to member edits of existing archives under `--block-policy-categories=archive`. "
                   "See `--help=safety` for the operation table and whole-archive replacement tradeoff.",
        .topic = "safety",
    },
    {
        .name = "--block-archive-content-overwrite",
        .display = "--block-archive-content-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally block archive content overwrite",
        .details = "Applies to member edits of existing archives under `--block-policy-categories=archive`. "
                   "See `--help=safety` for the operation table and whole-archive replacement tradeoff.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-archive-content-overwrite",
        .display = "--safe-block-archive-content-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "include in the safe profile: archive content overwrite",
        .details = "Applies to member edits of existing archives under `--block-policy-categories=archive`. "
                   "See `--help=safety` for the operation table and whole-archive replacement tradeoff.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-archive-content-overwrite",
        .display = "--no-safe-block-archive-content-overwrite",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude from the safe profile: archive content overwrite",
        .details = "Applies to member edits of existing archives under `--block-policy-categories=archive`. "
                   "See `--help=safety` for the operation table and whole-archive replacement tradeoff.",
        .topic = "safety",
    },
    {
        .name = "--block-archive-content-deletion",
        .display = "--block-archive-content-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "unconditionally prohibit archive deletion; later flags cannot remove this block",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--safe-block-archive-content-deletion",
        .display = "--safe-block-archive-content-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "include archive deletion in the active safe-mode restrictions",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe-block-archive-content-deletion",
        .display = "--no-safe-block-archive-content-deletion",
        .group = "safety",
        .header = "Safety",
        .summary = "exclude archive deletion from the safe profile; unconditional blocks still apply",
        .details = "See `--help=safety` for capability coverage, profile composition, and dry-run limits.",
        .topic = "safety",
    },
    {
        .name = "--no-safe",
        .display = "--no-safe",
        .group = "safety",
        .header = "Safety",
        .summary = "disable the safe-mode profile; unconditional blocks remain enforced",
        .topic = "safety",
    },
    {
        .name = "--safe",
        .display = "--safe",
        .group = "safety",
        .header = "Safety",
        .summary = "activate the configured safe-mode profile",
        .details = "Initially the profile blocks execution and file/archive deletion, writing, and overwrite. "
                   "`--safe-block-*` and `--no-safe-block-*` customize it without activating it. "
                   "`--no-safe` deactivates the profile. Unconditional `--block-*` restrictions always apply. "
                   "Prohibited actions are rejected before traversal; overwrite collisions are enforced at creation.",
        .topic = "safety",
    },
    {
        .name = "--dry-run",
        .display = "--dry-run",
        .group = "safety",
        .header = "Safety",
        .summary = "preview permitted actions without writes, deletion, or execution",
        .details =
            "Policy is checked first; dry run cannot bypass a block. Reads and normal output remain enabled. "
            "File output, deletion, and archives are previewed. Commands are reported but never launched. "
            "A skipped command or capture has no result: evaluation of that entry stops with an incomplete-preview "
            "error, rather than guessing which subsequent actions would run.",
        .topic = "safety",
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
        .see_also = "safety",
    },
    {
        .name = "--exec-fields",
        .display = "--exec-fields",
        .group = "fields",
        .header = "Fields & Exec",
        .summary = "render -exec tokens through the field vocabulary ({name}, {path}, ...)",
        .see_also = "fields,output",
    },
    {
        .name = "--define",
        .display = "--define=NAME=VALUE",
        .group = "fields",
        .header = "Fields & Exec",
        .summary = "define a value referenced as {def.NAME}",
        .see_also = "fields,output",
        .repetition = GlobalFlag::Repetition::kKeyed,
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
        .primary_expansion_topic = "time",
        .see_also = "time,fields",
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
        .see_also = "time,fields",
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
        .see_also = "time,fields",
        .value_check = GlobalFlag::ValueCheck::kTristate,
    },
});

static_assert(
    std::ranges::all_of(
        kGlobals,
        [](const GlobalFlag& entry) {
          return entry.primary_expansion_topic.empty()
                 || std::ranges::any_of(entry.see_also | std::views::split(','), [&](auto topic) {
                      return std::string_view(topic.begin(), topic.end()) == entry.primary_expansion_topic;
                    });
        }),
    "primary_expansion_topic must be an element of see_also");

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

mbo::types::OptionalRef<const GlobalFlag> LookupGlobalArgument(std::string_view arg) {
  if (const mbo::types::OptionalRef<const GlobalFlag> exact = LookupGlobal(arg); exact.has_value()) {
    return exact;
  }
  if (arg == "-0") {
    return LookupGlobal("--format");
  }
  if (arg == "-i") {
    return LookupGlobal("--case");
  }
  if (arg.starts_with("-j") && arg.size() > 2 && !arg.starts_with("-j=")) {
    return LookupGlobal("--jobs");
  }
  for (const GlobalFlag& flag : Globals()) {
    if (absl::c_contains(flag.sign_forms, arg)) {
      return flag;
    }
  }
  if (const std::string_view::size_type equals = arg.find('='); equals != std::string_view::npos) {
    const mbo::types::OptionalRef<const GlobalFlag> valued = LookupGlobal(arg.substr(0, equals));
    if (valued.has_value() && absl::StrContains(valued->display, '=')) {
      return valued;
    }
  }
  return std::nullopt;
}

namespace {
bool AcceptsEnumValue(const GlobalFlag& flag, std::string_view value) {
  return absl::c_any_of(flag.values, [value](const ValueDoc& doc) { return doc.value == value; });
}

bool AcceptsEnumList(const GlobalFlag& flag, std::string_view value) {
  return value.empty() || absl::c_all_of(absl::StrSplit(value, ','), [&](std::string_view item) {
           return AcceptsEnumValue(flag, item);
         });
}

std::string AcceptedValues(const GlobalFlag& flag) {
  if (flag.value_check == GlobalFlag::ValueCheck::kBuffer) {
    return "auto, off, all, a non-negative row count with optional k/M/G/T multiplier, or a byte budget";
  }
  if (flag.value_check == GlobalFlag::ValueCheck::kPositiveInteger) {
    return "a positive integer";
  }
  std::string accepted;
  if (flag.value_check == GlobalFlag::ValueCheck::kEnumList || flag.value_check == GlobalFlag::ValueCheck::kEnum
      || flag.value_check == GlobalFlag::ValueCheck::kEnumOrTemplate) {
    for (const ValueDoc& doc : flag.values) {
      if (doc.hidden) {
        continue;  // accepted, but not something to suggest
      }
      absl::StrAppend(&accepted, accepted.empty() ? "" : ", ", doc.value);
    }
  } else {
    accepted = flag.value_check == GlobalFlag::ValueCheck::kBool
                   ? "yes, no, on, off, true, false, 1, 0"
                   : "auto, always, never, on, off, yes, no, true, false, 1, 0";
  }
  return accepted;
}
}  // namespace

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
    case GlobalFlag::ValueCheck::kWidth: return ResolveHelpWidth(value, 0).status();
    case GlobalFlag::ValueCheck::kBuffer:
      if (format::ParseBufferWindow(value).has_value() || format::ParseByteBudget(value).has_value()) {
        return absl::OkStatus();
      }
      break;
    case GlobalFlag::ValueCheck::kPositiveInteger: {
      std::size_t parsed = 0;
      if (absl::SimpleAtoi(value, &parsed) && parsed > 0) {
        return absl::OkStatus();
      }
      break;
    }
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
    case GlobalFlag::ValueCheck::kEnumList:
      if (AcceptsEnumList(*flag, value)) {
        return absl::OkStatus();
      }
      break;
    case GlobalFlag::ValueCheck::kEnum:
      if (AcceptsEnumValue(*flag, value)) {
        return absl::OkStatus();
      }
      break;
    case GlobalFlag::ValueCheck::kEnumOrTemplate:
      if (value.starts_with('{') || AcceptsEnumValue(*flag, value)) {
        return absl::OkStatus();
      }
      break;
  }
  // The accepted list comes from the same table the help prints (or from the shared vocabulary),
  // so the error and the documentation cannot disagree.
  return absl::InvalidArgumentError(
      absl::StrCat("unknown value '", value, "' for ", flag->name, " (accepted: ", AcceptedValues(*flag), ")"));
}

bool IsKnownGlobal(std::string_view arg) {
  return LookupGlobalArgument(arg).has_value();
}

bool ExtraEnabled(std::string_view key) {
  // Answered by the LINKER, not by a parallel define: an extra registers itself into its backend slot
  // at static init, so asking the slot cannot drift from what the binary actually contains (the same
  // question `Pcre2Available()` answers for the regex slot). In the lean default build nothing
  // registers and every extra reads as off. New extras add a branch here and in ExtraBuildFlag.
  if (key == "apple") {
    return absl::c_any_of(
        archive::ContainerReadFormats(), [](const archive::ReadFormatInfo& format) { return format.name == "pbzx"; });
  }
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
      "apple",
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
  if (key == "apple") {
    return "--//xff:xff_apple";
  }
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
