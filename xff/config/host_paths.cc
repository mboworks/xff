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

// Expose the reentrant POSIX account lookup under strict C++ mode on glibc.
#if defined(__linux__) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE 1
#endif

#include <pwd.h>
#include <unistd.h>

#include <cstddef>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "xff/config/loader.h"

namespace xff::config {
namespace {

// XFF_HOST_IO: query the effective OS account independently of environment overrides.
absl::StatusOr<std::string> ReadAccountHome(std::size_t buffer_size) {
  std::vector<char> buffer(buffer_size);
  struct passwd account{};
  // XFF_ABI_POINTER: getpwuid_r returns an observer into account and buffer.
  struct passwd* result = nullptr;
  const int error = getpwuid_r(geteuid(), &account, buffer.data(), buffer.size(), &result);
  if (error != 0) {
    return absl::ErrnoToStatus(error, "cannot resolve the effective OS account");
  }
  if (result == nullptr || account.pw_dir == nullptr) {
    return absl::NotFoundError("the effective OS account has no home directory");
  }
  return std::string(account.pw_dir);
}

}  // namespace

absl::StatusOr<ConfigPaths> DefaultConfigPaths() {
  return ConfigPathsFromAccountLookup(ReadAccountHome);
}

}  // namespace xff::config
