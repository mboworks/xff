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

#include "xff/cli/help_build.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/span.h"
#include "xff/archive/archive_backend.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/cli/help_model.h"
#include "xff/cli/help_parse.h"
#include "xff/datetime/datetime.h"
#include "xff/engine/evaluate.h"
#include "xff/license/license.h"
#include "xff/matching/regex/regex.h"
#include "xff/presentation/fields/fields.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using DocPair = std::pair<std::string_view, std::string_view>;

// The expression registry split into the three EXPRESSION subsections, in display order.
struct KindSection {
  registry::Kind kind;
  std::string_view title;
};

constexpr std::array<KindSection, 3> kKindSections = {{
    {.kind = registry::Kind::kTest, .title = "Tests"},
    {.kind = registry::Kind::kAction, .title = "Actions"},
    {.kind = registry::Kind::kOperator, .title = "Operators"},
}};

// A single prose paragraph block from an authored string (backtick inline markup).
Content ProseOf(std::string_view text) {
  return Content{.node = Prose{.runs = ParseInline(text)}};
}

// A verbatim example block (no wrapping); optional info string for the fenced backends.
Content ExampleOf(std::string text, std::string lang = "") {
  return Content{.node = Example{.text = std::move(text), .lang = std::move(lang)}};
}

// A term/description vocabulary table; the description carries backtick markup.
Content RowsOf(absl::Span<const DocPair> pairs) {
  Rows rows;
  rows.rows.reserve(pairs.size());
  for (const auto& [term, desc] : pairs) {
    rows.rows.push_back(Row{.term = std::string(term), .description = ParseInline(desc)});
  }
  return Content{.node = std::move(rows)};
}

// The classification tags after a flag's term, e.g. (global, xff).
std::vector<std::string> FlagTags(const GlobalFlag& flag) {
  return {"global", flag.xff ? "xff" : "find"};
}

// The classification tags after a primary's term, e.g. (test, find) or
// (action, xff, runs commands).
std::vector<std::string> PrimaryTags(const registry::Descriptor& descriptor) {
  std::vector<std::string> tags;
  switch (descriptor.kind) {
    case registry::Kind::kAction: tags.emplace_back("action"); break;
    case registry::Kind::kOperator: tags.emplace_back("operator"); break;
    case registry::Kind::kTest: tags.emplace_back("test"); break;
  }
  tags.emplace_back(descriptor.style == registry::Style::kXff ? "xff" : "find");
  if (descriptor.safety == registry::Safety::kSecurity) {
    tags.emplace_back("runs commands");
  } else if (descriptor.safety == registry::Safety::kSafety) {
    tags.emplace_back("modifies the filesystem");
  }
  return tags;
}

// Every global flag whose `affects` list names `name` (a flag or primary), in
// Globals() display order - the reverse of the forward `affects` declaration, so the
// "Affected by:" block on any entry stays in lock-step with the flags' declarations.
std::vector<std::string_view> AffectedByFlags(std::string_view name) {
  std::vector<std::string_view> out;
  for (const GlobalFlag& flag : Globals()) {
    for (const std::string_view token : absl::StrSplit(flag.affects, ',', absl::SkipEmpty())) {
      if (token == name) {
        out.push_back(flag.name);
        break;
      }
    }
  }
  return out;
}

// Appends an influence detail line ("Header: a, b, c") to `blocks` when non-empty.
void AppendInfluence(Blocks& blocks, std::string_view header, const std::vector<std::string_view>& names) {
  if (names.empty()) {
    return;
  }
  blocks.push_back(ProseOf(absl::StrCat(header, " ", absl::StrJoin(names, ", "))));
}

// The heading line above a flag's value table: "GROUP is one of:" for a display that collapsed its
// value grammar to a `<PLACEHOLDER>` (e.g. "--summary[=<GROUP>]"), so the rows read as that
// placeholder's values. A display that spells its values inline instead (`--archive[=none|roots|all]`)
// has no placeholder to name, and gets the bare "One of:" - naming a placeholder that is not there
// used to read as the nonsense "One of is one of:".
std::string ValueHeading(std::string_view display) {
  const auto open = display.find('<');
  const auto close = display.find('>', open + 1);
  if (open == std::string_view::npos || close == std::string_view::npos) {
    return "One of:";
  }
  return absl::StrCat(display.substr(open + 1, close - open - 1), " is one of:");
}

// A definition entry for a global flag: its display, summary, and (when `with_details`)
// its detail blocks enriched with a "not built into this binary" note when its build
// extra is absent and the Affects / Affected-by influence blocks. `with_details` is
// false for the terse usage page (summary + tags only).
Content FlagEntry(const GlobalFlag& flag, bool with_details = true, Audience audience = Audience::kThisBinary) {
  Blocks details;
  // The not-built note shows even on the terse usage page: a flag whose build extra is
  // absent is a hard error if used, so the reader must see it up front. It is OMITTED for a
  // published reference, which documents the tool rather than whichever binary generated it -
  // the flag's own details still say it is a build extra and name the flag to rebuild with.
  if (audience == Audience::kThisBinary && !flag.extra.empty() && !ExtraEnabled(flag.extra)) {
    details.push_back(ProseOf(
        absl::StrCat(
            "NOT built into this binary: rebuild with `", ExtraBuildFlag(flag.extra),
            "` (used as-is, it is a hard error).")));
  }
  if (with_details) {
    // The allowed-value table (for a flag whose synopsis collapsed a value grammar to a
    // `<PLACEHOLDER>`) leads, before the prose: a "<LABEL> is one of:" heading line (see
    // ValueHeading) so the
    // rows read as that placeholder's values, then the aligned wrapping `value  meaning` list.
    if (!flag.values.empty()) {
      details.push_back(ProseOf(ValueHeading(flag.display)));
      Rows rows;
      rows.rows.reserve(flag.values.size());
      for (const ValueDoc& value : flag.values) {
        if (value.hidden) {
          continue;  // an alias or reserved name: accepted by the check, not listed here
        }
        rows.rows.push_back(Row{.term = std::string(value.value), .description = ParseInline(value.meaning)});
      }
      // A title-less subsection nests the value table one level deeper than its label.
      Subsection group;
      group.children.push_back(Content{.node = std::move(rows)});
      details.push_back(Content{.node = std::move(group)});
    }
    for (Content& block : ParseBlocks(flag.details)) {
      details.push_back(std::move(block));
    }
    AppendInfluence(details, "Affects:", absl::StrSplit(flag.affects, ',', absl::SkipEmpty()));
    AppendInfluence(details, "Affected by:", AffectedByFlags(flag.name));
  }
  return Content{
      .node = Entry{
          .term = std::string(flag.display),
          .summary = ParseInline(flag.summary),
          .details = std::move(details),
          .xff = flag.xff,
          .tags = FlagTags(flag),
          .anchor = absl::StrCat("flag-", flag.name),
      }};
}

// A definition entry for an expression primary: its synopsis, summary, and (when
// `with_details`) its detail blocks + the Affected-by block. `with_details` is false
// for the terse usage page (summary + tags only).
Content PrimaryEntry(const registry::Descriptor& descriptor, bool with_details = true) {
  Blocks details;
  if (with_details) {
    details = ParseBlocks(descriptor.details);
    AppendInfluence(details, "Affected by:", AffectedByFlags(descriptor.name));
  }
  const std::string hint = ArgHint(descriptor);
  std::string term = absl::StrCat(descriptor.name, hint);
  if (!descriptor.alias.empty()) {
    absl::StrAppend(&term, ", ", descriptor.alias, hint);
  }
  return Content{
      .node = Entry{
          .term = std::move(term),
          .summary = ParseInline(descriptor.summary),
          .details = std::move(details),
          .xff = descriptor.style == registry::Style::kXff,
          .tags = PrimaryTags(descriptor),
          .anchor = absl::StrCat("primary-", descriptor.name),
      }};
}

// FIELDS: translate the fields module's canonical vocabulary and syntax documentation
// into backend-neutral help-model nodes.
Section BuildFields() {
  Section section{.title = "Fields"};
  section.children.push_back(ProseOf(
      "The `{field}` placeholder vocabulary, substituted per entry in `--template` / `--format`, in "
      "`-printf` via the `%{field}` escape, and (with `--exec-fields`) in `-exec`."));

  const absl::Span<const fields::FieldDoc> docs = fields::FieldDocs();
  std::string_view group;
  Subsection current;
  bool have_current = false;
  Rows rows;
  const auto flush = [&] {
    if (have_current) {
      if (!rows.rows.empty()) {
        current.children.push_back(Content{.node = std::move(rows)});
        rows = Rows{};
      }
      section.children.push_back(Content{.node = std::move(current)});
    }
  };
  for (const fields::FieldDoc& doc : docs) {
    if (doc.group != group) {
      flush();
      group = doc.group;
      current = Subsection{.title = std::string(doc.header)};
      have_current = true;
    }
    std::string term = absl::StrCat("{", doc.name, "}");
    for (const std::string_view alias : doc.aliases) {
      absl::StrAppend(&term, " {", alias, "}");
    }
    rows.rows.push_back(Row{.term = std::move(term), .description = ParseInline(doc.summary)});
  }
  flush();

  const fields::FieldHelpDocs& help = fields::FieldSyntaxDocs();
  Subsection braces{.title = "Braces"};
  Bullets brace_rules;
  for (const std::string& rule : help.brace_rules) {
    brace_rules.items.push_back(ParseInline(rule));
  }
  braces.children.push_back(Content{.node = std::move(brace_rules)});
  section.children.push_back(Content{.node = std::move(braces)});

  Subsection dynamic{.title = "Dynamic namespaces"};
  Rows dynamic_rows;
  for (const fields::FieldHelpRow& row : help.dynamic_namespaces) {
    dynamic_rows.rows.push_back(Row{.term = row.term, .description = ParseInline(row.description)});
  }
  dynamic.children.push_back(Content{.node = std::move(dynamic_rows)});
  section.children.push_back(Content{.node = std::move(dynamic)});

  Subsection quals{.title = "Qualifiers ({field:QUAL})"};
  Rows qualifier_rows;
  for (const fields::FieldHelpRow& row : help.qualifiers) {
    qualifier_rows.rows.push_back(Row{.term = row.term, .description = ParseInline(row.description)});
  }
  quals.children.push_back(Content{.node = std::move(qualifier_rows)});
  quals.children.push_back(ProseOf(help.qualifier_pipeline));
  quals.children.push_back(ExampleOf(help.qualifier_example));
  quals.children.push_back(ProseOf(help.printf_note));
  section.children.push_back(Content{.node = std::move(quals)});
  return section;
}

