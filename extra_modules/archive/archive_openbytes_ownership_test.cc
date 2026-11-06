// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstdint>
#include <optional>
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
#include "xff/archive/archive_extension.h"
#include "xff/archive/archive_fs.h"
#include "xff/archive/memory_archive_test.h"
#include "xff/archive/memory_phar_test.h"
#include "xff/vfs/read_source.h"

namespace xff::archive {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::HasSubstr;

struct ArchiveOpenBytesOwnershipTest : ::testing::Test {};

TEST_F(ArchiveOpenBytesOwnershipTest, StreamedMemberContentOutlivesInputAndFilesystem) {
  const std::string content(1'048'576, 't');
  MBO_ASSERT_OK_AND_ASSIGN(const auto source, [&]() -> absl::StatusOr<vfs::SharedReadSource> {
    const auto members = std::to_array<MemoryArchiveMember>({
        {
            .path = "directory/member.txt",
            .content = content,
        },
    });
    MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryTar(members));
    MBO_ASSIGN_OR_RETURN(const auto fs, ArchiveFileSystem::OpenBytes("memory.tar", std::move(bytes)));
    return fs.ContentSource("memory.tar!directory/member.txt");
  }());
  EXPECT_THAT(vfs::ReadSourceBytes(*source, content.size()), IsOkAndHolds(EqualsText(content)));
  EXPECT_THAT(vfs::ReadSourceBytes(*source, content.size()), IsOkAndHolds(EqualsText(content)));
}

TEST_F(ArchiveOpenBytesOwnershipTest, LegacyPharMemberContentOutlivesInputAndFilesystem) {
  const std::string content(1'048'576, 'p');
  MBO_ASSERT_OK_AND_ASSIGN(const auto source, [&]() -> absl::StatusOr<vfs::SharedReadSource> {
    MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryPhar("directory/member.txt", content));
    MBO_ASSIGN_OR_RETURN(const auto fs, ArchiveFileSystem::OpenBytes("memory.phar", std::move(bytes)));
    return fs.ContentSource("memory.phar!directory/member.txt");
  }());
  EXPECT_THAT(vfs::ReadSourceBytes(*source, content.size()), IsOkAndHolds(EqualsText(content)));
  EXPECT_THAT(vfs::ReadSourceBytes(*source, content.size()), IsOkAndHolds(EqualsText(content)));
}

TEST_F(ArchiveOpenBytesOwnershipTest, NestedStreamedArchiveRetainsItsOwnedInput) {
  const std::string content(65'537, 'n');
  MBO_ASSERT_OK_AND_ASSIGN(const auto source, [&]() -> absl::StatusOr<vfs::SharedReadSource> {
    const auto inner_members = std::to_array<MemoryArchiveMember>({
        {
            .path = "member.txt",
            .content = content,
        },
    });
    MBO_ASSIGN_OR_RETURN(const auto inner_bytes, MakeMemoryTar(inner_members));
    const auto outer_members = std::to_array<MemoryArchiveMember>({
        {
            .path = "inner.tar",
            .content = inner_bytes,
        },
    });
    MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryTar(outer_members));
    MBO_ASSIGN_OR_RETURN(const auto outer, ArchiveFileSystem::OpenBytes("outer.tar", std::move(bytes)));
    MBO_ASSIGN_OR_RETURN(auto nested_bytes, outer.ReadContent("outer.tar!inner.tar"));
    MBO_ASSIGN_OR_RETURN(
        const auto inner, ArchiveFileSystem::OpenBytes("outer.tar!inner.tar", std::move(nested_bytes)));
    return inner.ContentSource("outer.tar!inner.tar!member.txt");
  }());
  EXPECT_THAT(vfs::ReadSourceBytes(*source, content.size()), IsOkAndHolds(EqualsText(content)));
}

TEST_F(ArchiveOpenBytesOwnershipTest, CompressionFallbackRetainsItsDecodedTarBytes) {
  const std::string content(65'537, 'w');
  const auto members = std::to_array<MemoryArchiveMember>({
      {.path = "member.txt", .content = content},
  });
  MBO_ASSERT_OK_AND_ASSIGN(auto decoded, MakeMemoryTar(members));
  RegisterCompressionExtension({
      .name = "owned-bytes-wrapper",
      .suffixes = {".owned-bytes-wrapper"},
      .decoder = [bytes = std::move(decoded)](std::string_view, std::optional<std::string_view>, std::uint64_t)
          -> absl::StatusOr<std::string> { return bytes; },
  });
  MBO_ASSERT_OK_AND_ASSIGN(const auto source, [&]() -> absl::StatusOr<vfs::SharedReadSource> {
    MBO_ASSIGN_OR_RETURN(
        const auto fs, ArchiveFileSystem::OpenBytes("memory.owned-bytes-wrapper", "unrecognized wrapper"));
    return fs.ContentSource("memory.owned-bytes-wrapper!member.txt");
  }());
  EXPECT_THAT(vfs::ReadSourceBytes(*source, content.size()), IsOkAndHolds(EqualsText(content)));
}

TEST_F(ArchiveOpenBytesOwnershipTest, CompressionFallbackPreservesDecoderErrors) {
  RegisterCompressionExtension({
      .name = "owned-bytes-error",
      .suffixes = {".owned-bytes-error"},
      .decoder = [](std::string_view, std::optional<std::string_view>, std::uint64_t) -> absl::StatusOr<std::string> {
        return absl::DataLossError("invalid wrapped stream");
      },
  });
  EXPECT_THAT(
      ArchiveFileSystem::OpenBytes("memory.owned-bytes-error", "unrecognized wrapper"),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("invalid wrapped stream")));
}

TEST_F(ArchiveOpenBytesOwnershipTest, UnknownAndCorruptContentKeepDistinctErrors) {
  EXPECT_THAT(
      ArchiveFileSystem::OpenBytes("unknown.bin", "ordinary non-archive input"),
      StatusIs(absl::StatusCode::kInvalidArgument));
  std::string truncated = "<?php __HALT_COMPILER(); ?>\n";
  truncated += MemoryPharUint32(19);
  truncated += MemoryPharUint32(0);
  truncated.append("\x11\x10", 2);
  truncated.append(12, '\0');
  EXPECT_THAT(
      ArchiveFileSystem::OpenBytes("broken.phar", std::move(truncated)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated")));
}

TEST_F(ArchiveOpenBytesOwnershipTest, EmptyInputRetainsExistingEmptyArchiveBehavior) {
  EXPECT_THAT(
      ArchiveFileSystem::OpenBytes("empty.tar", {}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("empty input")));
}

}  // namespace
}  // namespace xff::archive
