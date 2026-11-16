// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/content/line_match.h"
#include "xff/vfs/read_source.h"

namespace xff::content {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Lt;

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

TEST_F(LineScanTest, StreamingContextMatchesBufferedSelectionAcrossChunkBoundaries) {
  stream.bytes = "old\r\npre\r\nhit\r\nhit\npost\nskip\nskip\npre\nhit\ntail";
  const auto matches = [](std::string_view line) { return line == "hit"; };
  for (const std::size_t before : {0UZ, 1UZ, 2UZ, 20UZ}) {
    for (const std::size_t after : {0UZ, 1UZ, 2UZ, 20UZ}) {
      SCOPED_TRACE(before);
      SCOPED_TRACE(after);
      const auto expected = CollectLineMatchesWithContext(stream.bytes, matches, before, after);
      LineContext context(before, after);
      std::size_t emitted = 0;
      stream.position = 0;
      stream.chunk = 3;
      const auto emit = [&](const ContextLine& line) {
        ASSERT_THAT(emitted, Lt(expected.size()));
        const auto& want = expected.at(emitted++);
        EXPECT_THAT(line.number, Eq(want.number));
        EXPECT_THAT(line.text, Eq(want.text));
        EXPECT_THAT(line.is_match, Eq(want.is_match));
        EXPECT_THAT(line.group, Eq(want.group));
      };
      EXPECT_THAT(
          ScanLines(
              stream, 0,
              [&](std::size_t number, std::string_view line) {
                context.Push(number, line, matches(line), emit);
                return true;
              }),
          IsOkAndHolds(Field(&LineScanResult::binary, false)));
      EXPECT_THAT(emitted, Eq(expected.size()));
    }
  }
}

// A single byte cursor can change its advertised read capability after each read.
class CapabilityStream final : public vfs::ReadStream {
 public:
  std::string bytes;
  std::vector<bool> direct_per_read{true};
  std::size_t chunk = 1;
  std::size_t position = 0;
  std::size_t read_calls = 0;
  std::size_t owned_reads = 0;
  std::size_t direct_reads = 0;
  bool poison_unused = false;
  bool invalid_count = false;
  absl::Status tail;

  bool SupportsReadInto() const noexcept override {
    return !direct_per_read.empty() && direct_per_read.at(std::min(read_calls, direct_per_read.size() - 1));
  }

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    ++owned_reads;
    MBO_ASSIGN_OR_RETURN(const auto part, NextBytes(max_bytes));
    return std::string(part);
  }

  absl::StatusOr<std::size_t> ReadInto(std::span<char> destination) override {
    ++direct_reads;
    if (!SupportsReadInto()) {
      return absl::UnimplementedError("direct reads are currently unavailable");
    }
    MBO_ASSIGN_OR_RETURN(const auto part, NextBytes(destination.size()));
    if (poison_unused) {
      std::ranges::fill(destination, '\0');
    }
    std::ranges::copy(part, destination.begin());
    return invalid_count ? destination.size() + 1 : part.size();
  }

 private:
  absl::StatusOr<std::string_view> NextBytes(std::size_t max_bytes) {
    ++read_calls;
    if (position == bytes.size()) {
      MBO_RETURN_IF_ERROR(tail);
      return std::string_view{};
    }
    const auto size = std::min({chunk, max_bytes, bytes.size() - position});
    const auto result = std::string_view(bytes).substr(position, size);
    position += size;
    return result;
  }
};

struct DirectLineScanTest : ::testing::Test {
  CapabilityStream stream;
  std::vector<std::string> lines;

  bool Visit(std::size_t number, std::string_view text) {
    EXPECT_THAT(number, Eq(lines.size() + 1));
    lines.emplace_back(text);
    return true;
  }

  void Rewind() {
    stream.position = 0;
    stream.read_calls = 0;
    stream.owned_reads = 0;
    stream.direct_reads = 0;
    lines.clear();
  }
};