// Builds one section whose children are a lead prose paragraph followed by a vocabulary table.
Section VocabSection(std::string_view title, std::string_view prose, absl::Span<const DocPair> pairs) {
  Section section{.title = std::string(title)};
  section.children.push_back(ProseOf(prose));
  section.children.push_back(RowsOf(pairs));
  return section;
}

// The sub-vocabulary sections, each also standalone as its own `--help=TOPIC` (see
// TopicReference). Named so BuildReference and the topic render share one definition.
Section PrintfSection() {
  return VocabSection(
      "Printf directives", "Directives for `-printf` / `-fprintf` / `-println` FORMAT, and the `%{field}` escape.",
      engine::PrintfDocs());
}

Section TimeSection() {
  Section section = VocabSection(
      "Time formats", "Presets and strftime patterns for --time-format, --timezone, and time-field {:qualifiers}.",
      datetime::FormatDocs());
  section.children.push_back(ProseOf(
      "Time zone. `--timezone=ZONE` (alias `--tz`) sets the zone every time is interpreted and rendered in: "
      "`local` (the default), `utc` (also `z` / `zulu`), an IANA name like `Europe/London`, or a fixed offset "
      "like `+02:00` / `-0800`. It shifts the wall-clock digits of every time field and governs `-newerXt` "
      "comparisons; it does not by itself add or remove the printed zone suffix."));
  section.children.push_back(ProseOf(
      "Zone suffix. `--time-zone-suffix=never` drops the trailing offset (`+0100`, `+01:00`) from a preset that "
      "shows it by default (`space`, `iso` / `iso8601-*`, `rfc3339`); `always` forces one on, even onto `asctime` "
      "which omits it; `auto` (the default) keeps each preset's built-in behavior. `true` / `false` are accepted "
      "for `always` / `never`. Two things it never touches: the inherently-zoned `zulu` / `zulu-dense` / `asn1z` "
      "keep their mandatory `Z` (UTC is the format's identity), and a custom strftime `--time-format` is left "
      "exactly as written - control its zone there with `%z` / `%Ez` / `%Z` yourself. `asn1`'s zone is optional, "
      "so `always` appends its ASN.1-style offset (`+0100`, no separator) and `never` / `auto` leave it bare."));
  return section;
}

Section SizeSection() {
  Section section = VocabSection(
      "Size units", "Units for -size / -blocks [+|-]N[unit]; spell SI and IEC explicitly.", engine::SizeUnitDocs());
  section.children.push_back(ProseOf(
      "All byte counts use an unsigned 64-bit value, so the largest representable size is "
      "`18446744073709551615 B` (about `18.45 EB`, just under `16 EiB`). A number multiplied by its "
      "unit must fit that range: `18EB` and `15EiB` fit, while `19EB` and `16EiB` overflow and are "
      "usage errors. `ZB` and `ZiB` are not accepted because one zettabyte/zebibyte already exceeds "
      "the representation; listing their suffixes would promise a unit for which no positive whole "
      "value can be represented."));
  return section;
}

Section GrammarsSection() {
  return VocabSection(
      "Regex grammars",
      "The grammar for `-regex` / `-iregex` and the content matchers `-rxc` / `-grep`, chosen by `--regextype` "
      "(default `RE2`). `ERE`, `EXACT`, `FNMATCH`, `GLOB` and `SHGLOB` are core engines, always built in; `PCRE2` is a "
      "build-time extra (see `--help=extras`). RE2 and PCRE2 have canonical external references, so the "
      "smaller engines are spelled out in full here: they have no single authoritative man page, and "
      "ERE and FNMATCH delegate to the platform's regcomp(3) and fnmatch(3), whose locale, class, and collation "
      "details vary by system.",
      regex::GrammarDocs());
}

// ENVIRONMENT: the variables xff honors. Hand-authored - the getenv sites are scattered across
// color / pager / help-width / config with no single registry - and kept honest by help_render_test.
// Standalone as `--help=environment` (env) and folded into the full reference / man page.
Section EnvironmentSection() {
  Section env{.title = "Environment"};
  env.children.push_back(ProseOf(
      "Environment variables xff reads. An explicit command-line flag generally overrides the matching "
      "variable."));
  static constexpr auto kVars = std::to_array<DocPair>({
      {"NO_COLOR",
       "when set (any value), disables color like `--color=never`; `--color=always` still wins "
       "(https://no-color.org)"},
      {"XFF_PAGER", "the first automatic environment fallback when neither `less` nor `more` is available"},
      {"PAGER", "the final automatic environment fallback when no known or xff-specific pager is available"},
      {"XFF_MANPAGER",
       "the pager / formatter for `--man`; overrides the built-in `mandoc` pipeline; set empty to disable"},
      {"COLUMNS", "terminal width used to wrap plain `--help` text for `--width=auto` when the tty size is unknown"},
      {"XFF_CONFIG",
       "explicit path to the config file, taking precedence over the XDG / HOME search (see `--help=config`)"},
      {"XDG_CONFIG_HOME", "config search root: `$XDG_CONFIG_HOME/xff/config` (see `--help=config`)"},
      {"HOME", "config fallback: `$HOME/.config/xff/config` when `$XDG_CONFIG_HOME` is unset"},
      {"LC_ALL, LC_CTYPE, LANG",
       "locale for `--unicode=auto`: a UTF-8 locale selects the Unicode `--format=tree` connectors, else ASCII"},
      {"LSCOLORS",
       "the same theme in BSD / macOS spelling (11 letter pairs); read when `$LS_COLORS` is unset, which "
       "is what makes a themed macOS shell work (see `--color-scheme`)"},
      {"LS_COLORS",
       "the terminal's colour theme, as `ls` / `dircolors` set it: type keys (`di`, `ln`, `ex`, ...) and "
       "per-extension `*.tar=` entries, used by default (see `--color-scheme`)"},
      {"XDG_RUNTIME_DIR",
       "preferred directory for a member extracted by `--archive-extract`: it is a memory-backed tmpfs, so "
       "the copy avoids disk when this location is usable (`/dev/shm` is tried next)"},
      {"TMPDIR",
       "where a temporary file goes when no memory-backed directory fits it: an extracted member "
       "(`--archive-extract`) and the in-progress rewrite of a container (`--archive-delete`)"},
  });
  env.children.push_back(RowsOf(kVars));
  env.children.push_back(ProseOf(
      "Any process environment variable is also readable in the field vocabulary as `{env.NAME}` (see "
      "`--help=fields`)."));
  return env;
}

// STATISTICS: the two terminal reductions (--summary / --histogram). The flags are
// pulled from the globals SOT via the "stats" topic tag so the list cannot drift, then
// worked examples. Standalone as `--help=stats` (see TopicReference) and folded into
// the full reference. `in_full` (the folded-in case) omits the per-flag entries: the
// full reference already documents every flag in its grouped Options section, so
// repeating them here is pure duplication - the narrative + examples are what add value.
Section StatsSection(bool in_full) {
  Section section{.title = "Statistics"};
  section.children.push_back(ProseOf(
      "xff statistics reductions. `--summary` and `--histogram` replace the per-match listing with an "
      "aggregate over all matches; they are independent and combinable (one walk feeds both), and an "
      "explicit action (`-print` / `-exec`) still runs. `--format=jsonl` emits machine rows instead."));
  for (const GlobalFlag& flag : Globals()) {
    if (!in_full && flag.topic == "stats") {
      section.children.push_back(FlagEntry(flag));
    }
  }
  static constexpr std::array<DocPair, 5> kExamples = {{
      {"xff --summary=ext", "files + total size per extension"},
      {"xff --histogram=ext", "a bar chart of files per extension"},
      {"xff --histogram='ext:sum(lines)'", "total lines per extension"},
      {"xff --histogram=size", "the file-size distribution"},
      {"xff --summary=type --histogram=ext --format=jsonl", "both, as machine rows"},
  }};
  Subsection examples{.title = "Examples"};
  // Each example is a verbatim (copy-pastable) command with its explanation as prose,
  // which wraps to the width - the cookbook pattern, not a term/desc table whose wide
  // command column squeezes the explanation to one word per line at narrow widths.
  for (const auto& [command, explanation] : kExamples) {
    examples.children.push_back(ExampleOf(std::string(command), "sh"));
    examples.children.push_back(ProseOf(explanation));
  }
  section.children.push_back(Content{.node = std::move(examples)});
  return section;
}

