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

#include "xff/config/config.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"

namespace xff::config {
namespace {

constexpr std::string_view kAllowNoConfig = "--allow-no-config";
constexpr std::string_view kAllowNoSystemConfig = "--allow-no-system-config";
constexpr std::string_view kAllowNoUserConfig = "--allow-no-user-config";
constexpr std::string_view kAllowXffrc = "--allow-xffrc";
constexpr std::string_view kNoAllowNoSystemConfig = "--no-allow-no-system-config";
constexpr std::string_view kNoAllowNoUserConfig = "--no-allow-no-user-config";
constexpr std::string_view kNoAllowNoConfig = "--no-allow-no-config";
constexpr std::string_view kNoAllowXffrc = "--no-allow-xffrc";

bool IsSkipPermission(std::string_view flag) {
  return flag == kAllowNoConfig || flag == kNoAllowNoConfig || flag == kAllowNoSystemConfig
         || flag == kAllowNoUserConfig || flag == kNoAllowNoSystemConfig || flag == kNoAllowNoUserConfig
         || flag == kAllowXffrc || flag == kNoAllowXffrc;
}

// An .xffrc line applies under the active --config selectors when its base is
// "common"/empty or names an active config, AND its config is empty or names one.
bool LineApplies(const RcLine& line, const std::vector<std::string>& configs) {
  const bool base_ok = line.base.empty() || line.base == "common" || absl::c_contains(configs, line.base);
  const bool config_ok = line.config.empty() || absl::c_contains(configs, line.config);
  return base_ok && config_ok;
}

void AppendMatching(
    std::vector<ResolvedFlag>& out,
    const std::vector<RcLine>& lines,
    const std::vector<std::string>& configs,
    Source source) {
  for (const RcLine& line : lines) {
    if (!LineApplies(line, configs)) {
      continue;
    }
    for (const std::string& flag : line.flags) {
      if (!IsSkipPermission(flag)) {
        out.push_back(ResolvedFlag{.flag = flag, .source = source});
      }
    }
  }
}

class OrderedResolver {
 public:
  OrderedResolver(const ConfigInputs& inputs, std::string_view invocation_selector)
      : inputs_(inputs), selectors_{std::string(invocation_selector)}, user_emitted_(inputs.user.size()) {
    file_loaded_.resize(inputs.xffrc.size());
    file_emitted_.reserve(inputs.xffrc.size());
    for (const ExplicitConfig& file : inputs.xffrc) {
      file_emitted_.emplace_back(file.lines.size());
    }
  }

  std::vector<ResolvedFlag> Resolve(const std::vector<std::string>& cli_globals) {
    EmitSystem();
    EmitMatching();
    for (const std::string& global : cli_globals) {
      EmitFlag(global, Source::kCli);
      LoadExplicitFile(global);
    }
    return std::move(application_);
  }

 private:
  void EmitSystem() {
    if (inputs_.no_system_config) {
      return;
    }
    for (const std::string& flag : inputs_.system.defaults) {
      if (!IsSkipPermission(flag)) {
        EmitFlag(flag, Source::kSystem);
      }
    }
  }

  void EmitMatching() {
    if (!inputs_.no_user_config) {
      EmitLines(inputs_.user, user_emitted_, Source::kUser);
    }
    for (std::size_t index = 0; index < inputs_.xffrc.size(); ++index) {
      if (file_loaded_[index]) {
        EmitLines(inputs_.xffrc[index].lines, file_emitted_[index], Source::kXffrc);
      }
    }
  }

  void EmitLines(const std::vector<RcLine>& lines, std::vector<bool>& emitted, Source source) {
    for (std::size_t index = 0; index < lines.size(); ++index) {
      if (emitted[index] || !LineApplies(lines[index], selectors_)) {
        continue;
      }
      emitted[index] = true;
      for (const std::string& flag : lines[index].flags) {
        if (!IsSkipPermission(flag)) {
          EmitFlag(flag, source);
        }
      }
    }
  }

  void EmitFlag(const std::string& flag, Source source) {
    application_.push_back({.flag = flag, .source = source});
    constexpr std::string_view kConfig = "--config=";
    if (flag.starts_with(kConfig)) {
      selectors_.push_back(flag.substr(kConfig.size()));
      EmitMatching();
    }
  }

  void LoadExplicitFile(std::string_view global) {
    constexpr std::string_view kXffrc = "--xffrc=";
    if (!global.starts_with(kXffrc) || next_file_ >= inputs_.xffrc.size()) {
      return;
    }
    file_loaded_[next_file_] = true;
    EmitLines(inputs_.xffrc[next_file_].lines, file_emitted_[next_file_], Source::kXffrc);
    ++next_file_;
  }

