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

bool IsSystemControl(std::string_view token) {
  return token == "--allow-no-config" || token == "--no-allow-no-config" || token == "--allow-no-system-config"
         || token == "--no-allow-no-system-config" || token == "--allow-no-user-config" || token == "--no-allow-exec"
         || token == "--no-allow-no-user-config" || token == "--allow-xffrc" || token == "--no-allow-xffrc";
}

absl::Status ValidateTokens(const std::vector<std::string>& tokens) {
  std::vector<std::string> arguments = {"."};
  for (std::size_t pos = 0; pos < tokens.size(); ++pos) {
    const std::string_view token = tokens[pos];
    if (IsSystemControl(token)) {
      continue;
    }
    if (LookupGlobalArgument(token).has_value()) {
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
    if (primary->arity < 0) {
      bool terminated = false;
      while (++pos < tokens.size()) {
        arguments.push_back(tokens[pos]);
        if (tokens[pos] == ";" || tokens[pos] == "+") {
          terminated = true;
          break;
        }
      }
      if (!terminated) {
        return absl::InvalidArgumentError(absl::StrCat("'", token, "' requires a terminating ';' or '+'"));
      }
      continue;
    }
    const auto arity = static_cast<std::size_t>(primary->arity);
    if (arity > tokens.size() - pos - 1) {
      return absl::InvalidArgumentError(absl::StrCat("'", token, "' requires ", arity, " argument(s)"));
    }
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

struct SectionSettings {
  std::string key;
  absl::flat_hash_set<std::string> names;
};

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

void FindRcOverrides(
    const std::vector<config::RcLine>& lines,
    std::string_view file,
    std::vector<std::string>& notices) {
  std::vector<SectionSettings> sections;
  for (const config::RcLine& line : lines) {
    const std::string key = absl::StrCat(line.base, ":", line.config);
    auto section = sections.begin();
    while (section != sections.end() && section->key != key) {
      ++section;
    }
    if (section == sections.end()) {
      sections.push_back({.key = key});
      section = sections.end() - 1;
    }
    const std::string location = absl::StrCat(file, " section '", key, "'");
    FindOverrides(line.flags, location, section->names, notices);
  }
}

}  // namespace

SystemConfigValidation ValidateSystemConfig(
    config::SystemConfig config,
    const std::vector<std::string>& selected_configs,
    std::string_view path) {
  SystemConfigValidation result{.config = std::move(config), .selected_configs_status = absl::OkStatus()};
  if (!result.config.global_lines.empty()) {
    result.config.globals.clear();
  }
  absl::flat_hash_set<std::string> controls;
  for (const config::IniLine& line : result.config.global_lines) {
    absl::Status status = ValidateTokens(line.tokens);
    auto next_controls = controls;
    if (status.ok()) {
      for (const std::string_view token : config::DirectiveTokens(line.tokens)) {
        const std::string name =
            token.starts_with("--no-allow-no-") ? absl::StrCat("--", token.substr(5)) : std::string(token);
        if ((name == "--allow-no-config" || name == "--allow-no-system-config" || name == "--allow-no-user-config")
            && !next_controls.insert(name).second) {
          status = absl::InvalidArgumentError(absl::StrCat(name, " and its negative form may occur only once"));
          break;
        }
      }
    }
    if (status.ok()) {
      controls = std::move(next_controls);
      result.config.globals.insert(result.config.globals.end(), line.tokens.begin(), line.tokens.end());
    } else {
      result.diagnostics.push_back(
          absl::StrCat(path, ":", line.number, ": invalid global config line '", line.text, "': ", status.message()));
    }
  }

  absl::flat_hash_set<std::string> disabled;
  for (const config::IniSection& section : result.config.named) {
    for (const config::IniLine& line : section.lines) {
      const bool has_control = std::ranges::any_of(config::DirectiveTokens(line.tokens), IsSystemControl);
      const absl::Status status =
          has_control ? absl::InvalidArgumentError("system controls must precede every system config section")
                      : ValidateTokens(line.tokens);
      if (!status.ok()) {
        disabled.insert(section.name);
        result.diagnostics.push_back(
            absl::StrCat(
                path, ":", line.number, ": disabling config [", section.name, "] because line '", line.text,
                "' is invalid: ", status.message()));
      }
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (const config::IniSection& section : result.config.named) {
      if (disabled.contains(section.name)) {
        continue;
      }
      for (const std::string& dependency : ConfigReferences(section)) {
        if (disabled.contains(dependency)) {
          disabled.insert(section.name);
          result.diagnostics.push_back(
              absl::StrCat(
                  path, ":", section.number, ": disabling config [", section.name,
                  "] because it references disabled config [", dependency, "]"));
          changed = true;
          break;
        }
      }
    }
  }

  result.disabled_configs.assign(disabled.begin(), disabled.end());
  std::ranges::sort(result.disabled_configs);
  result.config.named.erase(
      std::remove_if(
          result.config.named.begin(), result.config.named.end(),
          [&disabled](const config::IniSection& section) { return disabled.contains(section.name); }),
      result.config.named.end());
  for (const std::string& selected : selected_configs) {
    if (disabled.contains(selected)) {
      result.selected_configs_status = absl::InvalidArgumentError(
          absl::StrCat("selected config [", selected, "] is disabled; see earlier diagnostics"));
      break;
    }
  }
  return result;
}

absl::StatusOr<parser::Command> ApplyResolvedConfig(
    parser::Command command,
    const std::vector<config::ResolvedFlag>& resolved) {
  command.globals.clear();
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
  absl::flat_hash_set<std::string> system_globals;
  FindOverrides(inputs.system.globals, "system config globals", system_globals, notices);
  for (const config::IniSection& section : inputs.system.named) {
    absl::flat_hash_set<std::string> settings;
    for (const config::IniLine& line : section.lines) {
      FindOverrides(line.tokens, absl::StrCat("system config section '[", section.name, "]'"), settings, notices);
    }
  }
  FindRcOverrides(inputs.user, "user config", notices);
  for (const config::ExplicitConfig& file : inputs.xffrc) {
    FindRcOverrides(file.lines, absl::StrCat("--xffrc file ", file.path), notices);
  }
  return notices;
}

}  // namespace xff::cli