// IGNORE AND VCS: the independent filters that decide which paths enter the walk. The flags are
// pulled from the globals SOT via the "ignore" topic tag so the list cannot drift. Standalone as
// `--help=ignore` (aliases `ignores` / `vcs`) and folded into the full reference; the folded form
// omits entries already present in the Options section.
Section IgnoreSection(bool in_full) {
  Section section{.title = "Ignore and VCS traversal"};
  section.children.push_back(ProseOf(
      "Ignoring a path and pruning version-control metadata are separate decisions. Ignore rules "
      "filter ordinary paths by pattern; `--skip-vcs` prevents xff from entering administrative "
      "trees such as `.git` or `.hg` at all. Hidden-path filtering is a third independent switch. "
      "Changing one does not silently change the others."));

  static constexpr std::array<DocPair, 6> kAxes = {{
      {"--exclude / --include", "command-line gitignore-style patterns; repeatable, later matches win"},
      {"--gitignore / -g", "Git's `.gitignore`, `.git/info/exclude`, and `core.excludesFile` layer"},
      {"--ignore-files", "per-directory `.ignore` and `.xffignore` files"},
      {"--ignore-file=PATH", "an explicitly named rule file, rooted at its own directory"},
      {"--skip-vcs", "prune VCS metadata names; independent of pattern-based ignore files"},
      {"--hidden / --no-hidden", "show or skip dot-prefixed path components"},
  }};
  Subsection axes{.title = "Independent axes"};
  axes.children.push_back(RowsOf(kAxes));
  section.children.push_back(Content{.node = std::move(axes)});

  Subsection defaults{.title = "Defaults and overrides"};
  defaults.children.push_back(ProseOf(
      "The find and xff styles start with ignore files off and hidden paths visible. The rg style "
      "honours VCS, `.ignore`, and `.xffignore` files and skips hidden paths. Tree comparison does not "
      "change these defaults or enable an ignore source: both sides use the selected style and the "
      "ignore options the user supplies."));
  defaults.children.push_back(ProseOf(
      "Bare `-g` / `--gitignore` is automatic: it activates only inside a Git working tree. "
      "`-g+` / `--gitignore=on` forces the Git layer anywhere; `-g-` / `--gitignore=off` disables it. "
      "`--ignore-vcs` and `--no-ignore-vcs` are the rg-style spellings for that same layer. Within "
      "this family the last flag wins."));
  defaults.children.push_back(ProseOf(
      "When the Git ignore layer is active, xff also implicitly prunes `.git` as if "
      "`--skip-vcs=git` were present. An explicit `--skip-vcs[=LIST]` replaces that implicit choice; "
      "bare or `=all` selects every known VCS, while `--no-skip-vcs` / `=none` keeps metadata in the "
      "walk. A list such as `--skip-vcs=git,hg` is frozen to exactly those systems."));
  defaults.children.push_back(ProseOf(
      "`--no-ignore` / `-u` is the master off switch for ignore-file sources, including explicit "
      "`--ignore-file` inputs. It does not cancel command-line `--exclude` patterns or an explicit "
      "`--skip-vcs`. Use `--no-skip-vcs` separately when metadata directories must remain visible."));
  section.children.push_back(Content{.node = std::move(defaults)});

  Subsection precedence{.title = "Pattern precedence"};
  precedence.children.push_back(ProseOf(
      "For a path, command-line `--exclude` / `--include` patterns decide first, then explicit "
      "`--ignore-file` sources, then per-directory files. Within a source, gitignore last-match-wins "
      "semantics apply; a matched directory is pruned, so a later rule cannot recover descendants "
      "that were never visited."));
  section.children.push_back(Content{.node = std::move(precedence)});

  if (!in_full) {
    Subsection flags{.title = "Ignore and traversal flags"};
    for (const GlobalFlag& flag : Globals()) {
      if (flag.topic == "ignore") {
        flags.children.push_back(FlagEntry(flag));
      }
    }
    section.children.push_back(Content{.node = std::move(flags)});
  }

  static constexpr std::array<DocPair, 4> kExamples = {{
      {"xff -g . -name '*.cc'", "honour Git rules automatically and search the remaining tree"},
      {"xff --skip-vcs=git,hg .", "prune only Git and Mercurial metadata, without enabling ignore files"},
      {"xff -g --no-skip-vcs .", "honour Git rules while allowing nested `.git` metadata into the walk"},
      {"xff -u --skip-vcs .", "ignore no rule files, but still prune every known VCS metadata tree"},
  }};
  Subsection examples{.title = "Examples"};
  for (const auto& [command, explanation] : kExamples) {
    examples.children.push_back(ExampleOf(std::string(command), "sh"));
    examples.children.push_back(ProseOf(explanation));
  }
  section.children.push_back(Content{.node = std::move(examples)});
  return section;
}

// ARCHIVES: what it means to walk INTO a container, and the whole `--archive` family. The flags are
// pulled from the globals SOT via the "archive" topic tag so the list cannot drift; the identity /
// read-only / cost rules are prose, because they are what a reader needs before the flags mean
// anything. Standalone as `--help=archive` (see TopicReference) and folded into the full reference.
// `in_full` (the folded-in case) omits the per-flag entries, which the grouped Options section
// already carries.
Section ArchiveSection(bool in_full) {
  Section section{.title = "Archives"};
  section.children.push_back(ProseOf(
      "With `--archive`, an archive is a directory: xff opens it and walks its members as ordinary "
      "entries, so every predicate and action applies to them unchanged - `-name`, `-type`, "
      "`-grep`, `{hash}`, `--summary`. Nothing in the expression vocabulary knows about archives. "
      "Needs at least one container-reader extra; `--help=extras` says which readers this binary "
      "has."));

  static constexpr std::array<DocPair, 4> kModes = {{
      {"none", "an archive is one plain file (find's behaviour, and the find-style default)"},
      {"roots", "dive only when a search root IS an archive (the xff-family default)"},
      {"all", "dive archives met during the walk too (what a bare `--archive` selects)"},
      {"any", "`all`, and offer EVERY file to the reader rather than only container-looking names"},
  }};
  Subsection modes{.title = "How far diving goes"};
  modes.children.push_back(RowsOf(kModes));
  modes.children.push_back(ProseOf(
      "Two axes, spelled independently: the RUNG says how much to look at, the CASE of the short "
      "form says whether writing is armed. So a slipped shift key changes the capability, never the "
      "level - and arming is not doing: something still has to ask for a write, and `--safe` / "
      "`--dry-run` still apply."));
  modes.children.push_back(ProseOf(
      "Later wins, per axis, which is what makes the two useful together: `-Z++ -z-` arms writing "
      "with reading OFF. Permission alone performs no operation: with diving enabled it is consumed "
      "by member-mutating actions such as `-delete` or the exec family; with diving off it has no "
      "observable effect unless the run also names a creation sink such as `--pack`. Creating a new "
      "archive does not itself require member-write permission, because it does not mutate an "
      "existing member. `-Z-` is the full reset, turning reading off AND disarming writing whatever "
      "an earlier flag or a config file asked for. A lower-case form never disarms; only `-Z-` does."));
  modes.children.push_back(
      Content{
          .node = Example{
              .text = "                   read only    + write (--archive-write)\n"
                      "  none              -z-          -Z-  (also disarms writing)\n"
                      "  roots (default)   -z           -Z\n"
                      "  all               -z+          -Z+\n"
                      "  any               -z++         -Z++",
              .lang = "text"}});
  modes.children.push_back(ProseOf(
      "Under `all` a file is only opened when its NAME looks like a container, so walking a source "
      "tree does not read every file in it; `any` (also spelled `--archive-any`) drops that gate. "
      "Nesting has its own cap (`--archive-depth`, default 1) because a container inside a container "
      "is where a decompression bomb lives - and it is deliberately NOT part of any rung, since "
      "raising the bomb cap is a different decision from looking in more places. `-maxdepth` keeps "
      "counting member levels as ordinary depth."));
  section.children.push_back(Content{.node = std::move(modes)});

  Subsection identity{.title = "A member is an entry, a container is still a file"};
  identity.children.push_back(ProseOf(
      "A member's path is the container's, the separator, then the member: `a.tar!dir/two.txt` "
      "(`--archive-separator` / `--archive-prefix` spell it differently). The container keeps its own "
      "identity at the same time - it is a real `-type f` you can match and delete - so a dive shows "
      "you both, which is also why `--archive-aggregate` exists: a reduction that counted the "
      "container AND its members would describe no filesystem that exists."));
  identity.children.push_back(ProseOf(
      "Format-defined file-like parts use that same entry model. A native phar's executable stub is "
      "the readable regular entry `.phar/stub.php`, so it can be matched, searched, formatted, and "
      "included in `--summary` / `--histogram` like any other member. There is never a second entry "
      "with the same path: if the manifest stores `.phar/stub.php` explicitly, that stored member "
      "wins over the synthetic view. Incidental metadata that the format does not model as a file is "
      "not invented as one. A future visibility option would control presentation of these parts; it "
      "must not create duplicate path identities."));
  identity.children.push_back(ProseOf(
      "An Electron `.asar` bundle follows the same rule: packed files, external "
      "`.asar.unpacked` files, directories, and links are entries, while integrity records are "
      "metadata and never appear as synthetic files. The ASAR reader is read-only and verifies "
      "declared SHA256 whole-file and block hashes when content is read."));
  identity.children.push_back(ProseOf(
      "A SquashFS image is another read-only virtual filesystem. The independent SquashFS extra "
      "covers raw images, Snap packages, and the embedded filesystem in a type-2 AppImage without "
      "mounting it; indexed metadata, links, and member contents use the ordinary expression vocabulary."));
  identity.children.push_back(ProseOf(
      "Members are READ-ONLY by default. `-delete` and the exec family refuse one rather than "
      "silently doing nothing, because a member has no path a process can open and no way to be "
      "unlinked; `--archive-extract` runs the child over a temporary copy, and `--archive-delete` "
      "rewrites the container without the member. Both are opt-in, and both say so in the refusal "
      "you get without them."));
  section.children.push_back(Content{.node = std::move(identity)});

  const std::vector<archive::ReadFormatInfo> read_formats = archive::ContainerReadFormats();
  Subsection formats{.title = "Formats this binary understands"};
  if (read_formats.empty()) {
    // The extras convention: a lean binary still documents the surface and says what is absent,
    // rather than silently dropping the subsection (the rest of this topic does the same).
    formats.children.push_back(ProseOf(
        absl::StrCat(
            "NOT built into this binary: rebuild with `--config=xff_full` or enable a reader such as `",
            ExtraBuildFlag("archive"),
            "`, `--//xff:xff_asar`, or `--//xff:xff_squashfs`. A build with reader extras lists every readable "
            "format, its extensions, and "
            "whether `--pack` can write it, in a table here.")));
    section.children.push_back(Content{.node = std::move(formats)});
  } else {
    formats.children.push_back(ProseOf(
        "Reading is decided by CONTENT (the reader sniffs the bytes), so the extensions are what "
        "the name gate dives on under `all` and how the format is usually spelled - a container "
        "with an unlisted name still reads under `any`. Package extensions ride their underlying "
        "format: a `.jar` is a zip, a `.deb` an ar, an `.rpm` a cpio, `.crate` and `.gem` are "
        "tars, and `file` is a compressed SINGLE file (`notes.txt.gz`, one member). Write means "
        "`--pack` can create it."));
    Table table{.header = {"format", "read", "write", "extensions"}};
    table.cells.reserve(read_formats.size());
    for (const archive::ReadFormatInfo& format : read_formats) {
      // A format is writable when any of its suffixes names a registered pack format; the writer's
      // own suffix rule (longest dotted match, case folded) decides, so the two cannot disagree.
      const bool writable = absl::c_any_of(format.suffixes, [](const std::string& suffix) {
        return !archive::ContainerPackFormatFor(absl::StrCat("x", suffix)).empty();
      });
      table.cells.push_back({format.name, "yes", writable ? "yes" : "no", absl::StrJoin(format.suffixes, ", ")});
    }
    formats.children.push_back(Content{.node = std::move(table)});
    section.children.push_back(Content{.node = std::move(formats)});
  }

  Subsection creating{.title = "Creating one"};
  creating.children.push_back(ProseOf(
      "`--pack=FILE` turns the walk around: every match is written into a NEW archive instead of "
      "being listed, so the member list is an expression rather than a pipeline into `tar`. The "
      "output name picks the format, each member keeps the path it had relative to its search root, "
      "and `--sort` decides the order inside. It is a sink like `--summary`, the archive appears only "
      "when the walk finished, and a member of another container is refused - harvesting files out of "
      "one archive to re-pack them into another is a separate feature, which is also what `-Z++ -z-` "
      "is reserved for."));
  // Same rule as the option table below: the formats come from the LINKED writer, so a format added
  // to the extra cannot leave this list behind.
  const std::vector<std::string> pack_formats = archive::ContainerPackFormats();
  if (!pack_formats.empty()) {
    // Rendered WITH the leading dot: these are the filename suffixes a user types after `--pack=`,
    // and the dotless form is the one spelling nobody writes.
    creating.children.push_back(ProseOf(
        absl::StrCat("Output filename suffixes this binary writes: `.", absl::StrJoin(pack_formats, "`, `."), "`.")));
  }
  // The vocabulary comes from the LINKED writer, never from a list kept here: a lean binary then
  // simply has no table (it has no packer either), and adding an option to the extra cannot leave the
  // documentation behind.
  const std::vector<archive::PackOptionInfo> pack_options = archive::ContainerPackVocabulary();
  if (!pack_options.empty()) {
    creating.children.push_back(ProseOf(
        "`--pack-option=NAME=VALUE|@FILE.json` (repeatable, last value for a NAME wins) tunes the writer. The "
        "names are xff's own and are translated for whichever library does the writing, so an unknown "
        "one is a usage error and this list is exactly what THIS binary accepts:"));
    creating.children.push_back(ProseOf(
        "The `@FILE.json` form reads one JSON object. Its keys are the option names below; values may "
        "be strings, integers, or booleans, with booleans translated to `yes` or `no`. A file is "
        "expanded where it occurs among repeated options, so later file or inline values override "
        "earlier ones uniformly."));
    Rows rows;
    rows.rows.reserve(pack_options.size());
    for (const archive::PackOptionInfo& option : pack_options) {
      rows.rows.push_back(
          Row{.term = absl::StrCat(option.name, "=", option.value_syntax),
              .description =
                  ParseInline(absl::StrCat(option.detail, " (`", absl::StrJoin(option.formats, "`, `"), "`)"))});
    }
    creating.children.push_back(Content{.node = std::move(rows)});
  }
  creating.children.push_back(ProseOf(
      "PHP phars are the exception: xff reads them and can rewrite one to remove members, but it does "
      "not CREATE one, because a phar is a PHP program with a stub, a manifest and a signature rather "
      "than a container of files. Build one with `box` (box-project/box) or PHP's own `Phar` class, "
      "and verify or install one with `phive` (phar-io/phive), which checks the signature xff will "
      "not forge."));
  section.children.push_back(Content{.node = std::move(creating)});

  for (const GlobalFlag& flag : Globals()) {
    if (!in_full && flag.topic == "archive") {
      section.children.push_back(FlagEntry(flag));
    }
  }

  static constexpr std::array<DocPair, 6> kExamples = {{
      {"xff --archive=roots a.tar", "list the archive and its members"},
      {"xff -z+ . -grep TODO", "search inside every archive met in the tree"},
      {"xff --archive=roots a.tgz --summary", "count what is INSIDE, not the compressed container"},
      {"xff --archive=roots --archive-extract a.tar -name '*.json' -exec jq . {} \\;",
       "run a tool over a member, via a temporary copy"},
      {"xff --archive=roots --archive-delete a.tar -name '*.bak' -delete", "rewrite the archive without those members"},
      {"xff . -name '*.cc' -newer VERSION --pack=changed.tar.gz",
       "pack what the expression matched into a new archive"},
  }};
  Subsection examples{.title = "Examples"};
  for (const auto& [command, explanation] : kExamples) {
    examples.children.push_back(ExampleOf(std::string(command), "sh"));
    examples.children.push_back(ProseOf(explanation));
  }
  section.children.push_back(Content{.node = std::move(examples)});
  return section;
}

