// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_reader.h"
#include "xff/vfs/read_budget.h"
#include "xff/vfs/read_source.h"

namespace xff::archive {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::Lt;

constexpr std::size_t kReadBlock = 64UZ * 1'024;

struct ReadOptions {
  bool caller_owned = true;
  std::size_t chunk = kReadBlock;
  std::size_t fail_at = std::numeric_limits<std::size_t>::max();
  bool oversized_count = false;
  bool alternate_capability = false;
};

struct ReadCounts {
  std::size_t owned = 0;
  std::size_t caller_owned = 0;
  std::size_t eof = 0;
  bool stable_storage = true;
};

class CountingStream final : public vfs::ReadStream {
 public:
  CountingStream(std::shared_ptr<const std::string> bytes, ReadOptions options, std::shared_ptr<ReadCounts> counts)
      : bytes_(std::move(bytes)), options_(options), counts_(std::move(counts)) {}

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    ++counts_->owned;
    ++callbacks_;
    if (max_bytes > 4 && offset_ >= options_.fail_at) {
      return absl::DataLossError("injected archive source failure");
    }
    const auto count = std::min({max_bytes, options_.chunk, bytes_->size() - offset_});
    auto result = bytes_->substr(offset_, count);
    offset_ += count;
    return result;
  }

  bool SupportsReadInto() const noexcept override {
    return options_.caller_owned && (!options_.alternate_capability || callbacks_ % 2 == 0);
  }

  absl::StatusOr<std::size_t> ReadInto(std::span<char> buffer) override {
    ++counts_->caller_owned;
    ++callbacks_;
    // Alternating paths intentionally release the inactive buffer; addresses may change.
    if (!options_.alternate_capability) {
      if (first_buffer_.empty()) {
        first_buffer_ = buffer;
      } else {
        counts_->stable_storage &= first_buffer_.data() == buffer.data();
      }
    }
    if (offset_ >= options_.fail_at) {
      return absl::DataLossError("injected archive source failure");
    }
    if (options_.oversized_count) {
      return buffer.size() + 1;
    }
    const auto count = std::min({buffer.size(), options_.chunk, bytes_->size() - offset_});
    std::ranges::copy(std::string_view(*bytes_).substr(offset_, count), buffer.begin());
    offset_ += count;
    counts_->eof += count == 0 ? 1 : 0;
    return count;
  }

 private:
  std::shared_ptr<const std::string> bytes_;
  ReadOptions options_;
  std::shared_ptr<ReadCounts> counts_;
  std::size_t offset_ = 0;
  std::size_t callbacks_ = 0;
  std::span<char> first_buffer_;
};

class CountingSource final : public vfs::ReadSource {
 public:
  CountingSource(
      std::string bytes,
      ReadOptions options,
      std::shared_ptr<ReadCounts> counts,
      std::shared_ptr<vfs::ReadBudget> budget = std::make_shared<vfs::ReadBudget>())
      : ReadSource(std::move(budget)),
        bytes_(std::make_shared<const std::string>(std::move(bytes))),
        options_(options),
        counts_(std::move(counts)) {}

  absl::StatusOr<std::unique_ptr<vfs::ReadStream>> Open() const override {
    return std::make_unique<CountingStream>(bytes_, options_, counts_);
  }

 private:
  std::shared_ptr<const std::string> bytes_;
  ReadOptions options_;
  std::shared_ptr<ReadCounts> counts_;
};

struct ArchiveSourceBufferTest : ::testing::Test {
  static std::string MakeTar(std::string_view content, bool compressed = false, bool sparse = false) {
    // XFF_ABI_POINTER: libarchive owns its C writer handle until archive_write_free.
    struct ::archive* writer = ::archive_write_new();
    EXPECT_THAT(
        sparse ? ::archive_write_set_format_pax(writer) : ::archive_write_set_format_pax_restricted(writer),
        Eq(ARCHIVE_OK));
    if (compressed) {
      EXPECT_THAT(::archive_write_add_filter_gzip(writer), Eq(ARCHIVE_OK));
    }
    std::string bytes(content.size() + kReadBlock, '\0');
    std::size_t used = 0;
    EXPECT_THAT(::archive_write_open_memory(writer, bytes.data(), bytes.size(), &used), Eq(ARCHIVE_OK));
    // XFF_ABI_POINTER: libarchive owns the entry until archive_entry_free.
    struct ::archive_entry* entry = ::archive_entry_new();
    ::archive_entry_set_pathname(entry, "data.txt");
    ::archive_entry_set_size(entry, static_cast<la_int64_t>(content.size()));
    ::archive_entry_set_filetype(entry, AE_IFREG);
    ::archive_entry_set_perm(entry, 0644);
    if (sparse) {
      ::archive_entry_sparse_add_entry(entry, static_cast<la_int64_t>(kReadBlock), 4);
    }
    EXPECT_THAT(::archive_write_header(writer, entry), Eq(ARCHIVE_OK));
    EXPECT_THAT(::archive_write_data(writer, content.data(), content.size()), Eq(content.size()));
    ::archive_entry_free(entry);
    EXPECT_THAT(::archive_write_close(writer), Eq(ARCHIVE_OK));
    EXPECT_THAT(::archive_write_free(writer), Eq(ARCHIVE_OK));
    bytes.resize(used);
    return bytes;
  }
};

