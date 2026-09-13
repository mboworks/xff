// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/config_validation.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "mbo/types/optional_ref.h"
#include "xff/cli/globals.h"
#include "xff/config/config.h"
#include "xff/config/xffrc.h"
#include "xff/parser/parser.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

bool IsDirectoryRoot(std::string_view token) {
  const auto name = token.substr(0, token.find('='));
  return name == "--temp-root" || name == "--output-root";
}

bool IsSystemControl(std::string_view token) {
  return IsDirectoryRoot(token) || token == "--no-require-system-config" || token == "--require-system-config"
         || token == "--no-require-user-config" || token == "--require-user-config" || token == "--allow-xffrc"
         || token == "--no-allow-xffrc" || token == "--allow-rc-globals" || token == "--no-allow-rc-globals"
         || token == "--detailed-block-policy" || token.starts_with("--detailed-block-policy=");
}

absl::StatusOr<std::size_t> PrimaryArgumentCount(
    const std::vector<std::string>& tokens,
    std::size_t pos,
    const registry::Descriptor& primary) {
  if (primary.arity < 0) {
    for (std::size_t end = pos + 1; end < tokens.size(); ++end) {
      if (tokens[end] == ";" || tokens[end] == "+") {
        return end - pos;
      }
    }
    return absl::InvalidArgumentError(absl::StrCat("'", tokens[pos], "' requires a terminating ';' or '+'"));
  }
  const auto arity = static_cast<std::size_t>(primary.arity);
  if (arity > tokens.size() - pos - 1) {
    return absl::InvalidArgumentError(absl::StrCat("'", tokens[pos], "' requires ", arity, " argument(s)"));
  }
  return arity;
}

absl::StatusOr<absl::flat_hash_set<std::string>> ValidateControls(
    const config::IniLine& line,
    absl::flat_hash_set<std::string> controls) {
  for (const std::string_view token : config::DirectiveTokens(line.tokens)) {
    const std::string name = token == "--no-allow-rc-globals" ? "--allow-rc-globals"
                             : IsDirectoryRoot(token)         ? std::string(token.substr(0, token.find('=')))
                             : token.starts_with("--detailed-block-policy=") ? "--detailed-block-policy"
                             : token.starts_with("--no-require-")            ? absl::StrCat("--", token.substr(5))
                                                                             : std::string(token);
    if ((IsDirectoryRoot(name) || name == "--require-system-config" || name == "--require-user-config"
         || name == "--detailed-block-policy" || name == "--allow-rc-globals")
        && !controls.insert(name).second) {
      return absl::InvalidArgumentError(absl::StrCat(name, " and its negative form may occur only once"));
    }
  }
  return controls;
}

absl::Status ValidateSystemControl(std::string_view token) {
  if (IsDirectoryRoot(token)) {
    const auto equals = token.find('=');
    if (equals == std::string_view::npos || !token.substr(equals + 1).starts_with('/')
        || token.substr(equals + 1) == "/") {
      return absl::InvalidArgumentError("directory roots require =PATH with an absolute, non-root directory");
    }
    return absl::OkStatus();
  }
  if (!token.starts_with("--detailed-block-policy")) {
    return absl::OkStatus();
  }
  if (!token.contains('=')) {
    return absl::InvalidArgumentError("--detailed-block-policy requires =LIST (empty or comma-separated categories)");
  }
  return ValidateGlobalValue(token);
}

