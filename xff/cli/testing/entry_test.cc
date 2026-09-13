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

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/cli/main.h"
#include "xff/env/env.h"

// XFF_ABI_POINTER: C runtime entry point supplies argv.
int main(int argc, char** argv) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): C runtime argument range
  const std::vector<std::string> args(argv + 1, argv + argc);
  const xff::config::ConfigPaths paths{
      .system = xff::env::Get("XFF_TEST_SYSTEM_CONFIG").value_or("/nonexistent/xff-test-system.ini"),
      .user = xff::env::Get("XFF_TEST_USER_CONFIG").value_or("/nonexistent/xff-test-user.ini"),
  };
  return xff::cli::Run(*argv, args, [&paths]() -> absl::StatusOr<xff::config::ConfigPaths> { return paths; });
}
