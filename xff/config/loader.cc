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

#include "xff/config/loader.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"

namespace xff::config {
namespace {

void AppendXffrc(std::vector<RcLine>& out, std::string_view text) {
  const std::vector<RcLine> lines = ParseXffrc(text);
  out.insert(out.end(), lines.begin(), lines.end());
}

}  // namespace

std::string UserConfigPath(const DiscoveryOptions& opts) {
  if (opts.xff_config.has_value() && !opts.xff_config->empty()) {
    return *opts.xff_config;
  }
  if (opts.xdg_config_home.has_value() && !opts.xdg_config_home->empty()) {
    return absl::StrCat(*opts.xdg_config_home, "/xff/config");
  }
  if (opts.home.has_value() && !opts.home->empty()) {
    return absl::StrCat(*opts.home, "/.config/xff/config");
  }
  return "";
}

ConfigInputs Discover(const DiscoveryOptions& opts, FileReader read) {
  ConfigInputs inputs;
  inputs.no_system_config = opts.no_system_config;
  inputs.no_user_config = opts.no_user_config;
  inputs.configs = opts.configs;

  // System defaults and policy, at the lowest-precedence config tier.
  {
    const std::optional<std::string> text = read("/etc/xff.ini");
    inputs.sources.push_back({.path = "/etc/xff.ini", .layer = Source::kSystem, .found = text.has_value()});
    if (text.has_value()) {
      inputs.system = ParseIni(*text);
    }
  }
  // User: the first existing of $XFF_CONFIG / $XDG_CONFIG_HOME/xff/config / ~/.config/xff/config.
  if (const std::string user_path = UserConfigPath(opts); !user_path.empty()) {
    const std::optional<std::string> text = read(user_path);
    inputs.sources.push_back({.path = user_path, .layer = Source::kUser, .found = text.has_value()});
    if (text.has_value()) {
      AppendXffrc(inputs.user, *text);
    }
  }
  // Explicit --xffrc files form their own tier (inputs.xffrc), in order. Naming the file is the
  // consent to LOAD it, not to arm it: its dangerous directives stay inert unless --allow-exec is
  // set from a trusted tier (the gate enforces this). There is no auto-discovered project layer.
  for (const std::string& path : opts.xffrc_files) {
    const std::optional<std::string> text = read(path);
    inputs.sources.push_back({.path = path, .layer = Source::kXffrc, .found = text.has_value()});
    inputs.xffrc.push_back(
        ExplicitConfig{.path = path, .lines = text.has_value() ? ParseXffrc(*text) : std::vector<RcLine>{}});
  }
  return inputs;
}

DiscoveryOptions SelectorsFromGlobals(const std::vector<std::string>& globals) {
  constexpr std::string_view kConfig = "--config=";
  constexpr std::string_view kXffrc = "--xffrc=";
  DiscoveryOptions opts;
  for (const std::string& global : globals) {
    if (global == "--no-config") {
      opts.no_system_config = true;
      opts.no_user_config = true;
    } else if (global == "--no-system-config") {
      opts.no_system_config = true;
    } else if (global == "--no-user-config") {
      opts.no_user_config = true;
    } else if (global.starts_with(kConfig)) {
      opts.configs.push_back(global.substr(kConfig.size()));
    } else if (global.starts_with(kXffrc)) {
      opts.xffrc_files.push_back(global.substr(kXffrc.size()));
    }
  }
  return opts;
}

}  // namespace xff::config