TEST_F(ArchiveSourceBufferTest, ReusesTheCallerOwnedBufferForFullAndShortBlocks) {
  const auto counts = std::make_shared<ReadCounts>();
  const auto source =
      std::make_shared<CountingSource>(MakeTar(std::string(3 * kReadBlock, 'x')), ReadOptions{}, counts);
  EXPECT_THAT(ListMembersOfSource(source), IsOkAndHolds(ElementsAre(Field(&Member::path, EqualsText("data.txt")))));
  EXPECT_THAT(counts->owned, Eq(0));  // The independent format probe also fills its reserved storage directly.
  EXPECT_THAT(counts->caller_owned, Gt(3));
  EXPECT_THAT(counts->stable_storage, IsTrue());
}

TEST_F(ArchiveSourceBufferTest, ShortReadsDoNotMeanEndOfArchiveOrLeakPreviousBytes) {
  const std::string content = std::string(20'000, 'x') + "tail";
  const auto counts = std::make_shared<ReadCounts>();
  const auto source = std::make_shared<CountingSource>(MakeTar(content), ReadOptions{.chunk = 37}, counts);
  const auto member = MemberReadSource(source, "data.txt");
  EXPECT_THAT(vfs::ReadSourceBytes(*member, content.size()), IsOkAndHolds(EqualsText(content)));
  EXPECT_THAT(counts->caller_owned, Gt(500));
  EXPECT_THAT(counts->stable_storage, IsTrue());
}

TEST_F(ArchiveSourceBufferTest, UnsupportedCursorsKeepTheOwnedReadFallback) {
  const std::string content(100'000, 'x');
  const auto counts = std::make_shared<ReadCounts>();
  const auto source =
      std::make_shared<CountingSource>(MakeTar(content), ReadOptions{.caller_owned = false, .chunk = 2'051}, counts);
  EXPECT_THAT(
      vfs::ReadSourceBytes(*MemberReadSource(source, "data.txt"), content.size()), IsOkAndHolds(EqualsText(content)));
  EXPECT_THAT(counts->owned, Gt(40));
  EXPECT_THAT(counts->caller_owned, Eq(0));
}

TEST_F(ArchiveSourceBufferTest, AlternatingCapabilitiesRetainContentAndOneSessionReservation) {
  const std::string content(3 * kReadBlock, 'x');
  const auto budget = std::make_shared<vfs::ReadBudget>(kReadBlock + 4);
  const auto counts = std::make_shared<ReadCounts>();
  const auto source =
      std::make_shared<CountingSource>(MakeTar(content), ReadOptions{.alternate_capability = true}, counts, budget);
  const auto member = MemberReadSource(source, "data.txt");
  EXPECT_THAT(vfs::ReadSourceBytes(*member, content.size()), IsOkAndHolds(EqualsText(content)));
  EXPECT_THAT(counts->owned, Gt(1));
  EXPECT_THAT(counts->caller_owned, Gt(1));
  EXPECT_THAT(budget->MemoryUsed(), Eq(0));
}

TEST_F(ArchiveSourceBufferTest, CallerOwnedFailureIsPreservedDuringOpen) {
  const auto source =
      std::make_shared<CountingSource>(MakeTar("content"), ReadOptions{.fail_at = 0}, std::make_shared<ReadCounts>());
  EXPECT_THAT(ListMembersOfSource(source), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("injected")));
}

TEST_F(ArchiveSourceBufferTest, CallerOwnedFailureAfterOpenIsNotReportedAsEndOfFile) {
  const auto source = std::make_shared<CountingSource>(
      MakeTar(std::string(3 * kReadBlock, 'x')), ReadOptions{.fail_at = kReadBlock}, std::make_shared<ReadCounts>());
  MBO_ASSERT_OK_AND_ASSIGN(auto stream, MemberReadSource(source, "data.txt")->Open());
  EXPECT_THAT(stream->Read(kReadBlock), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("injected")));
}

TEST_F(ArchiveSourceBufferTest, RejectsAnImpossibleReadCountBeforeCommittingTheStringSize) {
  const auto source = std::make_shared<CountingSource>(
      MakeTar("content"), ReadOptions{.oversized_count = true}, std::make_shared<ReadCounts>());
  EXPECT_THAT(ListMembersOfSource(source), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("more bytes")));
  EXPECT_THAT(source->Budget()->MemoryUsed(), Eq(0));
}

TEST_F(ArchiveSourceBufferTest, ZeroCountRemainsEofAndEmptySourcesRemainInvalid) {
  const auto counts = std::make_shared<ReadCounts>();
  const auto source = std::make_shared<CountingSource>("", ReadOptions{}, counts);
  EXPECT_THAT(ListMembersOfSource(source), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(counts->eof, Gt(0));
  EXPECT_THAT(source->Budget()->MemoryUsed(), Eq(0));
}

TEST_F(ArchiveSourceBufferTest, RetainsExactlyOneSessionReservationAndReleasesItWithTheCursor) {
  const auto budget = std::make_shared<vfs::ReadBudget>(kReadBlock + 4);
  const auto source = std::make_shared<CountingSource>(
      MakeTar("content", true), ReadOptions{.chunk = 23}, std::make_shared<ReadCounts>(), budget);
  MBO_ASSERT_OK_AND_ASSIGN(auto first, MemberReadSource(source, "data.txt")->Open());
  EXPECT_THAT(first->Read(7), IsOkAndHolds(EqualsText("content")));
  EXPECT_THAT(budget->MemoryUsed(), Eq(kReadBlock));
  EXPECT_THAT(MemberReadSource(source, "data.txt")->Open(), StatusIs(absl::StatusCode::kResourceExhausted));
  first.reset();
  EXPECT_THAT(budget->MemoryUsed(), Eq(0));
  MBO_ASSERT_OK_AND_ASSIGN(auto second, MemberReadSource(source, "data.txt")->Open());
  EXPECT_THAT(second->Read(7), IsOkAndHolds(EqualsText("content")));
  EXPECT_THAT(second->Read(7), IsOkAndHolds(IsEmpty()));
  second.reset();
  EXPECT_THAT(budget->MemoryUsed(), Eq(0));
}

TEST_F(ArchiveSourceBufferTest, MemberReadIntoCapsChunksPreservesTailAndSharesOwnedReadPosition) {
  std::string content((2 * kReadBlock) + 17, 'x');
  for (std::size_t at = 0; at < content.size(); ++at) {
    content.at(at) = static_cast<char>('a' + (at % 26));
  }
  const auto budget = std::make_shared<vfs::ReadBudget>(kReadBlock + 4);
  auto source = vfs::MemoryReadSource(MakeTar(content), budget);
  auto member = MemberReadSource(source, "data.txt");
  MBO_ASSERT_OK_AND_ASSIGN(auto cursor, member->Open());
  source.reset();
  member.reset();
  EXPECT_THAT(cursor->SupportsReadInto(), IsTrue());
  std::string buffer(70'000, '!');
  const std::span span(buffer);
  EXPECT_THAT(cursor->ReadInto(std::span<char>()), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(cursor->ReadInto(span.first(0)), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(cursor->ReadInto(span), IsOkAndHolds(Eq(kReadBlock)));
  EXPECT_THAT(std::string_view(buffer).substr(0, kReadBlock), EqualsText(content.substr(0, kReadBlock)));
  EXPECT_THAT(std::string_view(buffer).substr(kReadBlock), EqualsText(std::string(buffer.size() - kReadBlock, '!')));
  EXPECT_THAT(cursor->Read(3), IsOkAndHolds(EqualsText(content.substr(kReadBlock, 3))));
  EXPECT_THAT(cursor->ReadInto(span), IsOkAndHolds(Eq(kReadBlock)));
  EXPECT_THAT(std::string_view(buffer).substr(0, kReadBlock), EqualsText(content.substr(kReadBlock + 3, kReadBlock)));
  EXPECT_THAT(cursor->ReadInto(span), IsOkAndHolds(Eq(14)));
  EXPECT_THAT(std::string_view(buffer).substr(0, 14), EqualsText(content.substr(content.size() - 14)));
  EXPECT_THAT(cursor->ReadInto(span), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(budget->MemoryUsed(), Eq(kReadBlock));
  cursor.reset();
  EXPECT_THAT(budget->MemoryUsed(), Eq(0));
}

TEST_F(ArchiveSourceBufferTest, MemberReadIntoPreservesLateSourceFailures) {
  const auto source = std::make_shared<CountingSource>(
      MakeTar(std::string(3 * kReadBlock, 'x')), ReadOptions{.fail_at = kReadBlock}, std::make_shared<ReadCounts>());
  MBO_ASSERT_OK_AND_ASSIGN(auto cursor, MemberReadSource(source, "data.txt")->Open());
  std::string buffer(kReadBlock, '!');
  EXPECT_THAT(cursor->ReadInto(std::span<char>(buffer)), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("injected")));
}

TEST_F(ArchiveSourceBufferTest, SparseMemberReadIntoInitializesHolesInPreviouslyUsedStorage) {
  std::string content(2 * kReadBlock, '\0');
  content.replace(kReadBlock, 4, "data");
  for (const bool compressed : {false, true}) {
    SCOPED_TRACE(compressed);
    const std::string bytes = MakeTar(content, compressed, true);
    // The uncompressed archive is smaller than the logical member, proving it stores holes.
    EXPECT_THAT(bytes.size(), Lt(content.size()));
    MBO_ASSERT_OK_AND_ASSIGN(auto cursor, MemberReadSource(vfs::MemoryReadSource(bytes), "data.txt")->Open());
    std::string buffer(kReadBlock, '!');
    const std::span span(buffer);
    // Sparse extents can yield short reads; verify only each returned prefix, then continue.
    for (std::size_t position = 0; position < content.size();) {
      std::ranges::fill(buffer, '!');
      MBO_ASSERT_OK_AND_ASSIGN(const auto count, cursor->ReadInto(span));
      ASSERT_THAT(count, Gt(0));
      ASSERT_THAT(count, Le(buffer.size()));
      EXPECT_THAT(std::string_view(buffer).substr(0, count), EqualsText(content.substr(position, count)));
      EXPECT_THAT(std::string_view(buffer).substr(count), EqualsText(std::string(buffer.size() - count, '!')));
      position += count;
    }
    EXPECT_THAT(cursor->ReadInto(span), IsOkAndHolds(Eq(0)));
  }
}

TEST_F(ArchiveSourceBufferTest, NestedMemberBuffersRetainParentsAndReleaseSharedReservations) {
  for (const bool outer_compressed : {false, true}) {
    for (const bool inner_compressed : {false, true}) {
      SCOPED_TRACE(outer_compressed);
      SCOPED_TRACE(inner_compressed);
      const std::string content(20'000, 'x');
      // The temporary prefix reader and both four-byte format probes can overlap.
      const auto budget = std::make_shared<vfs::ReadBudget>((3 * kReadBlock) + 8);
      auto source = vfs::MemoryReadSource(MakeTar(MakeTar(content, inner_compressed), outer_compressed), budget);
      auto nested = MemberReadSource(source, "data.txt");
      auto member = MemberReadSource(nested, "data.txt");
      MBO_ASSERT_OK_AND_ASSIGN(auto cursor, member->Open());
      source.reset();
      nested.reset();
      member.reset();
      EXPECT_THAT(cursor->SupportsReadInto(), IsTrue());
      std::string buffer(content.size() + 8, '!');
      const std::span span(buffer);
      EXPECT_THAT(cursor->ReadInto(span), IsOkAndHolds(Eq(content.size())));
      EXPECT_THAT(std::string_view(buffer).substr(0, content.size()), EqualsText(content));
      EXPECT_THAT(std::string_view(buffer).substr(content.size()), EqualsText("!!!!!!!!"));
      EXPECT_THAT(cursor->ReadInto(span), IsOkAndHolds(Eq(0)));
      EXPECT_THAT(budget->MemoryUsed(), Eq(2 * kReadBlock));
      cursor.reset();
      EXPECT_THAT(budget->MemoryUsed(), Eq(0));
    }
  }
}

}  // namespace
}  // namespace xff::archive
