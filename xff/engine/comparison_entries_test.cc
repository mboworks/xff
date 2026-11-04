// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/comparison_entries.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/parallel_compare.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::SizeIs;

struct ComparisonEntriesTest : ::testing::Test {
  ComparisonEntries entries;
};

TEST_F(ComparisonEntriesTest, EmptyAndRepeatedOrdering) {
  EXPECT_THAT(entries.Ordered(), IsEmpty());
  entries.Add("z", {.path = "root/z"});
  entries.Add("a", {.path = "root/a"});
  ASSERT_THAT(entries.Ordered(), SizeIs(2));
  EXPECT_THAT(entries.Ordered().front().first, EqualsText("a"));
  EXPECT_THAT(entries.Ordered().back().first, EqualsText("z"));
  EXPECT_THAT(entries.Ordered().front().second, Eq(1));
  entries.Add("b", {.path = "root/b"});
  ASSERT_THAT(entries.Ordered(), SizeIs(3));
  EXPECT_THAT(entries.Ordered().at(1).first, EqualsText("b"));
}

TEST_F(ComparisonEntriesTest, DuplicateKeepsFirstPathAndMetadata) {
  entries.Add("file", {.path = "first/file", .metadata = {.size = 42, .mode = 0755}});
  entries.Add("file", {.path = "second/file", .metadata = {.size = 99, .mode = 0644}});
  ASSERT_THAT(entries.Ordered(), SizeIs(1));
  const auto& first = entries.Get(entries.Ordered().front().second);
  EXPECT_THAT(first.path, EqualsText("first/file"));
  EXPECT_THAT(first.metadata.size, Eq(42));
  EXPECT_THAT(first.metadata.mode, Eq(0755));
}

TEST_F(ComparisonEntriesTest, CopiesTemporaryBytesAndNonSuffixKeys) {
  {
    const std::string path = "short";
    const std::string relative = "not-a-suffix";
    entries.Add(relative, {.path = path});
  }
  ASSERT_THAT(entries.Ordered(), SizeIs(1));
  EXPECT_THAT(entries.Ordered().front().first, EqualsText("not-a-suffix"));
  EXPECT_THAT(entries.Get(0).path, EqualsText("short"));
  entries.Add(".", {.path = "root"});
  EXPECT_THAT(entries.Ordered().front().first, EqualsText("."));
}

TEST_F(ComparisonEntriesTest, PathsAndRecordsSurviveManySegmentsAndArenaBlocks) {
  const std::string prefix(150, 'p');
  entries.Add("first", {.path = prefix + "/first"});
  const auto& first = entries.Get(0);
  for (std::size_t index = 0; index < 10'000; ++index) {
    const std::string relative = "file-" + std::to_string(index);
    std::string path(prefix);
    path += '/';
    path += relative;
    entries.Add(relative, {.path = path, .metadata = {.size = index}});
  }
  ASSERT_THAT(entries.Ordered(), SizeIs(10'001));
  EXPECT_THAT(first.path, EqualsText(prefix + "/first"));
  EXPECT_THAT(&entries.Get(0), Eq(&first));
  for (const auto& [relative, index] : entries.Ordered()) {
    EXPECT_THAT(entries.Get(index).path, EqualsText(prefix + "/" + std::string(relative)));
  }
}

TEST_F(ComparisonEntriesTest, PreservesGlobalByteOrderIncludingDirectoryPrefixes) {
  entries.Add("b/sub", {.path = "root/b/sub"});
  entries.Add("b!/sub", {.path = "root/b!/sub"});
  entries.Add("!", {.path = "root/!"});
  entries.Add(".", {.path = "root"});
  ASSERT_THAT(entries.Ordered(), SizeIs(4));
  EXPECT_THAT(entries.Ordered().at(0).first, EqualsText("!"));
  EXPECT_THAT(entries.Ordered().at(1).first, EqualsText("."));
  EXPECT_THAT(entries.Ordered().at(2).first, EqualsText("b!/sub"));
  EXPECT_THAT(entries.Ordered().at(3).first, EqualsText("b/sub"));
}

TEST_F(ComparisonEntriesTest, RetainsAllComparisonMetadataFields) {
  entries.Add(
      "link", {
                  .path = "root/link",
                  .metadata =
                      {
                          .type = vfs::FileType::kSymlink,
                          .source = vfs::Source::kArchiveMember,
                          .size = 123,
                          .mode = 0777,
                      },
              });
  const auto& metadata = entries.Get(0).metadata;
  EXPECT_THAT(metadata.type, Eq(vfs::FileType::kSymlink));
  EXPECT_THAT(metadata.source, Eq(vfs::Source::kArchiveMember));
  EXPECT_THAT(metadata.size, Eq(123));
  EXPECT_THAT(metadata.mode, Eq(0777));
}

// No host reads or mutations: this backend exists only to verify retained ownership.
struct OwnedComparisonFs final : vfs::FileSystem {
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::UnimplementedError("unused");
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return absl::UnimplementedError("unused");
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return "target"; }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return "content"; }
};

TEST_F(ComparisonEntriesTest, RetainsBackendUntilEntriesAreDestroyed) {
  std::weak_ptr<const vfs::FileSystem> observer;
  {
    ComparisonEntries owned;
    {
      const auto backend = std::make_shared<OwnedComparisonFs>();
      observer = backend;
      owned.Add("file", {.path = "root/file", .fs = *backend, .fs_owner = backend});
    }
    EXPECT_THAT(observer.expired(), IsFalse());
    const auto& entry = owned.Get(0);
    EXPECT_THAT(entry.fs->ReadContent(entry.path), IsOkAndHolds(EqualsText("content")));
  }
  EXPECT_THAT(observer.expired(), IsTrue());
}

TEST_F(ComparisonEntriesTest, EmptyTextAndRootKeyRemainDistinct) {
  entries.Add("", {.path = ""});
  entries.Add(".", {.path = "root"});
  ASSERT_THAT(entries.Ordered(), SizeIs(2));
  EXPECT_THAT(entries.Get(0).path, IsEmpty());
  EXPECT_THAT(entries.Ordered().front().first, IsEmpty());
  EXPECT_THAT(entries.Ordered().back().first, EqualsText("."));
}
}  // namespace
}  // namespace xff::engine
