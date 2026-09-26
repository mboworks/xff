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

#include "xff/parser/rg.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/parser/parser.h"

namespace xff::parser {
namespace {
constexpr auto kOptions = std::to_array<RgOption>({
    {
        .name = "regexp",
        .short_name = 'e',
        .argument = "PATTERN",
        .effect = RgOption::Effect::kPattern,
        .summary = "Add a search pattern; repeatable, combined as a union.",
    },
    {
        .name = "file",
        .short_name = 'f',
        .argument = "FILE",
        .effect = RgOption::Effect::kFile,
        .summary = "Read patterns from `FILE`, one per line; `-` reads stdin; repeatable.",
    },
    {
        .name = "glob",
        .short_name = 'g',
        .argument = "GLOB",
        .effect = RgOption::Effect::kGlob,
        .summary = "Include glob; a leading `!` excludes; last matching rule wins.",
    },
    {
        .name = "only-matching",
        .short_name = 'o',
        .replacement = "--only-matching",
        .summary = "Print only matched portions.",
    },
    {
        .name = "invert-match",
        .short_name = 'v',
        .replacement = "--invert-match",
        .summary = "Select nonmatching lines.",
    },
    {
        .name = "line-number",
        .short_name = 'n',
        .replacement = "--line-number",
        .summary = "Print line-number prefixes.",
    },
    {
        .name = "no-line-number",
        .short_name = 'N',
        .replacement = "--no-line-number",
        .summary = "Omit line-number prefixes (the rg default).",
    },
    {
        .name = "with-filename",
        .short_name = 'H',
        .replacement = "--with-filename",
        .summary = "Always print path prefixes.",
    },
    {
        .name = "no-filename",
        .short_name = 'I',
        .replacement = "--no-filename",
        .summary = "Omit path prefixes; otherwise automatic for multiple inputs and archive members.",
    },
    {
        .name = "files-with-matches",
        .short_name = 'l',
        .replacement = "--files-with-matches",
        .summary = "Print filenames with selected lines.",
    },
    {
        .name = "files-without-match",
        .replacement = "--files-without-match",
        .summary = "Print filenames without selected lines.",
    },
    {
        .name = "count",
        .short_name = 'c',
        .replacement = "--count",
        .summary = "Print selected line counts.",
    },
    {
        .name = "count-matches",
        .replacement = "--count-matches",
        .summary = "Print matched occurrence counts.",
    },
    {
        .name = "ignore-case",
        .short_name = 'i',
        .replacement = "--case=insensitive",
        .summary = "Match case-insensitively.",
    },
    {
        .name = "case-sensitive",
        .short_name = 's',
        .replacement = "--case=sensitive",
        .summary = "Match case-sensitively (the rg default).",
    },
    {
        .name = "smart-case",
        .short_name = 'S',
        .replacement = "--case=smart",
        .summary = "Ignore case unless the pattern contains an uppercase letter.",
    },
    {
        .name = "fixed-strings",
        .short_name = 'F',
        .replacement = "--regextype=EXACT",
        .summary = "Use literal matching.",
    },
    {
        .name = "pcre2",
        .short_name = 'P',
        .replacement = "--regextype=PCRE2",
        .summary = "Use the optional PCRE2 backend; RE2 is the default.",
    },
    {
        .name = "follow",
        .short_name = 'L',
        .replacement = "-L",
        .summary = "Follow symbolic links.",
    },
    {
        .name = "quiet",
        .short_name = 'q',
        .replacement = "--quiet",
        .summary = "Suppress output; preserve match-sensitive exit status.",
    },
    {
        .name = "context",
        .short_name = 'C',
        .replacement = "--context=",
        .argument = "N",
        .summary = "Print `N` context lines before and after each match.",
    },
    {
        .name = "before-context",
        .short_name = 'B',
        .replacement = "--before-context=",
        .argument = "N",
        .summary = "Print `N` lines before each match.",
    },
    {
        .name = "after-context",
        .short_name = 'A',
        .replacement = "--after-context=",
        .argument = "N",
        .summary = "Print `N` lines after each match.",
    },
    {
        .name = "threads",
        .short_name = 'j',
        .argument = "N",
        .effect = RgOption::Effect::kThreads,
        .summary = "Worker allowance; `0` selects automatic.",
    },
    {
        .name = "word-regexp",
        .short_name = 'w',
        .effect = RgOption::Effect::kWord,
        .summary = "Match whole words.",
    },
    {
        .name = "line-regexp",
        .short_name = 'x',
        .effect = RgOption::Effect::kLine,
        .summary = "Match whole lines.",
    },
    {
        .name = "text",
        .short_name = 'a',
        .effect = RgOption::Effect::kText,
        .summary = "Search binary content as text.",
    },
    {
        .name = "max-columns",
        .short_name = 'M',
        .argument = "N",
        .effect = RgOption::Effect::kColumns,
        .summary = "Replace output lines longer than `N` bytes with an omission marker; `0` disables the limit.",
    },
    {
        .name = "root",
        .argument = "NAME=PATH",
        .effect = RgOption::Effect::kRoot,
        .summary = "Add a named search root before `--xff`.",
    },
    {
        .name = "hidden",
        .replacement = "--hidden",
        .summary = "Include hidden entries.",
    },
    {
        .name = "no-hidden",
        .replacement = "--no-hidden",
        .summary = "Skip hidden entries.",
    },
    {
        .name = "no-ignore",
        .replacement = "--no-ignore",
        .summary = "Disable ignore-file filtering.",
    },
    {
        .name = "color",
        .replacement = "--color=",
        .argument = "WHEN",
        .summary = "Color policy: `auto`, `always`, or `never`.",
    },
    {
        .name = "help",
        .short_name = 'h',
        .replacement = "--help=rg",
        .summary = "Show rg help; `--help=TOPIC` selects another help topic.",
    },
    {
        .name = "version",
        .short_name = 'V',
        .replacement = "--version",
        .summary = "Print the program version.",
    },
});

struct RootOperand {
  std::string path;
  std::string name;
};

class RgParser {
 public:
  RgParser(const std::vector<std::string>& args, std::size_t start) : args_(args), index_(start) {}