TEST_F(DirectLineScanTest, ShortReadsPreserveCrLfBlankAndFinalLines) {
  static constexpr std::array kChunks = std::to_array<std::size_t>({
      1,
      2,
      3,
      8,
      65'536,
  });
  stream.bytes = "alpha\r\n\nbeta\r\nlast\r";
  for (const auto chunk : kChunks) {
    SCOPED_TRACE(chunk);
    Rewind();
    stream.chunk = chunk;
    EXPECT_THAT(
        ScanLines(stream, 8'000, [this](auto number, auto text) { return Visit(number, text); }),
        IsOkAndHolds(Field("binary", &LineScanResult::binary, IsFalse())));
    EXPECT_THAT(lines, ElementsAre(EqualsText("alpha"), EqualsText(""), EqualsText("beta"), EqualsText("last")));
    EXPECT_THAT(stream.owned_reads, Eq(0));
    EXPECT_THAT(stream.direct_reads, Gt(0));
  }
}

TEST_F(DirectLineScanTest, EmptyAndExactBlockInputsFinishWithoutPhantomLines) {
  EXPECT_THAT(
      ScanLines(stream, 0, [this](auto number, auto text) { return Visit(number, text); }),
      IsOkAndHolds(Field("binary", &LineScanResult::binary, IsFalse())));
  EXPECT_THAT(lines, IsEmpty());
  Rewind();
  stream.chunk = 65'536;
  stream.bytes.assign(65'536, 'x');
  EXPECT_THAT(
      ScanLines(stream, 0, [this](auto number, auto text) { return Visit(number, text); }),
      IsOkAndHolds(Field("binary", &LineScanResult::binary, IsFalse())));
  EXPECT_THAT(lines, ElementsAre(EqualsText(stream.bytes)));
  EXPECT_THAT(stream.owned_reads, Eq(0));
  EXPECT_THAT(stream.direct_reads, Ge(2));
}

TEST_F(DirectLineScanTest, UnusedBufferTailCannotBecomeContentOrBinaryEvidence) {
  stream.bytes = "ok\nx";
  stream.chunk = 2;
  stream.poison_unused = true;
  EXPECT_THAT(
      ScanLines(stream, 8'000, [this](auto number, auto text) { return Visit(number, text); }),
      IsOkAndHolds(Field("binary", &LineScanResult::binary, IsFalse())));
  EXPECT_THAT(lines, ElementsAre(EqualsText("ok"), EqualsText("x")));
  EXPECT_THAT(stream.owned_reads, Eq(0));
}

TEST_F(DirectLineScanTest, CapabilityChangesPreserveOneCursorPositionInBothDirections) {
  static constexpr std::array kPatterns = std::to_array<std::array<bool, 5>>({
      {false, true, false, true, false},
      {true, false, true, false, true},
  });
  stream.bytes = "a\nb\n";
  for (const auto& pattern : kPatterns) {
    Rewind();
    stream.direct_per_read.assign(pattern.begin(), pattern.end());
    EXPECT_THAT(
        ScanLines(stream, 8'000, [this](auto number, auto text) { return Visit(number, text); }),
        IsOkAndHolds(Field("binary", &LineScanResult::binary, IsFalse())));
    EXPECT_THAT(lines, ElementsAre(EqualsText("a"), EqualsText("b")));
    EXPECT_THAT(stream.position, Eq(stream.bytes.size()));
    EXPECT_THAT(stream.direct_reads, Gt(0));
    EXPECT_THAT(stream.owned_reads, Gt(0));
  }
}

TEST_F(DirectLineScanTest, GiantLineSurvivesSwitchesBetweenOwnedAndDirectBlocks) {
  const std::string giant(200'000, 'x');
  stream.bytes = giant + "\nnext";
  stream.chunk = 65'536;
  stream.direct_per_read = {true, false, true, false, true};
  EXPECT_THAT(
      ScanLines(stream, 0, [this](auto number, auto text) { return Visit(number, text); }),
      IsOkAndHolds(Field("binary", &LineScanResult::binary, IsFalse())));
  EXPECT_THAT(lines, ElementsAre(EqualsText(giant), EqualsText("next")));
}

TEST_F(DirectLineScanTest, CallbackStopStillDrainsAndPropagatesLateReadFailure) {
  stream.bytes = "hit\nlater\n";
  stream.direct_per_read = {true, false, true, false};
  stream.tail = absl::DataLossError("late read failure");
  EXPECT_THAT(
      ScanLines(
          stream, 0,
          [this](auto number, auto text) {
            Visit(number, text);
            return false;
          }),
      StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(lines, ElementsAre(EqualsText("hit")));
  EXPECT_THAT(stream.position, Eq(stream.bytes.size()));
}

TEST_F(DirectLineScanTest, CallbackStopStillDetectsLateBinaryData) {
  stream.bytes = "hit\nlater";
  stream.bytes.push_back('\0');
  stream.chunk = 2;
  EXPECT_THAT(
      ScanLines(
          stream, std::numeric_limits<std::uint64_t>::max(),
          [this](auto number, auto text) {
            Visit(number, text);
            return false;
          }),
      IsOkAndHolds(Field("binary", &LineScanResult::binary, IsTrue())));
  EXPECT_THAT(lines, ElementsAre(EqualsText("hit")));
  EXPECT_THAT(stream.position, Eq(stream.bytes.size()));
}

TEST_F(DirectLineScanTest, BinaryDataStillPropagatesLaterReadFailures) {
  stream.bytes = std::string("\0later", 6);
  stream.tail = absl::DataLossError("after binary detection");
  EXPECT_THAT(
      ScanLines(stream, 8'000, [this](auto number, auto text) { return Visit(number, text); }),
      StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(lines, IsEmpty());
}

TEST_F(DirectLineScanTest, InvalidReturnedCountFailsBeforeVisitingAnyBytes) {
  stream.bytes = "x";
  stream.invalid_count = true;
  EXPECT_THAT(
      ScanLines(stream, 8'000, [this](auto number, auto text) { return Visit(number, text); }),
      StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(lines, IsEmpty());
}

TEST_F(DirectLineScanTest, BinaryPrefixBoundaryIsIndependentOfShortReadBoundaries) {
  stream.bytes = std::string(7'999, 'a') + std::string("\0\n", 2);
  stream.chunk = 3;
  for (const std::uint64_t limit : {0, 7'999, 8'000}) {
    SCOPED_TRACE(limit);
    Rewind();
    EXPECT_THAT(
        ScanLines(stream, limit, [this](auto number, auto text) { return Visit(number, text); }),
        IsOkAndHolds(Field("binary", &LineScanResult::binary, Eq(limit == 8'000))));
    if (limit == 8'000) {
      EXPECT_THAT(lines, IsEmpty());
    } else {
      EXPECT_THAT(lines, ElementsAre(EqualsText(stream.bytes.substr(0, stream.bytes.size() - 1))));
    }
  }
}

TEST_F(DirectLineScanTest, ReentrantScansPreserveBorrowedOuterLineUntilCallbackReturns) {
  stream.bytes = "outer one\nouter two\n";
  stream.chunk = 65'536;
  std::vector<std::string> inner_lines;
  EXPECT_THAT(
      ScanLines(
          stream, 0,
          [&](auto number, auto text) {
            CapabilityStream inner;
            inner.bytes = "inner\n";
            EXPECT_THAT(
                ScanLines(
                    inner, 0,
                    [&](auto, auto line) {
                      inner_lines.emplace_back(line);
                      return true;
                    }),
                IsOkAndHolds(Field("binary", &LineScanResult::binary, IsFalse())));
            return Visit(number, text);
          }),
      IsOkAndHolds(Field("binary", &LineScanResult::binary, IsFalse())));
  EXPECT_THAT(lines, ElementsAre(EqualsText("outer one"), EqualsText("outer two")));
  EXPECT_THAT(inner_lines, ElementsAre(EqualsText("inner"), EqualsText("inner")));
}
}  // namespace
}  // namespace xff::content
