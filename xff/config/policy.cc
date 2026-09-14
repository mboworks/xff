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

#include <algorithm>
#include <array>
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

constexpr std::string_view kNoRequireSystemGlobals = "--no-require-system-globals";
constexpr std::string_view kNoRequireUserGlobals = "--no-require-user-globals";
constexpr std::string_view kAllowXffrc = "--allow-xffrc";
constexpr std::string_view kRequireSystemGlobals = "--require-system-globals";
constexpr std::string_view kRequireUserGlobals = "--require-user-globals";
constexpr std::string_view kNoAllowXffrc = "--no-allow-xffrc";

bool IsDirectoryRoot(std::string_view flag) {
  const auto name = flag.substr(0, flag.find('='));
  return name == "--temp-root" || name == "--output-root";
}

bool IsDetailedPolicy(std::string_view flag) {
  return flag == "--block-policy-categories" || flag.starts_with("--block-policy-categories=");
}

bool IsSystemControl(std::string_view flag) {
  return flag == kNoRequireSystemGlobals || flag == kRequireSystemGlobals;
}

bool IsUserControl(std::string_view flag) {
  return flag == kNoRequireUserGlobals || flag == kRequireUserGlobals;
}

bool IsXffrcControl(std::string_view flag) {
  return flag == kAllowXffrc || flag == kNoAllowXffrc;
}

bool IsRcGlobalsControl(std::string_view flag) {
  return flag == "--allow-rc-globals" || flag == "--no-allow-rc-globals";
}

absl::Status ValidateSingleControl(
    const std::vector<std::string>& flags,
    bool (*is_control)(std::string_view),
    std::string_view message) {
  if (absl::c_count_if(DirectiveTokens(flags), is_control) > 1) {
    return absl::InvalidArgumentError(message);
  }
  return absl::OkStatus();
}

