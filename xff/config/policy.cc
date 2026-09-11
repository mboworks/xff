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

constexpr std::string_view kAllowNoConfig = "--allow-no-config";
constexpr std::string_view kAllowNoSystemConfig = "--allow-no-system-config";
constexpr std::string_view kAllowNoUserConfig = "--allow-no-user-config";
constexpr std::string_view kAllowXffrc = "--allow-xffrc";
constexpr std::string_view kNoAllowNoConfig = "--no-allow-no-config";
constexpr std::string_view kNoAllowNoSystemConfig = "--no-allow-no-system-config";
constexpr std::string_view kNoAllowNoUserConfig = "--no-allow-no-user-config";
constexpr std::string_view kNoAllowXffrc = "--no-allow-xffrc";

bool IsSystemControl(std::string_view flag) {
  return flag == kAllowNoSystemConfig || flag == kNoAllowNoSystemConfig;
}

bool IsUserControl(std::string_view flag) {
  return flag == kAllowNoUserConfig || flag == kNoAllowNoUserConfig;
}

bool IsXffrcControl(std::string_view flag) {
  return flag == kAllowXffrc || flag == kNoAllowXffrc;
}

bool IsNoConfigControl(std::string_view flag) {
  return flag == kAllowNoConfig || flag == kNoAllowNoConfig;
}

absl::Status ValidateSingleControl(
    const std::vector<std::string>& flags,
    bool (*is_control)(std::string_view),
    std::string_view message) {
  if (absl::c_count_if(flags, is_control) > 1) {
    return absl::InvalidArgumentError(message);
  }
  return absl::OkStatus();
}

absl::Status ValidateSystemControlLocations(const SystemConfig& system) {
  for (const std::string& flag : system.defaults) {
    if (IsNoConfigControl(flag) || IsSystemControl(flag) || IsUserControl(flag)) {
      return absl::InvalidArgumentError(absl::StrCat(flag, " must precede every system config section"));
    }
  }
  if (absl::Status status = ValidateSingleControl(
          system.globals, IsNoConfigControl,
          "the system config may set --allow-no-config or --no-allow-no-config only once");
      !status.ok()) {
    return status;
  }
  if (absl::Status status = ValidateSingleControl(
          system.globals, IsSystemControl,
          "the system config may set --allow-no-system-config or --no-allow-no-system-config only once");
      !status.ok()) {
    return status;
  }
  return ValidateSingleControl(
      system.globals, IsUserControl,
      "the system config may set --allow-no-user-config or --no-allow-no-user-config only once");
}

absl::Status ValidateUserControlLocations(const std::vector<RcLine>& user) {
  std::size_t user_controls = 0;
  bool saw_user_section = false;
  for (const RcLine& line : user) {
    saw_user_section = saw_user_section || !line.base.empty() || !line.config.empty();
    for (const std::string& flag : line.flags) {
      if (IsNoConfigControl(flag) || IsSystemControl(flag)) {
        return absl::InvalidArgumentError(absl::StrCat(flag, " is permitted only in the system config"));
      }
      if (IsUserControl(flag) && saw_user_section) {
        return absl::InvalidArgumentError(
            "--allow-no-user-config and --no-allow-no-user-config must precede every user config section");
      }
      user_controls += IsUserControl(flag) ? 1 : 0;
    }
  }
  if (user_controls > 1) {
    return absl::InvalidArgumentError(
        "the user config may set --allow-no-user-config or --no-allow-no-user-config only once");
  }
  return absl::OkStatus();
}

