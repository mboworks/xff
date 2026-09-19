// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/read_measurement.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::Optional;

class MemorySource final : public vfs::FileSystem {
 public:
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    return std::vector<vfs::Entry>{
        {.path = std::string(path) + "/file", .name = "file", .type = vfs::FileType::kRegular}};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    if (path == "/left" || path == "/right") {
      return vfs::Metadata{.type = vfs::FileType::kDirectory};
    }
    return vfs::Metadata{
        .type = vfs::FileType::kRegular,
        .size = 3,
        .ino = path.starts_with("/right") ? 2U : 1U,
        .dev = 1,
    };
  }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "Observer forwarded a mutation";
    return absl::InternalError("Mutation reached source");
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return std::string("target"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("memory"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    if (path.ends_with("missing")) {
      return absl::NotFoundError("missing");
    }
    return path == "empty" ? std::string() : std::string("abc");
  }
};

struct ReadMeasurementTest : ::testing::Test {
  MemorySource source_;
};

TEST_F(ReadMeasurementTest, EveryWorkloadExecutesAgainstTheObservedBackend) {
  for (const auto name :
       std::to_array<std::string_view>({"listing", "summary", "hash", "hash_twice", "hash_lines", "compare"})) {
    SCOPED_TRACE(name);
    ASSERT_OK_AND_ASSIGN(const auto workload, ParseReadWorkload(name));
    std::vector<std::string> roots{"/left"};
    if (workload == ReadWorkload::kCompare) {
      roots.emplace_back("/right");
    }
    ASSERT_OK_AND_ASSIGN(const auto result, MeasureReads(workload, roots, 2, source_));
    EXPECT_THAT(result.errors, Eq(0));
    EXPECT_THAT(result.output_bytes, Gt(0));
    EXPECT_THAT(result.first_output_seconds, Optional(Ge(0.0)));
    EXPECT_THAT(result.elapsed_seconds, Ge(0.0));
    if (workload == ReadWorkload::kListing || workload == ReadWorkload::kSummary) {
      EXPECT_THAT(result.reads.whole.attempts, Eq(0));
    } else {
      EXPECT_THAT(result.reads.whole.bytes + result.reads.range.bytes, Ge(3));
      if (workload == ReadWorkload::kCompare) {
        EXPECT_THAT(result.reads.range.attempts, Gt(0));
      }
    }
  }
}

TEST_F(ReadMeasurementTest, RejectsUnknownWorkloadsAndOptionLikeRoots) {
  EXPECT_THAT(ParseReadWorkload("exec"), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      MeasureReads(ReadWorkload::kListing, {"--no-safe"}, 1, source_), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      MeasureReads(ReadWorkload::kListing, {"/file"}, 0, source_), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      MeasureReads(ReadWorkload::kCompare, {"/file"}, 1, source_), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ReadMeasurementTest, ReadFailuresRemainVisibleInTheMeasurement) {
  ASSERT_OK_AND_ASSIGN(const auto result, MeasureReads(ReadWorkload::kHash, {"/missing"}, 1, source_));
  // Hash field failures follow the engine's field semantics; attempted/successful reads are
  // independently visible even when an individual field's failure does not become a walk error.
  EXPECT_THAT(result.reads.whole.attempts, Gt(0));
  EXPECT_THAT(result.reads.whole.successes, Eq(0));
  EXPECT_THAT(result.reads.whole.bytes, Eq(0));
}

}  // namespace
}  // namespace xff::engine
