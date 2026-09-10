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

#include "xff/config/policy.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::config {
namespace {

// The registry safety class of one flag token (kNone for globals/unknowns). An
// attached binding like "-capture:tag" is classified by its base name "-capture".
registry::Safety FlagSafety(std::string_view flag) {
  const std::string_view base = flag.substr(0, flag.find(':'));
  const auto descriptor = registry::Lookup(base);
  return descriptor.has_value() ? descriptor->safety : registry::Safety::kNone;
}

// @safe/@sensitive/@destructive -> the matching class; nullopt if not a class token.
std::optional<registry::Safety> ClassToken(std::string_view token) {
  if (token == "@safe") {
    return registry::Safety::kNone;
  }
  if (token == "@destructive") {
    return registry::Safety::kSafety;
  }
  if (token == "@sensitive") {
    return registry::Safety::kSecurity;
  }
  return std::nullopt;
}

// Whether a [policy] rule `token` matches `line`: @safe matches a wholly safe
// line, while a dangerous @class token matches when any primary on the line has
// that exact class. A flag-name token matches when any line flag equals it or carries
// it as an attached global value or primary qualification (flag == token, or flag starts with the
// token plus `=` / `:` respectively). Accepting both delimiters here is structural, not a CLI alias:
// globals such as `--jobs=4` and primaries such as `-capture:tag` share this policy matcher.
bool TokenMatchesLine(std::string_view token, const RcLine& line) {
  if (const std::optional<registry::Safety> cls = ClassToken(token); cls.has_value()) {
    if (*cls == registry::Safety::kNone) {
      return LineSafety(line) == registry::Safety::kNone;
    }
    return absl::c_any_of(line.flags, [&](std::string_view flag) { return FlagSafety(flag) == *cls; });
  }
  const std::string with_value = absl::StrCat(token, "=");
  const std::string with_qualifier = absl::StrCat(token, ":");
  return absl::c_any_of(line.flags, [&](std::string_view flag) {
    return flag == token || absl::StartsWith(flag, with_value) || absl::StartsWith(flag, with_qualifier);
  });
}

// The human name of a safety class, for warnings and --explain.
std::string_view ClassName(registry::Safety safety) {
  switch (safety) {
    case registry::Safety::kNone: return "safe";
    case registry::Safety::kSafety: return "destructive";
    case registry::Safety::kSecurity: return "sensitive";
  }
  return "safe";
}

}  // namespace

registry::Safety LineSafety(const RcLine& line) {
  // Safety is declared in increasing restrictiveness (kNone < kSafety < kSecurity),
  // so the line's class is the maximum by enum value over its flags.
  registry::Safety worst = registry::Safety::kNone;
  for (const std::string& flag : line.flags) {
    const registry::Safety current = FlagSafety(flag);
    if (static_cast<int>(current) > static_cast<int>(worst)) {
      worst = current;
    }
  }
  return worst;
}

bool LinePermitted(const RcLine& line, Source layer, const SystemConfig& policy) {
  // No layer is denied by default now (the untrusted project layer was dropped 2026-07-06,
  // Option B): the trusted system/user layers may do anything, so a line is permitted unless the
  // root-owned system [policy] explicitly DENIES it for this layer. An @class or flag-name token
  // matches; a deny rule bars the line. (An allow rule has nothing left to loosen, so it is inert.)
  const std::string_view layer_name = SourceName(layer);
  for (const PolicyRule& rule : policy.policy) {
    if (rule.allow || rule.layer != layer_name) {
      continue;
    }
    if (absl::c_any_of(rule.tokens, [&](std::string_view token) { return TokenMatchesLine(token, line); })) {
      return false;
    }
  }
  return true;
}

bool OverloadsPreset(const RcLine& line) {
  return line.config.empty() && IsBuiltinStyle(line.base);
}

GateResult GateConfig(const ConfigInputs& inputs, bool xffrc_armed) {
  GateResult result{.config = inputs};
  result.config.user.clear();
  result.config.xffrc.clear();
  const auto record = [&](const RcLine& line, Source layer, DropReason reason) {
    result.drops.push_back(Drop{.line = line, .layer = layer, .safety = LineSafety(line), .reason = reason});
  };
  const auto gate = [&](const std::vector<RcLine>& lines, Source layer, std::vector<RcLine>& out) {
    for (const RcLine& line : lines) {
      // A preset-overloading line is dropped in every layer: a config file may not attach behavior
      // to find/xff/rg (it would change what a plain preset run does). Checked first so the warning
      // names the real reason.
      if (OverloadsPreset(line)) {
        record(line, layer, DropReason::kPresetOverload);
        continue;
      }
      // The --xffrc tier is non-arming: a dangerous (sensitive/destructive) line is inert unless
      // --allow-exec was set from a trusted tier. A named file thus cannot authorize its own -exec.
      if (layer == Source::kXffrc && !xffrc_armed && LineSafety(line) != registry::Safety::kNone) {
        record(line, layer, DropReason::kUnarmedXffrc);
        continue;
      }
      if (LinePermitted(line, layer, inputs.system)) {
        out.push_back(line);
      } else {
        record(line, layer, DropReason::kSafetyPolicy);
      }
    }
  };
  gate(inputs.user, Source::kUser, result.config.user);
  gate(inputs.xffrc, Source::kXffrc, result.config.xffrc);
  return result;
}

std::string DropMessage(const Drop& drop) {
  if (drop.reason == DropReason::kPresetOverload) {
    return absl::StrCat("'", drop.line.base, ":' in the ", SourceName(drop.layer), " .xffrc");
  }
  const std::string_view primary = drop.line.flags.empty() ? std::string_view("(empty line)") : drop.line.flags.front();
  if (drop.reason == DropReason::kUnarmedXffrc) {
    return absl::StrCat("'", primary, "' from the --xffrc file (", ClassName(drop.safety), "; needs --allow-exec)");
  }
  return absl::StrCat("'", primary, "' from the ", SourceName(drop.layer), " .xffrc (", ClassName(drop.safety), ")");
}

}  // namespace xff::config