absl::Status ValidateExplicitControlLocations(const std::vector<ExplicitConfig>& files) {
  for (const ExplicitConfig& file : files) {
    for (const RcLine& line : file.lines) {
      for (const std::string& flag : line.flags) {
        if (IsNoConfigControl(flag) || IsSystemControl(flag) || IsUserControl(flag) || IsXffrcControl(flag)) {
          return absl::InvalidArgumentError(absl::StrCat(flag, " is not permitted in an --xffrc file"));
        }
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateControlLocations(const ConfigInputs& inputs) {
  if (absl::Status status = ValidateSystemControlLocations(inputs.system); !status.ok()) {
    return status;
  }
  if (absl::Status status = ValidateUserControlLocations(inputs.user); !status.ok()) {
    return status;
  }
  return ValidateExplicitControlLocations(inputs.xffrc);
}

bool LineApplies(const RcLine& line, const std::vector<std::string>& configs) {
  const bool base_ok = line.base.empty() || line.base == "common" || absl::c_contains(configs, line.base);
  const bool config_ok = line.config.empty() || absl::c_contains(configs, line.config);
  return base_ok && config_ok;
}

bool XffrcAllowed(const ConfigInputs& inputs) {
  bool allowed = true;
  const auto apply = [&](const std::vector<std::string>& flags) {
    for (const std::string& flag : flags) {
      if (IsXffrcControl(flag)) {
        allowed = flag == kAllowXffrc;
      }
    }
  };
  apply(inputs.system.globals);
  apply(inputs.system.defaults);
  for (const RcLine& line : inputs.user) {
    if (LineApplies(line, inputs.configs) && LinePermitted(line, Source::kUser, inputs.system)) {
      apply(line.flags);
    }
  }
  return allowed;
}

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

std::optional<bool> SystemPermission(const ConfigInputs& inputs, std::string_view permission) {
  std::optional<bool> allowed;
  for (const std::string& flag : inputs.system.globals) {
    if (flag == permission) {
      allowed = true;
    } else if (
        (permission == kAllowNoConfig && flag == kNoAllowNoConfig)
        || (permission == kAllowNoSystemConfig && flag == kNoAllowNoSystemConfig)
        || (permission == kAllowNoUserConfig && flag == kNoAllowNoUserConfig)) {
      allowed = false;
    }
  }
  return allowed;
}

bool SourceWasFound(const ConfigInputs& inputs, Source layer) {
  return absl::c_any_of(
      inputs.sources, [layer](const ConfigSource& source) { return source.layer == layer && source.found; });
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

absl::Status ValidateConfigSkips(const ConfigInputs& inputs) {
  if (const absl::Status locations = ValidateControlLocations(inputs); !locations.ok()) {
    return locations;
  }
  if (!inputs.xffrc.empty() && !XffrcAllowed(inputs)) {
    return absl::PermissionDeniedError("--xffrc is disabled by the resolved --no-allow-xffrc setting");
  }
  if (inputs.no_config) {
    if ((SourceWasFound(inputs, Source::kSystem) || SourceWasFound(inputs, Source::kUser))
        && !SystemPermission(inputs, kAllowNoConfig).value_or(false)) {
      return absl::PermissionDeniedError(
          "--no-config requires --allow-no-config in /etc/xff.ini and may be denied by --no-allow-no-config");
    }
    return absl::OkStatus();
  }
  const std::optional<bool> system_permission = SystemPermission(inputs, kAllowNoSystemConfig);
  const bool skip_system = inputs.no_system_config;
  if (skip_system && SourceWasFound(inputs, Source::kSystem) && !system_permission.value_or(false)) {
    return absl::PermissionDeniedError("--no-system-config requires --allow-no-system-config in /etc/xff.ini");
  }
  std::optional<bool> user_permission;
  for (const RcLine& line : inputs.user) {
    for (const std::string& flag : line.flags) {
      if (IsUserControl(flag) && LinePermitted(line, Source::kUser, inputs.system)) {
        user_permission = flag == kAllowNoUserConfig;
      }
    }
  }
  const std::optional<bool> authoritative_user_permission = SystemPermission(inputs, kAllowNoUserConfig);
  const bool skip_user = inputs.no_user_config;
  if (skip_user && SourceWasFound(inputs, Source::kUser)
      && !authoritative_user_permission.value_or(user_permission.value_or(false))) {
    return absl::PermissionDeniedError("--no-user-config requires --allow-no-user-config in the system or user config");
  }
  return absl::OkStatus();
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
    if (layer == Source::kUser && inputs.no_user_config) {
      return;
    }
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
  for (const ExplicitConfig& file : inputs.xffrc) {
    ExplicitConfig gated_file{.path = file.path};
    gate(file.lines, Source::kXffrc, gated_file.lines);
    result.config.xffrc.push_back(std::move(gated_file));
  }
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
