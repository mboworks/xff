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

#include <unistd.h>

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/cli/help_backend.h"
#include "xff/cli/help_build.h"
#include "xff/cli/help_width.h"
#include "xff/cli/html.h"
#include "xff/cli/manpage.h"
#include "xff/cli/markdown.h"
#include "xff/cli/pager.h"
#include "xff/cli/plain_backend.h"
#include "xff/cli/wrap.h"
#include "xff/config/config.h"
#include "xff/config/loader.h"
#include "xff/config/policy.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/run.h"
#include "xff/env/env.h"
#include "xff/matching/regex/regex.h"
#include "xff/parser/parser.h"
#include "xff/presentation/color/color.h"
#include "xff/presentation/format/format.h"
#include "xff/registry/descriptor.h"
#include "xff/vfs/local_fs.h"

namespace {

// Environment variable as an optional (nullopt when unset), for config discovery.
std::optional<std::string> EnvOpt(std::string_view name) {
  return xff::env::Get(name);
}

// Reads a whole file, or nullopt if it cannot be opened: the config FileReader.
std::optional<std::string> ReadFile(std::string_view path) {
  const xff::vfs::LocalFs fs;
  const absl::StatusOr<std::string> content = fs.ReadContent(path);
  if (!content.ok()) {
    return std::nullopt;
  }
  return *content;
}

// The flavor feature-map: one row per style-scoped behavior, its controlling flag(s), each
// style's default, and (when `current` is set) the value resolved for this invocation. All
// values come from engine::FlavorFacets() -- each facet wraps its own resolver -- so the table
// cannot drift from an actual run. `--help=styles` shows the static comparison (no `current`);
// `--explain` adds the `current` column from the resolved style + globals.
//
// Two-tier: the facets that actually vary lead (the styles disagree, or -- in --explain -- a flag
// overrode the active style's default), then the facets identical in every style follow, so the
// signal is not buried among the many behaviors that never differ.
std::string RenderFlavorTable(const std::vector<std::string>& globals, std::optional<xff::registry::Style> current) {
  using ::xff::registry::Style;
  const std::size_t columns = current.has_value() ? 6 : 5;
  std::vector<std::string> header = {"behavior", "flag", "find", "xff", "rg"};
  if (current.has_value()) {
    header.emplace_back("current");
  }

  struct FacetRow {
    std::vector<std::string> cells;
    bool relevant = false;
  };

  std::vector<FacetRow> rows;
  for (const xff::engine::FlavorFacet& facet : xff::engine::FlavorFacets()) {
    const std::string find = facet.value({}, Style::kFind);
    const std::string xff = facet.value({}, Style::kXff);
    const std::string rg = facet.value({}, Style::kRg);
    std::vector<std::string> cells = {std::string(facet.behavior), std::string(facet.flag), find, xff, rg};
    bool relevant = !(find == xff && xff == rg);  // the styles disagree on this behavior
    if (current.has_value()) {
      const std::string resolved = facet.value(globals, *current);
      cells.push_back(resolved);
      if (resolved != facet.value({}, *current)) {
        relevant = true;  // a flag overrode the active style's default this run
      }
    }
    rows.push_back({.cells = std::move(cells), .relevant = relevant});
  }
  const auto section = [&](std::string_view title, bool want_relevant) -> std::string {
    xff::format::Table table(std::vector<xff::format::Align>(columns, xff::format::Align::kLeft));
    table.AddRow(header);
    std::size_t count = 0;
    for (const FacetRow& row : rows) {
      if (row.relevant == want_relevant) {
        table.AddRow(row.cells);
        ++count;
      }
    }
    return count == 0 ? std::string() : absl::StrCat(title, "\n", table.Render());
  };
  const std::string_view lead_title = current.has_value() ? "Relevant to this run:" : "Where the styles differ:";
  std::string out = section(lead_title, /*want_relevant=*/true);
  if (const std::string same = section("Same in every style:", /*want_relevant=*/false); !same.empty()) {
    absl::StrAppend(&out, out.empty() ? "" : "\n", same);
  }
  return out;
}

// The tail of --help=styles: what to type when you arrive from another finder. Not a facet table -
// these are not style-scoped behaviours xff resolves, they are OTHER tools' spellings - so it is a
// plain mapping, and it lives with the style comparison because "which flavor am I in" and "how do I
// say what I used to say" are the same question to a new user.
//
// fd is the interesting case: its single positional pattern is a REGEX by default and `--glob`
// switches THAT pattern to glob semantics, so the flag has no counterpart here - xff has no
// positional pattern, and the primary is the choice. (`-g` is xff's gitignore toggle, and would be
// the wrong letter anyway.)
std::string RenderComingFrom() {
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 10> kFromFd = {{
      {"PATTERN (fd's default: a regex)", "-regex PATTERN, or -name '*glob*' for a glob"},
      {"-g / --glob PATTERN", "-name PATTERN (the primary IS the choice; there is no mode to switch)"},
      {"-p / --full-path", "-path (matches the whole path, where -name matches the basename)"},
      {"-e / --extension EXT", "-name '*.EXT' (a glob on the basename)"},
      {"-t / --type f|d|l|x", "-type f|d|l, and -perm /111 for executable"},
      {"-H / --hidden", "the default; --no-hidden (or --config=rg) skips dotfiles"},
      {"-I / --no-ignore", "-u / --no-ignore (xff also starts with ignore files OFF)"},
      {"-x / --exec CMD", "-exec CMD \\;   (and -X / --exec-batch is -exec CMD +)"},
      {"--changed-within 1d", "-mtime -1d (see --help=time)"},
      {"-d / --max-depth N", "-maxdepth N (find's spelling; -mindepth too)"},
  }};
  std::string out = "Coming from fd:\n";
  for (const auto& [theirs, ours] : kFromFd) {
    absl::StrAppendFormat(&out, "  %-32s %s\n", theirs, ours);
  }
  absl::StrAppend(
      &out,
      "\nComing from ripgrep: -grep PATTERN is rg's search (with --count, --context, --color), and\n"
      "--config=rg starts from rg's defaults - ignore files honored, dotfiles skipped, smart case.\n");
  return out;
}

// The grammar contract behind xff's spellings. This belongs in --help=styles because style is more
// than a bundle of defaults: it also answers why a familiar-looking short exists, why another one
// deliberately does not, and whether a token configures the whole run or participates in the
// expression. Keep the boundary explicit so one convenient alias cannot accidentally establish a
// policy of assigning every primary its first unused letter.
std::string RenderSyntaxConventions(const xff::cli::HelpRenderContext& context) {
  constexpr std::string_view kPosition =
      "Double-dash globals are position-independent at parser boundaries: they may precede roots, "
      "sit among them, or follow expression nodes. They are not taken out of a primary's argument "
      "run: between -exec and its closing ; or +, for example, --flag belongs to the child command. "
      "A bare -- ends option parsing and disables later global hoisting.";
  constexpr std::string_view kAliases =
      "Short aliases are exceptional, not generated from first letters. -n/-p are retained as the "
      "frequent symmetric basename/whole-path glob pair (-name/-path); exact-token parsing keeps -p "
      "distinct from -print, -prune, and -printf. That does not imply -r, -x, -c, or -f: each has "
      "multiple plausible meanings. A new short needs a compelling compatibility or usage case and "
      "one unambiguous meaning. fd's -p is --full-path, so the table below maps it to xff's -path; "
      "xff's -p is its own primary alias, not a claim of fd command-line compatibility.";
  constexpr std::string_view kValues =
      "An equals sign assigns a whole-run global: --sort=tree or --define=NAME=VALUE. In the latter, "
      "the global's VALUE is itself the conventional NAME=VALUE definition. A colon instead qualifies "
      "one expression node: -fuzzy:fzf:80% foo selects how that -fuzzy matches its separate PATTERN "
      "operand, while -hash:sha256 selects how that one action renders its digest. Inner punctuation "
      "still belongs to the selected value grammar, as in -capture:NAME=REGEX.";
  return absl::StrCat(
      "Flag and primary conventions:\n"
      "  --flag          configures the whole run (output, traversal, or another global mode)\n"
      "  --flag=VALUE    assigns that global's value\n"
      "  -primary        is part of the per-entry expression: a test, action, or operator\n"
      "  -primary:QUAL   qualifies that one expression node; operands remain separate\n"
      "  -h/-help/etc.   compatibility aliases for established global spellings, not new primaries\n\n",
      xff::cli::WrapText(kPosition, context.width, "", ""), "\n", xff::cli::WrapText(kValues, context.width, "", ""),
      "\n", xff::cli::WrapText(kAliases, context.width, "", ""));
}

// The --help=extras topic: the optional build-time features and whether THIS binary links each.
// Availability is per-binary, so this is a runtime topic (not folded into the static --help=full /
// man / markdown reference). PCRE2 is the --regextype value extra (regex::Pcre2Available, from the
// self-registering @xff_pcre2 backend); the flag-gated extras come from the globals SOT (each
// distinct GlobalFlag.extra key, ExtraEnabled) so the list cannot drift from the flags.
std::string RenderExtras() {
  std::string out =
      "xff build extras. Optional features compiled in at build time; a lean build omits them, and a "
      "full build (--config=xff_full, or the installed `xff_full` binary) includes them all. Where an "
      "extra is not built in, its option stays listed but is a hard error if used.\n\n";
  const auto row = [&out](std::string_view name, bool built_in, std::string_view rebuild, std::string_view what) {
    absl::StrAppendFormat(
        &out, "  %-9s %s\n            %s\n", name,
        built_in ? "[built into this binary]" : absl::StrCat("[not built in; rebuild with ", rebuild, "]"), what);
  };
  row("fuse", xff::cli::ExtraEnabled("fuse"), xff::cli::ExtraBuildFlag("fuse"),
      "mount containers as read-only directories via the platform's fuse3 (probed at runtime)");
  row("asar", xff::cli::ExtraEnabled("asar"), xff::cli::ExtraBuildFlag("asar"),
      "Electron ASAR directory trees, packed content, unpacked sidecars, links, and integrity metadata");
  row("brotli", xff::cli::ExtraEnabled("brotli"), xff::cli::ExtraBuildFlag("brotli"),
      "RFC 9841 and raw Brotli compression for the archive extra");
  row("pcre2", xff::cli::ExtraEnabled("pcre2"), xff::cli::ExtraBuildFlag("pcre2"),
      "--regextype=PCRE2: Perl-compatible regex (lookahead, backreferences, ...)");
  row("language-db", xff::cli::ExtraEnabled("language-db"), xff::cli::ExtraBuildFlag("language-db"),
      "Brotli-compressed GitHub Linguist language names, suffixes, aliases, and colours");
  row("mime-db", xff::cli::ExtraEnabled("mime-db"), xff::cli::ExtraBuildFlag("mime-db"),
      "comprehensive media-type names, suffixes, and metadata from mime-db");
  row("squashfs", xff::cli::ExtraEnabled("squashfs"), xff::cli::ExtraBuildFlag("squashfs"),
      "read SquashFS images, Snap packages, and AppImage payloads as virtual filesystems");
  absl::flat_hash_set<std::string_view> seen;
  for (const xff::cli::GlobalFlag& flag : xff::cli::Globals()) {
    if (!flag.extra.empty() && seen.insert(flag.extra).second) {
      row(flag.extra, xff::cli::ExtraEnabled(flag.extra), xff::cli::ExtraBuildFlag(flag.extra), flag.summary);
    }
  }
  return out;
}

// The one-line pointer at the foot of a SINGLE help page (`--help=NAME`, `--help=TOPIC`). Those
// are the pages a user lands on without having seen the help system's own map: the usage page
// lists the topics, but nothing on `--help=-regex` says the index or this topic exist.
//
// Deliberately NOT on: the usage page (which already explains all of this and lists the topics),
// `--help=help` itself, the `list` / `all` / `full` aggregates (they ARE the map), and never in
// `--man` / `--help=full:FORMAT`, where a tip would be embedded in a document people install or publish.
// A trailer on every surface is a trailer nobody reads.
std::string HelpTip(const xff::cli::HelpRenderContext& context) {
  static constexpr std::string_view kTip =
      "Tip: 'xff --help=help' explains the help system; 'xff --help=topics' lists the help topics.";
  // Dim when colour is on, so it reads as a footnote rather than as part of the entry. It is still
  // flowing help text: WrapText ignores the ANSI escapes when measuring the configured width.
  const std::string text = context.color ? absl::StrCat("\033[2m", kTip, "\033[0m") : std::string(kTip);
  return absl::StrCat("\n", xff::cli::WrapText(text, context.width, "", ""));
}

// Whether `topic` gets that trailer: everything except the maps and the self-referential page.
bool TopicTakesTip(std::string_view topic) {
  static constexpr auto kNoTip = std::to_array<std::string_view>({
      "all",
      "expressions",
      "full",
      "help",
      "license",
      "licenses",
      "list",
      "long",
      "notice",
      "notices",
      "topic",
      "topics",
  });
  if (absl::StartsWith(topic, "license=")) {
    return false;
  }
  return !absl::c_linear_search(kNoTip, topic);
}

// forward declaration (FullReference recurses); `context` carries the render meta
// (wrap width, ...) every model-rendered topic applies.
absl::StatusOr<std::string> RenderTopic(std::string_view topic, xff::cli::HelpRenderContext context);

enum class Meta : std::uint8_t { kNone, kUsage, kTopic, kVersion, kMan, kMarkdown, kHtml, kInvalid };

struct MetaSelection {
  Meta kind = Meta::kNone;
  std::string topic;
};

std::string NormalizeHelpTopic(std::string_view topic) {
  const std::size_t value = topic.find('=');
  return value == std::string_view::npos
             ? absl::AsciiStrToLower(topic)
             : absl::StrCat(absl::AsciiStrToLower(topic.substr(0, value)), topic.substr(value));
}

MetaSelection ParseHelpSelector(std::string_view selector) {
  const std::size_t separator = selector.rfind(':');
  if (separator == std::string_view::npos) {
    return {.kind = Meta::kTopic, .topic = NormalizeHelpTopic(selector)};
  }
  const std::string_view topic = selector.substr(0, separator);
  const std::string format = absl::AsciiStrToLower(selector.substr(separator + 1));
  const std::string normalized_topic = absl::AsciiStrToLower(topic);
  if (normalized_topic != "full" && normalized_topic != "long") {
    return {
        .kind = Meta::kInvalid,
        .topic = absl::StrCat("formatted help is supported only for 'full' or 'long', not '", topic, "'"),
    };
  }
  if (format == "plain") {
    return {.kind = Meta::kTopic, .topic = normalized_topic};
  }
  if (format == "markdown" || format == "md") {
    return {.kind = Meta::kMarkdown};
  }
  if (format == "html") {
    return {.kind = Meta::kHtml};
  }
  if (format == "roff") {
    return {.kind = Meta::kMan};
  }
  return {
      .kind = Meta::kInvalid,
      .topic = absl::StrCat("unknown help format '", format, "'; use plain, markdown, html, or roff"),
  };
}

MetaSelection SelectMeta(const std::vector<std::string>& flags) {
  if (flags.empty()) {
    return {};
  }
  const std::string& arg = flags.front();  // first meta wins, matching the historical behavior
  if (arg == "--help" || arg == "-help" || arg == "-h") {
    return {.kind = Meta::kUsage};
  }
  if (arg == "--help-all") {
    return {.kind = Meta::kTopic, .topic = "all"};
  }
  if (arg == "--help-full" || arg == "--help-long") {
    return {.kind = Meta::kTopic, .topic = "full"};
  }
  if (arg.starts_with("--help=")) {
    return ParseHelpSelector(std::string_view(arg).substr(7));
  }
  if (arg == "--version" || arg == "-version") {
    return {.kind = Meta::kVersion};
  }
  return {.kind = Meta::kMan};  // --man is the only remaining parser-classified meta flag
}

std::string_view ProgramBasename(std::string_view program) {
  const std::string_view::size_type slash = program.find_last_of("/\\");
  program = slash == std::string_view::npos ? program : program.substr(slash + 1);
  return program.empty() ? std::string_view("xff") : program;
}

// The full detailed reference (--help=full / long and --help-full / --help-long): every
// option and primary with explanations, then each sub-vocabulary topic marked in_full --
// so adding a topic auto-includes it here, no hand-maintained list.
std::string FullReference(xff::cli::HelpRenderContext context) {
  // The complete reference is the whole help model (the same Document --man / --help=full:FORMAT
  // render), in plain text and wrapped to the context width.
  xff::cli::PlainTextBackend backend(context);
  xff::cli::RenderDocument(xff::cli::BuildReference(), backend);
  return backend.Take();
}

// The single dispatch for `--help=TOPIC`: the CLI-rendered topics (needing the engine /
// datetime / flavor facets), else the registry-backed cli::RenderHelp. Shared by the
// --help= handler, the --help-* shortcuts, and FullReference (which never asks for the
// self-referential full/long, so there is no recursion).
absl::StatusOr<std::string> RenderTopic(std::string_view topic, xff::cli::HelpRenderContext context) {
  if (topic == "styles" || topic == "flavors") {
    return absl::StrCat(
        RenderFlavorTable({}, std::nullopt), "\n", RenderSyntaxConventions(context), "\n", RenderComingFrom());
  }
  // The sub-vocabulary topics (fields / printf / time / size / grammars) and the index
  // topics (list / all / expressions) render from the model so they wrap + indent.
  {
    std::optional<xff::cli::Document> topic_doc = xff::cli::TopicReference(topic);
    if (!topic_doc.has_value()) {
      topic_doc = xff::cli::IndexReference(topic);
    }
    if (topic_doc.has_value()) {
      xff::cli::PlainTextBackend backend(context);
      xff::cli::RenderDocument(*topic_doc, backend);
      return backend.Take();
    }
  }
  if (topic == "extras") {
    return RenderExtras();
  }
  if (topic == "full" || topic == "long") {
    return FullReference(context);
  }
  // Otherwise a single primary / flag entry, rendered from the model so it wraps (and
  // later colors) via the context. Topic names take precedence over same-named flags
  // (e.g. `config` resolves to the topic above, not the `--config` flag) because the
  // TopicReference / IndexReference branches run first.
  if (std::optional<xff::cli::Document> entry = xff::cli::EntryReference(topic); entry.has_value()) {
    xff::cli::PlainTextBackend backend(context);
    xff::cli::RenderDocument(*entry, backend);
    return backend.Take();
  }
  return absl::NotFoundError("");  // unknown topic; the caller composes the user-facing message
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
// resolve config, dispatch meta flags, build + run the expression) is one cohesive sequence.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
int RunMain(int argc, char** argv) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): intentional
  const std::vector<std::string> args(argv + 1, argv + argc);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): intentional
  const char* const program = argv[0];

  // Read the fixed set of environment variables xff consults into the env cache up front, in one
  // locked pass, so every later read is a pure cache hit. Dynamic {env.NAME} field references are
  // not here (they are user-supplied); env::Get caches those lazily on first use.
  // Deduced rather than sized: a hand-written count silently pads the array with an empty name when it
  // does not match, which is exactly what happened here (14 declared, 13 written, LS_COLORS missing).
  static constexpr auto kKnownEnv = std::to_array<std::string_view>({
      "COLUMNS",
      "HOME",
      "LANG",
      "LC_ALL",
      "LC_CTYPE",
      "LSCOLORS",
      "LS_COLORS",
      "NO_COLOR",
      "PAGER",
      "TMPDIR",
      "XDG_CONFIG_HOME",
      "XDG_RUNTIME_DIR",
      "XFF_CONFIG",
      "XFF_MANPAGER",
      "XFF_PAGER",
  });
  xff::env::Prewarm(absl::MakeConstSpan(kKnownEnv));

  // The plain-help wrap width (--width), resolved once so every help / topic render
  // shares it. A bare --width means auto; --width=VALUE carries the value. Scanned
  // here (like the help flags) since --help short-circuits before the full parse.
  std::optional<std::string_view> width_flag;
  for (const std::string& arg : args) {
    if (arg == "--width") {
      width_flag = "auto";
    } else if (arg.starts_with("--width=")) {
      width_flag = std::string_view(arg).substr(std::string_view("--width=").size());
    }
  }
  const absl::StatusOr<std::size_t> help_width =
      xff::cli::ResolveHelpWidth(width_flag, xff::cli::DetectTerminalWidth());
  if (!help_width.ok()) {
    std::cerr << "xff: " << help_width.status().message() << "\n";
    return 2;
  }
  const bool stdout_is_tty = ::isatty(STDOUT_FILENO) != 0;
  // --color drives help color too (auto = a tty with NO_COLOR unset; always overrides).
  // Scanned from argv like --width, since --help short-circuits before the full parse.
  const bool help_color = xff::color::Enabled(xff::color::ResolveWhen(args), stdout_is_tty, xff::env::Has("NO_COLOR"));
  const xff::cli::HelpRenderContext help_context{.width = *help_width, .color = help_color};
  // Parse once before dispatching help/version. The parser identifies meta flags
  // only at option/expression boundaries, so `-exec echo --help ;` passes
  // `--help` to the child instead of turning the whole xff invocation into help.
  absl::StatusOr<xff::parser::Command> parsed = xff::parser::Parse(args);
  if (!parsed.ok()) {
    std::cerr << "xff: " << parsed.status().message() << "\n";
    return 2;
  }
  // Resolve only the globals the parser identified at option boundaries. In particular, a token
  // such as `--pager=always` inside an -exec argument run belongs to the child command.
  const absl::StatusOr<xff::cli::PagerConfig> pager = xff::cli::ResolvePager(parsed->globals);
  if (!pager.ok()) {
    std::cerr << "xff: " << pager.status().message() << "\n";
    return 2;
  }
  const xff::cli::PagerDecision meta_pager = xff::cli::DecidePager(*pager, xff::cli::PagerOutput::kMeta, stdout_is_tty);

  // Help and version are accepted anywhere globals may appear (find prints usage
  // on a bare --help wherever it lands). xff stays flag-only -- no `help` subcommand --
  // so the grammar is identical in find and xff flavors; only the vocabulary
  // differs. Accepted forms:
  //   --help / -h        usage page
  //   -help              GNU find compatibility (single-dash long option)
  //   --help=TOPIC       xff: registry-backed help for one primary/operator/action
  //   --help= / =list    xff: the whole-vocabulary index
  //   --version          version
  //   -version           GNU find compatibility
  //
  // A meta flag is NOTED here and rendered only after the remaining globals validate:
  // `xff --help=archive --bogus` is a broken command line, and the
  // one-line unknown-option error serves better than pages of help hiding the typo.
  // The parser has already separated these tokens from the command proper.
  const MetaSelection meta = SelectMeta(parsed->meta_flags);

  if (meta.kind != Meta::kNone) {
    // Bad flags are hard errors even when help was asked for. Ignoring them because
    // help "wins" is how a typo silently vanishes behind 200 lines of output.
    for (const std::string& global : parsed->globals) {
      if (!xff::cli::IsKnownGlobal(global)) {
        std::cerr << "xff: unknown option '" << global << "'\n"
                  << "Try 'xff --help' for usage, or 'xff --help=NAME' for one option.\n";
        return 2;
      }
      if (const absl::Status status = xff::cli::ValidateGlobalValue(global); !status.ok()) {
        std::cerr << "xff: " << status.message() << "\n"
                  << "Try 'xff --help=" << std::string_view(global).substr(0, global.find('='))
                  << "' for its values.\n";
        return 2;
      }
    }
    switch (meta.kind) {
      case Meta::kUsage: {
        xff::cli::PlainTextBackend backend(help_context);
        xff::cli::RenderDocument(xff::cli::BuildUsage(), backend);
        xff::cli::EmitPaged(backend.Take(), meta_pager);
        return 0;
      }
      case Meta::kTopic: {
        const absl::StatusOr<std::string> help = RenderTopic(meta.topic, help_context);
        if (help.ok()) {
          const std::string tip = TopicTakesTip(meta.topic) ? HelpTip(help_context) : std::string();
          xff::cli::EmitPaged(absl::StrCat(*help, tip), meta_pager);
          return 0;
        }
        // RenderTopic's only failure is unknown-topic; point at the list instead of a bare error.
        // `--help=license=NAME` fails the same way but for a different reason - the topic exists
        // and the COMPONENT does not - so it gets the component names, which is the list the
        // reader actually needs.
        if (absl::StartsWith(meta.topic, "license=") || absl::StartsWith(meta.topic, "licenses=")) {
          std::cerr << "xff: no licensed component '" << meta.topic.substr(meta.topic.find('=') + 1)
                    << "' in this binary; it has: " << absl::StrJoin(xff::cli::LicenseComponentNames(), ", ") << "\n";
          return 2;
        }
        std::cerr << "xff: no help topic '" << meta.topic << "'; 'xff --help=topics' lists them\n";
        return 2;
      }
      case Meta::kVersion:
        std::cout << "xff 0.0.0\n";  // short and machine-scraped: never paged
        return 0;
      case Meta::kMan:
        // roff(1); on a tty the man kind formats it (mandoc) so it reads like `man xff`,
        // while a redirect stays raw roff for `mandoc` / `man -l -` / installing as xff.1.
        xff::cli::EmitPaged(xff::cli::ManPage(ProgramBasename(program)), meta_pager, xff::cli::PagerKind::kMan);
        return 0;
      case Meta::kMarkdown:
        // GitHub-renderable vocabulary reference
        xff::cli::EmitPaged(xff::cli::MarkdownReference(), meta_pager);
        return 0;
      case Meta::kHtml:
        // Standalone semantic HTML5 vocabulary reference.
        xff::cli::EmitPaged(xff::cli::HtmlReference(), meta_pager);
        return 0;
      case Meta::kInvalid: std::cerr << "xff: " << meta.topic << "\n"; return 2;
      case Meta::kNone: break;  // unreachable; keeps the switch exhaustive
    }
  }

  // xff is flag-only -- there is no `help` / `version` subcommand. A user reaching
  // for one out of git/cargo habit would otherwise have the word silently taken as a
  // path to search, so (in the xff flavor only; find must keep `find help` meaning
  // "search ./help") catch a leading operand that names one and point at the flag.
  if (xff::config::DefaultStyleForProgram(program) != "find") {
    for (const std::string& arg : args) {
      if (arg == "--") {
        break;  // explicit end-of-options: the next token is deliberately an operand
      }
      if (arg.starts_with("-") || arg.starts_with("+")) {
        continue;  // a leading global, not yet the first operand
      }
      if (arg == "help" || arg == "version") {
        const std::string_view flag_hint =
            arg == "help" ? " for usage, or '--help=NAME' for one primary (e.g. '--help=-regex')" : "";
        std::cerr << "xff: '" << arg << "' is not a subcommand (xff is flag-only). Use '--" << arg << "'" << flag_hint
                  << ". To search a path literally named '" << arg << "', use './" << arg << "'.\n";
        return 2;
      }
      break;  // the first operand is something else; carry on to normal parsing
    }
  }

  xff::parser::Command command = *std::move(parsed);

  // Reject an unknown leading global option (usually a typo) with a usage error,
  // instead of silently ignoring it. Meta flags (--help / --version / --man) are already handled
  // above, so they never reach here.
  for (const std::string& global : command.globals) {
    if (!xff::cli::IsKnownGlobal(global)) {
      std::cerr << "xff: unknown option '" << global << "'\n"
                << "Try 'xff --help' for usage, or 'xff --help=NAME' for one option.\n";
      return 2;
    }
    // And the VALUE, for a flag whose vocabulary is closed: a typo used to select the default
    // silently, so the run looked like it worked. Checked here rather than in each resolver
    // because several of them (--color, --width, --pager) run before the parse, from raw argv,
    // with nowhere to report an error; by the time this loop runs every global is in hand.
    if (const absl::Status status = xff::cli::ValidateGlobalValue(global); !status.ok()) {
      std::cerr << "xff: " << status.message() << "\n"
                << "Try 'xff --help=" << std::string_view(global).substr(0, global.find('=')) << "' for its values.\n";
      return 2;
    }
  }

  // A composable-extra flag (e.g. --archive) is always recognized, but if the extra it needs is not
  // compiled into this binary, using it is a hard immediate error naming what to rebuild with - never
  // a silent no-op. Derived from the flag's SOT `extra` key + the compile-time ExtraEnabled map.
  for (const std::string_view global : command.globals) {
    std::string_view name = global.substr(0, global.find('='));
    // A chmod-style suffix sign is part of the VALUE, not the name (`-z+` is the short
    // `--archive=all`), so strip it before the lookup or the short forms would slip past
    // this gate entirely and fail later with a confusing message.
    const bool suffix_off = name.size() > 1 && name.back() == '-';
    // A whole RUN of signs, not one: the archive ladder spells its top rung `-z++`, so stripping a
    // single character would leave `-z+`, which is not a flag name, and the run would miss this gate
    // and fail later with a wordier message.
    while (name.size() > 1 && (name.back() == '+' || name.back() == '-')) {
      name.remove_suffix(1);
    }
    // The upper-case archive family is the lower-case one with writing armed, so it needs the same
    // extra; the table knows it under the flag that arms writing.
    if (name == "-Z") {
      name = "--archive-write";
    }
    const mbo::types::OptionalRef<const xff::cli::GlobalFlag> flag = xff::cli::LookupGlobal(name);
    if (!flag.has_value() || flag->extra.empty() || xff::cli::ExtraEnabled(flag->extra)) {
      continue;
    }
    // Explicitly turning the capability OFF needs no extra: `--archive=none` / `-z-` asks
    // for the plain (find) behavior a lean build already has, so demanding a rebuild there
    // would be nonsense. Only a request for the capability itself is a hard error.
    const std::string_view value = global.substr(0, global.find('=')) == global
                                       ? std::string_view()
                                       : std::string_view(global).substr(global.find('=') + 1);
    if (suffix_off || value == "none" || value == "off") {
      continue;
    }
    std::cerr << "xff: " << flag->name << ": this build has no " << flag->extra << " support";
    if (const std::string_view rebuild = xff::cli::ExtraBuildFlag(flag->extra); !rebuild.empty()) {
      std::cerr << "; rebuild with " << rebuild;
    }
    std::cerr << "\n";
    return 2;
  }

  // Load the layered config (system + user + explicit --xffrc) and resolve the
  // effective flags. --explain writes that effective configuration and exits.
  xff::config::DiscoveryOptions opts = xff::config::SelectorsFromGlobals(command.globals);
  // argv[0] dispatch: the program name picks the base style (invoked as `find` ->
  // find expression style; as `xff` or any other alias -> modern xff) as the lowest-precedence
  // selector, so an explicit --config still overrides it (design-config.md "CLI
  // selectors"). Prepended before discovery so find:/xff: .xffrc lines gate on it too.
  opts.configs.insert(opts.configs.begin(), std::string(xff::config::DefaultStyleForProgram(program)));
  opts.xff_config = EnvOpt("XFF_CONFIG");
  opts.xdg_config_home = EnvOpt("XDG_CONFIG_HOME");
  opts.home = EnvOpt("HOME");
  // Config is system + user + explicit --xffrc only; there is no auto-discovered project layer
  // (Option B, 2026-07-06), so the search roots do not feed config discovery.
  const xff::config::ConfigInputs inputs = xff::config::Discover(opts, ReadFile);
  if (const absl::Status status = xff::config::ValidateConfigSkips(inputs); !status.ok()) {
    std::cerr << "xff: " << status.message() << "\n";
    return 2;
  }
  // --allow-exec arms the dangerous directives an --xffrc file may carry, but only when it comes
  // from a trusted tier (the CLI, or the user/system config) - never from an --xffrc file itself,
  // so a named config cannot authorize its own -exec/-delete. The gate uses this to keep unarmed
  // --xffrc dangerous lines inert.
  const bool xffrc_armed = xff::config::ArmedFromTrustedTier(inputs, command.globals, "--allow-exec");
  const xff::config::GateResult gated = xff::config::GateConfig(inputs, xffrc_armed);
  const std::string_view invocation_selector = xff::config::DefaultStyleForProgram(program);
  const std::vector<xff::config::ResolvedFlag> resolved =
      xff::config::ResolveConfigInOrder(gated.config, command.globals, invocation_selector);
  std::vector<std::string> effective_configs = {std::string(invocation_selector)};
  constexpr std::string_view kConfigPrefix = "--config=";
  for (const xff::config::ResolvedFlag& flag : resolved) {
    if (flag.flag.starts_with(kConfigPrefix)) {
      effective_configs.push_back(flag.flag.substr(kConfigPrefix.size()));
    }
  }
  const xff::registry::Style style = xff::config::ActiveStyle(effective_configs);
  if (absl::c_contains(command.globals, "--explain")) {
    std::cout << xff::config::ExplainSources(inputs.sources, style);
    std::cout << xff::config::ExplainConfig(resolved);
    for (const xff::config::Drop& drop : gated.drops) {
      std::cout << "dropped\t" << xff::config::DropMessage(drop) << "\n";
    }
    std::cout << "\n# flavor defaults per style, and the value resolved for this run:\n";
    std::cout << RenderFlavorTable(command.globals, style);
    return 0;
  }
  // A disallowed config line is dropped, never fatal: warn (self-documenting) and
  // carry on with the survivors (design-config.md "Enforcement & self-documentation").
  for (const xff::config::Drop& drop : gated.drops) {
    std::string_view why = " - denied by config policy";
    if (drop.reason == xff::config::DropReason::kPresetOverload) {
      why = " - a config file cannot change a preset; use a named config (--config=NAME)";
    } else if (drop.reason == xff::config::DropReason::kUnarmedXffrc) {
      why = " - inert unless armed with --allow-exec (from the CLI or user/system config)";
    }
    std::cerr << "xff: ignoring " << xff::config::DropMessage(drop) << why << "\n";
  }
  // Apply the single resolved stream in its exact precedence order.
  std::vector<std::string> config_flags;
  config_flags.reserve(resolved.size());
  for (const xff::config::ResolvedFlag& flag : resolved) {
    config_flags.push_back(flag.flag);
  }
  command.globals = std::move(config_flags);

  // The find style (--config=find) accepts only find's own expression vocabulary;
  // reject xff extensions (e.g. -println) so a find-style run behaves like GNU
  // find (design-config.md "CLI selectors"). The default xff style accepts all.
  if (const absl::Status status = xff::parser::EnforceStyle(command, style); !status.ok()) {
    std::cerr << "xff: " << status.message() << "\n";
    return 2;
  }

  // Apply the resolved case mode to the matchers (--case / -i / -s[+|-]; rg defaults
  // smart), in place before the walk: sets folding on the case-sensitive matchers and
  // recompiles their pre-compiled regex. A no-op under the sensitive default.
  xff::parser::BindMatchers(
      command, xff::parser::GrammarFromGlobals(command.globals), xff::parser::ResolveCaseMode(command.globals, style));

  // Walk the roots and evaluate the expression, printing matches. Per-path errors
  // -> exit 2 (the xff exit-code model; design.md "Exit-code model"). Match-sensitive
  // exit is opt-in: --quiet suppresses output and exits by match, --exit-match keeps
  // output but exits by match; either makes "1 = no match" reachable. An error still
  // outranks match status (exit 2).
  const bool quiet = absl::c_contains(command.globals, "--quiet") || absl::c_contains(command.globals, "-q");
  const bool match_sensitive = quiet || absl::c_contains(command.globals, "--exit-match");
  // Auto pages a terminal listing; always and an explicit command also page through a pipe. Every
  // mode streams the whole walk rather than buffering per line, and steps aside for terminal-using
  // expressions (-ok / -exec and friends) or --quiet.
  const xff::cli::PagerDecision listing_pager = xff::cli::DecidePager(
      *pager, xff::cli::PagerOutput::kListing, stdout_is_tty, quiet || xff::parser::TakesTerminal(command));
  const xff::cli::PagerStream pager_stream(listing_pager);
  const xff::vfs::LocalFs fs;
  const xff::engine::RunResult result = xff::engine::RunFind(
      command, fs,
      [quiet](std::string_view record) {
        if (!quiet) {  // --quiet suppresses output; the match is still recorded via `matched`
          std::cout.write(record.data(), static_cast<std::streamsize>(record.size()));
        }
      },
      [](std::string_view path, absl::Status status) {
        std::cerr << "xff: " << path << ": " << status.message() << "\n";
      },
      style);  // style-scoped traversal defaults (xff -> sorted + bounded; find/rg -> unordered)
  if (result.errors != 0) {
    return 2;  // an error outranks match status
  }
  return match_sensitive && !result.any_match ? 1 : 0;
}

// Entry point: run xff, then flush stdout explicitly before the process exits.
// xff writes results and the help / man / markdown pages to std::cout, which is
// fully buffered when stdout is a pipe. An abnormal exit after main -- notably
// LeakSanitizer's _exit under `--config=asan` on Linux, which runs before libc++
// would flush the stream -- otherwise truncates large output (a partial `--man` or
// `--help=list`). Flushing here, before returning into the C++ exit sequence,
// guarantees the bytes are written regardless of what the exit path does next.
}  // namespace

int main(int argc, char** argv) {
  const int exit_code = RunMain(argc, argv);
  std::cout.flush();
  return exit_code;
}
