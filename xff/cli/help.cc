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

#include "xff/cli/help.h"

#include <array>
#include <string>

#include "absl/strings/str_cat.h"
#include "xff/registry/descriptor.h"

namespace xff::cli {

// The `--help=cookbook` topic (aliases examples / recipes), folded into --help=full: task-oriented
// worked examples that compose xff's building blocks end to end. The SOT is the recipe list below;
// every command is kept runnable as written, and is executed end to end by the tests in
// //xff/examples:cookbook_test - whose guard case fails CI if a recipe is added or reworded here
// without a matching test, so these examples ship tested, not just rendered. This complements the
// reference topics (--help=fields / --help=stats / --help=NAME), which describe pieces in isolation.
// Rendered as model nodes by help_build.cc's BuildExamples().
absl::Span<const Recipe> CookbookRecipes() {
  static constexpr auto kRecipes = std::to_array<Recipe>({
      {
          .task = "Ten largest files",
          .command = "xff . -type f -printf '%s\\t%p\\n' | sort -rn | head",
          .note = "%s is the size, %p the path; the shell sorts and takes the top ten. -printf builds any "
                  "columnar line you need.",
      },
      {
          .task = "Disk use per file type",
          .command = "xff . -type f --summary=ext",
          .note = "a count + total size per extension; the --summary global reads naturally at the end, "
                  "after the expression (a --long global may sit anywhere). Swap in --histogram=ext for "
                  "bars, or --histogram='ext:sum(lines)' to rank by lines. See --help=stats.",
      },
      {
          .task = "Delete stale temp files, safely",
          .command = "xff . -type f -name '*.tmp' -mtime +7 -delete --dry-run",
          .note = "lists what -delete WOULD remove (guarded by --dry-run); rerun without it to delete. "
                  "-delete implies -depth so directories empty first.",
      },
      {
          .task = "Search code content, filtered by language",
          .command = "xff src -lang 'C*' -grep 'TODO'",
          .note = "prints every TODO line as path:lineno:text in C / C++ / C# files; add -c for per-file "
                  "counts or --context=2 for surrounding lines.",
      },
      {
          .task = "Per-file git-blame author line counts",
          .command = "xff . -text -exec git blame --line-porcelain {} \\; "
                     "| grep '^author ' | sort | uniq -c | sort -rn",
          .note = "runs git blame on each text file; the shell pipe tallies lines per author across the "
                  "tree. -text skips binaries (which git blame cannot line-blame). -exec feeds any pipeline "
                  "the field vocabulary cannot express alone.",
      },
      {
          .task = "Author line counts, natively (no shell pipe)",
          .command = "xff -g . -text -capturedir:blame git blame --line-porcelain {} \\; "
                     "--summary='{capture.blame:m/^author (.+)$/\\1/}'",
          .note = "the recipe above with the awk|sort tail folded into xff. -capturedir runs git blame in each "
                  "file's own directory (repo-safe, works across nested repos); --summary folds that output via "
                  "an m// extraction, tallying lines per author across the tree - no external pipe. -g honors "
                  ".gitignore and skips .git; -text keeps blame off binaries. Pass several roots (a b c ...) to "
                  "span multiple trees. A single-dash global like -g leads; double-dash globals such as "
                  "--summary may sit anywhere (before or after the paths).",
      },
      {
          .task = "Checksum manifest for a tree",
          .command = "xff . -type f -hash:sha256",
          .note = "prints `DIGEST  PATH` per file (like sha256sum); redirect to a file to snapshot a tree, "
                  "then diff two runs to spot changes.",
      },
      {
          .task = "Create a patch between repository trees",
          .command = "xff -g+ --compare=diff old-tree new-tree > changes.patch",
          .note = "walks both trees using the requested traversal and ignore options, then writes one unified patch "
                  "for added, "
                  "removed, and changed text files, including files found only on the right - unlike a one-sided "
                  "`-diff TARGET` walk. Binary differences are reported in the patch stream. Use --compare-select to "
                  "restrict which result kinds contribute.",
      },
      {
          .task = "Recently changed files as machine rows",
          .command = "xff . -type f -mtime -1 --format=jsonl",
          .note = "everything modified in the last day, one JSON object per file, ready for jq or a script.",
      },
  });
  return kRecipes;
}

// Read from the descriptor grammar (arity / binding) so the synopsis never drifts
// from the parser; shared with the man page. Documented in help.h.
std::string ArgHint(const registry::Descriptor& descriptor) {
  if (descriptor.binding == registry::Binding::kLabel) {
    return "[:[!]NAME]";
  }
  if (descriptor.binding == registry::Binding::kLabelRegex) {
    return ":[!]NAME[=REGEX] CMD... ;";
  }
  if (descriptor.binding == registry::Binding::kFormat) {
    return "[:FORMAT] PATTERN";
  }
  if (descriptor.binding == registry::Binding::kStyle) {
    return descriptor.default_argument.empty() ? "[:STYLE] TARGET" : "[:STYLE] [TARGET]";
  }
  if (descriptor.binding == registry::Binding::kHash) {
    return descriptor.arity == 0 ? "[:ALGO[/ENCODING]]" : "[:ALGO[/ENCODING]] EXPECTED";
  }
  if (descriptor.binding == registry::Binding::kText) {
    return "[:FLAVOR]";
  }
  if (descriptor.binding == registry::Binding::kFuzzy) {
    return "[:MODEL[:PCT%]|PCT%] PATTERN";
  }
  if (descriptor.binding == registry::Binding::kSimilarity) {
    return "[:WIDTH[:PCT%]|PCT%] TARGET";
  }
  if (descriptor.arity < 0) {
    return " CMD... ;";  // variadic until ';' (or '+' for -exec / -execdir)
  }
  std::string hint;
  for (int i = 0; i < descriptor.arity; ++i) {
    absl::StrAppend(&hint, " ARG");
  }
  return hint;
}

absl::Span<const HelpTopic> HelpTopics() {
  static constexpr auto kListAliases = std::to_array<std::string_view>({"topic", "topics"});
  static constexpr auto kRegexAliases = std::to_array<std::string_view>({"reg", "regexp"});
  static constexpr auto kEnvironmentAliases = std::to_array<std::string_view>({"env"});
  static constexpr auto kIgnoreAliases = std::to_array<std::string_view>({"ignores", "vcs"});
  static constexpr auto kStyleAliases = std::to_array<std::string_view>({"flavors"});
  static constexpr auto kArchiveAliases = std::to_array<std::string_view>({"archives"});
  static constexpr auto kCookbookAliases = std::to_array<std::string_view>({"examples", "recipes"});
  static constexpr auto kNoticeAliases = std::to_array<std::string_view>({"notices"});
  static constexpr auto kLicenseAliases = std::to_array<std::string_view>({"licenses"});
  static constexpr auto kFullAliases = std::to_array<std::string_view>({"long"});
  static constexpr auto kSafetySeeAlso = std::to_array<std::string_view>({"config", "archive", "output"});
  static constexpr auto kHelpSeeAlso = std::to_array<std::string_view>({"cookbook", "config", "expressions"});
  static constexpr auto kAllSeeAlso = std::to_array<std::string_view>({"expressions", "cookbook", "config"});
  static constexpr auto kExpressionsSeeAlso = std::to_array<std::string_view>({
      "regex",
      "content",
      "output",
      "safety",
  });
  static constexpr auto kFieldsSeeAlso = std::to_array<std::string_view>({
      "printf",
      "output",
      "time",
      "content",
  });
  static constexpr auto kOutputSeeAlso = std::to_array<std::string_view>({
      "fields",
      "printf",
      "stats",
      "compare",
      "safety",
  });
  static constexpr auto kPrintfSeeAlso = std::to_array<std::string_view>({"fields", "output", "time"});
  static constexpr auto kTimeSeeAlso = std::to_array<std::string_view>({"fields", "printf"});
  static constexpr auto kSizeSeeAlso = std::to_array<std::string_view>({"stats", "fields"});
  static constexpr auto kRegexSeeAlso = std::to_array<std::string_view>({"grammars", "content"});
  static constexpr auto kGrammarsSeeAlso = std::to_array<std::string_view>({"regex", "content"});
  static constexpr auto kRgSeeAlso = std::to_array<std::string_view>({"content", "regex", "config"});
  static constexpr auto kContentSeeAlso = std::to_array<std::string_view>({"regex", "fields", "output"});
  static constexpr auto kDiffSeeAlso = std::to_array<std::string_view>({
      "compare",
      "-diff",
      "-rdiff",
      "content",
      "fields",
      "ignore",
  });
  static constexpr auto kCompareSeeAlso = std::to_array<std::string_view>({
      "diff",
      "stats",
      "output",
      "content",
      "--summary",
      "--compare-select",
      "--summary-scope",
      "--summary-precision",
      "--format",
      "-diff",
      "-rdiff",
  });
  static constexpr auto kIgnoreSeeAlso = std::to_array<std::string_view>({"config", "archive"});
  static constexpr auto kConfigSeeAlso = std::to_array<std::string_view>({"safety", "ignore", "archive"});
  static constexpr auto kEnvironmentSeeAlso = std::to_array<std::string_view>({"config", "output"});
  static constexpr auto kStylesSeeAlso = std::to_array<std::string_view>({
      "config",
      "expressions",
      "cookbook",
  });
  static constexpr auto kExtrasSeeAlso = std::to_array<std::string_view>({"archive", "regex", "notice"});
  static constexpr auto kArchiveSeeAlso = std::to_array<std::string_view>({"safety", "config", "output"});
  static constexpr auto kStatsSeeAlso = std::to_array<std::string_view>({"compare", "fields", "output"});
  static constexpr auto kCookbookSeeAlso = std::to_array<std::string_view>({
      "config",
      "expressions",
      "output",
      "safety",
  });
  static constexpr auto kNoticeSeeAlso = std::to_array<std::string_view>({"extras", "license"});
  static constexpr auto kLicenseSeeAlso = std::to_array<std::string_view>({"notice"});

  static constexpr auto kTopics = std::to_array<HelpTopic>({
      {
          .name = "safety",
          .summary = "mandatory blocks, safe profiles, archive policy, and dry-run",
          .see_also = kSafetySeeAlso,
          .in_full = true,
      },
      {
          .name = "help",
          .aliases = {},
          .summary = "how the help system works, and the topics here",
          .see_also = kHelpSeeAlso,
      },
      {
          .name = "list",
          .aliases = kListAliases,
          .summary = "this list of help topics",
          .see_also = kHelpSeeAlso,
      },
      {
          .name = "all",
          .aliases = {},
          .summary = "every option and primary, summaries only",
          .see_also = kAllSeeAlso,
      },
      {
          .name = "expressions",
          .aliases = {},
          .summary = "the expression vocabulary: tests, operators, actions",
          .see_also = kExpressionsSeeAlso,
      },
      {
          .name = "fields",
          .aliases = {},
          .summary = "the {field} placeholder vocabulary",
          .see_also = kFieldsSeeAlso,
          .in_full = true,
      },
      {
          .name = "output",
          .aliases = {},
          .summary = "implicit listing, output formats, columns, templates, and filename safety",
          .see_also = kOutputSeeAlso,
          .in_full = true,
      },
      {
          .name = "printf",
          .aliases = {},
          .summary = "the -printf % directives and the %{field} escape",
          .see_also = kPrintfSeeAlso,
          .in_full = true,
      },
      {
          .name = "time",
          .aliases = {},
          .summary = "time-format presets and strftime patterns",
          .see_also = kTimeSeeAlso,
          .in_full = true,
      },
      {
          .name = "size",
          .aliases = {},
          .summary = "-size/-blocks legacy, SI, and IEC units plus +/-",
          .see_also = kSizeSeeAlso,
          .in_full = true,
      },
      {
          .name = "regex",
          .aliases = kRegexAliases,
          .summary = "regex matching, grammars, case, and related controls",
          .see_also = kRegexSeeAlso,
          .in_full = true,
      },
      {
          .name = "grammars",
          .aliases = {},
          .summary = "the --regextype grammars (RE2, ERE, EXACT, FNMATCH, GLOB, SHGLOB, PCRE2)",
          .see_also = kGrammarsSeeAlso,
          .in_full = true,
      },
      {
          .name = "rg",
          .aliases = {},
          .summary = "ripgrep-style arguments, match output, and the --xff grammar switch",
          .see_also = kRgSeeAlso,
          .in_full = true,
      },
      {
          .name = "content",
          .aliases = {},
          .summary = "reading inside files: -grep, -content, -rxc, -text, and their fields",
          .see_also = kContentSeeAlso,
          .in_full = true,
      },
      {
          .name = "diff",
          .aliases = {},
          .summary = "one-sided and two-tree patch output, direction, and destination paths",
          .see_also = kDiffSeeAlso,
          .in_full = true,
      },
      {
          .name = "compare",
          .aliases = {},
          .summary = "compare two selected trees as statuses or a patch",
          .see_also = kCompareSeeAlso,
          .in_full = true,
      },
      {
          .name = "ignore",
          .aliases = kIgnoreAliases,
          .summary = "ignore sources, VCS metadata pruning, hidden paths, and precedence",
          .see_also = kIgnoreSeeAlso,
          .in_full = true,
      },
      {
          .name = "config",
          .aliases = {},
          .summary = "config tiers, style selection (--config / argv[0]), and arming",
          .see_also = kConfigSeeAlso,
      },
      {
          .name = "environment",
          .aliases = kEnvironmentAliases,
          .summary = "the environment variables xff reads (NO_COLOR, PAGER, ...)",
          .see_also = kEnvironmentSeeAlso,
          .in_full = true,
      },
      {
          .name = "styles",
          .aliases = kStyleAliases,
          .summary = "the find / xff / rg flavor comparison",
          .see_also = kStylesSeeAlso,
      },
      {
          .name = "extras",
          .aliases = {},
          .summary = "optional build extras (PCRE2, archive) and whether this binary has them",
          .see_also = kExtrasSeeAlso,
      },
      {
          .name = "archive",
          .aliases = kArchiveAliases,
          .summary = "walking INTO containers: dive modes, member paths, and what is writable",
          .see_also = kArchiveSeeAlso,
      },
      {
          .name = "stats",
          .aliases = {},
          .summary = "the --summary and --histogram reductions",
          .see_also = kStatsSeeAlso,
      },
      {
          .name = "cookbook",
          .aliases = kCookbookAliases,
          .summary = "worked examples that compose xff end to end",
          .see_also = kCookbookSeeAlso,
          .in_full = true,
      },
      {
          .name = "notice",
          .aliases = kNoticeAliases,
          .summary = "third-party components + what this binary contains",
          .see_also = kNoticeSeeAlso,
      },
      {
          .name = "license",
          .aliases = kLicenseAliases,
          .summary = "xff's license in full (Apache-2.0); =COMPONENT for one component's",
          .see_also = kLicenseSeeAlso,
      },
      {
          .name = "full",
          .aliases = kFullAliases,
          .summary = "every option and primary, with the long explanations",
          .see_also = kHelpSeeAlso,
      },
  });
  return kTopics;
}

absl::Span<const HelpFlag> HelpFlags() {
  static constexpr auto kFlags = std::to_array<HelpFlag>({
      {.display = "-h, --help, -help", .summary = "print this usage page and exit (-help for GNU find compatibility)"},
      {.display = "--help=NAME", .summary = "full help for one option or primary (e.g. --help=-regex, --help=--sort)"},
      {.display = "--help=TOPIC", .summary = "detailed help for a topic:"},
      {
          .display = "--help=full",
          .summary = "full reference; also --help=long; select its renderer with --help-format",
      },
      {.display = "--help-full", .summary = "full reference (also --help-long); --help-all = --help=all"},
      {
          .display = "--man",
          .summary = "conventional alias for --help=full --help-format=roff; formatted on a terminal, else raw roff",
      },
      {.display = "--version, -version", .summary = "print the version and exit"},
  });
  return kFlags;
}

}  // namespace xff::cli
