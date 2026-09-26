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

#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::cli {
namespace {
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::_;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Return;

struct HostFs final : vfs::FileSystem {
  MOCK_METHOD(
      absl::StatusOr<vfs::Metadata>,
      StatFields,
      (std::string_view path, bool follow, vfs::MetadataFields fields),
      (const, override));
  MOCK_METHOD(
      absl::StatusOr<std::string>,
      ReadContentRange,
      (std::string_view path, std::uint64_t offset, std::size_t length),
      (const, override));

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

TEST_F(RgInputTest, FieldAwareMetadataKeepsTheHostRequestAndStatus) {
  const RgInputFs fs(host, "input");
  EXPECT_CALL(host, StatFields("other", false, vfs::MetadataFields::kBasic))
      .WillOnce(Return(vfs::Metadata{.size = 42}));
  EXPECT_CALL(host, StatFields("link", true, vfs::MetadataFields::kBirthTime))
      .WillOnce(Return(absl::PermissionDeniedError("metadata denied")));
  EXPECT_THAT(
      fs.StatFields("other", false, vfs::MetadataFields::kBasic), IsOkAndHolds(Field(&vfs::Metadata::size, Eq(42))));
  EXPECT_THAT(
      fs.StatFields("link", true, vfs::MetadataFields::kBirthTime),
      StatusIs(absl::StatusCode::kPermissionDenied, "metadata denied"));
}

TEST_F(RgInputTest, BoundedHostReadsKeepTheRequestAndStatus) {
  const RgInputFs fs(host, "input");
  EXPECT_CALL(host, ReadContentRange("other", 7, 3)).WillOnce(Return("bounded"));
  EXPECT_CALL(host, ReadContentRange("missing", 0, 1)).WillOnce(Return(absl::NotFoundError("missing content")));
  EXPECT_THAT(fs.ReadContentRange("other", 7, 3), IsOkAndHolds("bounded"));
  EXPECT_THAT(fs.ReadContentRange("missing", 0, 1), StatusIs(absl::StatusCode::kNotFound, "missing content"));
}

TEST_F(RgInputTest, StdinMetadataAndRangesDoNotReachTheHost) {
  const RgInputFs fs(host, "input");
  EXPECT_CALL(host, StatFields(_, _, _)).Times(0);
  EXPECT_CALL(host, ReadContentRange(_, _, _)).Times(0);
  for (const auto fields : {vfs::MetadataFields::kBasic, vfs::MetadataFields::kBirthTime}) {
    ASSERT_OK_AND_ASSIGN(const auto metadata, fs.StatFields("-", true, fields));
    EXPECT_THAT(metadata.type, Eq(vfs::FileType::kRegular));
    EXPECT_THAT(metadata.size, Eq(5));
  }
  EXPECT_THAT(fs.ReadContentRange("-", 0, 5), IsOkAndHolds("input"));
  EXPECT_THAT(fs.ReadContentRange("-", 1, 2), IsOkAndHolds("np"));
  EXPECT_THAT(fs.ReadContentRange("-", 3, 99), IsOkAndHolds("ut"));
  EXPECT_THAT(fs.ReadContentRange("-", 1, 0), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(fs.ReadContentRange("-", 5, 1), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(fs.ReadContentRange("-", std::numeric_limits<std::uint64_t>::max(), 1), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(RgInputFs(host, "").ReadContentRange("-", 0, 1), IsOkAndHolds(IsEmpty()));
}

TEST_F(RgInputTest, NoInputLeavesLiteralDashOnTheHostFilesystem) {
  const RgInputFs fs(host, std::nullopt);
  EXPECT_THAT(fs.ReadContent("-"), IsOkAndHolds("host content"));
  EXPECT_CALL(host, StatFields("-", true, vfs::MetadataFields::kBasic)).WillOnce(Return(vfs::Metadata{.size = 17}));
  EXPECT_CALL(host, ReadContentRange("-", 2, 4)).WillOnce(Return("dash"));
  EXPECT_THAT(fs.StatFields("-", true, vfs::MetadataFields::kBasic), IsOkAndHolds(Field(&vfs::Metadata::size, Eq(17))));
  EXPECT_THAT(fs.ReadContentRange("-", 2, 4), IsOkAndHolds("dash"));
}
}  // namespace
}  // namespace xff::cli
