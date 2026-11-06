// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <string>
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
#include "xff/vfs/read_source.h"

namespace xff::archive {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsFalse;
using ::testing::IsTrue;

constexpr std::string_view kPayload = "retained member content\n";

struct ArchiveMemberKeyTest : ::testing::Test {
  static std::string Directory() {
    std::string directory(96, 'd');
    return directory;
  }

  static absl::StatusOr<ArchiveFileSystem> Fs() {
    const std::string file = Directory() + "/file.txt";
    const std::string link = Directory() + "/link";
    const auto members = std::to_array<MemoryArchiveMember>({
        {
            .path = file,
            .content = kPayload,
        },
        {
            .path = link,
            .symlink_target = file,
        },
    });
    MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryTar(members));
    return ArchiveFileSystem::OpenSource("fixture.tar", vfs::MemoryReadSource(std::move(bytes)));
  }
};

TEST_F(ArchiveMemberKeyTest, BoundedNormalizedInputViewsKeepExactMemberIdentity) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs());
  const std::string member = Directory() + "/file.txt";
  constexpr std::string_view kPrefix = "unrelated-prefix:";
  constexpr std::string_view kSuffix = ":unrelated-suffix";
  const std::string path = std::string(kPrefix) + "fixture.tar!././" + member + "///" + std::string(kSuffix);
  const std::string_view bounded =
      std::string_view(path).substr(kPrefix.size(), path.size() - kPrefix.size() - kSuffix.size());
  EXPECT_THAT(fs.Stat(bounded, false), IsOkAndHolds(Field(&vfs::Metadata::size, Eq(kPayload.size()))));
  EXPECT_THAT(fs.Access(bounded, vfs::AccessMode::kRead), IsTrue());
  EXPECT_THAT(fs.ReadContent(bounded), IsOkAndHolds(EqualsText(kPayload)));
  EXPECT_THAT(fs.ReadContent(bounded), IsOkAndHolds(EqualsText(kPayload)));
  const std::string link = "fixture.tar!././" + Directory() + "/link///";
  EXPECT_THAT(fs.ReadLink(link), IsOkAndHolds(EqualsText(member)));
}

TEST_F(ArchiveMemberKeyTest, DeferredContentSourceOwnsKeyAfterCallerAndFilesystemDestruction) {
  const auto make = [] -> absl::StatusOr<vfs::SharedReadSource> {
    MBO_ASSIGN_OR_RETURN(const auto fs, Fs());
    std::string path = "fixture.tar!././" + Directory() + "/file.txt///";
    MBO_ASSIGN_OR_RETURN(auto source, fs.ContentSource(path));
    std::ranges::fill(path, 'x');
    return source;
  };
  MBO_ASSERT_OK_AND_ASSIGN(const auto source, make());
  EXPECT_THAT(vfs::ReadSourceBytes(*source, 1'024), IsOkAndHolds(EqualsText(kPayload)));
  EXPECT_THAT(vfs::ReadSourceBytes(*source, 1'024), IsOkAndHolds(EqualsText(kPayload)));
}

TEST_F(ArchiveMemberKeyTest, RootMissingForeignAndReadOnlyAccessKeepTheirMeaning) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs());
  EXPECT_THAT(fs.Stat("fixture.tar", false), IsOkAndHolds(Field(&vfs::Metadata::type, Eq(vfs::FileType::kDirectory))));
  EXPECT_THAT(fs.Access("fixture.tar", vfs::AccessMode::kRead), IsTrue());
  EXPECT_THAT(fs.Access("fixture.tar!" + Directory() + "/file.txt", vfs::AccessMode::kWrite), IsFalse());
  EXPECT_THAT(fs.Stat("fixture.tar!" + Directory() + "/missing", false), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(fs.Stat("other.tar!" + Directory() + "/file.txt", false), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveMemberKeyTest, BoundedDirectoryRangeExcludesEqualLengthAndAdjacentSiblingKeys) {
  constexpr auto kMembers = std::to_array<MemoryArchiveMember>({
      {
          .path = "aa/selected.txt",
      },
      {
          .path = "aa/deep/hidden.txt",
      },
      {
          .path = "a!/outside.txt",
      },
      {
          .path = "aa-/outside.txt",
      },
      {
          .path = "aa0/outside.txt",
      },
      {
          .path = "ab/outside.txt",
      },
      {
          .path = "zz/outside.txt",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(auto bytes, MakeMemoryTar(kMembers));
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, ArchiveFileSystem::OpenBytes("fixture.tar", std::move(bytes)));
  const auto children =
      ElementsAre(Field(&vfs::Entry::name, EqualsText("deep")), Field(&vfs::Entry::name, EqualsText("selected.txt")));
  EXPECT_THAT(fs.ReadDir("fixture.tar!././aa///"), IsOkAndHolds(children));
  EXPECT_THAT(
      fs.ReadDirMetadata("fixture.tar!aa", vfs::MetadataFields::kBasic),
      IsOkAndHolds(Field(&vfs::DirectoryMetadata::entries, children)));
}

}  // namespace
}  // namespace xff::archive
