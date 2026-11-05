// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/vfs/darwin_bulk.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"

namespace xff::vfs::internal {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Optional;

struct DarwinBulkTest : ::testing::Test {
  template<typename T>
  static void Set(std::vector<std::byte>& bytes, std::size_t offset, T value) {
    std::memcpy(std::span(bytes).subspan(offset, sizeof(value)).data(), &value, sizeof(value));
  }

  static std::vector<std::byte> Record(std::uint32_t type = 1) {
    // Fields are 4-byte packed. File attributes are absent for directories.
    const std::size_t fixed = type == 2 ? 128 : 148;
    std::vector<std::byte> bytes((fixed + 5 + 7) / 8 * 8);
    Set(bytes, 0, static_cast<std::uint32_t>(bytes.size()));
    Set(bytes, 4, kBulkCommonAttributes);
    Set(bytes, 16, kBulkFileAttributes);
    Set(bytes, 28, static_cast<std::int32_t>(fixed - 28));
    Set(bytes, 32, std::uint32_t{5});
    Set(bytes, 36, std::uint32_t{7});
    Set(bytes, 40, type);
    Set(bytes, 44, std::int64_t{1});
    Set(bytes, 60, std::int64_t{2});
    Set(bytes, 76, std::int64_t{3});
    Set(bytes, 92, std::int64_t{4});
    Set(bytes, 108, std::uint32_t{501});
    Set(bytes, 112, std::uint32_t{20});
    Set(bytes, 116, std::uint32_t{0644});
    Set(bytes, 120, std::uint64_t{999});
    if (type != 2) {
      Set(bytes, 128, std::uint32_t{2});
      Set(bytes, 132, std::int64_t{12'288});
      Set(bytes, 140, std::int64_t{13});
    }
    constexpr std::string_view kName = "file";
    std::memcpy(std::span(bytes).subspan(fixed, kName.size()).data(), kName.data(), kName.size());
    return bytes;
  }
};

TEST_F(DarwinBulkTest, CompleteRegularObservationPreservesDataLengthAndAllForkBlocks) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto record, DecodeDarwinBulkRecord(Record()));
  EXPECT_THAT(record.name, EqualsText("file"));
  EXPECT_THAT(record.type, Eq(FileType::kRegular));
  EXPECT_THAT(
      record.metadata,
      Optional(AllOf(
          Field(&Metadata::size, Eq(13)), Field(&Metadata::blocks, Eq(24)), Field(&Metadata::mode, Eq(0100644)),
          Field(&Metadata::nlink, Eq(2)), Field(&Metadata::dev, Eq(7)), Field(&Metadata::ino, Eq(999)),
          Field(&Metadata::uid, Eq(501)), Field(&Metadata::gid, Eq(20)),
          Field(&Metadata::btime, Optional(absl::FromUnixSeconds(1))),
          Field(&Metadata::mtime, Eq(absl::FromUnixSeconds(2))), Field(&Metadata::ctime, Eq(absl::FromUnixSeconds(3))),
          Field(&Metadata::atime, Eq(absl::FromUnixSeconds(4))))));
}

TEST_F(DarwinBulkTest, DirectoryLayoutOmitsFileSlotsAndRequiresOrdinaryStat) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto record, DecodeDarwinBulkRecord(Record(2)));
  EXPECT_THAT(record.name, EqualsText("file"));
  EXPECT_THAT(record.type, Eq(FileType::kDirectory));
  EXPECT_THAT(record.metadata, Eq(std::nullopt));
}

