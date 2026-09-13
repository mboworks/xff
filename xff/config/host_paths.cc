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

#include <cerrno>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/config/loader.h"

namespace xff::config {

// XFF_HOST_IO: query the effective OS account independently of environment overrides.
absl::StatusOr<ConfigPaths> DefaultConfigPaths() {
  std::vector<char> buffer(16'384);
  struct passwd account{};
  // XFF_ABI_POINTER: getpwuid_r returns an observer into account and buffer.
  struct passwd* result = nullptr;
  for (;;) {
    const int error = getpwuid_r(geteuid(), &account, buffer.data(), buffer.size(), &result);
    if (error == ERANGE && buffer.size() < 1'048'576) {
      buffer.resize(buffer.size() * 2);
      continue;
    }
    if (error != 0) {
      return absl::InternalError(absl::StrCat("cannot resolve the effective OS account: ", error));
    }
    if (result == nullptr || account.pw_dir == nullptr) {
      return absl::NotFoundError("the effective OS account has no home directory");
    }
    MBO_ASSIGN_OR_RETURN(auto user, UserConfigPath(account.pw_dir));
    return ConfigPaths{.user = std::move(user)};
  }
}

}  // namespace xff::config
