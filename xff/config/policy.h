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

// Whether a `line` from `layer` is permitted under the system `policy`. With the
// untrusted project layer gone (2026-07-06, Option B), no layer is denied by
// default: the trusted system/user layers may do anything, so a line is permitted
// unless the root-owned system [policy] explicitly DENIES it for this layer,
// addressed by flag name or an @safe/@sensitive/@destructive class token. Only the
// system layer supplies [policy].
bool LinePermitted(const RcLine& line, Source layer, const SystemConfig& policy);

// Why the gate dropped a line: a safety-policy denial (its safety class bars it from the layer),
// a structural rule (it attaches behavior to a built-in preset, which no config file may do), or
// an unarmed dangerous directive loaded from an --xffrc file (kSafety/kSecurity, no --allow-exec).
enum class DropReason { kSafetyPolicy, kPresetOverload, kUnarmedXffrc };

// A config line dropped by the gate: the line, the layer it came from, the safety class (relevant
// to kSafetyPolicy), and why it was dropped (for the stderr warning and --explain).
struct Drop {
  RcLine line;
  Source layer;
  registry::Safety safety;
  DropReason reason = DropReason::kSafetyPolicy;
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

// Filters the user + --xffrc .xffrc lines of `inputs` through the gate (with inputs.system as the
// policy), returning both a copy with the denied lines removed and a record of every drop. Every
// layer drops a preset-overloading
// line (kPresetOverload) and a system-[policy]-denied line (kSafetyPolicy). The
// --xffrc tier additionally drops a dangerous (kSafety/kSecurity) line as
// kUnarmedXffrc unless `xffrc_armed` is set (--allow-exec from a trusted tier; see
// ArmedFromTrustedTier). The system [defaults] are root-authored and never gated;
// CLI flags are not config and never gated.
GateResult GateConfig(const ConfigInputs& inputs, bool xffrc_armed);

// A one-line human description of a dropped line for the stderr warning and
// --explain, e.g. "'-exec' from the --xffrc file (sensitive; needs --allow-exec)".
std::string DropMessage(const Drop& drop);

}  // namespace xff::config

#endif  // XFF_CONFIG_POLICY_H_