// CONTENT: what it means to read INSIDE files, and the whole content-matching family. The primaries
// are pulled from the registry SOT via Descriptor.topic and the flags via GlobalFlag.topic, so the
// lists cannot drift; the cross-cutting rules are prose, because they are what a reader needs before
// the family means anything. Standalone as `--help=content` and folded into the full reference.
Section ContentSection(bool in_full) {
  Section section{.title = "Content"};
  section.children.push_back(ProseOf(
      "These primaries read the entry's BYTES, not its metadata: `-grep` prints matching lines the "
      "way ripgrep does, `-content` / `-icontent` test for a literal, `-rxc` / `-irxc` for a regex "
      "(grammar per `--regextype`, see `--help=grammars`), and `-text` / `-eofcr` / `-eofcrlf` "
      "classify line endings and completeness. `{lines}`, `{text}`, `{line}`, `{match}` and "
      "`{column}` carry the results into templates (`--help=fields`)."));
  section.children.push_back(ProseOf(
      "Every one of them reads through the entry's OWN filesystem, so under `--archive` a member is "
      "searched inside its container exactly like a plain file - `a.tar!notes.txt` greps without "
      "unpacking anything. Reading is per entry and streamed, so a match in a huge tree costs the "
      "bytes of the files visited, not of the tree."));
  for (const registry::Descriptor& descriptor : registry::All()) {
    if (!in_full && descriptor.topic == "content") {
      section.children.push_back(PrimaryEntry(descriptor));
    }
  }
  for (const GlobalFlag& flag : Globals()) {
    if (!in_full && flag.topic == "content") {
      section.children.push_back(FlagEntry(flag));
    }
  }
  static constexpr std::array<DocPair, 3> kExamples = {{
      {"xff src -name '*.cc' -grep 'TODO\\('", "matching lines, rg-style, from the files an expression picked"},
      {"xff . -type f ! -text", "the files that are NOT line-oriented text"},
      {"xff -z logs.tar -grep ERROR --count", "per-member match counts inside an archive"},
  }};
  Subsection examples{.title = "Examples"};
  for (const auto& [command, explanation] : kExamples) {
    examples.children.push_back(ExampleOf(std::string(command), "sh"));
    examples.children.push_back(ProseOf(explanation));
  }
  section.children.push_back(Content{.node = std::move(examples)});
  return section;
}

// OUTPUT: how the implicit listing relates to expression actions and what each renderer guarantees.
Section OutputSection(bool in_full) {
  Section section{.title = "Output"};
  section.children.push_back(ProseOf(
      "When the expression contains no action, xff implicitly lists every match. An explicit action such as "
      "`-print`, `-grep`, or `-exec` suppresses that default listing unless `--implicit-print=yes` restores it. "
      "`--summary`, `--histogram`, and `--pack` are terminal sinks: they replace the implicit per-match listing "
      "but do not suppress actions written in the expression."));

  static constexpr std::array<DocPair, 8> kFormats = {{
      {"plain", "one path plus newline; streaming; use `--path-encoding=escape` for visible control bytes"},
      {"nul", "raw paths separated by NUL; streaming and safe for arbitrary path bytes"},
      {"jsonl", "one JSON object per record; the default listing uses a `path` member"},
      {"csv", "RFC 4180 rows; streaming; header and `--columns` supported"},
      {"tsv", "tab-separated rows with tabs, newlines, carriage returns, and backslashes escaped"},
      {"aligned", "human-readable columns; buffers rows to determine display widths"},
      {"markdown", "a GitHub-flavored Markdown table; also spelled `md`; buffered"},
      {"tree", "an indented path hierarchy; buffered; Unicode or ASCII connectors per `--unicode`"},
  }};
  Subsection formats{.title = "Formats"};
  formats.children.push_back(RowsOf(kFormats));
  formats.children.push_back(ProseOf(
      "`--columns=FIELD,...` applies only to `csv`, `tsv`, `aligned`, and `markdown`; the default column is "
      "`path`. Those formats include a header unless `--no-header` is set. `--template` is the free-form "
      "alternative for a custom per-entry record; see `--help=fields` for placeholders."));
  section.children.push_back(Content{.node = std::move(formats)});

  Subsection safety{.title = "Filename safety"};
  safety.children.push_back(ProseOf(
      "A Unix path may contain tabs and newlines. Plain output preserves those bytes by default, so it is for "
      "people rather than reliable parsing. Prefer `--format=nul` for shell pipelines, `jsonl` for structured "
      "consumers, or a tabular format whose escaping rules are defined above. `--path-encoding=escape` makes "
      "control bytes visible in plain output but is not a reversible record separator."));
  section.children.push_back(Content{.node = std::move(safety)});

  if (!in_full) {
    Subsection flags{.title = "Output options"};
    for (const GlobalFlag& flag : Globals()) {
      if (flag.topic == "output") {
        flags.children.push_back(FlagEntry(flag));
      }
    }
    section.children.push_back(Content{.node = std::move(flags)});
  }

  Subsection examples{.title = "Examples"};
  examples.children.push_back(ExampleOf("xff . -type f --format=nul | xargs -0 sha256sum --", "sh"));
  examples.children.push_back(ProseOf("pass arbitrary filenames safely to another command"));
  examples.children.push_back(ExampleOf("xff . -type f --format=csv --columns=path,size,mtime", "sh"));
  examples.children.push_back(ProseOf("stream selected fields as CSV with a header row"));
  examples.children.push_back(ExampleOf("xff . -type f --template='{size:human}  {path}'", "sh"));
  examples.children.push_back(ProseOf("render a custom human-readable record for each match"));
  section.children.push_back(Content{.node = std::move(examples)});
  return section;
}

