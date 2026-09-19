// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/read_observer.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/mutations.h"

namespace xff::engine {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

class MemorySource final : public vfs::FileSystem {
 public:
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override { return std::vector<vfs::Entry>{}; }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override { return vfs::Metadata{}; }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "Observer forwarded a mutation";
    return absl::InternalError("Mutation reached source");
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return std::string("target"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("memory"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    if (path == "missing") {
      return absl::NotFoundError("missing");
    }
    return path == "empty" ? std::string() : std::string("abc");
  }
};

struct ReadObserverTest : ::testing::Test {
  MemorySource source_;
  ReadObserver observer_{source_};
};

TEST_F(ReadObserverTest, CountsSuccessfulEmptyAndFailedWholeReads) {
  EXPECT_THAT(observer_.ReadContent("file"), IsOkAndHolds(Eq("abc")));
  EXPECT_THAT(observer_.ReadContent("empty"), IsOkAndHolds(Eq("")));
  EXPECT_THAT(observer_.ReadContent("missing"), StatusIs(absl::StatusCode::kNotFound));
  const auto counts = observer_.Snapshot();
  EXPECT_THAT(counts.whole.attempts, Eq(3));
  EXPECT_THAT(counts.whole.successes, Eq(2));
  EXPECT_THAT(counts.whole.bytes, Eq(3));
  EXPECT_THAT(counts.range.attempts, Eq(0));
}

TEST_F(ReadObserverTest, RangeFallbackCountsOnlyReturnedBytesOnce) {
  EXPECT_THAT(observer_.ReadContentRange("file", 1, 20), IsOkAndHolds(Eq("bc")));
  EXPECT_THAT(observer_.ReadContentRange("file", 3, 20), IsOkAndHolds(Eq("")));
  EXPECT_THAT(observer_.ReadContentRange("file", 0, 0), IsOkAndHolds(Eq("")));
  EXPECT_THAT(observer_.ReadContentRange("missing", 0, 1), StatusIs(absl::StatusCode::kNotFound));
  const auto counts = observer_.Snapshot();
  EXPECT_THAT(counts.range.attempts, Eq(4));
  EXPECT_THAT(counts.range.successes, Eq(3));
  EXPECT_THAT(counts.range.bytes, Eq(2));
  EXPECT_THAT(counts.whole.attempts, Eq(0));
  EXPECT_THAT(counts.whole.bytes, Eq(0));
}

TEST_F(ReadObserverTest, ConcurrentReadsAreCountedAfterJoining) {
  {
    std::vector<std::jthread> readers;
    readers.reserve(4);
    for (std::size_t worker = 0; worker < 4; ++worker) {
      readers.emplace_back([&] {
        for (std::size_t index = 0; index < 16; ++index) {
          EXPECT_THAT(observer_.ReadContent("file"), IsOkAndHolds(Eq("abc")));
        }
      });
    }
  }
  const auto counts = observer_.Snapshot().whole;
  EXPECT_THAT(counts.attempts, Eq(64));
  EXPECT_THAT(counts.successes, Eq(64));
  EXPECT_THAT(counts.bytes, Eq(192));
}

TEST_F(ReadObserverTest, ReadOnlyMetadataQueriesKeepBackendResults) {
  EXPECT_THAT(observer_.ReadDir("root"), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(observer_.Stat("file", false), IsOk());
  EXPECT_THAT(observer_.ReadLink("link"), IsOkAndHolds(Eq("target")));
  EXPECT_THAT(observer_.FsType("file"), IsOkAndHolds(Eq("memory")));
  EXPECT_THAT(observer_.IsCaseSensitive("file"), IsOkAndHolds(IsTrue()));
  EXPECT_THAT(observer_.Access("file", vfs::AccessMode::kRead), IsTrue());
  EXPECT_THAT(observer_.SupportsDirectoryPolicies(), IsFalse());
  EXPECT_THAT(observer_.Snapshot().whole.attempts, Eq(0));
  EXPECT_THAT(observer_.Snapshot().range.attempts, Eq(0));
}

TEST_F(ReadObserverTest, MutationEndpointsFailWithoutForwarding) {
  const vfs::MutationPolicy policy;
  EXPECT_THAT(observer_.Remove("file"), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(observer_.WriteContent("file", "x"), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(observer_.OpenOutput("file", false), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(observer_.RemoveControlled("file", policy), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(observer_.OpenControlledOutput("file", false, policy), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(observer_.Access("file", vfs::AccessMode::kWrite), IsFalse());
}

}  // namespace
}  // namespace xff::engine
