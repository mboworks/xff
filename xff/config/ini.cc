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

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace xff::config {
namespace {

// An INI option line -> a CLI token: "--color = auto" -> "--color=auto"; a bare
// "--warn" stays "--warn".
std::string DefaultFlag(std::string_view line) {
  const std::string_view::size_type eq = line.find('=');
  if (eq == std::string_view::npos) {
    return std::string(absl::StripAsciiWhitespace(line));
  }
  return absl::StrCat(
      absl::StripAsciiWhitespace(line.substr(0, eq)), "=", absl::StripAsciiWhitespace(line.substr(eq + 1)));
}

std::vector<std::string> FlagTokens(std::string_view line) {
  const std::string normalized = DefaultFlag(line);
  std::vector<std::string> result;
  for (const std::string_view token : absl::StrSplit(normalized, absl::ByAnyChar(" \t"), absl::SkipEmpty())) {
    result.emplace_back(token);
  }
  return result;
}

}  // namespace

ConfigFile ParseIni(std::string_view text) {
  ConfigFile config;
  std::size_t line_number = 0;
  for (const std::string_view raw : absl::StrSplit(text, '\n')) {
    ++line_number;
    const std::string_view line = absl::StripAsciiWhitespace(raw);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;  // blank or comment
    }
    if (line.front() == '[' && line.back() == ']') {
      const std::string_view name = absl::StripAsciiWhitespace(line.substr(1, line.size() - 2));
      config.named.push_back({.name = std::string(name), .number = line_number});
      continue;
    }
    if (config.named.empty()) {
      const IniLine parsed{.number = line_number, .text = std::string(line), .tokens = FlagTokens(line)};
      config.globals.insert(config.globals.end(), parsed.tokens.begin(), parsed.tokens.end());
      config.global_lines.push_back(parsed);
    } else {
      config.named.back().lines.push_back(
          {.number = line_number, .text = std::string(line), .tokens = FlagTokens(line)});
    }
  }
  return config;
}

}  // namespace xff::config
