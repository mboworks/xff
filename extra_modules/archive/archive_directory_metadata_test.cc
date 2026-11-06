// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
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
#include "xff/vfs/filesystem.h"

namespace xff::archive {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::SizeIs;

constexpr std::string_view kContainer = "fixture.tar";
constexpr auto kMembers = std::to_array<MemoryArchiveMember>({
    {
        .path = "plain.txt",
        .content = "plain",
    },
    {
        .path = "nested/leaf.txt",
        .content = "content",
    },
    {
        .path = "empty/",
        .directory = true,
    },
    {
        .path = "link",
        .symlink_target = "nested/leaf.txt",
    },
    {
        .path = "/absolute/item.txt",
        .content = "absolute",
    },
});

auto MatchesMetadata(const vfs::Metadata& value) {
  return AllOf(
      Field(&vfs::Metadata::type, Eq(value.type)), Field(&vfs::Metadata::source, Eq(value.source)),
      Field(&vfs::Metadata::size, Eq(value.size)), Field(&vfs::Metadata::blocks, Eq(value.blocks)),
      Field(&vfs::Metadata::mode, Eq(value.mode)), Field(&vfs::Metadata::nlink, Eq(value.nlink)),
      Field(&vfs::Metadata::uid, Eq(value.uid)), Field(&vfs::Metadata::gid, Eq(value.gid)),
      Field(&vfs::Metadata::ino, Eq(value.ino)), Field(&vfs::Metadata::dev, Eq(value.dev)),
      Field(&vfs::Metadata::atime, Eq(value.atime)), Field(&vfs::Metadata::mtime, Eq(value.mtime)),
      Field(&vfs::Metadata::ctime, Eq(value.ctime)), Field(&vfs::Metadata::btime, Eq(value.btime)));
}

struct ArchiveDirectoryMetadataTest : ::testing::Test {
  static absl::StatusOr<ArchiveFileSystem> Fs(std::string_view container = kContainer, MemberPathOptions options = {}) {
    MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryTar(kMembers));
    return ArchiveFileSystem::OpenBytes(container, std::move(bytes), options);
  }
};

TEST_F(ArchiveDirectoryMetadataTest, ObservationsMatchEveryStatFieldAndOrdinaryOrderedEntry) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs());
  constexpr auto kFields = std::to_array({vfs::MetadataFields::kBasic, vfs::MetadataFields::kBirthTime});
  constexpr auto kDirectories = std::to_array<std::string_view>({"", "nested", "/absolute", "empty"});
  for (const auto fields : kFields) {
    for (const auto directory : kDirectories) {
      SCOPED_TRACE(directory);
      const auto path = directory.empty() ? std::string(kContainer) : JoinMemberPath(kContainer, directory);
      MBO_ASSERT_OK_AND_ASSIGN(const auto ordinary, fs.ReadDir(path));
      MBO_ASSERT_OK_AND_ASSIGN(const auto observed, fs.ReadDirMetadata(path, fields));
      ASSERT_THAT(observed.entries, SizeIs(ordinary.size()));
      ASSERT_THAT(observed.observations, SizeIs(ordinary.size()));
      for (std::size_t index = 0; index < ordinary.size(); ++index) {
        const auto& expected = ordinary.at(index);
        const auto& actual = observed.entries.at(index);
        EXPECT_THAT(actual.path, EqualsText(expected.path));
        EXPECT_THAT(actual.name, EqualsText(expected.name));
        EXPECT_THAT(actual.type, Eq(expected.type));
        EXPECT_THAT(actual.source, Eq(expected.source));
        EXPECT_THAT(actual.read_only, Eq(expected.read_only));
        MBO_ASSERT_OK_AND_ASSIGN(const auto metadata, fs.StatFields(actual.path, false, fields));
        EXPECT_THAT(observed.observations.at(index).status, IsOk());
        EXPECT_THAT(observed.observations.at(index).value, MatchesMetadata(metadata));
      }
    }
  }
}