// COMPARE: the two-root mode needs a mental model beyond the individual flag entries, especially
// because it deliberately inherits traversal/filtering without enabling any of it implicitly.
Section CompareSection(bool in_full) {
  Section section{.title = "Comparing trees"};
  section.children.push_back(ProseOf(
      "`--compare` requires exactly two roots. xff walks both roots with the same expression and the "
      "same explicitly selected traversal, ignore, archive, hidden-file, and symlink options. Comparison "
      "does not enable `--gitignore`, `--no-ignore`, `-L`, or any other policy on its own. The matched entries "
      "are paired by path relative to their respective roots, so the root directory names need not match."));
  section.children.push_back(ProseOf(
      "Regular files are equal when their bytes are equal, for both text and binary data. Unfollowed symlinks "
      "are equal when their link targets are equal; `-H` / `-L` instead apply their ordinary traversal meaning. "
      "Directories and other non-regular entries are compared by type. Metadata such as permissions, owner, "
      "timestamps, and inode numbers is not compared."));
  section.children.push_back(ProseOf(
      "The two walks run concurrently and each retains its complete matched-entry inventory until both finish. "
      "xff then merges those inventories and emits comparison records in bytewise relative-path order. This final "
      "comparison order is fixed: `--sort` controls traversal and the timing of expression actions within each side, "
      "not the order of status records or patch entries. Explicit expression actions execute independently for both "
      "walks; their synchronized output may interleave. `--buffer` still controls the output and collection buffers "
      "documented for those features, but it does not cap the comparison inventories, whose memory use grows with "
      "the total number of matched entries."));

  static constexpr std::array<DocPair, 4> kStatuses = {{
      {"left-only", "the relative path matched only below the left root"},
      {"right-only", "the relative path matched only below the right root"},
      {"different", "both sides matched the path, but its type, bytes, or symlink target differs"},
      {"identical", "both sides matched and compare equal; omitted unless explicitly selected"},
  }};
  Subsection statuses{.title = "Status output"};
  statuses.children.push_back(ProseOf(
      "Bare `--compare` (or `--compare=status`) writes tab-separated `STATUS` and relative-path records. "
      "The default selection reports discrepancies only; `--compare-select=all` also includes equal entries. "
      "`--path-encoding=escape` makes control bytes in the path unambiguous."));
  statuses.children.push_back(RowsOf(kStatuses));
  section.children.push_back(Content{.node = std::move(statuses)});

  Subsection patch{.title = "Patch output"};
  patch.children.push_back(ProseOf(
      "`--compare=diff` writes one unified patch for added, removed, and changed regular files. Text files get "
      "hunks; binary and non-regular differences get a diagnostic line. `--diff-algorithm` and `--diff-context` "
      "tune this mode. `identical` is rejected because an unchanged entry has no patch representation."));
  section.children.push_back(Content{.node = std::move(patch)});

  if (!in_full) {
    Subsection flags{.title = "Comparison options"};
    for (const GlobalFlag& flag : Globals()) {
      if (flag.topic == "compare") {
        flags.children.push_back(FlagEntry(flag));
      }
    }
    section.children.push_back(Content{.node = std::move(flags)});
  }

  Subsection examples{.title = "Examples"};
  examples.children.push_back(ExampleOf("xff --compare left-tree right-tree", "sh"));
  examples.children.push_back(ProseOf("print only paths present on one side or different on both sides"));
  examples.children.push_back(ExampleOf("xff --compare --compare-select=all left-tree right-tree", "sh"));
  examples.children.push_back(ProseOf("include `identical` records in the status stream"));
  examples.children.push_back(ExampleOf("xff -g+ --compare=diff old-tree new-tree >changes.patch", "sh"));
  examples.children.push_back(ProseOf("apply Git ignore rules to both walks and create a patch"));
  examples.children.push_back(ExampleOf("xff -L --compare left-tree right-tree -type f -name '*.dat'", "sh"));
  examples.children.push_back(ProseOf("follow symlinks and compare only matching regular data files"));
  section.children.push_back(Content{.node = std::move(examples)});
  return section;
}