  const ConfigInputs& inputs_;
  std::vector<ResolvedFlag> application_;
  std::vector<std::string> selectors_;
  std::vector<bool> user_emitted_;
  std::vector<bool> file_loaded_;
  std::vector<std::vector<bool>> file_emitted_;
  std::size_t next_file_ = 0;
};

}  // namespace

std::vector<ResolvedFlag> ResolveConfig(const ConfigInputs& inputs) {
  std::vector<ResolvedFlag> resolved;
  if (!inputs.no_system_config) {
    for (const std::string& flag : inputs.system.defaults) {  // system defaults are lowest precedence
      if (!IsSkipPermission(flag)) {
        resolved.push_back(ResolvedFlag{.flag = flag, .source = Source::kSystem});
      }
    }
  }
  if (!inputs.no_user_config) {
    AppendMatching(resolved, inputs.user, inputs.configs, Source::kUser);
  }
  for (const ExplicitConfig& file : inputs.xffrc) {
    AppendMatching(resolved, file.lines, inputs.configs, Source::kXffrc);
  }
  return resolved;
}

std::vector<ResolvedFlag> ResolveConfigInOrder(
    const ConfigInputs& inputs,
    const std::vector<std::string>& cli_globals,
    std::string_view invocation_selector) {
  return OrderedResolver(inputs, invocation_selector).Resolve(cli_globals);
}

std::string_view SourceName(Source source) {
  switch (source) {
    case Source::kCli: return "cli";
    case Source::kSystem: return "system";
    case Source::kUnset: return "unset";
    case Source::kUser: return "user";
    case Source::kXffrc: return "xffrc";
  }
  return "unset";
}

bool ArmedFromTrustedTier(
    const ConfigInputs& inputs,
    const std::vector<std::string>& cli_globals,
    std::string_view flag) {
  if (absl::c_contains(cli_globals, flag)) {
    return true;  // typed on the CLI: explicit consent
  }
  if (absl::c_contains(inputs.system.defaults, flag)) {
    return !inputs.no_system_config;  // root-authored, applying system defaults
  }
  // An applying user .xffrc line (inputs.xffrc is intentionally NOT consulted: a named file
  // cannot arm itself). A line applies under the active --config selectors, like ResolveConfig.
  return !inputs.no_user_config && absl::c_any_of(inputs.user, [&](const RcLine& line) {
    return LineApplies(line, inputs.configs) && absl::c_contains(line.flags, flag);
  });
}

bool IsBuiltinStyle(std::string_view name) {
  return name == "find" || name == "rg" || name == "xff";
}

registry::Style ActiveStyle(const std::vector<std::string>& configs) {
  registry::Style style = registry::Style::kXff;  // the modern xff style is the default
  for (const std::string& name : configs) {
    std::string_view base = name;
    base = base.substr(0, base.find(':'));  // "xff:2" pins an epoch; the base picks the style
    if (base == "find") {
      style = registry::Style::kFind;
    } else if (base == "xff") {
      style = registry::Style::kXff;
    } else if (base == "rg") {
      style = registry::Style::kRg;  // the single opinionated style (gitignore, skip hidden, smart case)
    }
  }
  return style;
}

namespace {

// The active style's name for --explain output.
std::string_view StyleName(registry::Style style) {
  switch (style) {
    case registry::Style::kFind: return "find";
    case registry::Style::kRg: return "rg";
    case registry::Style::kXff: return "xff";
  }
  return "xff";
}

}  // namespace

std::string_view DefaultStyleForProgram(std::string_view argv0) {
  if (const std::string_view::size_type slash = argv0.rfind('/'); slash != std::string_view::npos) {
    argv0 = argv0.substr(slash + 1);  // basename: the last path component
  }
  if (argv0.empty()) {
    return "xff";  // no invocation name -> the modern default
  }
  // A `_full` suffix marks the extras-included twin of a program (the `xff_full` full build, and
  // more generally `<prefix>_full`); it behaves exactly as its base name, so `xff_full` -> xff,
  // `find_full` -> find, `rg_full` -> rg. Without this strip, `find_full` would not match the
  // `find` built-in selector and would silently fall through to the xff default, losing find
  // semantics. The `_full` suffix is thus reserved for the full build and never a config name.
  if (argv0.ends_with("_full")) {
    argv0.remove_suffix(std::string_view("_full").size());
    if (argv0.empty()) {
      return "xff";  // a bare `_full` invocation name -> the modern default
    }
  }
  // The invocation name is the leading --config selector, used verbatim: a built-in style name
  // (find/xff/rg) selects that preset; any other name (a `mytool` symlink to xff, and note there
  // is no `xfd`/`fd` magic - those are just names too) selects a same-named NAMED config, leaving
  // the base style at the modern xff default (ActiveStyle ignores a non-style selector). So
  // aliasing xff auto-activates the matching `mytool:` config block without a preset ever being
  // overloadable. An explicit --config still stacks (last wins).
  return argv0;
}

std::string ExplainConfig(const std::vector<ResolvedFlag>& application) {
  std::string out = "# xff effective configuration (application order; later overrides earlier)\n";
  for (const ResolvedFlag& flag : application) {
    absl::StrAppend(&out, SourceName(flag.source), "\t", flag.flag, "\n");
  }
  return out;
}

std::string ExplainSources(const std::vector<ConfigSource>& sources, registry::Style style) {
  std::string out = absl::StrCat("# xff active style: ", StyleName(style), "\n");
  absl::StrAppend(&out, "# config sources consulted (precedence order)\n");
  for (const ConfigSource& source : sources) {
    absl::StrAppend(
        &out, "source\t", SourceName(source.layer), "\t", source.found ? "found" : "absent", "\t", source.path, "\n");
  }
  return out;
}

}  // namespace xff::config
