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

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"

namespace xff::config {
namespace {
struct ReadSource {
  ConfigSource source;
  ConfigFile config;
};

absl::StatusOr<ReadSource> ReadConfigSource(std::string_view path, Source layer, FileReader read, bool required) {
  auto text = read(path);
  if (!text.ok()) {
    if (!required && absl::IsNotFound(text.status())) {
      return ReadSource{.source = {.path = std::string(path), .layer = layer}};
    }
    return absl::Status(
        text.status().code(), absl::StrCat("cannot read configuration '", path, "': ", text.status().message()));
  }
  return ReadSource{.source = {.path = std::string(path), .layer = layer, .found = true}, .config = ParseIni(*text)};
}
}  // namespace

absl::StatusOr<ConfigPaths> ConfigPathsFromAccountLookup(AccountHomeLookup lookup) {
  constexpr std::size_t kMaxBuffer = 1'048'576;
  for (std::size_t buffer_size = 16'384;; buffer_size *= 2) {
    auto home = lookup(buffer_size);
    if (absl::IsOutOfRange(home.status()) && buffer_size < kMaxBuffer) {
      continue;
    }
    MBO_ASSIGN_OR_RETURN(const auto account_home, std::move(home));
    MBO_ASSIGN_OR_RETURN(auto user, UserConfigPath(account_home));
    return ConfigPaths{.user = std::move(user)};
  }
}

absl::StatusOr<std::string> UserConfigPath(std::string_view account_home) {
  if (!account_home.starts_with('/')) {
    return absl::InvalidArgumentError("the OS account home must be an absolute path");
  }
  return absl::StrCat(account_home, account_home.ends_with('/') ? "" : "/", ".config/xff/config");
}

absl::StatusOr<ConfigInputs> DiscoverAutomatic(const DiscoveryOptions& opts, FileReader read) {
  ConfigInputs inputs;
  inputs.no_config = opts.no_config;
  inputs.no_system_config = opts.no_system_config;
  inputs.no_user_config = opts.no_user_config;
  inputs.configs = opts.configs;

  MBO_ASSIGN_OR_RETURN(auto system, ReadConfigSource(opts.paths.system, Source::kSystem, read, false));
  inputs.sources.push_back(std::move(system.source));
  inputs.system = std::move(system.config);
  if (!opts.paths.user.empty()) {
    MBO_ASSIGN_OR_RETURN(auto user, ReadConfigSource(opts.paths.user, Source::kUser, read, false));
    inputs.sources.push_back(std::move(user.source));
    inputs.user = std::move(user.config);
  }
  for (const std::string& path : opts.xffrc_files) {
    inputs.xffrc.push_back(ExplicitConfig{.path = path});
  }
  return inputs;
}

absl::StatusOr<ConfigInputs> DiscoverExplicit(ConfigInputs inputs, FileReader read) {
  for (ExplicitConfig& file : inputs.xffrc) {
    if (file.automatic) {
      continue;
    }
    MBO_ASSIGN_OR_RETURN(auto source, ReadConfigSource(file.path, Source::kXffrc, read, true));
    inputs.sources.push_back(std::move(source.source));
    file.config = std::move(source.config);
  }
  return inputs;
}

absl::StatusOr<ConfigInputs> Discover(const DiscoveryOptions& opts, FileReader read) {
  MBO_ASSIGN_OR_RETURN(auto automatic, DiscoverAutomatic(opts, read));
  return DiscoverExplicit(std::move(automatic), read);
}

DiscoveryOptions SelectorsFromGlobals(const std::vector<std::string>& globals) {
  constexpr std::string_view kConfig = "--config=";
  constexpr std::string_view kXffrc = "--xffrc=";
  DiscoveryOptions opts;
  for (const std::string& global : globals) {
    if (global == "--no-config") {
      opts.no_config = true;
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
