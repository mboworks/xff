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
#include "xff/cli/globals.h"
#include "xff/parser/parser.h"
#include "xff/registry/compatibility.h"
#include "xff/registry/registry.h"

namespace xff::parser {
namespace {

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
      if (options_) {
        const auto flag = cli::LookupGlobal(arg, mode_);
        if (flag.has_value() && flag->enters_mode.has_value()) {
          mode_ = *flag->enters_mode;
          continue;
        }
      }
      if (mode_ == registry::Mode::kXff) {
        NativeToken(arg);
        continue;
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
        if (!help_ && !search_.type_list) {
          return absl::InvalidArgumentError("--rg requires PATTERN or -e PATTERN / -f FILE");
        }
      } else {
        search_.patterns.push_back({.value = std::move(pattern->path)});
        positionals_.erase(pattern);
      }
    }
    // A sentinel separates leading globals from the native filter. The actual roots are
    // assigned directly, so rg paths that look like native operators stay literal paths.
    StartNative();
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
  void StartNative() {
    if (!native_started_) {
      native_.emplace_back(".");
      native_started_ = true;
    }
  }

  // Copy each primary and its complete argument run together. Mode-looking values
  // therefore remain literal, including command arguments and attached bindings.
  void NativeToken(std::string_view arg) {
    StartNative();
    native_.emplace_back(arg);
    const auto descriptor = registry::Lookup(arg.substr(0, arg.find(':')), mode_);
    if (!descriptor.has_value()) {
      return;
    }
    const bool capture = descriptor->binding == registry::Binding::kLabelRegex;
    if (capture || descriptor->arity < 0) {
      while (index_ + 1 < args_.size()) {
        const auto& operand = args_.at(++index_);
        native_.push_back(operand);
        if (operand == ";" || (!capture && operand == "+")) {
          break;
        }
      }
      return;
    }
    const auto next = index_ + 1 < args_.size() ? std::string_view(args_.at(index_ + 1)) : std::string_view{};
    for (int count = descriptor->ArgumentCount(next); count > 0 && index_ + 1 < args_.size(); --count) {
      native_.push_back(args_.at(++index_));
    }
  }

