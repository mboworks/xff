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
  std::string syntax_error;  // lexical failure; no partial tokens are usable
};

struct IniSection {
  std::string name;
  std::size_t number = 0;
  std::vector<IniLine> lines;
};

// Parsed config file, shared by system, user, and explicit files.
struct ConfigFile {
  std::vector<std::string> globals;  // options before the first section, in CLI token form
  std::vector<IniLine> global_lines;
  std::vector<IniSection> named;
};

// Parses shared INI text with shell-style single/double quoting and backslash escaping.
// Unquoted '#' begins a comment at a word boundary; ';' is an ordinary exec terminator,
// never a comment. No expansion or execution occurs. Quoted newlines and escaped line
// continuations are supported. Whitespace separates words; flags retain their exact CLI spelling.
// Lexical errors retain their starting line and source text.
// Unsectioned flags precede the first [NAME]. Every header is preserved, including empty and
// repeated declarations. Registry-aware validation and atomic section disabling happen in
// cli::ValidateConfigFile.
ConfigFile ParseIni(std::string_view text);

}  // namespace xff::config

#endif  // XFF_CONFIG_INI_H_
