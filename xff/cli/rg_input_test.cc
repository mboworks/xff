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

#include "xff/cli/rg_input.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::cli {
namespace {
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct HostFs final : vfs::FileSystem {
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override { return std::vector<vfs::Entry>{}; }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return vfs::Metadata{.type = vfs::FileType::kRegular};
  }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "mutation";
    return absl::OkStatus();
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return "target"; }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "host"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return false; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return "host content"; }
};

struct RgInputTest : ::testing::Test {
  HostFs host;
};

TEST_F(RgInputTest, StdinIsAnImmutableRegularFile) {
  const RgInputFs fs(host, "input");
  ASSERT_OK_AND_ASSIGN(const auto metadata, fs.Stat("-", false));
  EXPECT_THAT(metadata.type, Eq(vfs::FileType::kRegular));
  EXPECT_THAT(metadata.size, Eq(5));
  EXPECT_THAT(fs.ReadContent("-"), IsOkAndHolds("input"));
  EXPECT_THAT(fs.ReadDir("-"), StatusIs(absl::StatusCode::kFailedPrecondition, _));
  EXPECT_THAT(fs.ReadLink("-"), StatusIs(absl::StatusCode::kInvalidArgument, _));
  EXPECT_THAT(fs.FsType("-"), IsOkAndHolds("stream"));
  EXPECT_THAT(fs.IsCaseSensitive("-"), IsOkAndHolds(true));
  EXPECT_THAT(fs.Access("-", vfs::AccessMode::kRead), IsTrue());
  EXPECT_THAT(fs.Access("-", vfs::AccessMode::kWrite), IsFalse());
  EXPECT_THAT(fs.Remove("-"), StatusIs(absl::StatusCode::kPermissionDenied, _));
  EXPECT_THAT(fs.ContentSource("-"), IsOkAndHolds(_));
}

TEST_F(RgInputTest, HostReadsAreForwardedButMutationsRemainDisconnected) {
  const RgInputFs fs(host, "input");
  EXPECT_THAT(fs.Stat("other", true), IsOkAndHolds(_));
  EXPECT_THAT(fs.ReadDir("other"), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(fs.ReadContent("other"), IsOkAndHolds("host content"));
  EXPECT_THAT(fs.ReadLink("other"), IsOkAndHolds("target"));
  EXPECT_THAT(fs.FsType("other"), IsOkAndHolds("host"));
  EXPECT_THAT(fs.IsCaseSensitive("other"), IsOkAndHolds(false));
  EXPECT_THAT(fs.Access("other", vfs::AccessMode::kRead), IsFalse());
  EXPECT_THAT(fs.ContentSource("other"), IsOkAndHolds(_));
  EXPECT_THAT(fs.Remove("other"), StatusIs(absl::StatusCode::kPermissionDenied, _));
}

TEST_F(RgInputTest, NoInputLeavesLiteralDashOnTheHostFilesystem) {
  const RgInputFs fs(host, std::nullopt);
  EXPECT_THAT(fs.ReadContent("-"), IsOkAndHolds("host content"));
}
}  // namespace
}  // namespace xff::cli