// CONFIGURATION: how options resolve (layered tiers + the command line), how a style is
// chosen (--config / argv[0]), and how dangerous --xffrc directives are armed. The flags
// are pulled from the globals SOT via the "config" topic tag so the list cannot drift;
// the layering / argv[0] / arming rules are prose. Standalone as `--help=config` (see
// TopicReference) and folded into the full reference. `in_full` (the folded-in case) drops
// the per-flag "Config flags" subsection: the full reference's grouped Options section
// already documents each flag, so the layering / style / arming prose is all that adds value.
Section ConfigSection(bool in_full) {
  Section section{.title = "Configuration"};
  section.children.push_back(ProseOf(
      "xff configuration. Options resolve from layered config tiers, then the command line; later "
      "layers win. A style (`find` / `xff` / `rg`) sets the baseline defaults, which the tiers and the "
      "command line then adjust. Run `--explain` to print exactly what resolved."));

  static constexpr std::array<DocPair, 4> kLayers = {{
      {"system config", "machine-wide defaults (plus root-owned global controls that can prohibit arming)"},
      {"user config", "your personal defaults"},
      {"--xffrc=FILE", "an explicitly named file (repeatable) - a NON-ARMING tier"},
      {"command line", "flags and `--config`, highest"},
  }};
  Subsection layers{.title = "Layers (lowest to highest precedence)"};
  layers.children.push_back(RowsOf(kLayers));
  layers.children.push_back(ProseOf(
      "There is no project or ancestor `.xffrc` discovery: config comes from the system and user files "
      "plus any `--xffrc` you name. `--no-config` suppresses the automatic system and user tiers when their "
      "trusted permission directives allow it; those files may still be inspected for policy. An explicit "
      "command-line `--xffrc` remains active."));
  layers.children.push_back(ProseOf(
      "Permission flags (allow and require flags) are config-only directives, not command-line options. "
      "Require flags precede the first section. `--no-require-*` makes a file "
      "optional; "
      "`--require-*` prevents skipping an existing file, without requiring a missing file to exist. The system file "
      "may set one of "
      "`--no-require-system-config` / `--require-system-config` once and one of "
      "`--no-require-user-config` / `--require-user-config` once. Permissions govern which files may be skipped: "
      "`--no-config` requests both skips and fails if either existing file refuses. Missing files need no "
      "permission; an existing file without a grant cannot be skipped. The user file may set its user-control "
      "pair once before the first section. A "
      "system "
      "user-control decision is authoritative over the user file. Explicit `--xffrc` files may not contain any "
      "of these controls. Separately, `--allow-xffrc` / `--no-allow-xffrc` is a normal config-only setting usable "
      "in system defaults or any user config block; user selection and precedence decide whether command-line "
      "`--xffrc=FILE` is accepted, and an unsectioned system denial cannot be overridden. Admission is checked before "
      "explicit paths are opened."));
  layers.children.push_back(ProseOf(
      "System, user, and explicit `.xffrc` files share one INI grammar. Write unconditional options with their exact "
      "command-line spelling before the first "
      "section, and named configurations as plain `[NAME]` sections. For example, write `--color=auto`, then "
      "`[dev]` and `-E`; do not remove the option dashes or add a `config` section prefix. Global lines are "
      "validated independently and an invalid one is diagnosed and ignored. Named sections are atomic: one "
      "invalid line disables the entire section. A name may be declared only once per file, including empty "
      "sections; duplicate declarations disable that name. Other files may refine the same name. Disablement "
      "propagates through `--config=NAME` references, and "
      "selecting a disabled section is a usage error. Thus `-E development` disables its section because `-E` "
      "takes no value and `development` is an unexpected token."));
  layers.children.push_back(ProseOf(
      "A line may contain multiple directives: `--hidden --color=never` or `-name foo -name bar`. Adjacent "
      "predicates mean AND; use `-name foo -o -name bar` for either name. Config predicates and actions form "
      "an expression that is ANDed as a group with the CLI expression. Each primary consumes only its own "
      "arguments. Closed choices are validated by the shared CLI parser: `-type garbage` is invalid, while "
      "`-type f,d` is a valid any-of list."));
  section.children.push_back(Content{.node = std::move(layers)});

  static constexpr std::array<DocPair, 7> kConfigControls = {{
      {"--no-require-system-config",
       "permits skipping the system file with `--no-system-config` or `--no-config`; system config only, before the "
       "first section, and at most one "
       "of this pair"},
      {"--require-system-config",
       "forbids skipping the system file, including with `--no-config`; system config only, before the first section, "
       "and at most one of "
       "this pair"},
      {"--no-require-user-config",
       "permits skipping the user file with `--no-user-config` or `--no-config`; before the first section in the "
       "system or user config, and at "
       "most one of this pair per file; the system decision is authoritative"},
      {"--require-user-config",
       "forbids skipping the user file, including with `--no-config`; before the first section in the system or user "
       "config, and at most "
       "one of this pair per file; the system decision is authoritative"},
      {"--allow-xffrc",
       "allows command-line `--xffrc=FILE`; usable in system defaults or any user config block, with normal config "
       "selection and last-value precedence"},
      {"--no-allow-xffrc",
       "denies command-line `--xffrc=FILE`; usable in system defaults or any user config block, with normal config "
       "selection and last-value precedence; an unsectioned system denial is authoritative"},
      {"--no-allow-exec",
       "prohibits dangerous directives in user and explicit configs, even with CLI arming; system-only, before "
       "the first section; remains authoritative when system defaults are suppressed"},
  }};
  Subsection controls{.title = "Config-only controls"};
  controls.children.push_back(ProseOf(
      "These directives are accepted only inside the stated automatic config files, not on the command line or in "
      "an explicitly loaded `--xffrc` file. An allow/deny pair is one setting: where a pair is limited to one "
      "occurrence, its positive and negative forms may not both appear."));
  controls.children.push_back(RowsOf(kConfigControls));
  section.children.push_back(Content{.node = std::move(controls)});

  section.children.push_back(ProseOf(
      "Explicit `.xffrc` files cannot contain the require/no-require pairs, `--allow-xffrc`, "
      "`--no-allow-xffrc`, or the system-only `--no-allow-exec`. The separate runtime flag `--allow-exec` "
      "is accepted on the CLI and in config files, but an explicit file's own setting never arms its "
      "dangerous directives."));

  Subsection examples{.title = "Tiny config examples"};
  examples.children.push_back(ProseOf(
      "Defaults and a named override in `/etc/xff.ini`: `xff . --config=dev` ends with `--color=never`; "
      "adding `--color=always` after the selector overrides it."));
  examples.children.push_back(ExampleOf("--color=auto\n[dev]\n--color=never", "ini"));
  examples.children.push_back(ProseOf(
      "Refine `[dev]` across files: the system file supplies `--color=auto`, the user file supplies "
      "`--color=always`, and `task.xffrc` supplies `--color=never`. "
      "`xff . --config=dev --xffrc=task.xffrc` ends with `--color=never`. Each file declares `[dev]` only once."));
  examples.children.push_back(
      ExampleOf("# user config\n[dev]\n--color=always\n--config=checks\n[checks]\n-type f", "ini"));
  examples.children.push_back(ExampleOf("# task.xffrc\n[dev]\n--color=never", "ini"));
  examples.children.push_back(ProseOf(
      "`--config=checks` composes configurations; repeated references are allowed and each contributing line "
      "applies at most once. Later-file refinements wait for the earlier file's section to finish. "
      "Section names are literal: `[common]` is named, and `[xff:debug]` matches only "
      "`--config=xff:debug`. Unconditional flags go before the first section."));
  examples.children.push_back(ProseOf(
      "An explicit `task.xffrc` uses exactly the same INI format. Its unsectioned flags apply when loaded; "
      "named flags apply only when that configuration is selected:"));
  examples.children.push_back(ExampleOf("--hidden\n\n[quiet]\n--color=never", "ini"));
  examples.children.push_back(ProseOf(
      "`xff . --xffrc=task.xffrc` applies `--hidden`; adding `--config=quiet` also applies `--color=never`. "
      "The invocation name or another config's `--config=quiet` can also select the section. A selector "
      "alone does not discover files. Unsectioned `--config=NAME` directives can select sections when "
      "the file loads. There is no automatic `.xffrc` discovery."));
  examples.children.push_back(ProseOf(
      "Permit only the user-file skip: these unsectioned system controls allow `--no-user-config`, reject "
      "`--no-config` and `--no-system-config`, and override the user file's own skip permission."));
  examples.children.push_back(ExampleOf("--require-system-config\n--no-require-user-config", "ini"));
  examples.children.push_back(ProseOf(
      "`xff . --no-user-config` is allowed. `--no-system-config` and `--no-config` are rejected. "
      "To permit both individual skips and `--no-config`, grant both file permissions in the system file:"));
  examples.children.push_back(ExampleOf("--no-require-system-config\n--no-require-user-config", "ini"));
  examples.children.push_back(ProseOf(
      "To prevent dangerous directives from lower-trust configs, put `--no-allow-exec` before every section "
      "in an administrator-owned `/etc/xff.ini` that those users cannot modify. User and explicit-file "
      "dangerous lines are dropped even with CLI `--allow-exec`, and the prohibition survives suppression of "
      "system defaults. Add `--no-allow-xffrc` to reject explicit files altogether:"));
  examples.children.push_back(ExampleOf("--no-allow-exec\n--no-allow-xffrc", "ini"));
  examples.children.push_back(ProseOf(
      "Neither user `--allow-xffrc` nor an explicit file's own `--allow-exec` can undo those prohibitions. "
      "These controls constrain config-file capabilities, not actions typed directly on the CLI. Runtime "
      "`--safe`, `--dry-run`, and action-specific confirmation remain separate. Without a system prohibition, "
      "`--allow-exec` from the CLI or an applying automatic config permits dangerous explicit-file lines "
      "through the config gate; the explicit file cannot arm itself."));
  section.children.push_back(Content{.node = std::move(examples)});

  Subsection style{.title = "Choosing a style"};
  style.children.push_back(ProseOf(
      "Every `--config=NAME` remains active, so multiple named blocks can apply. Among built-in style selectors, "
      "the last `find`, `xff`, or `rg` selects the baseline; custom names do not change it. See `--help=styles` for "
      "the table. The invocation name (`argv[0]`) is the leading selector, so a symlink named `find` selects the "
      "find expression style and `rg` the rg style; any other name (e.g. a `mytool` symlink) activates a same-named "
      "config block "
      "over the xff default. Explicit `--config` selectors stack on top."));
  style.children.push_back(ProseOf(
      "Configuration expands in application order. Automatic system/user defaults and the invocation selector "
      "come first; each command-line `--config` then activates newly matching lines at that exact position, and "
      "each `--xffrc` loads its currently matching lines where it appears. Because conflicting options usually "
      "use the last value, moving a selector can intentionally change the result. A config line is applied at "
      "most once."));
  style.children.push_back(ProseOf(
      "Within one logical config section, repeating an overriding setting keeps the normal last-value-wins "
      "result but emits a warning: the earlier value is locally redundant and is usually a copy/paste mistake. "
      "Options that deliberately accumulate (such as `--exclude`, `--summary`, and `--define`) may repeat "
      "without warning, as may expression primaries. Separate config sections and tiers remain independent. "
      "Command-line repetition is never warned about, so a pasted command can be adjusted by appending an "
      "override."));
  section.children.push_back(Content{.node = std::move(style)});

  Subsection arming{.title = "Arming dangerous directives"};
  arming.children.push_back(ProseOf(
      "A dangerous directive (the exec family `-exec` / `-execdir` / `-ok` / `-capture`, or `-delete`) "
      "carried by an `--xffrc` file is inert unless `--allow-exec` is set from a trusted tier (the command "
      "line or the system/user config, never an `--xffrc` file itself). Unarmed lines are dropped with a "
      "warning; the unsectioned system `--no-allow-exec` prohibition overrides even CLI arming."));
  section.children.push_back(Content{.node = std::move(arming)});

  if (!in_full) {
    Subsection flags{.title = "Config flags"};
    for (const GlobalFlag& flag : Globals()) {
      if (flag.topic == "config") {
        flags.children.push_back(FlagEntry(flag));
      }
    }
    section.children.push_back(Content{.node = std::move(flags)});
  }
  return section;
}

Content NoticeEntry(const license::Notice& notice) {
  return Content{
      .node = Entry{
          .term = absl::StrCat(notice.component, "  [", notice.spdx, "]"),
          .summary = ParseInline(notice.text),
      }};
}

// The `--help=notice` topic (alias notices): the one build-dependent line (which extras THIS
// binary contains, via ExtraEnabled), then the component manifest. Unlike NoticeText(), which is
// the byte-stable repository/release artifact, this is structured help content so --width wraps
// every prose and notice body. Section changes visibly separate the main binary from each extra.
Section NoticeSection() {
  Section section;  // title-less: no heading, body at column 0
  section.children.push_back(ProseOf(
      absl::StrCat(
          "Build extras compiled into this binary: ",
          EnabledExtras().empty() ? "none (lean build)" : absl::StrJoin(EnabledExtras(), ", "))));
  section.children.push_back(ProseOf(license::CopyrightNotice()));
  section.children.push_back(ProseOf(license::NoticeIntroduction()));

  std::optional<Subsection> extension;
  for (const license::Notice& notice : license::Notices()) {
    if (notice.section.empty()) {
      section.children.push_back(NoticeEntry(notice));
      continue;
    }
    if (!extension.has_value() || extension->anchor != notice.section) {
      if (extension.has_value()) {
        section.children.push_back(Content{.node = std::move(*extension)});
      }
      extension = Subsection{
          .title = absl::StrCat("Build extension: ", notice.section),
          .anchor = std::string(notice.section),
      };
    }
    extension->children.push_back(NoticeEntry(notice));
  }
  if (extension.has_value()) {
    section.children.push_back(Content{.node = std::move(*extension)});
  }
  return section;
}

// The `--help=license` topic (alias licenses): xff's own license (Apache-2.0) in full, led by
// the copyright + grant statement (task #142). Legal text renders verbatim and must not reflow,
// so it is a title-less section holding a single Example (column 0, byte-exact, unwrapped).
Section LicenseSection() {
  Section section;  // title-less: the copyright leads, then the license body, verbatim
  section.children.push_back(
      Content{.node = Example{.text = absl::StrCat(license::CopyrightNotice(), license::LicenseText())}});
  return section;
}

