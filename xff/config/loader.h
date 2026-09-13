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

#ifndef XFF_CONFIG_LOADER_H_
#define XFF_CONFIG_LOADER_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "xff/config/config.h"
#include "xff/vfs/filesystem.h"

namespace xff::config {

// Reads config contents. NotFound means the path is absent; every other failure is an error.
using FileReader = absl::FunctionRef<absl::StatusOr<std::string>(std::string_view path)>;

struct ConfigPaths {
  std::string system = "/etc/xff.ini";
  std::string user;
};

// The effective OS account supplies the home; environment variables cannot redirect policy.
absl::StatusOr<ConfigPaths> DefaultConfigPaths();

// An account lookup receives its scratch-buffer size and returns a home directory.
// OutOfRange requests a larger buffer. Other errors propagate without an environment fallback.
using AccountHomeLookup = absl::FunctionRef<absl::StatusOr<std::string>(std::size_t buffer_size)>;
absl::StatusOr<ConfigPaths> ConfigPathsFromAccountLookup(AccountHomeLookup lookup);
absl::StatusOr<std::string> UserConfigPath(std::string_view account_home);

// CLI selectors and explicitly injected paths for discovery.
struct DiscoveryOptions {
  bool no_config = false;                // --no-config
  bool no_system_config = false;         // --no-system-config
  bool no_user_config = false;           // --no-user-config
  std::vector<std::string> configs;      // --config=NAME, in order
  std::vector<std::string> xffrc_files;  // --xffrc=FILE, in order
  ConfigPaths paths;
};

// Discovers and parses the config layers into ConfigInputs (ready for
// ResolveConfig), reading every file through `read`:
//   - system: /etc/xff.ini,
//   - user:   the fixed OS account path, in the shared INI grammar,
//   - --xffrc=FILE: separate parsed files, in order - a NON-ARMING tier whose
//     dangerous directives stay inert unless armed (naming the file is consent to load, not to arm).
// A requested skip still reads present trusted files so their controls can authorize it.
// Explicit files remain selected by --no-config. Root discovery is a separate,
// admission-checked step through DiscoverRc.
absl::StatusOr<ConfigInputs> Discover(const DiscoveryOptions& opts, FileReader read);

// Reads automatic files and records explicit paths without opening them. Validate the automatic
// system config and explicit-file admission before completing discovery with DiscoverExplicit.
absl::StatusOr<ConfigInputs> DiscoverAutomatic(const DiscoveryOptions& opts, FileReader read);

// Completes an automatic discovery by reading its explicit paths and recording their sources.
absl::StatusOr<ConfigInputs> DiscoverExplicit(ConfigInputs inputs, FileReader read);

// Resolve discovery from trusted system/user configuration and CLI directives only.
// --no-config suppresses discovery; .xffrc content cannot control discovery.
RcMode ResolveRcMode(
    const ConfigInputs& inputs,
    const std::vector<std::string>& cli_globals,
    std::string_view invocation_selector);
std::string_view RcModeName(RcMode mode);

// Discover physical .xffrc files in argument order, parent before children and
// lexicographically among siblings. Uses the VFS exclusively, never follows
// directory symlinks, and does not apply search filters to configuration discovery.
// Mode/admission must be resolved before calling. Discovered files precede explicit files.
absl::StatusOr<ConfigInputs> DiscoverRc(
    ConfigInputs inputs,
    const std::vector<std::string>& roots,
    const vfs::FileSystem& filesystem);

// Extracts the config selectors among `globals` into a DiscoveryOptions (the paths
// are supplied by the caller): --no-config, --no-system-config, --no-user-config,
// --config=NAME (in order),
// and --xffrc=FILE (in order). Every other global is ignored.
DiscoveryOptions SelectorsFromGlobals(const std::vector<std::string>& globals);

}  // namespace xff::config

#endif  // XFF_CONFIG_LOADER_H_
