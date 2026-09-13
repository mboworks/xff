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

#include "xff/cli/main.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::cli {
namespace {
using ::testing::Eq;

struct MainTest : ::testing::Test {};

TEST_F(MainTest, AccountLookupFailureStopsTheCliEvenWhenConfigSkipsAreRequested) {
  int calls = 0;
  const auto lookup = [&]() -> absl::StatusOr<config::ConfigPaths> {
    ++calls;
    return absl::UnavailableError("account service unavailable");
  };
  EXPECT_THAT(::xff::cli::Run("xff", {"--no-config", "--explain"}, lookup), Eq(2));
  EXPECT_THAT(calls, Eq(1));
}

TEST_F(MainTest, MetaHelpDoesNotRequireAccountLookup) {
  int calls = 0;
  const auto lookup = [&]() -> absl::StatusOr<config::ConfigPaths> {
    ++calls;
    return absl::UnavailableError("account service unavailable");
  };
  EXPECT_THAT(::xff::cli::Run("xff", {"--help=config"}, lookup), Eq(0));
  EXPECT_THAT(calls, Eq(0));
}

}  // namespace
}  // namespace xff::cli