// The COMPONENT in `license=COMPONENT` (or the `licenses=` alias), or nothing when `name` is an
// ordinary topic. Split once on the FIRST `=`, so a component name may itself contain one.
std::optional<std::string_view> LicenseComponentOf(std::string_view name) {
  const std::size_t eq = name.find('=');
  if (eq == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view head = name.substr(0, eq);
  if (head != "license" && head != "licenses") {
    return std::nullopt;
  }
  return name.substr(eq + 1);
}

// One component's license page: its notice line, then the full text of the license it names.
// Nothing when no such component is linked, so the caller can report the name as unknown.
std::optional<Section> LicenseComponentSection(std::string_view component) {
  const std::vector<license::Notice> notices = license::Notices();
  // Case-insensitive: component names are proper nouns with capitals, slashes and parentheses
  // ("RE2", "Abseil (C++)", "mboworks/mbo"), and requiring the exact casing to read a LICENSE would
  // be a spelling test rather than a lookup.
  const auto found = absl::c_find_if(notices, [component](const license::Notice& notice) {
    return absl::EqualsIgnoreCase(notice.component, component);
  });
  if (found == notices.end()) {
    return std::nullopt;
  }
  // #142's rule: every license page leads with xff's own copyright and grant, then the component's.
  std::string text =
      absl::StrCat(license::CopyrightNotice(), "\n", found->component, "  [", found->spdx, "]\n", found->text, "\n");
  const std::string_view body = license::LicenseBodyFor(found->spdx);
  if (body.empty()) {
    // Honest rather than silent: the license applies, this binary just does not carry its words.
    absl::StrAppend(
        &text, "\nThe full ", found->spdx,
        " text is not embedded in this binary. The notice above is the retained attribution;\n"
        "see the component's own distribution for the license text.\n");
  } else {
    absl::StrAppend(&text, "\n", body);
  }
  Section section;  // title-less, like the other license pages: the text is the whole page
  section.children.push_back(Content{.node = Example{.text = text}});
  return section;
}

// EXAMPLES: the cookbook recipes as structured nodes - each a subsection with the
// verbatim command (an Example, kept copy-pastable) and its explanation (Prose, which
// wraps). The recipe list is the SOT in help.cc, run end to end by cookbook_test.
Section BuildExamples() {
  Section section{.title = "Examples"};
  section.children.push_back(ProseOf(
      "Worked examples that compose xff's building blocks. Each shows a task, its command, and how it "
      "works. See `--help=fields` for the {field}s and `--help=stats` for the reductions."));
  for (const Recipe& recipe : CookbookRecipes()) {
    Subsection sub{.title = std::string(recipe.task)};
    sub.children.push_back(ExampleOf(std::string(recipe.command), "sh"));
    sub.children.push_back(ProseOf(recipe.note));
    section.children.push_back(Content{.node = std::move(sub)});
  }
  return section;
}

// DESCRIPTION: the two orientation paragraphs shared by the full reference and the
// usage page.
Section DescriptionSection() {
  Section description{.title = "Description"};
  description.children.push_back(ProseOf(
      "xff walks each starting path and acts on the entries matching an expression, like `find`(1). "
      "With no path it searches the current directory; with no action it prints each match. "
      "`xff --compare LEFT RIGHT` instead compares two directory trees as selected status records or a patch."));
  description.children.push_back(ProseOf(
      "xff has two flavors selected by the program name: invoked as `find` it restricts the expression "
      "to find-compatible primaries, operators, and values; invoked as `xff` it enables the modern "
      "extensions. Whole-run xff globals remain available as explicit controls in either flavor. An "
      "explicit `--config=find|xff` overrides the program name. Items marked as xff extensions below "
      "are the additions over find."));
  return description;
}

// COMMAND STRUCTURE: the grammar rules a reader needs before the option and primary catalogues.
// This belongs in the full reference rather than being repeated in individual entries.
Section CommandStructureSection() {
  Section section{.title = "Command structure"};
  section.children.push_back(ProseOf(
      "A command consists of whole-run options, zero or more starting paths, and an optional expression. "
      "Starting paths come before the expression; the first expression primary begins with a single dash."));

  Bullets rules;
  rules.items.push_back(ParseInline(
      "Whole-run `--long` options are position-independent, so `--summary=ext` may appear before the paths or "
      "after the expression. They remain literal arguments inside an argument-taking primary such as `-exec` "
      "or `-printf`."));
  rules.items.push_back(ParseInline(
      "The compatibility globals `-H`, `-L`, `-P`, `-g`, `-j`, and `-z` are leading-only because a single-dash "
      "word can otherwise be an expression primary."));
  rules.items.push_back(ParseInline(
      "Adjacent tests and actions have an implicit `-a` (AND). Use `!` for NOT, `-o` for OR, and shell-quoted "
      "or escaped `(` and `)` for grouping. Evaluation is left to right and short-circuits."));
  rules.items.push_back(ParseInline(
      "With no starting path, xff uses `.`. With no explicit action, it prints each matching entry. A bare `--` "
      "ends option parsing so a path beginning with `-` can be named unambiguously."));
  section.children.push_back(Content{.node = std::move(rules)});

  Subsection examples{.title = "Basic examples"};
  examples.children.push_back(ExampleOf("xff src -type f -name '*.cc'", "sh"));
  examples.children.push_back(ProseOf("find regular C++ source files below `src`"));
  examples.children.push_back(ExampleOf("xff . \\( -name '*.cc' -o -name '*.h' \\) -print", "sh"));
  examples.children.push_back(ProseOf("group alternatives explicitly; the shell escapes keep the parentheses for xff"));
  examples.children.push_back(ExampleOf("xff --compare left-tree right-tree", "sh"));
  examples.children.push_back(ProseOf("compare two trees and print only discrepancies"));
  section.children.push_back(Content{.node = std::move(examples)});
  return section;
}

// OPTIONS: the whole-run flags grouped by header. `with_details` false yields the terse
// usage-page form (summary + tags only). `audience` decides whether a flag whose build extra is
// missing carries the per-binary "NOT built into this binary" note.
Section OptionsSection(bool with_details, Audience audience = Audience::kThisBinary) {
  Section options{.title = "Options"};
  std::string_view group;
  Subsection current;
  bool have_current = false;
  for (const GlobalFlag& flag : Globals()) {
    if (flag.group != group) {
      if (have_current) {
        options.children.push_back(Content{.node = std::move(current)});
      }
      group = flag.group;
      current = Subsection{.title = std::string(flag.header)};
      have_current = true;
    }
    current.children.push_back(FlagEntry(flag, with_details, audience));
  }
  if (have_current) {
    options.children.push_back(Content{.node = std::move(current)});
  }
  return options;
}

// EXPRESSION: the primaries split into Tests / Actions / Operators. `with_details`
// false yields the terse usage-page form.
Section ExpressionSection(bool with_details) {
  Section expression{.title = "Expression"};
  for (const KindSection& kind_section : kKindSections) {
    Subsection sub{.title = std::string(kind_section.title)};
    for (const registry::Descriptor& descriptor : registry::All()) {
      if (descriptor.kind == kind_section.kind) {
        sub.children.push_back(PrimaryEntry(descriptor, with_details));
      }
    }
    expression.children.push_back(Content{.node = std::move(sub)});
  }
  return expression;
}

// The topic table rows: ALPHABETICAL (the list is long enough that SOT order reads as random), with
// each topic's informative aliases continuing the name in the term column - the same shape as flag
// aliases (`--help, -h`), so they share the term colour instead of blending into the summary prose.
// A bare plural (`archives` for `archive`) is omitted: it is guessable, lands on the same page
// anyway, and would only repeat the name - while an alias a reader cannot guess (`regex` for
// `grammars`, `env` for `environment`) is real functionality worth the width. The SOT vector keeps
// its curated order, which composes the full reference.
Rows TopicRows() {
  const absl::Span<const HelpTopic> help_topics = HelpTopics();
  std::vector<HelpTopic> topics(help_topics.begin(), help_topics.end());
  absl::c_sort(topics, [](const HelpTopic& lhs, const HelpTopic& rhs) { return lhs.name < rhs.name; });
  Rows rows;
  for (const HelpTopic& topic : topics) {
    std::string term(topic.name);
    for (const std::string_view alias : topic.aliases) {
      if (alias == absl::StrCat(topic.name, "s")) {
        continue;  // the guessable plural
      }
      absl::StrAppend(&term, ", ", alias);
    }
    rows.rows.push_back(Row{.term = std::move(term), .description = ParseInline(topic.summary)});
  }
  return rows;
}

// The `--help=TOPIC` index as a subsection, shared by the usage page and the guide.
Subsection TopicsSubsection() {
  Subsection topics{.title = "Topics (--help=TOPIC)"};
  topics.children.push_back(Content{.node = TopicRows()});
  return topics;
}

// HELP: the meta / doc flags and the `--help=TOPIC` index, both from their SOTs
// (HelpFlags / HelpTopics), for the usage page.
Section BuildHelpSection() {
  Section help{.title = "Help"};
  Rows flags;
  for (const HelpFlag& flag : HelpFlags()) {
    flags.rows.push_back(Row{.term = std::string(flag.display), .description = ParseInline(flag.summary)});
  }
  help.children.push_back(Content{.node = std::move(flags)});

  help.children.push_back(Content{.node = TopicsSubsection()});
  return help;
}

// The topics table alone: `--help=list` / `--help=topic` / `--help=topics` render exactly this, so
// "what can I ask for?" has an answer that is only the answer. Also embedded in the usage page and
// the `--help=help` guide via BuildHelpSection, all from the HelpTopics SOT.
Section TopicsSection() {
  // The flag spelling lives in prose, not the title: the plain backend uppercases titles, and
  // `--HELP=TOPIC` is not a thing anyone can type.
  Section section{.title = "Help topics"};
  section.children.push_back(ProseOf("Open one with `--help=TOPIC`; `--help=help` explains the help system."));
  section.children.push_back(Content{.node = TopicRows()});
  return section;
}

// The `--help=help` topic: a guide to the (subcommand-free) help system. Reuses
// BuildHelpSection (the SOT flags + topic index) with the framing prose prepended, so
// the guide can never drift from the actual help flags / topics.
Section GuideSection() {
  Section help = BuildHelpSection();
  help.children.insert(
      help.children.begin(),
      ProseOf(
          "xff has no subcommands; every kind of help is a flag. `--help` is this usage overview; "
          "`--help=NAME` documents one option or primary (e.g. `--help=-regex`, `--help=--sort`); "
          "`--help=TOPIC` opens one of the topics below; `--help=full` is the complete detailed reference. "
          "Append `:markdown` (or `:md`), `:html`, or `:roff` to select a non-console renderer, for example "
          "`--help=full:html`; `--man` is the conventional alias for `--help=full:roff`. On a terminal this help "
          "(and `--man`) is paged per `--pager`, and `--man` is formatted like a man page, so long output "
          "scrolls instead of scrolling off; through a pipe or redirect it stays unpaged (and `--man` stays "
          "raw roff for `mandoc` / installing). `--color` and `--width` control its coloring and wrap width; "
          "see the display options below."));
  // The output globals that shape how this help itself renders, pulled from the globals SOT so the
  // guide cannot drift from the actual flags.
  Subsection display{.title = "Display options (how help is shown)"};
  Rows display_rows;
  // The display-affecting globals, in the order the section presents them.
  static constexpr std::array kDisplayFlags = std::to_array<std::string_view>({
      "--color",
      "--pager",
      "--width",
  });
  for (const std::string_view name : kDisplayFlags) {
    if (const mbo::types::OptionalRef<const GlobalFlag> flag = LookupGlobal(name); flag.has_value()) {
      display_rows.rows.push_back(Row{.term = std::string(flag->display), .description = ParseInline(flag->summary)});
    }
  }
  display.children.push_back(Content{.node = std::move(display_rows)});
  help.children.push_back(Content{.node = std::move(display)});
  return help;
}

bool IsIgnoreTopicName(std::string_view name) {
  return name == "ignore" || name == "ignores" || name == "vcs";
}

}  // namespace

