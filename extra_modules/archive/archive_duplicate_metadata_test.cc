// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <span>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_fs.h"
#include "xff/archive/memory_archive_test.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::archive {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::SizeIs;

struct ArchiveDuplicateMetadataTest : ::testing::Test {
  static constexpr std::string_view kContainer = "duplicates.tar";
  static constexpr std::string_view kMember = "duplicates.tar!shared";

  static absl::StatusOr<ArchiveFileSystem> Open(std::span<const MemoryArchiveMember> members) {
    MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryTar(members));
    return ArchiveFileSystem::OpenBytes(kContainer, std::move(bytes));
  }
};

TEST_F(ArchiveDuplicateMetadataTest, FirstRegularSizeDescribesTheContentAndDirectoryObservation) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "./shared",
          .content = "x",
      },
      {
          .path = "shared",
          .content = "replacement",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Open(members));
  EXPECT_THAT(fs.Stat(kMember, false), IsOkAndHolds(Field("size", &vfs::Metadata::size, Eq(1U))));
  EXPECT_THAT(fs.ReadContent(kMember), IsOkAndHolds(EqualsText("x")));
  MBO_ASSERT_OK_AND_ASSIGN(const auto observed, fs.ReadDirMetadata(kContainer, {}));
  EXPECT_THAT(observed.entries, SizeIs(1));
  EXPECT_THAT(
      observed.observations,
      ElementsAre(AllOf(
          Field("status", &vfs::MetadataObservation::status, IsOk()),
          Field("value", &vfs::MetadataObservation::value, Field("size", &vfs::Metadata::size, Eq(1U))))));
}

TEST_F(ArchiveDuplicateMetadataTest, FirstRegularFileIsNotReplacedByADirectory) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "shared",
          .content = "first",
      },
      {
          .path = "./shared/",
          .directory = true,
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Open(members));
  EXPECT_THAT(
      fs.Stat(kMember, false), IsOkAndHolds(AllOf(
                                   Field("type", &vfs::Metadata::type, Eq(vfs::FileType::kRegular)),
                                   Field("mode", &vfs::Metadata::mode, Eq(0640U)))));
  EXPECT_THAT(fs.ReadContent(kMember), IsOkAndHolds(EqualsText("first")));
}

TEST_F(ArchiveDuplicateMetadataTest, FirstDirectoryIsNotReplacedByARegularFile) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "shared/",
          .directory = true,
      },
      {
          .path = "./shared",
          .content = "later",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Open(members));
  EXPECT_THAT(
      fs.Stat(kMember, false), IsOkAndHolds(AllOf(
                                   Field("type", &vfs::Metadata::type, Eq(vfs::FileType::kDirectory)),
                                   Field("mode", &vfs::Metadata::mode, Eq(0755U)))));
  EXPECT_THAT(fs.ReadContent(kMember), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(ArchiveDuplicateMetadataTest, FirstSymlinkIsNotReplacedByARegularFile) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "./shared",
          .symlink_target = "first-target",
      },
      {
          .path = "shared",
          .content = "later",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Open(members));
  EXPECT_THAT(fs.Stat(kMember, false), IsOkAndHolds(Field("type", &vfs::Metadata::type, Eq(vfs::FileType::kSymlink))));
  EXPECT_THAT(fs.ReadLink(kMember), IsOkAndHolds(EqualsText("first-target")));
  EXPECT_THAT(fs.ReadContent(kMember), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(ArchiveDuplicateMetadataTest, FirstSymlinkTargetIsRetained) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "shared",
          .symlink_target = "first-target",
      },
      {
          .path = "./shared",
          .symlink_target = "later-target",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Open(members));
  EXPECT_THAT(fs.ReadLink(kMember), IsOkAndHolds(EqualsText("first-target")));
}

TEST_F(ArchiveDuplicateMetadataTest, FirstStoredDirectoryReplacesItsSynthesizedPlaceholder) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "shared/leaf",
      },
      {
          .path = "./shared/",
          .directory = true,
      },
      {
          .path = "shared",
          .content = "later",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Open(members));
  EXPECT_THAT(
      fs.Stat(kMember, false), IsOkAndHolds(AllOf(
                                   Field("type", &vfs::Metadata::type, Eq(vfs::FileType::kDirectory)),
                                   Field("mode", &vfs::Metadata::mode, Eq(0755U)))));
  EXPECT_THAT(fs.ReadDir(kMember), IsOkAndHolds(SizeIs(1)));
}

}  // namespace
}  // namespace xff::archive