absl::Status ValidateRootControls(const ConfigFile& file) {
  static constexpr auto kRoots = std::to_array<std::string_view>({"--temp-root", "--output-root"});
  for (const std::string_view name : kRoots) {
    std::size_t count = 0;
    for (const std::string_view token : DirectiveTokens(file.globals)) {
      if (token.substr(0, token.find('=')) == name && ++count > 1) {
        return absl::InvalidArgumentError(absl::StrCat(name, " may occur only once per file"));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateSystemControlLocations(const ConfigFile& system) {
  if (const auto roots = ValidateRootControls(system); !roots.ok()) {
    return roots;
  }
  if (const auto status = ValidateSingleControl(
          system.globals, IsRcGlobalsControl, "the rc-globals permission pair may occur only once per file");
      !status.ok()) {
    return status;
  }
  if (const auto status = ValidateSingleControl(
          system.globals, IsDetailedPolicy, "--block-policy-categories may occur only once in the system config");
      !status.ok()) {
    return status;
  }
  for (const IniSection& section : system.named) {
    for (const IniLine& line : section.lines) {
      for (const std::string_view flag : DirectiveTokens(line.tokens)) {
        if (IsSystemControl(flag) || IsUserControl(flag) || IsXffrcControl(flag) || IsDetailedPolicy(flag)
            || IsDirectoryRoot(flag) || IsRcGlobalsControl(flag)) {
          return absl::InvalidArgumentError(absl::StrCat(flag, " must precede every system config section"));
        }
      }
    }
  }
  if (absl::Status status = ValidateSingleControl(
          system.globals, IsSystemControl,
          "the system config may set --no-require-system-globals or --require-system-globals only once");
      !status.ok()) {
    return status;
  }
  return ValidateSingleControl(
      system.globals, IsUserControl,
      "the system config may set --no-require-user-globals or --require-user-globals only once");
}

struct FileLine {
  std::string name;
  IniLine line;
};

std::vector<FileLine> Lines(const ConfigFile& file) {
  std::vector<FileLine> lines;
  std::size_t capacity = 1 + file.global_lines.size();
  for (const IniSection& section : file.named) {
    capacity += section.lines.size();
  }
  lines.reserve(capacity);
  if (file.global_lines.empty()) {
    if (!file.globals.empty()) {
      lines.push_back({.line = {.tokens = file.globals}});
    }
  } else {
    for (const IniLine& line : file.global_lines) {
      lines.push_back({.line = line});
    }
  }
  for (const IniSection& section : file.named) {
    for (const IniLine& line : section.lines) {
      lines.push_back({.name = section.name, .line = line});
    }
  }
  return lines;
}

absl::Status ValidateUserControlLocations(const ConfigFile& user) {
  if (const auto roots = ValidateRootControls(user); !roots.ok()) {
    return roots;
  }
  if (const auto status = ValidateSingleControl(
          user.globals, IsRcGlobalsControl, "the rc-globals permission pair may occur only once per file");
      !status.ok()) {
    return status;
  }
  if (const auto status = ValidateSingleControl(
          user.globals, IsDetailedPolicy, "--block-policy-categories may occur only once in the user config");
      !status.ok()) {
    return status;
  }
  std::size_t user_controls = 0;
  for (const FileLine& entry : Lines(user)) {
    for (const std::string_view flag : DirectiveTokens(entry.line.tokens)) {
      if (IsSystemControl(flag)) {
        return absl::InvalidArgumentError(absl::StrCat(flag, " is permitted only in the system config"));
      }
      if ((IsDetailedPolicy(flag) || IsDirectoryRoot(flag) || IsRcGlobalsControl(flag)) && !entry.name.empty()) {
        return absl::InvalidArgumentError(absl::StrCat(flag, " must precede every user config section"));
      }
      if (IsUserControl(flag) && !entry.name.empty()) {
        return absl::InvalidArgumentError(
            "--no-require-user-globals and --require-user-globals must precede every user config section");
      }
      user_controls += IsUserControl(flag) ? 1 : 0;
    }
  }
  if (user_controls > 1) {
    return absl::InvalidArgumentError(
        "the user config may set --no-require-user-globals or --require-user-globals only once");
  }
  return absl::OkStatus();
}

absl::Status ValidateExplicitControlLocations(const std::vector<ExplicitConfig>& files) {
  for (const ExplicitConfig& file : files) {
    for (const FileLine& entry : Lines(file.config)) {
      for (const std::string_view flag : DirectiveTokens(entry.line.tokens)) {
        if (IsSystemControl(flag) || IsUserControl(flag) || IsXffrcControl(flag) || IsDetailedPolicy(flag)
            || IsDirectoryRoot(flag) || IsRcGlobalsControl(flag) || flag == "--rc" || flag == "--rc-"
            || flag == "--rc+") {
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

bool XffrcAllowed(const ConfigInputs& inputs) {
  bool allowed = true;
  const auto apply = [&](const std::vector<std::string>& flags) {
    for (const std::string_view flag : DirectiveTokens(flags)) {
      if (IsXffrcControl(flag)) {
        allowed = flag == kAllowXffrc;
      }
    }
  };
  apply(inputs.system.globals);
  if (!allowed) {
    return false;  // A global prohibition is authoritative, even when system defaults are skipped.
  }
  ConfigInputs automatic = inputs;
  automatic.xffrc.clear();
  std::vector<std::string> selectors;
  selectors.reserve(inputs.configs.size());
  for (const std::string& name : inputs.configs) {
    selectors.push_back(absl::StrCat("--config=", name));
  }
  for (const ResolvedFlag& flag : ResolveConfigInOrder(automatic, selectors, "", ConfigControls::kInclude)) {
    if (flag.source == Source::kUser && !flag.is_argument && IsXffrcControl(flag.flag)) {
      allowed = flag.flag == kAllowXffrc;
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

registry::Safety LineSafety(const IniLine& line) {
  // Safety is declared in increasing restrictiveness (kNone < kSafety < kSecurity),
  // so the line's class is the maximum by enum value over its flags.
  registry::Safety worst = registry::Safety::kNone;
  for (const std::string_view flag : DirectiveTokens(line.tokens)) {
    const registry::Safety current = FlagSafety(flag);
    if (static_cast<int>(current) > static_cast<int>(worst)) {
      worst = current;
    }
  }
  return worst;
}

bool RcGlobalsAllowed(const ConfigInputs& inputs) {
  bool allowed = false;
  for (const auto flag : DirectiveTokens(inputs.system.globals)) {
    if (flag == "--no-allow-rc-globals") {
      return false;
    }
    if (flag == "--allow-rc-globals") {
      allowed = true;
    }
  }
  {
    const ConfigInputs applying = ApplyConfigSkips(inputs);
    for (const auto flag : DirectiveTokens(applying.user.globals)) {
      if (IsRcGlobalsControl(flag)) {
        allowed = flag == "--allow-rc-globals";
      }
    }
  }
  return allowed;
}

absl::Status ValidateConfigSkips(const ConfigInputs& inputs) {
  if (const absl::Status locations = ValidateControlLocations(inputs); !locations.ok()) {
    return locations;
  }
  if ((!inputs.xffrc.empty() || inputs.rc_mode != RcMode::kOff) && !XffrcAllowed(inputs)) {
    return absl::PermissionDeniedError(".xffrc loading is disabled by the resolved --no-allow-xffrc setting");
  }
  return absl::OkStatus();
}

bool OverloadsPreset(std::string_view name) {
  return IsBuiltinStyle(name);
}

namespace {

class ConfigGate {
 public:
  ConfigGate(const ConfigInputs& inputs, bool armed, const std::vector<std::string>& cli, std::string_view invocation)
      : result_{.config = inputs}, armed_(armed), trusted_names_{std::string(invocation)} {
    ConfigInputs trusted = inputs;
    trusted.xffrc.clear();
    std::erase_if(trusted.user.named, [](const IniSection& section) { return OverloadsPreset(section.name); });
    std::vector<std::string> selectors = cli;
    for (const auto& name : inputs.configs) {
      selectors.push_back(absl::StrCat("--config=", name));
    }
    for (const auto& flag : ResolveConfigInOrder(inputs, selectors, invocation)) {
      if (!flag.is_argument && flag.flag.starts_with("--config=")) {
        active_names_.push_back(flag.flag.substr(9));
      }
    }
    for (const auto& flag : ResolveConfigInOrder(trusted, selectors, invocation)) {
      if (!flag.is_argument && flag.flag.starts_with("--config=")) {
        trusted_names_.push_back(flag.flag.substr(9));
      }
    }
  }

  GateResult Apply() {
    result_.config.system = Filter(result_.config.system, Source::kSystem);
    result_.config.user = Filter(result_.config.user, Source::kUser);
    for (ExplicitConfig& file : result_.config.xffrc) {
      file.config = Filter(file.config, Source::kXffrc);
    }
    return std::move(result_);
  }

 private:
  std::optional<DropReason> Reason(const IniLine& line, std::string_view name, Source layer) const {
    if (layer != Source::kSystem && OverloadsPreset(name)) {
      return DropReason::kPresetOverload;
    }
    if (LineSafety(line) == registry::Safety::kNone) {
      return std::nullopt;
    }
    if (!armed_ && !name.empty() && layer != Source::kXffrc && absl::c_contains(active_names_, name)
        && !absl::c_contains(trusted_names_, name)) {
      return DropReason::kUntrustedSelection;
    }
    if (layer == Source::kXffrc && !armed_) {
      return DropReason::kUnarmedXffrc;
    }
    return std::nullopt;
  }

  bool Keep(const IniLine& line, std::string_view name, Source layer) {
    const auto reason = Reason(line, name, layer);
    if (!reason.has_value()) {
      return true;
    }
    result_.drops.push_back(
        {.line = line,
         .config_name = std::string(name),
         .layer = layer,
         .safety = LineSafety(line),
         .reason = *reason});
    return false;
  }

  ConfigFile Filter(ConfigFile file, Source layer) {
    if (file.global_lines.empty() && !file.globals.empty()) {
      file.global_lines.push_back({.tokens = file.globals});
    }
    std::erase_if(file.global_lines, [&](const IniLine& line) { return !Keep(line, "", layer); });
    file.globals.clear();
    for (const IniLine& line : file.global_lines) {
      file.globals.insert(file.globals.end(), line.tokens.begin(), line.tokens.end());
    }
    for (IniSection& section : file.named) {
      std::erase_if(section.lines, [&](const IniLine& line) { return !Keep(line, section.name, layer); });
    }
    return file;
  }

  GateResult result_;
  bool armed_;
  std::vector<std::string> trusted_names_;
  std::vector<std::string> active_names_;
};

}  // namespace

GateResult GateConfig(
    const ConfigInputs& inputs,
    bool xffrc_armed,
    const std::vector<std::string>& cli_globals,
    std::string_view invocation_selector) {
  return ConfigGate(inputs, xffrc_armed, cli_globals, invocation_selector).Apply();
}

std::string DropMessage(const Drop& drop) {
  if (drop.reason == DropReason::kPresetOverload) {
    return absl::StrCat("'[", drop.config_name, "]' in the ", SourceName(drop.layer), " .xffrc");
  }
  const std::string_view primary =
      drop.line.tokens.empty() ? std::string_view("(empty line)") : drop.line.tokens.front();
  if (drop.reason == DropReason::kUnarmedXffrc) {
    return absl::StrCat("'", primary, "' from the --xffrc file (", ClassName(drop.safety), "; needs --allow-exec)");
  }
  return absl::StrCat(
      "'", primary, "' from the ", SourceName(drop.layer), " .xffrc (", ClassName(drop.safety),
      "; selected only through an explicit file, needs --allow-exec)");
}

}  // namespace xff::config
