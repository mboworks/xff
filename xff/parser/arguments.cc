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

#include "xff/parser/arguments.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "mbo/container/limited_set.h"
#include "mbo/status/status_macros.h"
#include "xff/cli/globals.h"
#include "xff/parser/parser.h"
#include "xff/registry/compatibility.h"
#include "xff/registry/registry.h"

namespace xff::parser {
namespace {

bool StartsExpression(std::string_view arg) {
  if (arg.empty()) {
    return false;
  }
  if (arg[0] == '-') {
    return true;
  }
  return arg == "(" || arg == ")" || arg == "!" || arg == "," || arg == "+";
}

// A whole-run global written with a double dash (`--summary=ext`, `--sort`, `--top=10`). Every
// expression primary and operator is single-dash, so a `--`-prefixed token is unambiguously a global
// wherever it appears (after the roots, in the expression, at the tail) -- never a primary. That lets
// the parser hoist it out of any primary/operator position (but NOT out of a primary's argument run,
// e.g. an -exec command or a -printf format, consumed together with their primary). Bare `--`
// (exactly two dashes) is the end-of-options delimiter, not a global.
bool IsHoistableGlobal(std::string_view arg) {
  return arg.size() > 2 && arg[0] == '-' && arg[1] == '-';
}

// Meta flags are position-independent like double-dash globals, plus the GNU
// find-compatible single-dash spellings. Keeping this vocabulary in the parser
// is what prevents a lookalike inside `-exec`'s raw argument run from being
// mistaken for an xff request.
constexpr bool IsMetaFlag(std::string_view arg) {
  constexpr auto kMetaFlags = mbo::container::MakeLimitedSet<9>(std::initializer_list<std::string_view>{
      "--help",
      "-h",
      "-help",
      "--help-all",
      "--help-full",
      "--help-long",
      "--version",
      "-version",
      "--man",
  });
  return kMetaFlags.contains(arg) || absl::StartsWith(arg, "--help=");
}

absl::Status AppendNamedRoot(Command& command, std::string_view token) {
  constexpr std::string_view kPrefix = "--root=";
  const auto value = token.starts_with(kPrefix) ? token.substr(kPrefix.size()) : std::string_view();
  const auto equals = value.find('=');
  if (equals == std::string_view::npos || equals == 0 || equals + 1 == value.size()) {
    return absl::InvalidArgumentError("--root requires NAME=PATH with a non-empty name and path");
  }
  const auto name = value.substr(0, equals);
  if (name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos
      || absl::c_any_of(name, [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; })) {
    return absl::InvalidArgumentError("--root name must be a single directory component without control characters");
  }
  if (absl::c_linear_search(command.root_names, name)) {
    return absl::InvalidArgumentError(absl::StrCat("duplicate root name '", name, "'"));
  }
  command.roots.emplace_back(value.substr(equals + 1));
  command.root_names.emplace_back(name);
  return absl::OkStatus();
}

// The global pass has one active grammar. Descriptor arity protects native
// operands; compatibility declarations protect rg values. Neither pass searches
// ahead for selectors or reparses another grammar to see whether it fits.
class ArgumentParser {
 public:
  ArgumentParser(const std::vector<std::string>& args, std::size_t start, registry::Mode mode)
      : args_(args), index_(start) {
    SelectMode(mode);
    result_.expression.reserve(args.size() - start);
  }

  absl::StatusOr<ParsedArguments> Parse() {
    for (; index_ < args_.size(); ++index_) {
      MBO_RETURN_IF_ERROR(Consume(args_.at(index_)));
    }
    MBO_RETURN_IF_ERROR(FinishSearch());
    result_.options_ended = !options_;
    return std::move(result_);
  }

 private:
  enum class Phase { kGlobals, kRoots, kExpression };

  absl::Status Consume(std::string_view arg) {
    if (options_ && arg == "--") {
      options_ = false;
      if (phase_ == Phase::kGlobals) {
        phase_ = Phase::kRoots;
      }
      return absl::OkStatus();
    }
    if (options_ && IsHoistableGlobal(arg)) {
      const auto equals = arg.find('=');
      const auto flag = cli::LookupGlobal(arg.substr(0, equals), mode_);
      if (flag.has_value() && flag->enters_mode.has_value()) {
        if (equals != std::string_view::npos) {
          return absl::InvalidArgumentError("--rg and --xff do not take values");
        }
        SelectMode(*flag->enters_mode);
        return absl::OkStatus();
      }
    }
    if (mode_ != registry::Mode::kRg) {
      return Native(arg);
    }
    if (options_ && arg.starts_with("--")) {
      return Long(arg.substr(2));
    }
    if (options_ && arg.size() > 1 && arg.front() == '-') {
      return Short(arg.substr(1));
    }
    if (!pattern_root_.has_value()) {
      pattern_root_ = result_.command.roots.size();
    }
    result_.command.roots.emplace_back(arg);
    result_.command.root_names.emplace_back();
    return absl::OkStatus();
  }

