// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/config_validation.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "mbo/types/optional_ref.h"
#include "xff/cli/globals.h"
#include "xff/config/config.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

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

std::vector<std::string> ConfigOverrideNotices(const config::ConfigInputs& inputs) {
  std::vector<std::string> notices;
  absl::flat_hash_set<std::string> system_globals;
  FindOverrides(inputs.system.globals, "system config globals", system_globals, notices);
  absl::flat_hash_set<std::string> system_defaults;
  FindOverrides(inputs.system.defaults, "system config [defaults]", system_defaults, notices);
  FindRcOverrides(inputs.user, "user config", notices);
  for (const config::ExplicitConfig& file : inputs.xffrc) {
    FindRcOverrides(file.lines, absl::StrCat("--xffrc file ", file.path), notices);
  }
  return notices;
}

}  // namespace xff::cli