  // The option grammar deliberately keeps mode transitions, positional roots,
  // and native expression parsing in one pass so their ordering is explicit.
  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  absl::StatusOr<Command> Parse() {
    for (; index_ < args_.size(); ++index_) {
      const std::string_view arg = args_.at(index_);
      if (options_ && arg == "--xff") {
        ++index_;
        break;
      }
      if (options_ && arg == "--") {
        options_ = false;
      } else if (options_ && arg.starts_with("--")) {
        MBO_RETURN_IF_ERROR(Long(arg.substr(2)));
      } else if (options_ && arg.size() > 1 && arg.front() == '-') {
        MBO_RETURN_IF_ERROR(Short(arg.substr(1)));
      } else {
        positionals_.push_back({.path = std::string(arg)});
      }
    }
    if (!explicit_patterns_) {
      const auto pattern =
          std::ranges::find_if(positionals_, [](const RootOperand& root) { return root.name.empty(); });
      if (pattern == positionals_.end()) {
        if (!help_) {
          return absl::InvalidArgumentError("--rg requires PATTERN or -e PATTERN / -f FILE");
        }
      } else {
        search_.patterns.push_back({.value = std::move(pattern->path)});
        positionals_.erase(pattern);
      }
    }
    // A sentinel separates leading globals from the native filter. The actual roots are
    // assigned directly, so rg paths that look like native operators stay literal paths.
    native_.emplace_back(".");
    native_.insert(native_.end(), args_.begin() + static_cast<std::ptrdiff_t>(index_), args_.end());
    MBO_ASSIGN_OR_RETURN(auto command, parser::Parse(native_));
    if (command.roots.size() != named_roots_ + 1) {
      return absl::InvalidArgumentError("--xff starts a filter expression; put search paths before it");
    }
    command.roots.clear();
    command.root_names.clear();
    command.roots.reserve(positionals_.size());
    command.root_names.reserve(positionals_.size());
    for (auto& root : positionals_) {
      command.roots.push_back(std::move(root.path));
      command.root_names.push_back(std::move(root.name));
    }
    if (std::ranges::find(command.roots, "-") != command.roots.end()
        && std::ranges::any_of(
            search_.patterns, [](const RgPattern& pattern) { return pattern.file && pattern.value == "-"; })) {
      return absl::InvalidArgumentError("--rg cannot read patterns and search content from stdin simultaneously");
    }
    command.rg.emplace(std::move(search_));
    return command;
  }

