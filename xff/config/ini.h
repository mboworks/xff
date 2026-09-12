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

#ifndef XFF_CONFIG_INI_H_
#define XFF_CONFIG_INI_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xff::config {

struct IniLine {
  std::size_t number = 0;
  std::string text;
  std::vector<std::string> tokens;
};

struct IniSection {
  std::string name;
  std::size_t number = 0;
  std::vector<IniLine> lines;
};

// Parsed /etc/xff.ini global options and named configurations.
struct SystemConfig {
  std::vector<std::string> globals;  // options before the first section, in CLI token form
  std::vector<IniLine> global_lines;
  std::vector<IniSection> named;
};

// Parses system INI `text`. A file-global or named-section "key = value" line renders to CLI
// tokens ("--color = auto" -> "--color=auto"; a bare "--warn" stays "--warn").
// Blank lines and '#'/';' comments are skipped; file-global lines are accepted only before the
// first section. Every [NAME], including [policy], is an ordinary named configuration. Parse-only:
// registry-aware validation and atomic section disabling happen in
// cli::ValidateSystemConfig.
SystemConfig ParseIni(std::string_view text);

}  // namespace xff::config

#endif  // XFF_CONFIG_INI_H_
