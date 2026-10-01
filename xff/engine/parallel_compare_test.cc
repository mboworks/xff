// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/parallel_compare.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/vfs/read_source.h"

namespace xff::engine {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::SizeIs;

class ShortSource final : public vfs::ReadSource {
 public:
  std::string bytes = "abcdef";
  bool fail_open = false;
  std::size_t fail_at = 100;

  absl::StatusOr<std::unique_ptr<vfs::ReadStream>> Open() const override {
    if (fail_open) {
      return absl::PermissionDeniedError("cursor open");
    }
    return std::make_unique<Stream>(bytes, fail_at);
  }

 private:
  class Stream final : public vfs::ReadStream {
   public:
    Stream(std::string bytes, std::size_t fail_at) : bytes_(std::move(bytes)), fail_at_(fail_at) {}

    absl::StatusOr<std::string> Read(std::size_t maximum) override {
      if (offset_ >= fail_at_) {
        return absl::DataLossError("cursor read");
      }
      const auto chunk = std::string_view(bytes_).substr(offset_, std::min(std::size_t{2}, maximum));
      offset_ += chunk.size();
      return std::string(chunk);
    }

   private:
    std::string bytes_;
    std::size_t fail_at_;
    std::size_t offset_ = 0;
  };
};

struct CompareFs final : vfs::FileSystem {
  std::map<std::string, std::string> files{{"left", "left"}, {"right", "same"}};
  mutable std::atomic<std::size_t> sources = 0;
  mutable std::atomic<std::size_t> full_reads = 0;
  vfs::SharedReadSource source_override;
  bool track_threads = false;
  bool wait_for_peer = true;
  mutable std::mutex mutex;
  mutable std::condition_variable condition;
  mutable std::set<std::thread::id> threads;

  absl::StatusOr<vfs::SharedReadSource> ContentSource(std::string_view path) const override {
    ++sources;
    if (track_threads) {
      std::unique_lock lock(mutex);
      if (threads.insert(std::this_thread::get_id()).second) {
        condition.notify_all();
        if (wait_for_peer) {
          condition.wait_for(lock, std::chrono::seconds(2), [&] { return threads.size() > 1; });
        }
      }
    }
    if (source_override) {
      return source_override;
    }
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return absl::PermissionDeniedError(path);
    }
    return vfs::MemoryReadSource(found->second);
  }

  absl::StatusOr<std::string> ReadContentRange(std::string_view path, std::uint64_t offset, std::size_t length)
      const override {
    MBO_ASSIGN_OR_RETURN(const auto source, ContentSource(path));
    MBO_ASSIGN_OR_RETURN(auto block, vfs::ReadSourceRange(*source, offset, length));
    return std::move(block.bytes);
  }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override {
    ++full_reads;
    return absl::InternalError("comparison should open a cursor");
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::UnimplementedError("unused");
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return absl::UnimplementedError("unused");
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<std::string> ReadLink(std::string_view path) const override {
    return path == "missing" ? absl::StatusOr<std::string>(absl::NotFoundError(path)) : std::string(path);
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only"); }
};

struct ParallelCompareTest : ::testing::Test {
  CompareFs fs;
  TreeCompareEntry left{.path = "left", .metadata = {.type = vfs::FileType::kRegular, .size = 4}, .fs = fs};
  TreeCompareEntry right{.path = "right", .metadata = {.type = vfs::FileType::kRegular, .size = 4}, .fs = fs};
};

TEST_F(ParallelCompareTest, RetainsOnlyCompleteSmallDifferingInputs) {
  ParallelCompare compare(1, true);
  ASSERT_OK_AND_ASSIGN(const auto result, compare.Compare({{.path = "file", .left = left, .right = right}}).front());
  EXPECT_THAT(result.same, IsFalse());
  EXPECT_THAT(result.bytes, Optional(Field(&ComparisonBytes::left, EqualsText("left"))));
  EXPECT_THAT(result.bytes, Optional(Field(&ComparisonBytes::right, EqualsText("same"))));
  EXPECT_THAT(fs.sources.load(), Eq(2));
  EXPECT_THAT(fs.full_reads.load(), Eq(0));
  fs.files.at("right") = "same extra";  // Metadata can be stale; never reuse a truncated patch input.
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::bytes, Eq(std::nullopt))));
}

