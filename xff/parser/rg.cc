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

#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/parser/parser.h"

namespace xff::parser {
namespace {
enum class Effect { kGlobal, kPattern, kFile, kWord, kLine, kText, kColumns, kGlob };

struct Option {
  std::string_view name;
  char short_name = '\0';
  std::string_view replacement;
  bool value = false;
  Effect effect = Effect::kGlobal;
};

constexpr auto kOptions = std::to_array<Option>({
    {
        .name = "regexp",
        .short_name = 'e',
        .value = true,
        .effect = Effect::kPattern,
    },
    {
        .name = "file",
        .short_name = 'f',
        .value = true,
        .effect = Effect::kFile,
    },
    {
        .name = "glob",
        .short_name = 'g',
        .value = true,
        .effect = Effect::kGlob,
    },
    {
        .name = "only-matching",
        .short_name = 'o',
        .replacement = "--only-matching",
    },
    {
        .name = "invert-match",
        .short_name = 'v',
        .replacement = "--invert-match",
    },
    {
        .name = "line-number",
        .short_name = 'n',
        .replacement = "--line-number",
    },
    {
        .name = "no-line-number",
        .short_name = 'N',
        .replacement = "--no-line-number",
    },
    {
        .name = "with-filename",
        .short_name = 'H',
        .replacement = "--with-filename",
    },
    {
        .name = "no-filename",
        .short_name = 'I',
        .replacement = "--no-filename",
    },
    {
        .name = "files-with-matches",
        .short_name = 'l',
        .replacement = "--files-with-matches",
    },
    {
        .name = "files-without-match",
        .replacement = "--files-without-match",
    },
    {
        .name = "count",
        .short_name = 'c',
        .replacement = "--count",
    },
    {
        .name = "count-matches",
        .replacement = "--count-matches",
    },
    {
        .name = "ignore-case",
        .short_name = 'i',
        .replacement = "--case=insensitive",
    },
    {
        .name = "case-sensitive",
        .short_name = 's',
        .replacement = "--case=sensitive",
    },
    {
        .name = "smart-case",
        .short_name = 'S',
        .replacement = "--case=smart",
    },
    {
        .name = "fixed-strings",
        .short_name = 'F',
        .replacement = "--regextype=EXACT",
    },
    {
        .name = "pcre2",
        .short_name = 'P',
        .replacement = "--regextype=PCRE2",
    },
    {
        .name = "follow",
        .short_name = 'L',
        .replacement = "-L",
    },
    {
        .name = "quiet",
        .short_name = 'q',
        .replacement = "--quiet",
    },
    {
        .name = "context",
        .short_name = 'C',
        .replacement = "--context=",
        .value = true,
    },
    {
        .name = "before-context",
        .short_name = 'B',
        .replacement = "--before-context=",
        .value = true,
    },
    {
        .name = "after-context",
        .short_name = 'A',
        .replacement = "--after-context=",
        .value = true,
    },
    {
        .name = "threads",
        .short_name = 'j',
        .replacement = "--jobs=",
        .value = true,
    },
    {
        .name = "word-regexp",
        .short_name = 'w',
        .effect = Effect::kWord,
    },
    {
        .name = "line-regexp",
        .short_name = 'x',
        .effect = Effect::kLine,
    },
    {
        .name = "text",
        .short_name = 'a',
        .effect = Effect::kText,
    },
    {
        .name = "max-columns",
        .short_name = 'M',
        .value = true,
        .effect = Effect::kColumns,
    },
    {
        .name = "hidden",
        .replacement = "--hidden",
    },
    {
        .name = "no-hidden",
        .replacement = "--no-hidden",
    },
    {
        .name = "no-ignore",
        .replacement = "--no-ignore",
    },
    {
        .name = "color",
        .replacement = "--color=",
        .value = true,
    },
    {
        .name = "help",
        .short_name = 'h',
        .replacement = "--help=rg",
    },
    {.name = "version", .short_name = 'V', .replacement = "--version"},
});

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
        positionals_.emplace_back(arg);
      }
    }
    if (!explicit_patterns_) {
      if (positionals_.empty()) {
        if (!help_) {
          return absl::InvalidArgumentError("--rg requires PATTERN or -e PATTERN / -f FILE");
        }
      } else {
        search_.patterns.push_back({.value = positionals_.front()});
        positionals_.erase(positionals_.begin());
      }
    }
    // A sentinel separates leading globals from the native filter. The actual roots are
    // assigned directly, so rg paths that look like native operators stay literal paths.
    native_.emplace_back(".");
    native_.insert(native_.end(), args_.begin() + static_cast<std::ptrdiff_t>(index_), args_.end());
    MBO_ASSIGN_OR_RETURN(auto command, parser::Parse(native_));
    if (command.roots.size() != 1) {
      return absl::InvalidArgumentError("--xff starts a filter expression; put search paths before it");
    }
    command.roots = std::move(positionals_);
    command.root_names.assign(command.roots.size(), "");
    command.rg.emplace(std::move(search_));
    return command;
  }

 private:
  absl::Status Apply(const Option& option, std::optional<std::string_view> attached) {
    std::string_view value;
    if (option.value) {
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
      case Effect::kGlobal:
        native_.push_back(absl::StrCat(option.replacement, value));
        help_ = help_ || option.short_name == 'h' || option.short_name == 'V';
        break;
      case Effect::kPattern:
        search_.patterns.push_back({.value = std::string(value)});
        explicit_patterns_ = true;
        break;
      case Effect::kFile:
        search_.patterns.push_back({.value = std::string(value), .file = true});
        explicit_patterns_ = true;
        break;
      case Effect::kWord: search_.word = true; break;
      case Effect::kLine: search_.line = true; break;
      case Effect::kText: search_.text = true; break;
      case Effect::kColumns:
        if (!absl::SimpleAtoi(value, &search_.max_columns)) {
          return absl::InvalidArgumentError("--max-columns requires a nonnegative integer");
        }
        break;
      case Effect::kGlob:
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
          MBO_RETURN_IF_ERROR(Apply(option, option.value && !rest.empty() ? std::optional(rest) : std::nullopt));
          if (option.value) {
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
  std::vector<std::string> positionals_;
  std::vector<std::string> native_{"--config=rg", "--match-output", "--exit-match"};
};
}  // namespace

absl::StatusOr<Command> ParseRg(const std::vector<std::string>& args, std::size_t start) {
  return RgParser(args, start).Parse();
}
}  // namespace xff::parser