  void SelectMode(registry::Mode mode) {
    mode_ = mode;
    if (mode == registry::Mode::kRg) {
      if (!rg_selected_) {
        result_.command.globals.insert(
            result_.command.globals.end(), {"--config=rg", "--match-output", "--exit-match"});
        rg_selected_ = true;
      }
      phase_ = Phase::kExpression;
    }
  }

  absl::Status FinishSearch() {
    if (!rg_selected_) {
      return absl::OkStatus();
    }
    if (!explicit_patterns_) {
      if (!pattern_root_.has_value()) {
        if (!help_ && !search_.type_list) {
          return absl::InvalidArgumentError("--rg requires PATTERN or -e PATTERN / -f FILE");
        }
      } else {
        const auto index = static_cast<std::ptrdiff_t>(*pattern_root_);
        search_.patterns.push_back({.value = std::move(result_.command.roots.at(*pattern_root_))});
        result_.command.roots.erase(result_.command.roots.begin() + index);
        result_.command.root_names.erase(result_.command.root_names.begin() + index);
      }
    }
    if (std::ranges::contains(result_.command.roots, "-")
        && std::ranges::any_of(
            search_.patterns, [](const RgPattern& pattern) { return pattern.file && pattern.value == "-"; })) {
      return absl::InvalidArgumentError("--rg cannot read patterns and search content from stdin simultaneously");
    }
    result_.command.rg.emplace(std::move(search_));
    return absl::OkStatus();
  }

  absl::Status NativeGlobal(std::string_view arg) {
    if (IsMetaFlag(arg)) {
      help_ = true;
      result_.command.meta_flags.emplace_back(arg);
      return absl::OkStatus();
    }
    const auto flag = cli::LookupGlobal(arg.substr(0, arg.find('=')), mode_);
    if (flag.has_value() && flag->adds_root) {
      if (rg_selected_) {
        return absl::InvalidArgumentError("--xff starts a filter expression; put search paths before it");
      }
      MBO_RETURN_IF_ERROR(AppendNamedRoot(result_.command, arg));
      if (phase_ == Phase::kGlobals) {
        phase_ = Phase::kRoots;
      }
    }
    result_.command.globals.emplace_back(arg);
    return absl::OkStatus();
  }

  absl::Status Native(std::string_view arg) {
    if (phase_ == Phase::kGlobals) {
      const auto flag = !arg.empty() && arg.front() == '-' ? cli::LookupGlobalArgument(arg, mode_) : std::nullopt;
      if (flag.has_value() && !flag->alias_argument.empty() && arg.starts_with(flag->alias)) {
        const auto next =
            index_ + 1 < args_.size() ? std::optional<std::string_view>(args_.at(index_ + 1)) : std::nullopt;
        MBO_ASSIGN_OR_RETURN(auto alias, cli::ParseGlobalAliasArgument(*flag, arg, next));
        result_.command.globals.push_back(std::move(alias.token));
        index_ += static_cast<std::size_t>(alias.consumes_next);
        return absl::OkStatus();
      }
      if (!arg.empty() && (arg.front() == '-' || arg.front() == '+')) {
        return NativeGlobal(arg);
      }
      phase_ = Phase::kRoots;
    }
    if (options_ && (IsHoistableGlobal(arg) || IsMetaFlag(arg))) {
      return NativeGlobal(arg);
    }
    if (phase_ == Phase::kRoots && !StartsExpression(arg)) {
      result_.command.roots.emplace_back(arg);
      result_.command.root_names.emplace_back();
      return absl::OkStatus();
    }
    phase_ = Phase::kExpression;
    if (rg_selected_ && !StartsExpression(arg)) {
      return absl::InvalidArgumentError("--xff starts a filter expression; put search paths before it");
    }
    NativeToken(arg);
    return absl::OkStatus();
  }