TEST_F(ParallelCompareTest, LargeComparisonsOpenEachSourceOnceAndKeepNoPatchBytes) {
  fs.files.at("left") = std::string(200'000, 'a');
  fs.files.at("right") = fs.files.at("left");
  left.metadata.size = 200'000;
  right.metadata.size = 200'000;
  ParallelCompare compare(1, true);
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::same, IsTrue())));
  EXPECT_THAT(fs.sources.load(), Eq(2));
  fs.files.at("right").back() = 'b';
  const auto& result = compare.Compare({{.left = left, .right = right}}).front();
  EXPECT_THAT(result, IsOkAndHolds(Field(&ComparisonResult::same, IsFalse())));
  EXPECT_THAT(result, IsOkAndHolds(Field(&ComparisonResult::bytes, Eq(std::nullopt))));
  EXPECT_THAT(fs.sources.load(), Eq(4));
}

TEST_F(ParallelCompareTest, RepeatedBatchesKeepInputOrderAcrossWorkersAndErrors) {
  ParallelCompare compare(4);
  fs.track_threads = true;
  left.metadata.size = right.metadata.size = 262'144;
  const TreeCompareEntry missing{.path = "missing", .metadata = left.metadata, .fs = fs};
  for (int repeat = 0; repeat < 3; ++repeat) {
    std::vector<ComparisonPair> pairs(64, {.path = "file", .left = left, .right = right});
    pairs.at(7).left.set_ref(missing);
    const auto& results = compare.Compare(std::move(pairs));
    ASSERT_THAT(results, SizeIs(64));
    EXPECT_THAT(results.at(7), StatusIs(absl::StatusCode::kPermissionDenied));
    EXPECT_THAT(results.front(), IsOkAndHolds(Field(&ComparisonResult::same, IsFalse())));
    EXPECT_THAT(results.back(), IsOkAndHolds(Field(&ComparisonResult::same, IsFalse())));
    EXPECT_THAT(fs.threads.size(), Gt(1));
  }
}

TEST_F(ParallelCompareTest, ShortReadsAndEarlyEofPreserveRangeSemantics) {
  const auto source = std::make_shared<ShortSource>();
  fs.source_override = source;
  left.metadata.size = right.metadata.size = 100;
  ParallelCompare compare(1, true);
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::same, IsTrue())));
  EXPECT_THAT(fs.sources.load(), Eq(2));
  EXPECT_THAT(compare.Compare({}), IsEmpty());
  left.metadata.size = right.metadata.size = 262'144;
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::same, IsTrue())));
  source->bytes.clear();
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::same, IsTrue())));
}

TEST_F(ParallelCompareTest, CursorFailuresRemainErrors) {
  const auto source = std::make_shared<ShortSource>();
  fs.source_override = source;
  source->fail_open = true;
  left.metadata.size = right.metadata.size = 262'144;
  ParallelCompare compare(1, true);
  EXPECT_THAT(compare.Compare({{.left = left, .right = right}}).front(), StatusIs(absl::StatusCode::kPermissionDenied));
  source->fail_open = false;
  source->fail_at = 2;
  EXPECT_THAT(compare.Compare({{.left = left, .right = right}}).front(), StatusIs(absl::StatusCode::kDataLoss));
}

TEST_F(ParallelCompareTest, EmptyFilesNeedNoCursorAndOwnedSourcesStaySerial) {
  left.metadata.size = right.metadata.size = 0;
  ParallelCompare compare(4);
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::same, IsTrue())));
  EXPECT_THAT(fs.sources.load(), Eq(0));
  left.metadata.size = right.metadata.size = 262'144;
  const auto owner = std::make_shared<CompareFs>();
  left.fs.set_ref(*owner);
  left.fs_owner = owner;
  EXPECT_THAT(compare.Compare(std::vector<ComparisonPair>(64, {.left = left, .right = right})), SizeIs(64));
  EXPECT_THAT(owner->sources.load(), Eq(64));
  owner->track_threads = true;
  owner->wait_for_peer = false;
  left.fs_owner.reset();
  left.metadata.source = vfs::Source::kArchiveMember;
  EXPECT_THAT(compare.Compare(std::vector<ComparisonPair>(64, {.left = left, .right = right})), SizeIs(64));
  EXPECT_THAT(owner->threads, ElementsAre(std::this_thread::get_id()));
  left.metadata.source = vfs::Source::kLocalFs;
  right.fs_owner = owner;
  EXPECT_THAT(compare.Compare(std::vector<ComparisonPair>(64, {.left = left, .right = right})), SizeIs(64));
  right.fs_owner.reset();
  right.metadata.source = vfs::Source::kArchiveMember;
  EXPECT_THAT(compare.Compare(std::vector<ComparisonPair>(64, {.left = left, .right = right})), SizeIs(64));
  EXPECT_THAT(owner->threads, ElementsAre(std::this_thread::get_id()));
}

