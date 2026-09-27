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

#include "xff/registry/compatibility.h"

#include <array>

namespace xff::registry {
namespace {
constexpr auto kOptions = std::to_array<CompatibilityOption>({
    {
        .name = "--regexp",
        .alias = "-e",
        .argument = "PATTERN",
        .effect = CompatibilityOption::Effect::kPattern,
        .summary = "Add a search pattern; repeatable, combined as a union.",
    },
    {
        .name = "--file",
        .alias = "-f",
        .argument = "FILE",
        .effect = CompatibilityOption::Effect::kFile,
        .summary = "Read patterns from `FILE`, one per line; `-` reads stdin; repeatable.",
    },
    {
        .name = "--glob",
        .alias = "-g",
        .argument = "GLOB",
        .effect = CompatibilityOption::Effect::kGlob,
        .summary = "Include glob; a leading `!` excludes; last matching rule wins.",
    },
    {
        .name = "--type",
        .alias = "-t",
        .replacement = "--file-type=",
        .argument = "TYPE",
        .summary = "Search files matching this type; repeatable; `all` selects every defined type.",
    },
    {
        .name = "--type-not",
        .alias = "-T",
        .replacement = "--file-type-not=",
        .argument = "TYPE",
        .summary = "Exclude files matching this type; later matching type selections win.",
    },
    {
        .name = "--type-add",
        .replacement = "--type-add=",
        .argument = "TYPE:GLOB",
        .summary = "Add a type glob; `TYPE:include:TYPES` imports comma-separated type definitions.",
    },
    {
        .name = "--type-clear",
        .replacement = "--type-clear=",
        .argument = "TYPE",
        .summary = "Remove the globs for a type before subsequent additions.",
    },
    {
        .name = "--type-list",
        .effect = CompatibilityOption::Effect::kTypeList,
        .summary = "List available type names and definitions without searching.",
    },
    {
        .name = "--multiline",
        .alias = "-U",
        .effect = CompatibilityOption::Effect::kMultiline,
        .summary = "Allow matches to span lines; retains whole input for cross-line regex evaluation.",
    },
    {
        .name = "--no-multiline",
        .effect = CompatibilityOption::Effect::kNoMultiline,
        .summary = "Search one line at a time.",
    },
    {
        .name = "--multiline-dotall",
        .effect = CompatibilityOption::Effect::kDotall,
        .summary = "Make `.` match newlines when multiline search is enabled.",
    },
    {
        .name = "--no-multiline-dotall",
        .effect = CompatibilityOption::Effect::kNoDotall,
        .summary = "Restore the default dot behavior.",
    },
    {
        .name = "--heading",
        .effect = CompatibilityOption::Effect::kHeading,
        .summary = "Print the path above each file's matches; enabled by default on a terminal.",
    },
    {
        .name = "--no-heading",
        .effect = CompatibilityOption::Effect::kNoHeading,
        .summary = "Use per-line filename prefixes; the default for piped output.",
    },
    {
        .name = "--column",
        .effect = CompatibilityOption::Effect::kColumn,
        .summary = "Print one-based byte columns and enable line numbers.",
    },
    {
        .name = "--no-column",
        .effect = CompatibilityOption::Effect::kNoColumn,
        .summary = "Omit byte columns.",
    },
    {
        .name = "--pretty",
        .alias = "-p",
        .effect = CompatibilityOption::Effect::kPretty,
        .summary = "Enable headings, line numbers, and color, even when output is piped.",
    },
    {
        .name = "--unicode",
        .effect = CompatibilityOption::Effect::kUnicode,
        .summary = "Interpret content as UTF-8 and use Unicode word boundaries (default); no encoding detection.",
    },
    {
        .name = "--no-unicode",
        .effect = CompatibilityOption::Effect::kNoUnicode,
        .summary = "Match 8-bit bytes with ASCII word boundaries; no UTF-8 decoding or legacy-codepage conversion.",
    },
    {
        .name = "--only-matching",
        .alias = "-o",
        .replacement = "--only-matching",
        .summary = "Print only matched portions.",
    },
    {
        .name = "--invert-match",
        .alias = "-v",
        .replacement = "--invert-match",
        .summary = "Select nonmatching lines.",
    },
    {
        .name = "--line-number",
        .alias = "-n",
        .replacement = "--line-number",
        .summary = "Print line-number prefixes.",
    },
    {
        .name = "--no-line-number",
        .alias = "-N",
        .replacement = "--no-line-number",
        .summary = "Omit line-number prefixes.",
    },
    {
        .name = "--with-filename",
        .alias = "-H",
        .replacement = "--with-filename",
        .summary = "Always print path prefixes.",
    },
    {
        .name = "--no-filename",
        .alias = "-I",
        .replacement = "--no-filename",
        .summary = "Omit path prefixes; otherwise automatic for multiple inputs and archive members.",
    },
    {
        .name = "--files-with-matches",
        .alias = "-l",
        .replacement = "--files-with-matches",
        .summary = "Print filenames with selected lines.",
    },
    {
        .name = "--files-without-match",
        .replacement = "--files-without-match",
        .summary = "Print filenames without selected lines.",
    },
    {
        .name = "--count",
        .alias = "-c",
        .replacement = "--count",
        .summary = "Print selected line counts.",
    },
    {
        .name = "--count-matches",
        .replacement = "--count-matches",
        .summary = "Print matched occurrence counts.",
    },
    {
        .name = "--ignore-case",
        .alias = "-i",
        .replacement = "--case=insensitive",
        .summary = "Match case-insensitively.",
    },
    {
        .name = "--case-sensitive",
        .alias = "-s",
        .replacement = "--case=sensitive",
        .summary = "Match case-sensitively (the rg default).",
    },
    {
        .name = "--smart-case",
        .alias = "-S",
        .replacement = "--case=smart",
        .summary = "Ignore case unless the pattern contains an uppercase letter.",
    },
    {
        .name = "--fixed-strings",
        .alias = "-F",
        .replacement = "--regextype=EXACT",
        .summary = "Use literal matching.",
    },
    {
        .name = "--pcre2",
        .alias = "-P",
        .replacement = "--regextype=PCRE2",
        .summary = "Use the optional PCRE2 backend; RE2 is the default.",
    },
    {
        .name = "--follow",
        .alias = "-L",
        .replacement = "-L",
        .summary = "Follow symbolic links.",
    },
    {
        .name = "--quiet",
        .alias = "-q",
        .replacement = "--quiet",
        .summary = "Suppress output; preserve match-sensitive exit status.",
    },
    {
        .name = "--context",
        .alias = "-C",
        .replacement = "--context=",
        .argument = "N",
        .summary = "Print `N` context lines before and after each match.",
    },
    {
        .name = "--before-context",
        .alias = "-B",
        .replacement = "--before-context=",
        .argument = "N",
        .summary = "Print `N` lines before each match.",
    },
    {
        .name = "--after-context",
        .alias = "-A",
        .replacement = "--after-context=",
        .argument = "N",
        .summary = "Print `N` lines after each match.",
    },
    {
        .name = "--threads",
        .alias = "-j",
        .argument = "N",
        .effect = CompatibilityOption::Effect::kThreads,
        .summary = "Worker allowance; `0` selects automatic.",
    },
    {
        .name = "--word-regexp",
        .alias = "-w",
        .effect = CompatibilityOption::Effect::kWord,
        .summary = "Match whole words.",
    },
    {
        .name = "--line-regexp",
        .alias = "-x",
        .effect = CompatibilityOption::Effect::kLine,
        .summary = "Match whole lines.",
    },
    {
        .name = "--text",
        .alias = "-a",
        .effect = CompatibilityOption::Effect::kText,
        .summary = "Search binary content as text.",
    },
    {
        .name = "--max-columns",
        .alias = "-M",
        .argument = "N",
        .effect = CompatibilityOption::Effect::kColumns,
        .summary = "Replace output lines longer than `N` bytes with an omission marker; `0` disables the limit.",
    },
    {
        .name = "--root",
        .argument = "NAME=PATH",
        .effect = CompatibilityOption::Effect::kRoot,
        .summary = "Add a named search root before `--xff`.",
    },
    {
        .name = "--hidden",
        .replacement = "--hidden",
        .summary = "Include hidden entries.",
    },
    {
        .name = "--no-hidden",
        .replacement = "--no-hidden",
        .summary = "Skip hidden entries.",
    },
    {
        .name = "--no-ignore",
        .replacement = "--no-ignore",
        .summary = "Disable ignore-file filtering.",
    },
    {
        .name = "--color",
        .replacement = "--color=",
        .argument = "WHEN",
        .summary = "Color policy: `auto`, `always`, or `never`.",
    },
    {
        .name = "--help",
        .alias = "-h",
        .replacement = "--help=rg",
        .summary = "Show rg help; `--help=TOPIC` selects another help topic.",
    },
    {
        .name = "--version",
        .alias = "-V",
        .replacement = "--version",
        .summary = "Print the program version.",
    },
});

static_assert(
    [] {
      for (std::size_t first = 0; first < kOptions.size(); ++first) {
        for (std::size_t second = first + 1; second < kOptions.size(); ++second) {
          const auto& lhs = kOptions.at(first);
          const auto& rhs = kOptions.at(second);
          if (Overlaps(lhs.modes, rhs.modes)
              && (lhs.name == rhs.name || lhs.name == rhs.alias || lhs.alias == rhs.name
                  || (!lhs.alias.empty() && lhs.alias == rhs.alias))) {
            return false;
          }
        }
      }
      return true;
    }(),
    "compatibility spellings must be unique within each mode");
}  // namespace

std::span<const CompatibilityOption> CompatibilityOptions() {
  return kOptions;
}

mbo::types::OptionalRef<const CompatibilityOption> LookupCompatibilityOption(std::string_view name, Mode mode) {
  for (const auto& option : kOptions) {
    if (Supports(option.modes, mode) && (option.name == name || (!option.alias.empty() && option.alias == name))) {
      return option;
    }
  }
  return std::nullopt;
}
}  // namespace xff::registry