  // Copy each primary and its complete argument run together. Mode-looking values
  // therefore remain literal, including command arguments and attached bindings.
  void NativeToken(std::string_view arg) {
    const auto descriptor = registry::Lookup(arg.substr(0, arg.find(':')), mode_);
    result_.expression.push_back({.text = arg, .descriptor = descriptor});
    if (!descriptor.has_value()) {
      return;
    }
    const bool capture = descriptor->binding == registry::Binding::kLabelRegex;
    if (capture || descriptor->arity < 0) {
      while (index_ + 1 < args_.size()) {
        const auto& operand = args_.at(++index_);
        result_.expression.push_back({.text = operand});
        if (operand == ";" || (!capture && operand == "+")) {
          break;
        }
      }
      return;
    }
    const auto next = index_ + 1 < args_.size() ? std::string_view(args_.at(index_ + 1)) : std::string_view{};
    for (int count = descriptor->ArgumentCount(next); count > 0 && index_ + 1 < args_.size(); --count) {
      result_.expression.push_back({.text = args_.at(++index_)});
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
      case registry::CompatibilityOption::Effect::kMeta:
        result_.command.meta_flags.push_back(
            absl::StrCat(option.target, option.fixed_value.has_value() ? "=" : "", option.fixed_value.value_or("")));
        help_ = true;
        break;
      case registry::CompatibilityOption::Effect::kGlobal:
        result_.command.globals.push_back(
            absl::StrCat(
                option.target, option.fixed_value.has_value() || !option.argument.empty() ? "=" : "",
                option.fixed_value.value_or(value)));
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
        result_.command.globals.emplace_back("--type-list");
        break;
      case registry::CompatibilityOption::Effect::kMultiline: search_.multiline = true; break;
      case registry::CompatibilityOption::Effect::kNoMultiline: search_.multiline = false; break;
      case registry::CompatibilityOption::Effect::kDotall: search_.dotall = true; break;
      case registry::CompatibilityOption::Effect::kNoDotall: search_.dotall = false; break;
      case registry::CompatibilityOption::Effect::kHeading: search_.heading = true; break;
      case registry::CompatibilityOption::Effect::kNoHeading: search_.heading = false; break;
      case registry::CompatibilityOption::Effect::kColumn:
        search_.column = true;
        result_.command.globals.emplace_back("--line-number");
        break;
      case registry::CompatibilityOption::Effect::kNoColumn: search_.column = false; break;
      case registry::CompatibilityOption::Effect::kPretty:
        search_.heading = true;
        result_.command.globals.insert(result_.command.globals.end(), {"--line-number", "--color=always"});
        break;
      case registry::CompatibilityOption::Effect::kColumns:
        if (!absl::SimpleAtoi(value, &search_.max_columns)) {
          return absl::InvalidArgumentError("--max-columns requires a nonnegative integer");
        }
        break;
      case registry::CompatibilityOption::Effect::kThreads: {
        std::size_t workers = 0;
        const bool automatic = absl::SimpleAtoi(value, &workers) && workers == 0;
        result_.command.globals.push_back(absl::StrCat("--jobs=", automatic ? "all" : value));
        break;
      }
      case registry::CompatibilityOption::Effect::kRoot: {
        const std::string token = absl::StrCat("--root=", value);
        MBO_RETURN_IF_ERROR(AppendNamedRoot(result_.command, token));
        result_.command.globals.push_back(token);
        break;
      }
      case registry::CompatibilityOption::Effect::kGlob:
        search_.globs.emplace_back(value);
        result_.command.globals.push_back(
            absl::StrCat(
                value.starts_with('!') ? "--exclude=" : "--include=",
                value.starts_with('!') ? value.substr(1) : value));
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
      result_.command.meta_flags.push_back(absl::StrCat("--", arg));
      return absl::OkStatus();
    }
    if (const auto option = cli::LookupCompatibilityOption(absl::StrCat("--", name), registry::Mode::kRg);
        option.has_value()) {
      return Apply(*option, value);
    }
    // Double-dash XFF globals keep their canonical spelling. Validation still happens
    // through the ordinary registry; unsupported rg shorts never become XFF aliases.
    const std::string token = absl::StrCat("--", arg);
    if (!cli::LookupGlobalArgument(token, registry::Mode::kRg).has_value() && cli::IsKnownGlobal(token)) {
      return absl::InvalidArgumentError(absl::StrCat(token, " is not available in rg mode; use --xff first"));
    }
    result_.command.globals.push_back(token);
    return absl::OkStatus();
  }

  absl::Status Short(std::string_view arg) {
    for (std::size_t pos = 0; pos < arg.size(); ++pos) {
      const auto option = cli::LookupCompatibilityOption(absl::StrCat("-", arg.substr(pos, 1)), registry::Mode::kRg);
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
  registry::Mode mode_ = registry::Mode::kXff;
  Phase phase_ = Phase::kGlobals;
  bool rg_selected_ = false;
  bool explicit_patterns_ = false;
  bool help_ = false;
  std::optional<std::size_t> pattern_root_;
  RgSearch search_;
  ParsedArguments result_;
};
}  // namespace

absl::StatusOr<ParsedArguments> ParseArguments(
    const std::vector<std::string>& args,
    std::size_t start,
    registry::Mode mode) {
  if (start > args.size()) {
    return absl::InvalidArgumentError("argument offset exceeds argv length");
  }
  return ArgumentParser(args, start, mode).Parse();
}

}  // namespace xff::parser
