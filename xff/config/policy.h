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
registry::Safety LineSafety(const RcLine& line);

// Validates requests to suppress automatic configuration. A present system config must explicitly
// authorize suppressing its defaults; a present user config must authorize suppressing itself unless
// the higher-trust system config already does. Each positive/negative permission pair controls only
// its corresponding command-line skip flag and is unique per automatic config file.
// system-skip control is system-only, user-skip control may occur in the system or user file, and
// every control precedes all sections. No control is accepted from an explicitly named --xffrc
// file. Permission directives are config-only and never enter the resolved runtime flags.
// --allow-xffrc/--no-allow-xffrc is a separate config-only setting resolved through ordinary
// system/user selection and precedence, with an authoritative system global denial; an explicit
// --xffrc file cannot admit itself.
absl::Status ValidateConfigSkips(const ConfigInputs& inputs);

// Why the gate dropped a line: a global --no-allow-exec prohibition,
// a structural rule (it attaches behavior to a built-in preset, which no config file may do), or
// an unarmed dangerous directive loaded from an --xffrc file (kSafety/kSecurity, no --allow-exec).
enum class DropReason { kSystemProhibition, kPresetOverload, kUnarmedXffrc };

// A config line dropped by the gate: the line, the layer it came from, the safety class (relevant
// to kSystemProhibition), and why it was dropped (for the stderr warning and --explain).
struct Drop {
  RcLine line;
  Source layer;
  registry::Safety safety;
  DropReason reason = DropReason::kSystemProhibition;
};

// Whether `line` attaches behavior to a built-in preset: its base names a style (find/xff/rg)
// with no named-config component (`xff: --flag`), so it would apply whenever that preset is active
// and silently change what a plain `xff` run does. Config files customize via `common:` (always-on)
// or named blocks (`myconfig:`, or the style-scoped `xff:myconfig:`) instead, so a preset stays
// reproducible. Applies to every layer.
bool OverloadsPreset(const RcLine& line);

struct GateResult {
  ConfigInputs config;
  std::vector<Drop> drops;
};

// Filters user and explicit-file lines, recording preset overloads and dangerous lines denied by
// the trusted global --no-allow-exec prohibition. Explicit-file dangerous lines also require arming.
// System-authored flags and CLI expressions are not filtered by this gate.
GateResult GateConfig(const ConfigInputs& inputs, bool xffrc_armed);

// A one-line human description of a dropped line for the stderr warning and
// --explain, e.g. "'-exec' from the --xffrc file (sensitive; needs --allow-exec)".
std::string DropMessage(const Drop& drop);

}  // namespace xff::config

#endif  // XFF_CONFIG_POLICY_H_
