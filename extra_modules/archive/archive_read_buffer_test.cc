// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <string>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_reader.h"
#include "xff/archive/memory_archive_test.h"

namespace xff::archive {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;

struct ArchiveReadBufferTest : ::testing::Test {};

TEST_F(ArchiveReadBufferTest, ReturnsOnlyWrittenBytesForEmptyPartialAndMultipleBlocks) {
  constexpr auto kSizes = std::to_array<std::size_t>({0, 1, 1'024, 65'535, 65'536, 65'537, 131'073});
  for (const auto size : kSizes) {
    SCOPED_TRACE(size);
    std::string content(size, 'x');
    for (std::size_t index = 0; index < size; ++index) {
      content.at(index) = static_cast<char>(index % 251);
    }
    const auto members = std::to_array<MemoryArchiveMember>({
        {
            .path = "data.bin",
            .content = content,
        },
    });
    MBO_ASSERT_OK_AND_ASSIGN(const auto bytes, MakeMemoryTar(members));
    EXPECT_THAT(ReadMember(bytes, "data.bin"), IsOkAndHolds(EqualsText(content)));
    EXPECT_THAT(ReadMember(bytes, "data.bin", size), IsOkAndHolds(EqualsText(content)));
    if (size > 1) {
      EXPECT_THAT(ReadMember(bytes, "data.bin", size - 1), StatusIs(absl::StatusCode::kResourceExhausted));
    }
  }
}

TEST_F(ArchiveReadBufferTest, EmptyAndShortReadsDoNotExposeAnEarlierLargePayload) {
  const std::string large(131'073, 'x');
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = "large.txt",
          .content = large,
      },
      {
          .path = "empty.txt",
          .content = "",
      },
      {
          .path = "small.txt",
          .content = "short",
      },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto bytes, MakeMemoryTar(members));
  EXPECT_THAT(ReadMember(bytes, "large.txt"), IsOkAndHolds(EqualsText(large)));
  EXPECT_THAT(ReadMember(bytes, "empty.txt"), IsOkAndHolds(EqualsText("")));
  EXPECT_THAT(ReadMember(bytes, "small.txt"), IsOkAndHolds(EqualsText("short")));
}

}  // namespace
}  // namespace xff::archive