absl::Status ValidateTokens(const std::vector<std::string>& tokens) {
  std::vector<std::string> arguments = {"."};
  for (std::size_t pos = 0; pos < tokens.size(); ++pos) {
    const std::string_view token = tokens[pos];
    if (IsSystemControl(token)) {
      MBO_RETURN_IF_ERROR(ValidateSystemControl(token));
      continue;
    }
    if (const auto flag = LookupGlobalArgument(token); flag.has_value()) {
      if (flag->cli_only) {
        return absl::InvalidArgumentError(absl::StrCat(flag->name, " is command-line only"));
      }
      if (const absl::Status status = ValidateGlobalValue(token); !status.ok()) {
        return status;
      }
      continue;
    }
    arguments.emplace_back(token);
    if (token == "(" || token == ")" || token == ",") {
      continue;
    }
    const auto primary = registry::Lookup(token.substr(0, token.find(':')));
    if (!primary.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat("unexpected token '", token, "'"));
    }
    MBO_ASSIGN_OR_RETURN(const std::size_t arity, PrimaryArgumentCount(tokens, pos, *primary));
    for (std::size_t offset = 0; offset < arity; ++offset) {
      arguments.push_back(tokens[++pos]);
    }
  }
  return parser::Parse(arguments).status();
}

std::vector<std::string> ConfigReferences(const config::IniSection& section) {
  constexpr std::string_view kPrefix = "--config=";
  std::vector<std::string> result;
  for (const config::IniLine& line : section.lines) {
    for (const std::string_view token : config::DirectiveTokens(line.tokens)) {
      if (token.starts_with(kPrefix)) {
        result.emplace_back(token.substr(kPrefix.size()));
      }
    }
  }
  return result;
}

std::string CanonicalName(const GlobalFlag& flag) {
  constexpr std::string_view kNo = "--no-";
  if (!flag.name.starts_with(kNo)) {
    return std::string(flag.name);
  }
  const std::string positive = absl::StrCat("--", flag.name.substr(kNo.size()));
  const mbo::types::OptionalRef<const GlobalFlag> counterpart = LookupGlobal(positive);
  return std::string(
      counterpart.has_value() && counterpart->repetition == GlobalFlag::Repetition::kOverride ? counterpart->name
                                                                                              : flag.name);
}

std::string SettingName(std::string_view token, const GlobalFlag& flag) {
  std::string name = CanonicalName(flag);
  if (flag.repetition != GlobalFlag::Repetition::kKeyed) {
    return name;
  }
  const std::string_view::size_type first = token.find('=');
  if (first == std::string_view::npos) {
    return name;
  }
  const std::string_view value = token.substr(first + 1);
  const std::string_view key = value.substr(0, value.find('='));
  absl::StrAppend(&name, "=", key);
  return name;
}

void RecordSetting(
    std::string_view token,
    std::string_view location,
    absl::flat_hash_set<std::string>& seen,
    std::vector<std::string>& notices) {
  const mbo::types::OptionalRef<const GlobalFlag> flag = LookupGlobalArgument(token);
  if (!flag.has_value() || flag->repetition == GlobalFlag::Repetition::kAccumulate) {
    return;
  }
  const std::string name = SettingName(token, *flag);
  if (!seen.insert(name).second) {
    notices.push_back(absl::StrCat("setting ", name, " is overridden within ", location));
  }
}

std::string_view PrimaryName(std::string_view token) {
  return token.substr(0, token.find(':'));
}

void FindOverrides(
    const std::vector<std::string>& tokens,
    std::string_view location,
    absl::flat_hash_set<std::string>& seen,
    std::vector<std::string>& notices) {
  for (std::size_t pos = 0; pos < tokens.size(); ++pos) {
    if (tokens[pos] == "--") {
      break;
    }
    if (LookupGlobalArgument(tokens[pos]).has_value()) {
      RecordSetting(tokens[pos], location, seen, notices);
      continue;
    }
    const auto primary = registry::Lookup(PrimaryName(tokens[pos]));
    if (!primary.has_value()) {
      continue;
    }
    if (primary->arity < 0) {
      while (++pos < tokens.size() && tokens[pos] != ";" && tokens[pos] != "+") {}
    } else {
      pos += static_cast<std::size_t>(primary->arity);
    }
  }
}

