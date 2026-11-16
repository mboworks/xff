// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <archive_entry.h>

#include <array>
#include <memory>
#include <span>
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
#include "xff/archive/archive_reader.h"
#include "xff/archive/archive_reader_internal.h"
#include "xff/archive/memory_archive_test.h"
#include "xff/vfs/read_source.h"

namespace xff::archive {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::NotNull;

struct ArchiveMemberSourceTest : ::testing::Test {
  static absl::StatusOr<vfs::SharedReadSource> Source(std::span<const MemoryArchiveMember> members) {
    MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryTar(members));
    return vfs::MemoryReadSource(std::move(bytes));
  }
};

TEST_F(ArchiveMemberSourceTest, EntryNamesHandleAbsentDifferentAndNormalizedValues) {
  // XFF_ABI_POINTER: the test owns a C entry and releases it through libarchive.
  const std::unique_ptr<struct ::archive_entry, decltype(&::archive_entry_free)> entry(
      ::archive_entry_new(), &::archive_entry_free);
  ASSERT_THAT(entry, NotNull());
  EXPECT_THAT(archive_reader_internal::EntryMatchesNormalizedName(*entry, "dir/value"), IsFalse());
  EXPECT_THAT(archive_reader_internal::EntryMatchesNormalizedName(*entry, ""), IsFalse());
  ::archive_entry_set_pathname(entry.get(), "");
  EXPECT_THAT(archive_reader_internal::EntryMatchesNormalizedName(*entry, ""), IsTrue());
  ::archive_entry_set_pathname(entry.get(), "././dir/value///");
  EXPECT_THAT(archive_reader_internal::EntryMatchesNormalizedName(*entry, "dir/value"), IsTrue());
  EXPECT_THAT(archive_reader_internal::EntryMatchesNormalizedName(*entry, "dir/other"), IsFalse());
  // The adapter must not silently normalize a second copy of the requested name.
  EXPECT_THAT(archive_reader_internal::EntryMatchesNormalizedName(*entry, "./dir/value"), IsFalse());
}

TEST_F(ArchiveMemberSourceTest, NormalizedRequestsKeepTheFirstMatchingHeader) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "unrelated.txt",
      },
      {
          .path = "dir/value-other",
      },
      {
          .path = "././dir/value",
          .content = "first\n",
      },
      {
          .path = "dir/value",
          .content = "second\n",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto source, Source(members));
  static constexpr auto kRequests = std::to_array<std::string_view>({
      "dir/value",
      "./dir/value/",
      "././dir/value///",
  });
  for (const auto request : kRequests) {
    SCOPED_TRACE(request);
    const auto member = MemberReadSource(source, std::string(request));
    EXPECT_THAT(vfs::ReadSourceBytes(*member, 1'024), IsOkAndHolds(EqualsText("first\n")));
  }
}

TEST_F(ArchiveMemberSourceTest, ADirectoryMatchDoesNotFallThroughToARegularDuplicate) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "./blocked/",
          .directory = true,
      },
      {
          .path = "blocked",
          .content = "must not be returned",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto source, Source(members));
  const auto member = MemberReadSource(source, "././blocked///");
  EXPECT_THAT(vfs::ReadSourceBytes(*member, 1'024), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(ArchiveMemberSourceTest, ASymlinkMatchDoesNotFallThroughToARegularDuplicate) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "./link",
          .symlink_target = "elsewhere",
      },
      {
          .path = "link",
          .content = "must not be returned",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto source, Source(members));
  const auto member = MemberReadSource(source, "././link///");
  EXPECT_THAT(vfs::ReadSourceBytes(*member, 1'024), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(ArchiveMemberSourceTest, MissingNamesDoNotResolveDotSegmentsOrFoldCase) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "dir/value",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto source, Source(members));
  static constexpr auto kRequests = std::to_array<std::string_view>({
      "dir/missing",
      "dir/../dir/value",
      "Dir/value",
  });
  for (const auto request : kRequests) {
    SCOPED_TRACE(request);
    const auto member = MemberReadSource(source, std::string(request));
    EXPECT_THAT(vfs::ReadSourceBytes(*member, 1'024), StatusIs(absl::StatusCode::kNotFound));
  }
}

TEST_F(ArchiveMemberSourceTest, ReopenedSourcesOwnTheirOriginalRequestedName) {
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "dir/value",
          .content = "retained\n",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto source, Source(members));
  std::string request = "././dir/value///";
  const auto member = MemberReadSource(source, request);
  request.assign(1'024, 'x');
  for (const auto repeat : {0, 1}) {
    SCOPED_TRACE(repeat);
    EXPECT_THAT(vfs::ReadSourceBytes(*member, 1'024), IsOkAndHolds(EqualsText("retained\n")));
  }
}

TEST_F(ArchiveMemberSourceTest, InvalidSourceRemainsAnOpenError) {
  const auto source = vfs::MemoryReadSource("not an archive");
  const auto member = MemberReadSource(source, "missing");
  EXPECT_THAT(vfs::ReadSourceBytes(*member, 1'024), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::archive