TEST_F(ArchiveDirectoryMetadataTest, KnownFilesDirectoriesAndLinksKeepTheirStoredAttributes) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs());
  MBO_ASSERT_OK_AND_ASSIGN(const auto listing, fs.ReadDirMetadata("fixture.tar!nested", vfs::MetadataFields::kBasic));
  ASSERT_THAT(listing.entries, SizeIs(1));
  ASSERT_THAT(listing.observations, SizeIs(1));
  EXPECT_THAT(listing.entries.front().name, EqualsText("leaf.txt"));
  EXPECT_THAT(listing.entries.front().read_only, IsTrue());
  EXPECT_THAT(
      listing.observations.front().value,
      AllOf(
          Field(&vfs::Metadata::type, Eq(vfs::FileType::kRegular)),
          Field(&vfs::Metadata::source, Eq(vfs::Source::kArchiveMember)), Field(&vfs::Metadata::size, Eq(7)),
          Field(&vfs::Metadata::mode, Eq(0640)), Field(&vfs::Metadata::nlink, Eq(1)),
          Field(&vfs::Metadata::mtime, Eq(absl::FromUnixSeconds(1'700'000'000))),
          Field(&vfs::Metadata::btime, Eq(std::nullopt))));
  MBO_ASSERT_OK_AND_ASSIGN(const auto root, fs.ReadDirMetadata(kContainer, vfs::MetadataFields::kBirthTime));
  EXPECT_THAT(
      root.entries,
      Contains(
          AllOf(Field(&vfs::Entry::name, EqualsText("link")), Field(&vfs::Entry::type, Eq(vfs::FileType::kSymlink)))));
  EXPECT_THAT(
      root.entries,
      Contains(AllOf(
          Field(&vfs::Entry::name, EqualsText("nested")), Field(&vfs::Entry::type, Eq(vfs::FileType::kDirectory)))));
}

TEST_F(ArchiveDirectoryMetadataTest, OwnedPathsAndObservationValuesSurviveCallerAndFilesystemDestruction) {
  const auto make = [] -> absl::StatusOr<vfs::DirectoryMetadata> {
    MBO_ASSIGN_OR_RETURN(const auto fs, Fs());
    std::string directory = "fixture.tar!nested";
    MBO_ASSIGN_OR_RETURN(auto listing, fs.ReadDirMetadata(directory, vfs::MetadataFields::kBirthTime));
    std::ranges::fill(directory, 'x');
    return listing;
  };
  MBO_ASSERT_OK_AND_ASSIGN(auto listing, make());
  const auto retained = std::move(listing);
  ASSERT_THAT(retained.entries, SizeIs(1));
  ASSERT_THAT(retained.observations, SizeIs(1));
  EXPECT_THAT(retained.entries.front().path, EqualsText("fixture.tar!nested/leaf.txt"));
  EXPECT_THAT(retained.entries.front().name, EqualsText("leaf.txt"));
  EXPECT_THAT(retained.observations.front().status, IsOk());
  EXPECT_THAT(retained.observations.front().value.size, Eq(7));
}

TEST_F(ArchiveDirectoryMetadataTest, NestedCustomUriNormalizedAndAbsoluteDirectoryPathsKeepTheirMeanings) {
  constexpr auto kOptions = std::to_array<MemberPathOptions>({
      {
          .separator = "!",
      },
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
  constexpr std::string_view kNested = "outer.tar!inner.tar";
  for (const auto& options : kOptions) {
    MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs(kNested, options));
    const auto path = JoinMemberPath(kNested, "././nested///", options);
    MBO_ASSERT_OK_AND_ASSIGN(const auto listing, fs.ReadDirMetadata(path, vfs::MetadataFields::kBasic));
    ASSERT_THAT(listing.entries, SizeIs(1));
    ASSERT_THAT(listing.observations, SizeIs(1));
    EXPECT_THAT(listing.entries.front().path, EqualsText(JoinMemberPath(kNested, "nested/leaf.txt", options)));
    const auto absolute = JoinMemberPath(kNested, "/absolute", options);
    MBO_ASSERT_OK_AND_ASSIGN(const auto rooted, fs.ReadDirMetadata(absolute, vfs::MetadataFields::kBirthTime));
    ASSERT_THAT(rooted.entries, SizeIs(1));
    EXPECT_THAT(rooted.entries.front().path, EqualsText(JoinMemberPath(kNested, "/absolute/item.txt", options)));
  }
}

TEST_F(ArchiveDirectoryMetadataTest, EmptyAndInvalidDirectoriesPreserveListingResultsAndErrors) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs());
  MBO_ASSERT_OK_AND_ASSIGN(const auto empty, fs.ReadDirMetadata("fixture.tar!empty", vfs::MetadataFields::kBasic));
  EXPECT_THAT(empty.entries, IsEmpty());
  EXPECT_THAT(empty.observations, IsEmpty());
  EXPECT_THAT(
      fs.ReadDirMetadata("other.tar!nested", vfs::MetadataFields::kBasic),
      StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      fs.ReadDirMetadata("fixture.tar!missing", vfs::MetadataFields::kBirthTime),
      StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(
      fs.ReadDirMetadata("fixture.tar!plain.txt", vfs::MetadataFields::kBasic),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveDirectoryMetadataTest, ImmutableValuesSupportSerialAndParallelReadersWithFullPaths) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto fs, Fs());
  constexpr auto kWorkers = std::to_array<std::size_t>({1, 3, 10});
  for (const auto workers : kWorkers) {
    EXPECT_THAT(fs.SupportsDirectoryMetadata(workers), IsTrue());
  }
  EXPECT_THAT(fs.SupportsNameOnlyListings(), IsFalse());
}

}  // namespace
}  // namespace xff::archive
