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

#include <string>
#include <string_view>
#include <vector>

namespace xff::config {

// One [policy] rule: a layer, whether it allow- or deny-lists, and the flag /
// @class tokens it names (e.g. "--sort", "@sensitive").
struct PolicyRule {
  std::string layer;                // normally "user" / "xffrc" / "system"; parser accepts any name
  bool allow = true;                // allow-list (true) or deny-list (false)
  std::vector<std::string> tokens;  // flag names and/or @class tokens
};

// The parsed system policy (/etc/xff.ini): [defaults] flag lines + [policy]
// per-layer allow/deny rules.
struct SystemConfig {
  std::vector<std::string> defaults;  // [defaults] flags in CLI token form
  std::vector<PolicyRule> policy;     // [policy] rules, in file order
};

// Parses system INI `text`. A [defaults] "key = value" line renders to a CLI
// token ("--color = auto" -> "--color=auto"; a bare "--warn" stays "--warn").
// A [policy] "<layer>.<allow|deny> = <comma-list>" line becomes a PolicyRule.
// Blank lines and '#'/';' comments are skipped; lines outside [defaults]/[policy]
// and malformed [policy] lines are ignored. Parse-only: no registry validation
// and no enforcement (the policy gate does that).
SystemConfig ParseIni(std::string_view text);

}  // namespace xff::config

#endif  // XFF_CONFIG_INI_H_
