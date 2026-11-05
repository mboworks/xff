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
//
// Environment-agnostic like the loader tests: whether fuse3 exists here is what is being resolved,
// so both states are pinned. That this file COMPILES is itself half the slice - the fetched libfuse
// headers parse and the function types they declare match what FuseApi stores.

#include "xff/fuse/fuse_api.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/fuse/fuse_loader.h"

namespace xff::fuse {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::NotNull;

struct FuseApiTest : ::testing::Test {};

TEST_F(FuseApiTest, ResolveMatchesTheLoadersVerdict) {
  const absl::StatusOr<FuseApi> api = FuseApi::Resolve();
  EXPECT_THAT(api.ok(), FuseAvailable());
}

TEST_F(FuseApiTest, AvailableMeansEveryPointerIsTypedAndNonNull) {
  if (!FuseAvailable()) {
    // NOLINTNEXTLINE(readability-implicit-bool-conversion): fires inside GTEST_SKIP's expansion.
    GTEST_SKIP() << "no FUSE3 on this machine: " << FuseLoader::Instance().Error();
  }
  MBO_ASSERT_OK_AND_ASSIGN(const FuseApi api, FuseApi::Resolve());
  EXPECT_THAT(api.session_new, NotNull());
  EXPECT_THAT(api.session_mount, NotNull());
  EXPECT_THAT(api.session_loop, NotNull());
  EXPECT_THAT(api.session_exit, NotNull());
  EXPECT_THAT(api.session_unmount, NotNull());
  EXPECT_THAT(api.session_destroy, NotNull());
  EXPECT_THAT(api.reply_err, NotNull());
  EXPECT_THAT(api.reply_attr, NotNull());
  EXPECT_THAT(api.reply_entry, NotNull());
  EXPECT_THAT(api.reply_buf, NotNull());
  EXPECT_THAT(api.reply_open, NotNull());
  EXPECT_THAT(api.req_userdata, NotNull());
}

TEST_F(FuseApiTest, UnavailableCarriesTheLoadersReason) {
  if (FuseAvailable()) {
    // NOLINTNEXTLINE(readability-implicit-bool-conversion): fires inside GTEST_SKIP's expansion.
    GTEST_SKIP() << "FUSE3 present (" << FuseLoader::Instance().Library() << ")";
  }
  EXPECT_THAT(FuseApi::Resolve(), StatusIs(absl::StatusCode::kUnavailable));
}

}  // namespace
}  // namespace xff::fuse