 private:
  absl::Status Apply(const RgOption& option, std::optional<std::string_view> attached) {
    std::string_view value;
    if (!option.argument.empty()) {
      if (attached.has_value()) {
        value = *attached;
      } else {
        if (++index_ == args_.size()) {
          return absl::InvalidArgumentError(absl::StrCat("--", option.name, " requires a value"));
        }
        value = args_.at(index_);
      }
    } else if (attached.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat("--", option.name, " does not take a value"));
    }
    switch (option.effect) {
      case RgOption::Effect::kGlobal:
        native_.push_back(absl::StrCat(option.replacement, value));
        help_ = help_ || option.short_name == 'h' || option.short_name == 'V';
        break;
      case RgOption::Effect::kPattern:
        search_.patterns.push_back({.value = std::string(value)});
        explicit_patterns_ = true;
        break;
      case RgOption::Effect::kFile:
        search_.patterns.push_back({.value = std::string(value), .file = true});
        explicit_patterns_ = true;
        break;
      case RgOption::Effect::kWord: search_.word = true; break;
      case RgOption::Effect::kLine: search_.line = true; break;
      case RgOption::Effect::kText: search_.text = true; break;
      case RgOption::Effect::kColumns:
        if (!absl::SimpleAtoi(value, &search_.max_columns)) {
          return absl::InvalidArgumentError("--max-columns requires a nonnegative integer");
        }
        break;
      case RgOption::Effect::kThreads: {
        std::size_t workers = 0;
        const bool automatic = absl::SimpleAtoi(value, &workers) && workers == 0;
        native_.push_back(absl::StrCat("--jobs=", automatic ? "all" : value));
        break;
      }
      case RgOption::Effect::kRoot: {
        const std::string token = absl::StrCat("--root=", value);
        // Reuse native root validation; the complete native parse below also rejects duplicate names.
        MBO_ASSIGN_OR_RETURN(auto root, parser::Parse({token}));
        positionals_.push_back({.path = std::move(root.roots.front()), .name = std::move(root.root_names.front())});
        native_.push_back(token);
        ++named_roots_;
        break;
      }
      case RgOption::Effect::kGlob:
        search_.globs.emplace_back(value);
        native_.push_back(
            absl::StrCat(
                value.starts_with("!") ? "--exclude=" : "--include=",
                value.starts_with("!") ? value.substr(1) : value));
        break;
    }
    return absl::OkStatus();
  }

  absl::Status Long(std::string_view arg) {
    const auto equals = arg.find('=');
    const auto name = arg.substr(0, equals);
    const std::optional<std::string_view> value =
        equals == std::string_view::npos ? std::nullopt : std::optional(arg.substr(equals + 1));
    if (name == "help" && value.has_value()) {
      help_ = true;
      native_.push_back(absl::StrCat("--", arg));
      return absl::OkStatus();
    }
    for (const auto& option : kOptions) {
      if (option.name == name) {
        return Apply(option, value);
      }
    }
    // Double-dash XFF globals keep their canonical spelling. Validation still happens
    // through the ordinary registry; unsupported rg shorts never become XFF aliases.
    if (name == "rg") {
      return absl::InvalidArgumentError("--rg may only select the grammar once");
    }
    native_.push_back(absl::StrCat("--", arg));
    return absl::OkStatus();
  }

  absl::Status Short(std::string_view arg) {
    for (std::size_t pos = 0; pos < arg.size(); ++pos) {
      bool found = false;
      for (const auto& option : kOptions) {
        if (option.short_name != '\0' && option.short_name == arg.at(pos)) {
          found = true;
          const auto rest = arg.substr(pos + 1);
          MBO_RETURN_IF_ERROR(
              Apply(option, !option.argument.empty() && !rest.empty() ? std::optional(rest) : std::nullopt));
          if (!option.argument.empty()) {
            return absl::OkStatus();
          }
          break;
        }
      }
      if (!found) {
        return absl::InvalidArgumentError(
            absl::StrCat("unsupported rg option -", arg.substr(pos, 1), "; use --xff before XFF expressions"));
      }
    }
    return absl::OkStatus();
  }

  const std::vector<std::string>& args_;
  std::size_t index_;
  bool options_ = true;
  bool explicit_patterns_ = false;
  bool help_ = false;
  RgSearch search_;
  std::vector<RootOperand> positionals_;
  std::size_t named_roots_ = 0;
  std::vector<std::string> native_{"--config=rg", "--match-output", "--exit-match"};
};
}  // namespace

std::span<const RgOption> RgOptions() {
  return kOptions;
}

absl::StatusOr<Command> ParseRg(const std::vector<std::string>& args, std::size_t start) {
  return RgParser(args, start).Parse();
}
}  // namespace xff::parser
