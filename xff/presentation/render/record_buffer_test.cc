// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/presentation/render/record_buffer.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"

namespace xff::render {
namespace {

using ::mbo::testing::EqualsText;
using ::testing::ElementsAre;
using ::testing::Ge;
using ::testing::Le;

struct RawRecordBufferTest : ::testing::Test {};

TEST_F(RawRecordBufferTest, PreservesRawBytesAndBothTerminatorsAcrossGrowthAndReuse) {
  static constexpr auto kSizes = std::to_array<std::size_t>({0, 1, 15, 22, 23, 32, 160, 4'096, 7, 0});
  for (const char terminator : {'\n', '\0'}) {
    RawRecordBuffer buffer(terminator);
    for (const auto size : kSizes) {
      SCOPED_TRACE(size);
      const std::string path = std::string(size, 'x') + "\t\n\\";
      std::string expected = path;
      expected.push_back(terminator);
      buffer.EmitRecord(path, [&](std::string_view text) { EXPECT_THAT(text, EqualsText(expected)); });
    }
  }
}

TEST_F(RawRecordBufferTest, QueuedConsumersKeepIndependentRecords) {
  RawRecordBuffer buffer('\n');
  std::vector<std::string> rows;
  const auto keep = [&](std::string_view text) { rows.emplace_back(text); };
  buffer.EmitRecord("first", keep);
  buffer.EmitRecord(std::string(160, 'x'), keep);
  buffer.EmitRecord("last", keep);
  EXPECT_THAT(rows, ElementsAre(EqualsText("first\n"), EqualsText(std::string(160, 'x') + "\n"), EqualsText("last\n")));
}

TEST_F(RawRecordBufferTest, ExplicitTerminatorsAlternateWithoutChangingTheDefault) {
  RawRecordBuffer buffer('\n');
  std::vector<std::string> rows;
  const auto keep = [&](std::string_view text) { rows.emplace_back(text); };
  const std::string long_path(160, 'x');
  buffer.EmitRecord(long_path, '\0', keep);
  const auto grown = buffer.RetainedCapacity();
  buffer.EmitRecord("short", '\n', keep);
  buffer.EmitRecord("another", '\0', keep);
  buffer.EmitRecord("default", keep);
  EXPECT_THAT(buffer.RetainedCapacity(), Ge(grown));
  EXPECT_THAT(
      rows, ElementsAre(
                EqualsText(long_path + '\0'), EqualsText("short\n"), EqualsText(std::string("another\0", 8)),
                EqualsText("default\n")));
}

TEST_F(RawRecordBufferTest, RetainsUsefulCapacityButReleasesOversizedRecordsAfterConsumption) {
  RawRecordBuffer buffer('\n');
  const auto consume = [](std::string_view) {};
  buffer.EmitRecord(std::string(4'096, 'x'), consume);
  const auto grown = buffer.RetainedCapacity();
  EXPECT_THAT(grown, Ge(4'097));
  buffer.EmitRecord("small", consume);
  EXPECT_THAT(buffer.RetainedCapacity(), Ge(grown));
  const std::string oversized(RawRecordBuffer::kMaxRetainedBytes + 1, 'x');
  buffer.EmitRecord(oversized, [&](std::string_view text) { EXPECT_THAT(text, EqualsText(oversized + "\n")); });
  EXPECT_THAT(buffer.RetainedCapacity(), Le(RawRecordBuffer::kMaxRetainedBytes));
  buffer.EmitRecord("tail", [&](std::string_view text) { EXPECT_THAT(text, EqualsText("tail\n")); });
}

}  // namespace
}  // namespace xff::render