std::vector<std::string_view> LicenseComponentNames() {
  std::vector<std::string_view> names;
  for (const license::Notice& notice : license::Notices()) {
    names.push_back(notice.component);
  }
  return names;
}

Document FieldsReference() {
  Document doc;
  doc.sections.push_back(BuildFields());
  return doc;
}

Document BuildUsage() {
  Document doc{
      .name = "xff",
      .tagline = "eXtended File Find, a find(1)-compatible file finder with modern extensions",
      .usage = "[option...] [path...] [expression]",
  };
  doc.sections.push_back(DescriptionSection());
  doc.sections.push_back(OptionsSection(/*with_details=*/false));
  doc.sections.push_back(ExpressionSection(/*with_details=*/false));
  doc.sections.push_back(BuildHelpSection());
  return doc;
}

std::optional<Document> IndexReference(std::string_view name) {
  Document doc;
  if (name == "list" || name == "topic" || name == "topics") {
    // The list OF TOPICS, not the usage page it used to alias: `list`'s table row promised an index
    // and delivered plain --help, which reads as "does not work".
    doc.sections.push_back(TopicsSection());
    return doc;
  }
  if (name == "all") {
    // Every option + primary, summaries only (no detail blocks).
    doc.sections.push_back(OptionsSection(/*with_details=*/false));
    doc.sections.push_back(ExpressionSection(/*with_details=*/false));
  } else if (name == "expressions") {
    // The expression vocabulary (summaries), without the whole-run global flags.
    doc.sections.push_back(ExpressionSection(/*with_details=*/false));
  } else {
    return std::nullopt;
  }
  return doc;
}

namespace {

std::optional<Section> NamedTopicSection(std::string_view name) {
  if (name == "fields") {
    return BuildFields();
  } else if (name == "output") {
    return OutputSection(/*in_full=*/false);
  } else if (name == "printf") {
    return PrintfSection();
  } else if (name == "time") {
    return TimeSection();
  } else if (name == "size") {
    return SizeSection();
  } else if (name == "grammars" || name == "regex" || name == "regexp") {
    return GrammarsSection();
  } else if (name == "content") {
    return ContentSection(/*in_full=*/false);
  } else if (name == "compare") {
    return CompareSection(/*in_full=*/false);
  } else if (IsIgnoreTopicName(name)) {
    return IgnoreSection(/*in_full=*/false);
  } else if (name == "archive" || name == "archives") {
    return ArchiveSection(/*in_full=*/false);
  } else if (name == "stats") {
    return StatsSection(/*in_full=*/false);
  } else if (name == "config") {
    return ConfigSection(/*in_full=*/false);
  } else if (name == "environment" || name == "env") {
    return EnvironmentSection();
  } else if (name == "cookbook" || name == "examples" || name == "recipes") {
    return BuildExamples();
  } else if (name == "notice" || name == "notices") {
    return NoticeSection();
  } else if (name == "license" || name == "licenses") {
    return LicenseSection();
  } else if (name == "help") {
    return GuideSection();
  }
  return std::nullopt;
}

}  // namespace

std::optional<Document> TopicReference(std::string_view name) {
  Document doc;
  std::optional<Section> section;
  if (const std::optional<std::string_view> component = LicenseComponentOf(name); component.has_value()) {
    section = LicenseComponentSection(*component);
  } else {
    section = NamedTopicSection(name);
  }
  if (!section.has_value()) {
    return std::nullopt;
  }
  doc.sections.push_back(*std::move(section));
  return doc;
}

std::optional<Document> EntryReference(std::string_view name) {
  // An expression primary / operator / action (leading-dash convenience: regex -> -regex).
  const mbo::types::OptionalRef<const registry::Descriptor> descriptor = [&] {
    const auto exact = registry::Lookup(name);
    return !exact.has_value() && !name.empty() && name.front() != '-' && name.front() != '!'
               ? registry::Lookup(absl::StrCat("-", name))
               : exact;
  }();
  // Otherwise a whole-run global flag (leading-dashes convenience: sort -> --sort).
  const mbo::types::OptionalRef<const GlobalFlag> flag = [&] {
    if (descriptor.has_value()) {
      return mbo::types::OptionalRef<const GlobalFlag>{};
    }
    const mbo::types::OptionalRef<const GlobalFlag> exact = LookupGlobal(name);
    return !exact.has_value() && !name.empty() && name.front() != '-' ? LookupGlobal(absl::StrCat("--", name)) : exact;
  }();
  if (!descriptor.has_value() && !flag.has_value()) {
    return std::nullopt;
  }
  // A title-less section: the single entry renders without a section heading.
  Section section;
  section.children.push_back(descriptor.has_value() ? PrimaryEntry(*descriptor) : FlagEntry(*flag));
  Document doc;
  doc.sections.push_back(std::move(section));
  return doc;
}

Document BuildReference(Audience audience) {
  Document doc{
      .name = "xff",
      .tagline = "eXtended File Find, a find(1)-compatible file finder with modern extensions",
      .usage = "[option...] [path...] [expression]",
  };

  doc.sections.push_back(DescriptionSection());
  doc.sections.push_back(CommandStructureSection());
  doc.sections.push_back(ConfigSection(/*in_full=*/true));
  doc.sections.push_back(OptionsSection(/*with_details=*/true, audience));
  doc.sections.push_back(ExpressionSection(/*with_details=*/true));

  doc.sections.push_back(OutputSection(/*in_full=*/true));
  doc.sections.push_back(BuildFields());
  doc.sections.push_back(PrintfSection());
  doc.sections.push_back(TimeSection());
  doc.sections.push_back(SizeSection());
  doc.sections.push_back(GrammarsSection());
  doc.sections.push_back(ContentSection(/*in_full=*/true));
  doc.sections.push_back(CompareSection(/*in_full=*/true));
  doc.sections.push_back(IgnoreSection(/*in_full=*/true));
  doc.sections.push_back(ArchiveSection(/*in_full=*/true));
  doc.sections.push_back(StatsSection(/*in_full=*/true));
  doc.sections.push_back(EnvironmentSection());
  doc.sections.push_back(BuildExamples());

  Section exit_status{.title = "Exit status"};
  exit_status.children.push_back(ProseOf(
      "0 on success, 2 on error. With `--quiet` or `--exit-match` the exit is 0 when something "
      "matched and 1 when nothing did (an error still outranks the match status)."));
  doc.sections.push_back(std::move(exit_status));

  Section see_also{.title = "See also"};
  SeeAlso block{
      .refs =
          {{.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"},
           {.kind = RefTarget::Kind::kManPage, .id = "grep", .section = "1"},
           {.kind = RefTarget::Kind::kManPage, .id = "fnmatch", .section = "3"},
           {.kind = RefTarget::Kind::kManPage, .id = "glob", .section = "7"},
           {.kind = RefTarget::Kind::kManPage, .id = "pcre2pattern", .section = "3"}},
      .note = ParseInline(
          "For the `--regextype` grammars see the Regex grammars section above (`--help=grammars`). FNMATCH "
          "is the platform's fnmatch(3) and PCRE2 is pcre2pattern(3); GLOB and SHGLOB are xff's "
          "path-aware globs (compiled to RE2), NOT POSIX glob(7) - that page is listed only as background "
          "on shell globbing. The default RE2 grammar has no man page; its syntax is at "
          "https://github.com/google/re2/wiki/Syntax ."),
  };
  see_also.children.push_back(Content{.node = std::move(block)});
  doc.sections.push_back(std::move(see_also));

  return doc;
}

}  // namespace xff::cli