void FindFileOverrides(const config::ConfigFile& file, std::string_view path, std::vector<std::string>& notices) {
  absl::flat_hash_set<std::string> globals;
  FindOverrides(file.globals, absl::StrCat(path, " globals"), globals, notices);
  for (const config::IniSection& section : file.named) {
    absl::flat_hash_set<std::string> settings;
    for (const config::IniLine& line : section.lines) {
      FindOverrides(line.tokens, absl::StrCat(path, " section '[", section.name, "]'"), settings, notices);
    }
  }
}

class ConfigFileValidator {
 public:
  ConfigFileValidator(config::ConfigFile config, std::string_view path, config::Source source)
      : result_{.config = std::move(config), .status = absl::OkStatus()}, path_(path), source_(source) {}

  ConfigFileValidation Validate(const std::vector<std::string>& selected_configs) {
    ValidateGlobals();
    ValidateNames();
    ValidateSections();
    PropagateDisablement();
    result_.disabled_configs.assign(disabled_.begin(), disabled_.end());
    std::ranges::sort(result_.disabled_configs);
    result_.config.named.erase(
        std::remove_if(
            result_.config.named.begin(), result_.config.named.end(),
            [this](const config::IniSection& section) { return disabled_.contains(section.name); }),
        result_.config.named.end());
    for (const std::string& selected : selected_configs) {
      if (disabled_.contains(selected)) {
        result_.status = absl::InvalidArgumentError(
            absl::StrCat("selected config [", selected, "] is disabled; see earlier diagnostics"));
        break;
      }
    }
    return std::move(result_);
  }

 private:
  void ValidateGlobals() {
    if (!result_.config.global_lines.empty()) {
      result_.config.globals.clear();
    }
    std::vector<config::IniLine> valid_lines;
    for (const config::IniLine& line : result_.config.global_lines) {
      absl::Status status =
          line.syntax_error.empty() ? ValidateTokens(line.tokens) : absl::InvalidArgumentError(line.syntax_error);
      auto next_controls = ValidateControls(line, controls_);
      if (status.ok() && !next_controls.ok()) {
        status = next_controls.status();
      }
      if (status.ok()) {
        valid_lines.push_back(line);
        controls_ = *std::move(next_controls);
        result_.config.globals.insert(result_.config.globals.end(), line.tokens.begin(), line.tokens.end());
      } else {
        result_.diagnostics.push_back(
            absl::StrCat(
                path_, ":", line.number, ": invalid global config line '", line.text, "': ", status.message()));
        result_.status.Update(absl::InvalidArgumentError(result_.diagnostics.back()));
      }
    }
    result_.config.global_lines = std::move(valid_lines);
  }

  void ValidateNames() {
    absl::flat_hash_set<std::string> names;
    for (const config::IniSection& section : result_.config.named) {
      if (section.name.empty()) {
        disabled_.insert(section.name);
        result_.diagnostics.push_back(absl::StrCat(path_, ":", section.number, ": disabling empty config name"));
        continue;
      }
      if (!names.insert(section.name).second) {
        disabled_.insert(section.name);
        result_.diagnostics.push_back(
            absl::StrCat(
                path_, ":", section.number, ": disabling config [", section.name,
                "] because its name is declared more than once in this file"));
      }
    }
  }

  void ValidateSections() {
    for (const config::IniSection& section : result_.config.named) {
      for (const config::IniLine& line : section.lines) {
        const bool has_control =
            std::ranges::any_of(config::DirectiveTokens(line.tokens), [this](std::string_view token) {
              if (source_ == config::Source::kUser && (token == "--allow-xffrc" || token == "--no-allow-xffrc")) {
                return false;
              }
              return IsSystemControl(token);
            });
        const absl::Status status =
            has_control ? absl::InvalidArgumentError("system controls must precede every system config section")
                        : (line.syntax_error.empty() ? ValidateTokens(line.tokens)
                                                     : absl::InvalidArgumentError(line.syntax_error));
        if (!status.ok()) {
          disabled_.insert(section.name);
          result_.diagnostics.push_back(
              absl::StrCat(
                  path_, ":", line.number, ": disabling config [", section.name, "] because line '", line.text,
                  "' is invalid: ", status.message()));
        }
      }
    }
  }

