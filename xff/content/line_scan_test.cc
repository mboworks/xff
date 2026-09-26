// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/content/line_match.h"
#include "xff/vfs/read_source.h"

namespace xff::content {
namespace {
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;

class ChunkStream final : public vfs::ReadStream {
 public:
  std::string bytes;
  std::size_t chunk = 1;
  std::size_t position = 0;
  absl::Status tail;

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    EXPECT_THAT(max_bytes, Eq(65'536));
    if (position == bytes.size()) {
      if (!tail.ok()) {
        return tail;
      }
      return std::string();
    }
    const auto size = std::min({chunk, max_bytes, bytes.size() - position});
    auto result = bytes.substr(position, size);
    position += size;
    return result;
  }
};

struct LineScanTest : ::testing::Test {
  ChunkStream stream;
  std::vector<std::string> lines;

  bool Visit(std::size_t number, std::string_view text) {
    EXPECT_THAT(number, Eq(lines.size() + 1));
    lines.emplace_back(text);
    return true;
  }
};

TEST_F(LineScanTest, ChunkBoundariesPreserveCrLfBlankAndFinalLines) {
  constexpr auto kChunks = std::to_array<std::size_t>({1, 2, 3, 8, 65'536});
  for (const auto chunk : kChunks) {
    SCOPED_TRACE(chunk);
    stream.bytes = "alpha\r\n\nbeta\r\nlast\r";
    stream.chunk = chunk;
    stream.position = 0;
    lines.clear();
    EXPECT_THAT(
        ScanLines(stream, 8'000, [this](auto number, auto text) { return Visit(number, text); }),
        IsOkAndHolds(Field(&LineScanResult::binary, false)));
    EXPECT_THAT(lines, ElementsAre("alpha", "", "beta", "last"));
  }
}

TEST_F(LineScanTest, EmptyAndTerminatedInputHasNoPhantomFinalLine) {
  EXPECT_THAT(
      ScanLines(stream, 0, [this](auto number, auto text) { return Visit(number, text); }),
      IsOkAndHolds(Field(&LineScanResult::binary, false)));
  EXPECT_THAT(lines, IsEmpty());
  stream.bytes = "\r\n\n";
  EXPECT_THAT(
      ScanLines(stream, 0, [this](auto number, auto text) { return Visit(number, text); }),
      IsOkAndHolds(Field(&LineScanResult::binary, false)));
  EXPECT_THAT(lines, ElementsAre("", ""));
}

TEST_F(LineScanTest, LineCanSpanMultipleReadBlocks) {
  stream.chunk = 65'536;
  const std::string long_line(200'000, 'x');
  stream.bytes = long_line + "\nnext";
  EXPECT_THAT(
      ScanLines(stream, 0, [this](auto number, auto text) { return Visit(number, text); }),
      IsOkAndHolds(Field(&LineScanResult::binary, false)));
  EXPECT_THAT(lines, ElementsAre(long_line, "next"));
}

TEST_F(LineScanTest, EarlySelectionStopsCallbacksButDetectsLateReadFailure) {
  stream.bytes = "hit\nlate\n";
  stream.tail = absl::DataLossError("late failure");
  EXPECT_THAT(
      ScanLines(
          stream, 0,
          [this](auto number, auto text) {
            Visit(number, text);
            return false;
          }),
      StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(lines, ElementsAre("hit"));
  EXPECT_THAT(stream.position, stream.bytes.size());
}

TEST_F(LineScanTest, EarlySelectionStillDetectsLateBinaryData) {
  stream.bytes = "hit\nlate";
  stream.bytes.push_back('\0');
  EXPECT_THAT(
      ScanLines(
          stream, std::numeric_limits<std::uint64_t>::max(),
          [this](auto number, auto text) {
            Visit(number, text);
            return false;
          }),
      IsOkAndHolds(Field(&LineScanResult::binary, true)));
  EXPECT_THAT(lines, ElementsAre("hit"));
}

TEST_F(LineScanTest, DetectionIsLimitedToRequestedPrefixAndCanBeDisabled) {
  stream.bytes = std::string("a\0\n", 3);
  for (const auto limit : {0, 1, 2}) {
    stream.position = 0;
    lines.clear();
    EXPECT_THAT(
        ScanLines(stream, limit, [this](auto number, auto text) { return Visit(number, text); }),
        IsOkAndHolds(Field(&LineScanResult::binary, limit == 2)));
    if (limit == 2) {
      EXPECT_THAT(lines, IsEmpty());
    } else {
      EXPECT_THAT(lines, ElementsAre(std::string("a\0", 2)));
    }
  }
}

TEST_F(LineScanTest, BinaryInputsStillPropagateReadFailures) {
  stream.bytes = std::string("\0later", 6);
  stream.tail = absl::DataLossError("after binary detection");
  EXPECT_THAT(
      ScanLines(stream, 8'000, [this](auto number, auto text) { return Visit(number, text); }),
      StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(lines, IsEmpty());
}

TEST_F(LineScanTest, BufferedVisitorCanStopAfterFirstLine) {
  VisitLines("first\nsecond", [this](auto number, auto text) {
    Visit(number, text);
    return false;
  });
  EXPECT_THAT(lines, ElementsAre("first"));
}
}  // namespace
}  // namespace xff::content