TEST_F(ParallelCompareTest, CheapKindsAndSizesDoNotReadContent) {
  ParallelCompare compare(4);
  EXPECT_THAT(compare.Compare({{.left = left}}).front(), IsOkAndHolds(Field(&ComparisonResult::same, IsFalse())));
  EXPECT_THAT(compare.Compare({{.right = right}}).front(), IsOkAndHolds(Field(&ComparisonResult::same, IsFalse())));
  right.metadata.size = 5;
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::same, IsFalse())));
  left.metadata.type = vfs::FileType::kDirectory;
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::same, IsFalse())));
  right.metadata.type = vfs::FileType::kDirectory;
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::same, IsTrue())));
  left.metadata.type = vfs::FileType::kSymlink;
  right.metadata.type = vfs::FileType::kSymlink;
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::same, IsFalse())));
  EXPECT_THAT(fs.sources.load(), Eq(0));
}

TEST_F(ParallelCompareTest, LeftFailurePrecedesRightOpenAndReadErrors) {
  CompareFs right_fs;
  right.fs.set_ref(right_fs);
  const auto source = std::make_shared<ShortSource>();
  fs.source_override = source;
  source->fail_at = 0;
  left.metadata.size = right.metadata.size = 262'144;
  ParallelCompare compare(1);
  EXPECT_THAT(compare.Compare({{.left = left, .right = right}}).front(), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(right_fs.sources.load(), Eq(0));
  source->fail_at = 100;
  const auto right_source = std::make_shared<ShortSource>();
  right_source->fail_open = true;
  right_fs.source_override = right_source;
  EXPECT_THAT(compare.Compare({{.left = left, .right = right}}).front(), StatusIs(absl::StatusCode::kPermissionDenied));
  right_source->fail_open = false;
  right_source->fail_at = 2;
  EXPECT_THAT(compare.Compare({{.left = left, .right = right}}).front(), StatusIs(absl::StatusCode::kDataLoss));
}

TEST_F(ParallelCompareTest, PatchRetentionRequiresSuccessfulEofProbesOnBothSides) {
  CompareFs right_fs;
  right.fs.set_ref(right_fs);
  const auto source = std::make_shared<ShortSource>();
  source->bytes = "abcd";
  source->fail_at = 4;
  fs.source_override = source;
  ParallelCompare compare(1, true);
  EXPECT_THAT(compare.Compare({{.left = left, .right = right}}).front(), StatusIs(absl::StatusCode::kDataLoss));
  source->fail_at = 100;
  const auto right_source = std::make_shared<ShortSource>();
  right_source->bytes = "efgh";
  right_source->fail_at = 4;
  right_fs.source_override = right_source;
  EXPECT_THAT(compare.Compare({{.left = left, .right = right}}).front(), StatusIs(absl::StatusCode::kDataLoss));
  right_source->fail_at = 100;
  source->bytes += "grown";
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::bytes, Eq(std::nullopt))));
  source->bytes.clear();
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::bytes, Optional(Field(&ComparisonBytes::left, IsEmpty())))));
}

TEST_F(ParallelCompareTest, CursorBuffersRespectAndReleaseSourceBudget) {
  left.metadata.size = right.metadata.size = 262'144;
  const auto budget = std::make_shared<vfs::ReadBudget>(ParallelCompare::kRetainedFileBytes);
  fs.source_override = vfs::MemoryReadSource("short", budget);
  ParallelCompare compare(1);
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(), StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_THAT(budget->MemoryUsed(), Eq(0));
  const auto enough = std::make_shared<vfs::ReadBudget>(2 * ParallelCompare::kRetainedFileBytes);
  fs.source_override = vfs::MemoryReadSource("short", enough);
  EXPECT_THAT(
      compare.Compare({{.left = left, .right = right}}).front(),
      IsOkAndHolds(Field(&ComparisonResult::same, IsTrue())));
  EXPECT_THAT(enough->MemoryUsed(), Eq(0));
}

TEST_F(ParallelCompareTest, SymlinkErrorsRemainErrors) {
  left.metadata.type = right.metadata.type = vfs::FileType::kSymlink;
  ParallelCompare compare(1);
  left.path = "missing";
  EXPECT_THAT(compare.Compare({{.left = left, .right = right}}).front(), StatusIs(absl::StatusCode::kNotFound));
  left.path = "left";
  right.path = "missing";
  EXPECT_THAT(compare.Compare({{.left = left, .right = right}}).front(), StatusIs(absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace xff::engine
