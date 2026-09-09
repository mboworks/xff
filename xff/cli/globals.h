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

#ifndef XFF_CLI_GLOBALS_H_
#define XFF_CLI_GLOBALS_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "mbo/types/optional_ref.h"

namespace xff::cli {

// One allowed value of a flag that takes a `<PLACEHOLDER>` argument: the literal token
// and a short one-line meaning. A flag whose synopsis collapses a long value grammar to a
// placeholder (e.g. `--summary[=<GROUP>]`) lists its values here; the detail tier renders
// them as an aligned, wrapping `value  meaning` table, so the values stay documented
// without bloating the (never-wrapped) synopsis line.
struct ValueDoc {
  std::string_view value;    // the literal token, e.g. "ext"
  std::string_view meaning;  // one-line explanation, lower-case, no trailing period
  // Accepted but not LISTED: an alias whose canonical spelling is already shown (`md` for
  // `markdown`), or a name reserved for a better error elsewhere (`--regextype=MATCH`). The value
  // check accepts these; the help omits them, so one table stays the single source of truth for
  // both without the listing growing every synonym.
  bool hidden = false;
};

// One whole-run option ("global"), the flag counterpart of registry::Descriptor.
// Globals are processed by config / main, not by the expression parser, so they
// live here rather than in the registry; the help system and the planned man-page /
// .md generators enumerate them through Globals() the way they enumerate primaries
// through registry::All(), so the documentation cannot drift from the binary.
struct GlobalFlag {
  std::string_view name;     // primary lookup key, e.g. "--sort", "--jobs", "-H"
  std::string_view alias;    // alternate lookup key, or "" (e.g. "-j" for --jobs, "--tz")
  std::string_view display;  // human header, e.g. "-j N, -j=N, --jobs=N|all" or "--sort[=<ORDER>]"
  std::string_view group;    // short group key, e.g. "config", "traversal", "filter" (drives grouping)
  std::string_view header;   // display heading for the group, e.g. "Filter & Ignore" (shown at its first flag)
  std::string_view summary;  // one-line synopsis, lower-case, no trailing period
  // Optional multi-sentence explanation shown by `--help=NAME` and `--help=full` (the
  // long tier); empty falls back to the summary. `--help=all` shows the summary only.
  std::string_view details;
  // Optional allowed-value vocabulary for a flag whose synopsis carries a `<PLACEHOLDER>`
  // (e.g. `--summary[=<GROUP>]`). The detail tier renders these as an aligned, wrapping
  // `value  meaning` table above the explanation, so the value grammar stays off the
  // (never-wrapped) synopsis yet is fully documented. Empty = no value list.
  absl::Span<const ValueDoc> values;
  // The single source of truth for cross-references in the help system: a comma-separated
  // list of the expression primaries and other global flags whose behavior this flag changes
  // (e.g. "-diff" for --diff-format, "-grep,-diff" for --context). Detailed help derives an
  // "Affects:" block here and the reverse "Affected by:" block on each named entry, so the two
  // directions cannot drift (globals_test checks every token resolves). Each token is a primary
  // name ("-diff") or a global name ("--context"); a leading-'@' category token is a planned
  // extension (not yet resolved) for when one flag affects a whole family at once.
  std::string_view affects;
  // Help-topic membership, e.g. "stats". A `--help=TOPIC` body pulls every flag tagged with its
  // topic straight from this table, so the topic's flag list is the SOT and cannot drift. Empty
  // means the flag belongs to no topic body (it still appears in the grouped `--help` options).
  std::string_view topic;
  // The build-time "composable extra" this flag needs, e.g. "archive" (the `//xff:xff_archive` Bazel
  // flag + its `XFF_WITH_ARCHIVE` define). Empty = a core flag, always available. When set and that
  // extra is NOT compiled in (ExtraEnabled), the flag stays listed but is a hard error if used, and
  // the help system routes it into a separate "Extras (not built in)" group noting what to rebuild
  // with. The key alone is the SOT; the `--//xff:<extra>` hint is derived from it.
  std::string_view extra;
  // The chmod-style suffix signs a SHORT form accepts, as the literal spellings ("-z", "-z+",
  // "-z++"). IsKnownGlobal derives the accepted tokens from this, so a new sign-suffixed flag is
  // recognised by DECLARING it rather than by also editing a literal list somewhere else - the
  // drift that made `-Z` an "unknown option" until someone remembered.
  //
  // The suffix carries a LEVEL and nothing else (`-` off, none auto, `+` on, `++` on plus the
  // extra step); what each level MEANS is the flag's own business, and the families differ: `-g`
  // is a tri-state, `-z` is a nested ladder, `-Z` is that ladder with a capability added.
  absl::Span<const std::string_view> sign_forms;
  // How a `name=VALUE` form is CHECKED, so a typo is a usage error rather than a silent default
  // (`--case=insensitve` used to match case-sensitively and look like it worked). Only kNone
  // accepts anything: it is the default because most valued flags take free text (a path, a
  // format, a regex, a comma list they validate themselves).
  //
  // kEnum checks against this flag's own `values` table, which is therefore the SOT for both the
  // help and the check - they cannot disagree. kEnumOrTemplate additionally accepts a value that
  // starts with `{`; the flag's own parser validates that documented template grammar. kBool /
  // kTristate check against the shared vocabulary (values::ParseBool / ParseTristate), which accepts
  // more spellings than the table documents (`yes` / `1` beside `on`), so those must NOT be checked
  // as an enum.
  enum class ValueCheck : std::uint8_t { kNone, kEnum, kEnumOrTemplate, kBool, kTristate };
  ValueCheck value_check = ValueCheck::kNone;
  bool xff = true;  // false for a find-native option (-H/-L/-P); true for an xff extension
};

template<typename Sink>
void AbslStringify(Sink& sink, const GlobalFlag& flag) {
  sink.Append(flag.name);
}

// Whether the build-time extra `key` (e.g. "archive") is compiled into this binary. Reads the
// `XFF_WITH_*` defines the Bazel `//xff:<extra>` flags add via select(); an unknown key is false.
// The single point that maps an extra key to its compile-time availability, shared by main (the
// used-but-unavailable hard error) and the help system (the "Extras (not built in)" grouping).
bool ExtraEnabled(std::string_view key);

// The Bazel flag label that turns the extra `key` on, e.g. "--//xff:xff_archive" - what an error
// message tells the user to rebuild with. Empty for an unknown extra, so a caller omits the hint
// rather than naming a flag that does not exist. Kept beside ExtraEnabled so the two cannot drift.
std::string_view ExtraBuildFlag(std::string_view key);

// The extras compiled into THIS binary, by key, in a stable order - what the notice line and any
// "which extras do I have" surface print. Empty for a lean build.
[[nodiscard]] std::vector<std::string> EnabledExtras();

// All global options, in display order. The single enumeration point for the help
// system and the planned doc generators.
absl::Span<const GlobalFlag> Globals();

// The global option named `name` (matching the canonical name or an alias), or no reference if
// none. `name` carries its leading dashes (e.g. "--sort", "-j").
mbo::types::OptionalRef<const GlobalFlag> LookupGlobal(std::string_view name);

// Whether `arg` is a recognized whole-run global token, so `main` can reject an
// unknown leading option instead of silently ignoring it. Accepts: an exact name or
// alias; a valued `name=VALUE` / `alias=VALUE` form when the flag advertises a value
// (its `display` contains '='); and the compat
// aliases not in the table (`-0`, `-g+`, `-g-`). The meta flags `--help` / `--version`
// / `--man` / formatted full help are consumed before parsing and are not checked here.
bool IsKnownGlobal(std::string_view arg);

// Checks the VALUE of one `name=VALUE` global against the flag's declared vocabulary, so a
// mistyped value fails the run instead of silently selecting the default. Returns OK for a flag
// that takes free text (ValueCheck::kNone), for a token with no '=' (a bare or sign-suffixed
// form), and for an unknown flag (IsKnownGlobal reports that, with its own message).
//
// The error message lists the accepted values, generated from the flag's `values` table (or the
// shared vocabulary for a bool / tri-state), so it cannot drift from what the help prints.
absl::Status ValidateGlobalValue(std::string_view arg);

}  // namespace xff::cli

#endif  // XFF_CLI_GLOBALS_H_