TEST_F(DarwinBulkTest, SignedDeviceAndPermissionBitsMatchNativeStatWidths) {
  auto bytes = Record();
  Set(bytes, 36, std::int32_t{-1});
  Set(bytes, 116, std::uint32_t{0xFFFF0000U | 06755U});
  MBO_ASSERT_OK_AND_ASSIGN(const auto record, DecodeDarwinBulkRecord(bytes));
  EXPECT_THAT(
      record.metadata,
      Optional(AllOf(
          Field(&Metadata::dev, Eq(std::numeric_limits<std::uint64_t>::max())), Field(&Metadata::mode, Eq(0106755U)))));
  Set(bytes, 128, std::uint32_t{65'536});
  MBO_ASSERT_OK_AND_ASSIGN(const auto unsupported, DecodeDarwinBulkRecord(bytes));
  EXPECT_THAT(unsupported.metadata, Eq(std::nullopt));
}

TEST_F(DarwinBulkTest, SpecialTypesKeepNameAndConservativeMetadataFallback) {
  constexpr auto kTypes = std::to_array<std::uint32_t>({3, 4, 5, 6, 7, 99});
  for (const auto type : kTypes) {
    SCOPED_TRACE(type);
    MBO_ASSERT_OK_AND_ASSIGN(const auto record, DecodeDarwinBulkRecord(Record(type)));
    EXPECT_THAT(record.metadata, Eq(std::nullopt));
    EXPECT_THAT(record.name, EqualsText("file"));
  }
}

TEST_F(DarwinBulkTest, EveryTruncatedRecordIsRejected) {
  const auto bytes = Record();
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    SCOPED_TRACE(size);
    EXPECT_THAT(DecodeDarwinBulkRecord(std::span(bytes).first(size)), StatusIs(absl::StatusCode::kDataLoss));
  }
}

TEST_F(DarwinBulkTest, MissingOrInvalidObservationsFallBackWithoutLosingNames) {
  constexpr auto kFields = std::to_array<std::size_t>({4, 16, 24, 132, 140, 52, 68, 84, 100});
  for (const auto offset : kFields) {
    SCOPED_TRACE(offset);
    auto bytes = Record();
    if (offset == 4) {
      Set(bytes, offset, kBulkCommonAttributes & ~0x02000000U);
    } else if (offset == 16) {
      Set(bytes, offset, std::uint32_t{0});
    } else if (offset == 24) {
      Set(bytes, offset, std::uint32_t{13});
    } else {
      Set(bytes, offset, std::int64_t{-1});
    }
    MBO_ASSERT_OK_AND_ASSIGN(const auto record, DecodeDarwinBulkRecord(bytes));
    EXPECT_THAT(record.name, EqualsText("file"));
    EXPECT_THAT(record.metadata, Eq(std::nullopt));
  }
}

TEST_F(DarwinBulkTest, InvalidLengthsAndNameReferencesRejectTheRecord) {
  constexpr auto kFields = std::to_array<std::size_t>({0, 28, 32, 148, 149, 152});
  for (const auto offset : kFields) {
    SCOPED_TRACE(offset);
    auto bytes = Record();
    if (offset == 0) {
      Set(bytes, offset, std::uint32_t{4});
    } else if (offset == 28) {
      Set(bytes, offset, std::int32_t{-1});
    } else if (offset == 32) {
      Set(bytes, offset, std::uint32_t{0xFFFFFFFF});
    } else {
      Set(bytes, offset, offset == 149 ? '\0' : '/');
    }
    EXPECT_THAT(DecodeDarwinBulkRecord(bytes), StatusIs(absl::StatusCode::kDataLoss));
  }
}

TEST_F(DarwinBulkTest, ShortDeclaredRecordsNeverReadIntoTheFollowingPayload) {
  constexpr auto kLengths = std::to_array<std::uint32_t>({0, 4, 8, 16, 24, 128, 144});
  for (const auto length : kLengths) {
    SCOPED_TRACE(length);
    auto bytes = Record();
    Set(bytes, 0, length);
    EXPECT_THAT(DecodeDarwinBulkRecord(bytes), StatusIs(absl::StatusCode::kDataLoss));
  }
}

TEST_F(DarwinBulkTest, FollowingRecordBytesAreNotPartOfTheNameOrObservation) {
  auto bytes = Record();
  const auto first_size = bytes.size();
  bytes.append_range(Record(2));
  MBO_ASSERT_OK_AND_ASSIGN(const auto first, DecodeDarwinBulkRecord(bytes));
  EXPECT_THAT(first.bytes, Eq(first_size));
  EXPECT_THAT(first.metadata, Optional(Field(&Metadata::size, Eq(13))));
  MBO_ASSERT_OK_AND_ASSIGN(const auto second, DecodeDarwinBulkRecord(std::span(bytes).subspan(first.bytes)));
  EXPECT_THAT(second.type, Eq(FileType::kDirectory));
  EXPECT_THAT(second.metadata, Eq(std::nullopt));
}

}  // namespace
}  // namespace xff::vfs::internal