  absl::Status Apply(const registry::CompatibilityOption& option, std::optional<std::string_view> attached) {
    std::string_view value;
    if (!option.argument.empty()) {
      if (attached.has_value()) {
        value = *attached;
      } else {
        if (++index_ == args_.size()) {
          return absl::InvalidArgumentError(absl::StrCat(option.name, " requires a value"));
        }
        value = args_.at(index_);
      }
    } else if (attached.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat(option.name, " does not take a value"));
    }
    switch (option.effect) {
      case registry::CompatibilityOption::Effect::kGlobal:
        native_.push_back(absl::StrCat(option.replacement, value));
        help_ = help_ || option.replacement == "--help=rg" || option.replacement == "--version";
        break;
      case registry::CompatibilityOption::Effect::kPattern:
        search_.patterns.push_back({.value = std::string(value)});
        explicit_patterns_ = true;
        break;
      case registry::CompatibilityOption::Effect::kFile:
        search_.patterns.push_back({.value = std::string(value), .file = true});
        explicit_patterns_ = true;
        break;
      case registry::CompatibilityOption::Effect::kWord:
        search_.word = true;
        search_.line = false;
        break;
      case registry::CompatibilityOption::Effect::kLine:
        search_.line = true;
        search_.word = false;
        break;
      case registry::CompatibilityOption::Effect::kText: search_.text = true; break;
      case registry::CompatibilityOption::Effect::kUnicode:
        search_.unicode = true;
        search_.unicode_explicit = true;
        break;
      case registry::CompatibilityOption::Effect::kNoUnicode:
        search_.unicode = false;
        search_.unicode_explicit = true;
        break;
      case registry::CompatibilityOption::Effect::kTypeList:
        search_.type_list = true;
        native_.emplace_back("--type-list");
        break;
      case registry::CompatibilityOption::Effect::kMultiline: search_.multiline = true; break;
      case registry::CompatibilityOption::Effect::kNoMultiline: search_.multiline = false; break;
      case registry::CompatibilityOption::Effect::kDotall: search_.dotall = true; break;
      case registry::CompatibilityOption::Effect::kNoDotall: search_.dotall = false; break;
      case registry::CompatibilityOption::Effect::kHeading: search_.heading = true; break;
      case registry::CompatibilityOption::Effect::kNoHeading: search_.heading = false; break;
      case registry::CompatibilityOption::Effect::kColumn:
        search_.column = true;
        native_.emplace_back("--line-number");
        break;
      case registry::CompatibilityOption::Effect::kNoColumn: search_.column = false; break;
      case registry::CompatibilityOption::Effect::kPretty:
        search_.heading = true;
        native_.insert(native_.end(), {"--line-number", "--color=always"});
        break;
      case registry::CompatibilityOption::Effect::kColumns:
        if (!absl::SimpleAtoi(value, &search_.max_columns)) {
          return absl::InvalidArgumentError("--max-columns requires a nonnegative integer");
        }
        break;
      case registry::CompatibilityOption::Effect::kThreads: {
        std::size_t workers = 0;
        const bool automatic = absl::SimpleAtoi(value, &workers) && workers == 0;
        native_.push_back(absl::StrCat("--jobs=", automatic ? "all" : value));
        break;
      }
      case registry::CompatibilityOption::Effect::kRoot: {
        const std::string token = absl::StrCat("--root=", value);
        // Reuse native root validation; the complete native parse below also rejects duplicate names.
        MBO_ASSIGN_OR_RETURN(auto root, parser::Parse({token}));
        positionals_.push_back({.path = std::move(root.roots.front()), .name = std::move(root.root_names.front())});
        native_.push_back(token);
        ++named_roots_;
        break;
      }
      case registry::CompatibilityOption::Effect::kGlob:
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
    if (const auto option = registry::LookupCompatibilityOption(absl::StrCat("--", name), registry::Mode::kRg);
        option.has_value()) {
      return Apply(*option, value);
    }
    // Double-dash XFF globals keep their canonical spelling. Validation still happens
    // through the ordinary registry; unsupported rg shorts never become XFF aliases.
    const std::string token = absl::StrCat("--", arg);
    if (!cli::LookupGlobalArgument(token, registry::Mode::kRg).has_value() && cli::IsKnownGlobal(token)) {
      return absl::InvalidArgumentError(absl::StrCat(token, " is not available in rg mode; use --xff first"));
    }
    native_.push_back(token);
    return absl::OkStatus();
  }

  absl::Status Short(std::string_view arg) {
    for (std::size_t pos = 0; pos < arg.size(); ++pos) {
      const auto option =
          registry::LookupCompatibilityOption(absl::StrCat("-", arg.substr(pos, 1)), registry::Mode::kRg);
      if (!option.has_value()) {
        return absl::InvalidArgumentError(
            absl::StrCat("unsupported rg option -", arg.substr(pos, 1), "; use --xff before XFF expressions"));
      }
      const auto rest = arg.substr(pos + 1);
      MBO_RETURN_IF_ERROR(
          Apply(*option, !option->argument.empty() && !rest.empty() ? std::optional(rest) : std::nullopt));
      if (!option->argument.empty()) {
        return absl::OkStatus();
      }
    }
    return absl::OkStatus();
  }

  const std::vector<std::string>& args_;
  std::size_t index_;
  bool options_ = true;
  registry::Mode mode_ = registry::Mode::kRg;
  bool native_started_ = false;
  bool explicit_patterns_ = false;
  bool help_ = false;
  RgSearch search_;
  std::vector<RootOperand> positionals_;
  std::size_t named_roots_ = 0;
  std::vector<std::string> native_{"--config=rg", "--match-output", "--exit-match"};
};
}  // namespace

absl::StatusOr<Command> ParseRg(const std::vector<std::string>& args, std::size_t start) {
  return RgParser(args, start).Parse();
}
}  // namespace xff::parser
