// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_fs.h"
#include "xff/archive/member_path.h"
#include "xff/archive/memory_archive_test.h"
#include "xff/vfs/entry.h"

namespace xff::archive {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsTrue;

constexpr auto kMembers = std::to_array<MemoryArchiveMember>({
    {
        .path = "top.txt",
    },
    {
        .path = "dir/alone.txt",
        .content = "abc",
    },
    {
        .path = "dir/sub/deep.txt",
    },
    {
        .path = "dir/.hidden",
    },
    {
        .path = "dir-empty/",
        .directory = true,
    },
    {
        .path = "dir-other/outside.txt",
    },
    {
        .path = "dir0/outside.txt",
    },
    {
        .path = "directory/outside.txt",
    },
    {
        .path = "././normalized/sub/value.txt",
    },
});

struct ArchiveDirectoryRangeTest : ::testing::Test {
  static absl::StatusOr<ArchiveFileSystem> Fs(
      std::string_view container = "fixture.tar",
      MemberPathOptions options = {}) {
    MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryTar(kMembers));
    return ArchiveFileSystem::OpenBytes(container, std::move(bytes), options);
  }
};

TEST_F(ArchiveDirectoryRangeTest, RootAndSubdirectoryHaveExactSortedDirectChildren) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs());
  EXPECT_THAT(
      fs.ReadDir("fixture.tar"),
      IsOkAndHolds(ElementsAre(
          Field(&vfs::Entry::name, EqualsText("dir")), Field(&vfs::Entry::name, EqualsText("dir-empty")),
          Field(&vfs::Entry::name, EqualsText("dir-other")), Field(&vfs::Entry::name, EqualsText("dir0")),
          Field(&vfs::Entry::name, EqualsText("directory")), Field(&vfs::Entry::name, EqualsText("normalized")),
          Field(&vfs::Entry::name, EqualsText("top.txt")))));
  EXPECT_THAT(
      fs.ReadDir("fixture.tar!dir"),
      IsOkAndHolds(ElementsAre(
          Field(&vfs::Entry::name, EqualsText(".hidden")), Field(&vfs::Entry::name, EqualsText("alone.txt")),
          Field(&vfs::Entry::name, EqualsText("sub")))));
  EXPECT_THAT(
      fs.ReadDir("fixture.tar!dir/sub"),
      IsOkAndHolds(ElementsAre(Field(&vfs::Entry::path, EqualsText("fixture.tar!dir/sub/deep.txt")))));
  EXPECT_THAT(fs.ReadDir("fixture.tar!dir-empty"), IsOkAndHolds(IsEmpty()));
}

TEST_F(ArchiveDirectoryRangeTest, EntryIdentityAndStoredOrSynthesizedMetadataAreRetained) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs());
  EXPECT_THAT(
      fs.ReadDir("fixture.tar!dir"),
      IsOkAndHolds(Each(AllOf(
          Field(&vfs::Entry::source, Eq(vfs::Source::kArchiveMember)), Field(&vfs::Entry::read_only, IsTrue())))));
  EXPECT_THAT(
      fs.Stat("fixture.tar!dir/alone.txt", false),
      IsOkAndHolds(AllOf(
          Field(&vfs::Metadata::type, Eq(vfs::FileType::kRegular)), Field(&vfs::Metadata::size, Eq(3)),
          Field(&vfs::Metadata::mode, Eq(0640)),
          Field(&vfs::Metadata::mtime, Eq(absl::FromUnixSeconds(1'700'000'000))))));
  EXPECT_THAT(
      fs.Stat("fixture.tar!dir/sub", false),
      IsOkAndHolds(
          AllOf(Field(&vfs::Metadata::type, Eq(vfs::FileType::kDirectory)), Field(&vfs::Metadata::mode, Eq(0555)))));
  EXPECT_THAT(fs.Stat("fixture.tar!dir-empty", false), IsOkAndHolds(Field(&vfs::Metadata::mode, Eq(0755))));
}

TEST_F(ArchiveDirectoryRangeTest, NormalizedLookupAndErrorsKeepTheirExistingMeaning) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs());
  EXPECT_THAT(
      fs.ReadDir("fixture.tar!././dir/sub///"),
      IsOkAndHolds(ElementsAre(Field(&vfs::Entry::name, EqualsText("deep.txt")))));
  EXPECT_THAT(
      fs.ReadDir("fixture.tar!normalized/sub"),
      IsOkAndHolds(ElementsAre(Field(&vfs::Entry::name, EqualsText("value.txt")))));
  EXPECT_THAT(fs.ReadDir("fixture.tar!missing"), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(fs.ReadDir("fixture.tar!dir/alone.txt"), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(fs.ReadDir("other.tar!dir"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveDirectoryRangeTest, NestedLabelsCustomSeparatorsAndPrefixesKeepRoundTripPaths) {
  constexpr auto kOptions = std::to_array<MemberPathOptions>({
      {
          .separator = "!/",
      },
      {
          .separator = "::",
          .prefix = "virtual:",
      },
      {
          .separator = "!",
          .prefix = kUriPrefix,
      },
  });
  constexpr std::string_view kContainer = "outer.tar!inner.tar";
  for (const auto& options : kOptions) {
    SCOPED_TRACE(options.separator);
    MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs(kContainer, options));
    const auto dir = JoinMemberPath(kContainer, "dir/sub", options);
    const auto leaf = JoinMemberPath(kContainer, "dir/sub/deep.txt", options);
    EXPECT_THAT(fs.ReadDir(dir), IsOkAndHolds(ElementsAre(Field(&vfs::Entry::path, EqualsText(leaf)))));
    EXPECT_THAT(fs.ReadDir(JoinMemberPath(kContainer, "dir-empty", options)), IsOkAndHolds(IsEmpty()));
  }
}

TEST_F(ArchiveDirectoryRangeTest, AbsoluteMembersAndTheStoredSlashRootAreNotCanonicalizedAway) {
  constexpr auto kAbsolute = std::to_array<MemoryArchiveMember>({
      {
          .path = "/",
          .directory = true,
      },
      {
          .path = "/abs/deep/value.txt",
      },
      {
          .path = "relative.txt",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(auto bytes, MakeMemoryTar(kAbsolute));
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, ArchiveFileSystem::OpenBytes("fixture.tar", std::move(bytes)));
  EXPECT_THAT(fs.ReadDir("fixture.tar!/"), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(
      fs.ReadDir("fixture.tar!/abs"),
      IsOkAndHolds(ElementsAre(Field(&vfs::Entry::path, EqualsText("fixture.tar!/abs/deep")))));
  EXPECT_THAT(
      fs.ReadDir("fixture.tar!/abs/deep"),
      IsOkAndHolds(ElementsAre(Field(&vfs::Entry::path, EqualsText("fixture.tar!/abs/deep/value.txt")))));
  EXPECT_THAT(
      fs.ReadDir("fixture.tar"), IsOkAndHolds(ElementsAre(
                                     Field(&vfs::Entry::path, EqualsText("fixture.tar!/")),
                                     Field(&vfs::Entry::path, EqualsText("fixture.tar!/abs")),
                                     Field(&vfs::Entry::path, EqualsText("fixture.tar!relative.txt")))));
}

}  // namespace
}  // namespace xff::archive