  void PropagateDisablement() {
    bool changed = true;
    while (changed) {
      changed = false;
      for (const config::IniSection& section : result_.config.named) {
        if (disabled_.contains(section.name)) {
          continue;
        }
        for (const std::string& dependency : ConfigReferences(section)) {
          if (disabled_.contains(dependency)) {
            disabled_.insert(section.name);
            result_.diagnostics.push_back(
                absl::StrCat(
                    path_, ":", section.number, ": disabling config [", section.name,
                    "] because it references disabled config [", dependency, "]"));
            changed = true;
            break;
          }
        }
      }
    }
  }

  ConfigFileValidation result_;
  std::string_view path_;
  config::Source source_;
  absl::flat_hash_set<std::string> controls_;
  absl::flat_hash_set<std::string> disabled_;
};

}  // namespace

ConfigFileValidation ValidateConfigFile(
    config::ConfigFile file,
    const std::vector<std::string>& selected_configs,
    std::string_view path,
    config::Source source) {
  return ConfigFileValidator(std::move(file), path, source).Validate(selected_configs);
}

absl::Status ValidateConfigSelections(
    const config::ConfigInputs& inputs,
    const std::vector<config::ResolvedFlag>& resolved) {
  absl::flat_hash_set<std::string> names;
  const auto collect = [&](const config::ConfigFile& file) {
    for (const config::IniSection& section : file.named) {
      names.insert(section.name);
    }
  };
  if (!inputs.no_system_config) {
    collect(inputs.system);
  }
  if (!inputs.no_user_config) {
    collect(inputs.user);
  }
  for (const config::ExplicitConfig& file : inputs.xffrc) {
    collect(file.config);
  }
  constexpr std::string_view kPrefix = "--config=";
  for (const config::ResolvedFlag& flag : resolved) {
    if (flag.is_argument || !flag.flag.starts_with(kPrefix)) {
      continue;
    }
    const std::string_view name = std::string_view(flag.flag).substr(kPrefix.size());
    if (!config::IsBuiltinStyle(name) && !names.contains(name)) {
      return absl::InvalidArgumentError(absl::StrCat("selected config [", name, "] is not defined in an active file"));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<parser::Command> ApplyResolvedConfig(
    parser::Command command,
    const std::vector<config::ResolvedFlag>& resolved) {
  command.globals.clear();
  command.safety_flags_expanded = true;
  std::vector<std::string> expression = {"."};
  for (const config::ResolvedFlag& flag : resolved) {
    if (!flag.is_argument && (flag.source == config::Source::kCli || LookupGlobalArgument(flag.flag).has_value())) {
      command.globals.push_back(flag.flag);
    } else {
      expression.push_back(flag.flag);
    }
  }
  MBO_ASSIGN_OR_RETURN(parser::Command configured, parser::Parse(expression));
  if (!configured.expression) {
    return command;
  }
  if (command.expression) {
    command.expression = std::make_unique<parser::Expr>(parser::Expr{
        .kind = parser::Expr::Kind::kAnd,
        .lhs = std::move(configured.expression),
        .rhs = std::move(command.expression),
    });
  } else {
    command.expression = std::move(configured.expression);
  }
  return command;
}

std::vector<std::string> ConfigOverrideNotices(const config::ConfigInputs& inputs) {
  std::vector<std::string> notices;
  FindFileOverrides(inputs.system, "system config", notices);
  FindFileOverrides(inputs.user, "user config", notices);
  for (const config::ExplicitConfig& file : inputs.xffrc) {
    FindFileOverrides(file.config, absl::StrCat("--xffrc file ", file.path), notices);
  }
  return notices;
}

}  // namespace xff::cli
