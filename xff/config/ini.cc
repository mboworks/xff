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

#include "xff/config/ini.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace xff::config {
namespace {

// A [defaults] line -> a CLI token: "--color = auto" -> "--color=auto"; a bare
// "--warn" stays "--warn".
std::string DefaultFlag(std::string_view line) {
  const std::string_view::size_type eq = line.find('=');
  if (eq == std::string_view::npos) {
    return std::string(absl::StripAsciiWhitespace(line));
  }
  return absl::StrCat(
      absl::StripAsciiWhitespace(line.substr(0, eq)), "=", absl::StripAsciiWhitespace(line.substr(eq + 1)));
}

// A [policy] line "<layer>.<allow|deny> = <comma-list>" -> a PolicyRule. Returns
// false (line ignored) if it lacks '=', a '.', or a recognized allow/deny kind.
bool ParsePolicyLine(std::string_view line, PolicyRule& rule) {
  const std::string_view::size_type eq = line.find('=');
  if (eq == std::string_view::npos) {
    return false;
  }
  const std::string_view key = absl::StripAsciiWhitespace(line.substr(0, eq));
  const std::string_view::size_type dot = key.find('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  const std::string_view kind = key.substr(dot + 1);
  if (kind != "allow" && kind != "deny") {
    return false;
  }
  rule.layer = std::string(key.substr(0, dot));
  rule.allow = kind == "allow";
  rule.tokens.clear();
  for (const std::string_view tok : absl::StrSplit(line.substr(eq + 1), ',', absl::SkipEmpty())) {
    if (const std::string_view trimmed = absl::StripAsciiWhitespace(tok); !trimmed.empty()) {
      rule.tokens.emplace_back(trimmed);
    }
  }
  return true;
}

}  // namespace

SystemConfig ParseIni(std::string_view text) {
  SystemConfig config;
  std::string_view section;
  bool saw_section = false;
  for (const std::string_view raw : absl::StrSplit(text, '\n')) {
    const std::string_view line = absl::StripAsciiWhitespace(raw);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;  // blank or comment
    }
    if (line.front() == '[' && line.back() == ']') {
      saw_section = true;
      section = absl::StripAsciiWhitespace(line.substr(1, line.size() - 2));
      continue;
    }
    if (!saw_section) {
      config.globals.push_back(DefaultFlag(line));
    } else if (section == "defaults") {
      config.defaults.push_back(DefaultFlag(line));
    } else if (section == "policy") {
      if (PolicyRule rule; ParsePolicyLine(line, rule)) {
        config.policy.push_back(std::move(rule));
      }
    }
    // Lines inside an unknown section are ignored (parse-only, forgiving).
  }
  return config;
}

}  // namespace xff::config
