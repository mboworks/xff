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

#ifndef XFF_CONFIG_POLICY_H_
#define XFF_CONFIG_POLICY_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace xff::config {

// The safety class of a whole .xffrc line: the most restrictive class among its
// flags (registry Lookup; unknown tokens such as globals are kNone). A line
// counts as sensitive/destructive when ANY of its flags is. An attached binding
// like "-capture:tag" is classified by its base name before ':'.
registry::Safety LineSafety(const IniLine& line);

// Validates trusted control placement, uniqueness and xffrc admission for skip requests.
// Require/no-require globals pairs decide whether existing globals survive a skip; named sections
// are always excluded by a skip. System-global policy is system-only; user-global policy may be
// set by system or user globals, with the system decision authoritative. Each pair may appear
// once per permitted file, before any sections. These directives are config-only.
// --allow-xffrc/--no-allow-xffrc is a separate config-only setting resolved through ordinary
// system/user selection and precedence, with an authoritative system global denial; an explicit
// --xffrc file cannot admit itself.
absl::Status ValidateConfigSkips(const ConfigInputs& inputs);

// Unsectioned system/user permission for globals in automatically discovered .xffrc files.
// A system denial is authoritative. Explicit --xffrc files are unaffected.
bool RcGlobalsAllowed(const ConfigInputs& inputs);

// Why the gate dropped a line: activation only through explicit-file composition,
// a structural rule (it attaches behavior to a built-in preset, which no config file may do), or
// an unarmed dangerous directive loaded from an --xffrc file (kSafety/kSecurity, no --allow-exec).
enum class DropReason { kUntrustedSelection, kPresetOverload, kUnarmedXffrc };

// A config line dropped by the gate: the line, the layer it came from, the safety class (relevant
// to kUntrustedSelection), and why it was dropped (for the stderr warning and --explain).
struct Drop {
  IniLine line;
  std::string config_name;
  Source layer;
  registry::Safety safety;
  DropReason reason = DropReason::kUntrustedSelection;
};

// Whether a section name exactly matches a built-in preset (find/xff/rg). User and explicit
// files customize through unsectioned defaults or custom named sections instead.
bool OverloadsPreset(std::string_view name);

struct GateResult {
  ConfigInputs config;
  std::vector<Drop> drops;
};

// Filters preset overloads and unarmed actions supplied or activated by explicit config files.
// Trusted named actions require selection reachable without explicit files, or explicit arming.
// Unconditional capability blocks are enforced separately at runtime.
GateResult GateConfig(
    const ConfigInputs& inputs,
    bool xffrc_armed,
    const std::vector<std::string>& cli_globals = {},
    std::string_view invocation_selector = "xff");

// A one-line human description of a dropped line for the stderr warning and
// --explain, e.g. "'-exec' from the --xffrc file (sensitive; needs --allow-exec)".
std::string DropMessage(const Drop& drop);

}  // namespace xff::config

#endif  // XFF_CONFIG_POLICY_H_
