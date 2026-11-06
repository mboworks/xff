// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/vfs/read_source.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/vfs/mutations.h"

namespace xff::vfs {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

struct ReadSourceTest : ::testing::Test {};

class OwnedOnlyStream final : public ReadStream {
 public:
  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    auto result = bytes_.substr(offset_, max_bytes);
    offset_ += result.size();
    return result;
  }

 private:
  const std::string bytes_ = "abcdef";
  std::size_t offset_ = 0;
};

// Cursor ownership transfers through unique_ptr; duplicating the object can duplicate a native descriptor.
static_assert(!std::is_copy_constructible_v<OwnedOnlyStream>);
static_assert(!std::is_move_constructible_v<OwnedOnlyStream>);

TEST_F(ReadSourceTest, UnsupportedSkipNeverConsumesOwnedOnlyStream) {
  OwnedOnlyStream stream;
  EXPECT_THAT(stream.Skip(3), IsOkAndHolds(Eq(std::nullopt)));
  EXPECT_THAT(stream.Read(6), IsOkAndHolds(EqualsText("abcdef")));
}

TEST_F(ReadSourceTest, OwnedMemoryCursorOutlivesBothPublicOwners) {
  const std::string bytes("a\0b\xff", 4);
  const auto budget = std::make_shared<ReadBudget>(128, 32);
  auto input = MakeMemoryReadSource(bytes, budget);
  EXPECT_THAT(input.source->Budget(), Eq(budget));
  EXPECT_THAT(input.source->Size(), IsOkAndHolds(Eq(bytes.size())));
  EXPECT_THAT(*input.bytes, EqualsText(bytes));
  MBO_ASSERT_OK_AND_ASSIGN(auto cursor, input.source->Open());
  input.bytes.reset();
  input.source.reset();
  EXPECT_THAT(cursor->Read(bytes.size()), IsOkAndHolds(EqualsText(bytes)));
}

TEST_F(ReadSourceTest, OwnedMemoryBytesOutliveSourceAndCursor) {
  const std::string bytes("a\0b\xff", 4);
  auto input = MakeMemoryReadSource(bytes);
  MBO_ASSERT_OK_AND_ASSIGN(auto cursor, input.source->Open());
  const auto retained_bytes = std::move(input.bytes);
  input.source.reset();
  cursor.reset();
  EXPECT_THAT(*retained_bytes, EqualsText(bytes));
}

TEST_F(ReadSourceTest, MemorySkipSharesReadPositionAndBoundsCountsWithoutLosingOwners) {
  auto source = MemoryReadSource("abcdef");
  MBO_ASSERT_OK_AND_ASSIGN(auto cursor, source->Open());
  MBO_ASSERT_OK_AND_ASSIGN(auto second, source->OpenAt(1));
  source.reset();
  EXPECT_THAT(cursor->Skip(0), IsOkAndHolds(Optional(Eq(0))));
  EXPECT_THAT(cursor->Read(1), IsOkAndHolds(EqualsText("a")));
  EXPECT_THAT(cursor->Skip(2), IsOkAndHolds(Optional(Eq(2))));
  std::array<char, 3> buffer;
  EXPECT_THAT(cursor->ReadInto(buffer), IsOkAndHolds(Eq(3)));
  EXPECT_THAT(std::string_view(buffer.data(), buffer.size()), EqualsText("def"));
  EXPECT_THAT(cursor->Skip(std::numeric_limits<std::uint64_t>::max()), IsOkAndHolds(Optional(Eq(0))));
  EXPECT_THAT(second->Skip(std::numeric_limits<std::uint64_t>::max()), IsOkAndHolds(Optional(Eq(5))));
  EXPECT_THAT(second->Read(1), IsOkAndHolds(IsEmpty()));
}

TEST_F(ReadSourceTest, HostSkipUsesTheOwnedDescriptorAndPreservesUnsupportedReadErrors) {
  MBO_ASSERT_OK_AND_ASSIGN(auto directory, TemporaryDirectory::Create(::testing::TempDir() + "skip-source", {}));
  MBO_ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(directory->Path() + "/file", {}));
  ASSERT_THAT(output->Write("abcdef"), IsOk());
  MBO_ASSERT_OK_AND_ASSIGN(auto cursor, HostReadSource(output->Path())->OpenAt(1));
  MBO_ASSERT_OK_AND_ASSIGN(auto sequential, HostReadSource(directory->Path())->Open());
  output.reset();
  directory.reset();
  EXPECT_THAT(cursor->Skip(0), IsOkAndHolds(Optional(Eq(0))));
  EXPECT_THAT(cursor->Skip(2), IsOkAndHolds(Optional(Eq(2))));
  EXPECT_THAT(cursor->Read(1), IsOkAndHolds(EqualsText("d")));
  EXPECT_THAT(cursor->Skip(std::numeric_limits<std::uint64_t>::max()), IsOkAndHolds(Optional(Eq(2))));
  EXPECT_THAT(cursor->Read(1), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(cursor->Skip(1), IsOkAndHolds(Optional(Eq(0))));
  EXPECT_THAT(sequential->Skip(10), IsOkAndHolds(Eq(std::nullopt)));
  EXPECT_THAT(sequential->Skip(10), IsOkAndHolds(Eq(std::nullopt)));
  EXPECT_THAT(sequential->Read(1), StatusIs(absl::StatusCode::kDataLoss));
}

TEST_F(ReadSourceTest, UnsupportedReadIntoNeitherConsumesNorCopiesOwnedOnlyStream) {
  OwnedOnlyStream stream;
  EXPECT_THAT(stream.SupportsReadInto(), IsFalse());
  std::string buffer = "unchanged";
  EXPECT_THAT(stream.ReadInto(std::span<char>(buffer)), StatusIs(absl::StatusCode::kUnimplemented));
  EXPECT_THAT(buffer, EqualsText("unchanged"));
  EXPECT_THAT(stream.Read(6), IsOkAndHolds(EqualsText("abcdef")));
}

TEST_F(ReadSourceTest, MemoryReadIntoSharesOwnedReadPositionAndRetainsSourceBytes) {
  auto source = MemoryReadSource("abcdef");
  MBO_ASSERT_OK_AND_ASSIGN(auto stream, source->Open());
  MBO_ASSERT_OK_AND_ASSIGN(auto suffix, source->OpenAt(2));
  MBO_ASSERT_OK_AND_ASSIGN(auto beyond, source->OpenAt(99));
  source.reset();
  EXPECT_THAT(stream->SupportsReadInto(), IsTrue());
  std::string buffer(8, '!');
  const std::span span(buffer);
  EXPECT_THAT(stream->ReadInto(span.first(0)), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(stream->Read(2), IsOkAndHolds(EqualsText("ab")));
  EXPECT_THAT(stream->ReadInto(span.first(3)), IsOkAndHolds(Eq(3)));
  EXPECT_THAT(buffer, EqualsText("cde!!!!!"));
  EXPECT_THAT(stream->Read(99), IsOkAndHolds(EqualsText("f")));
  EXPECT_THAT(stream->ReadInto(span), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(buffer, EqualsText("cde!!!!!"));
  EXPECT_THAT(suffix->ReadInto(span), IsOkAndHolds(Eq(4)));
  EXPECT_THAT(buffer, EqualsText("cdef!!!!"));
  EXPECT_THAT(beyond->ReadInto(span), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(buffer, EqualsText("cdef!!!!"));
}

TEST_F(ReadSourceTest, MemoryReadIntoPreservesBinaryBytesAndHandlesEmptySpansAndSources) {
  const std::string bytes("a\0b\xff", 4);
  MBO_ASSERT_OK_AND_ASSIGN(auto cursor, MemoryReadSource(bytes)->Open());
  std::string buffer(8, '!');
  EXPECT_THAT(cursor->ReadInto(std::span<char>()), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(buffer, EqualsText("!!!!!!!!"));
  EXPECT_THAT(cursor->ReadInto(std::span<char>(buffer)), IsOkAndHolds(Eq(bytes.size())));
  EXPECT_THAT(std::string_view(buffer).substr(0, bytes.size()), EqualsText(bytes));
  EXPECT_THAT(std::string_view(buffer).substr(bytes.size()), EqualsText("!!!!"));
  const std::string previous = buffer;
  EXPECT_THAT(cursor->ReadInto(std::span<char>(buffer)), IsOkAndHolds(Eq(0)));
  MBO_ASSERT_OK_AND_ASSIGN(auto empty, MemoryReadSource("")->Open());
  EXPECT_THAT(empty->ReadInto(std::span<char>(buffer)), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(buffer, EqualsText(previous));
}

TEST_F(ReadSourceTest, HostReadIntoCapsChunksPreservesTailAndSharesCursorPosition) {
  MBO_ASSERT_OK_AND_ASSIGN(auto directory, TemporaryDirectory::Create(::testing::TempDir() + "read-into", {}));
  MBO_ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(directory->Path() + "/file", {}));
  std::string bytes((2UZ * 64 * 1'024) + 17, 'a');
  for (std::size_t at = 0; at < bytes.size(); ++at) {
    bytes.at(at) = static_cast<char>('a' + (at % 26));
  }
  ASSERT_THAT(output->Write(bytes), IsOk());
  const auto source = HostReadSource(output->Path());
  MBO_ASSERT_OK_AND_ASSIGN(auto stream, source->Open());
  EXPECT_THAT(stream->SupportsReadInto(), IsTrue());
  std::vector<char> storage(70'000, '!');
  const std::span buffer(storage);
  EXPECT_THAT(stream->ReadInto(buffer.first(0)), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(stream->ReadInto(buffer), IsOkAndHolds(Eq(64UZ * 1'024)));
  EXPECT_THAT(std::string_view(buffer.data(), 64UZ * 1'024), EqualsText(bytes.substr(0, 64UZ * 1'024)));
  const auto unused = buffer.subspan(64UZ * 1'024);
  EXPECT_THAT(
      std::string_view(unused.data(), unused.size()), EqualsText(std::string(buffer.size() - (64UZ * 1'024), '!')));
  EXPECT_THAT(stream->Read(3), IsOkAndHolds(EqualsText(bytes.substr(64UZ * 1'024, 3))));
  const auto remaining = bytes.substr((64UZ * 1'024) + 3);
  EXPECT_THAT(stream->ReadInto(buffer), IsOkAndHolds(Eq(64UZ * 1'024)));
  EXPECT_THAT(std::string_view(buffer.data(), 64UZ * 1'024), EqualsText(remaining.substr(0, 64UZ * 1'024)));
  EXPECT_THAT(stream->ReadInto(buffer), IsOkAndHolds(Eq(14)));
  EXPECT_THAT(std::string_view(buffer.data(), 14), EqualsText(bytes.substr(bytes.size() - 14)));
  EXPECT_THAT(stream->ReadInto(buffer), IsOkAndHolds(Eq(0)));
  MBO_ASSERT_OK_AND_ASSIGN(auto partial, source->OpenAt(bytes.size() - 3));
  EXPECT_THAT(partial->ReadInto(buffer), IsOkAndHolds(Eq(3)));
  EXPECT_THAT(std::string_view(buffer.data(), 3), EqualsText(bytes.substr(bytes.size() - 3)));
  MBO_ASSERT_OK_AND_ASSIGN(auto unreadable, HostReadSource(directory->Path())->Open());
  EXPECT_THAT(unreadable->ReadInto(buffer), StatusIs(absl::StatusCode::kDataLoss));
}

TEST_F(ReadSourceTest, HostIdentityIsAbsentForVirtualSources) {
  EXPECT_THAT(MemoryReadSource("content")->HostPath(), Eq(std::nullopt));
  EXPECT_THAT(HostReadSource("/host/file")->HostPath(), Optional(Eq("/host/file")));
}

TEST_F(ReadSourceTest, IndependentCursorsOwnTheirBytesBeyondSourceLifetime) {
  auto source = MemoryReadSource("abcdef");
  MBO_ASSERT_OK_AND_ASSIGN(auto first, source->Open());
  MBO_ASSERT_OK_AND_ASSIGN(auto second, source->Open());
  source.reset();
  EXPECT_THAT(first->Read(0), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(first->Read(2), IsOkAndHolds(EqualsText("ab")));
  EXPECT_THAT(second->Read(6), IsOkAndHolds(EqualsText("abcdef")));
  EXPECT_THAT(first->Read(99), IsOkAndHolds(EqualsText("cdef")));
  EXPECT_THAT(first->Read(1), IsOkAndHolds(IsEmpty()));
}

TEST_F(ReadSourceTest, MaterializationHonorsExactLimitAndEmptySource) {
  const auto source = MemoryReadSource(std::string(70'000, 'a'));
  EXPECT_THAT(ReadSourceBytes(*source, 70'000), IsOkAndHolds(EqualsText(std::string(70'000, 'a'))));
  EXPECT_THAT(ReadSourceBytes(*source, 69'999), StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_THAT(ReadSourceBytes(*MemoryReadSource(""), 0), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(ReadSourceBytes(*MemoryReadSource("x"), 0), StatusIs(absl::StatusCode::kResourceExhausted));
}

class BrokenStream final : public ReadStream {
 public:
  absl::StatusOr<std::string> Read(std::size_t) override { return absl::DataLossError("source read failed"); }
};

class BrokenSource final : public ReadSource {
 public:
  absl::StatusOr<std::unique_ptr<ReadStream>> Open() const override { return std::make_unique<BrokenStream>(); }
};

TEST_F(ReadSourceTest, ReadFailuresAreNotReportedAsEndOfFile) {
  EXPECT_THAT(ReadSourceBytes(BrokenSource(), 100), StatusIs(absl::StatusCode::kDataLoss));
}

TEST_F(ReadSourceTest, MissingHostSourcePreservesNotFound) {
  EXPECT_THAT(HostReadSource("")->Open(), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(ReadSourceBytes(*HostReadSource(""), 100), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(ReadSourceTest, HostCursorsReadIndependentlyAndReportDirectoryReadErrors) {
  MBO_ASSERT_OK_AND_ASSIGN(auto directory, TemporaryDirectory::Create(::testing::TempDir() + "read-source", {}));
  MBO_ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(directory->Path() + "/file", {}));
  EXPECT_THAT(output->Write("abcdef"), IsOk());
  auto source = HostReadSource(output->Path());
  MBO_ASSERT_OK_AND_ASSIGN(auto first, source->Open());
  MBO_ASSERT_OK_AND_ASSIGN(auto second, source->Open());
  source.reset();
  EXPECT_THAT(first->Read(0), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(first->Read(2), IsOkAndHolds(EqualsText("ab")));
  EXPECT_THAT(second->Read(20), IsOkAndHolds(EqualsText("abcdef")));
  EXPECT_THAT(first->Read(20), IsOkAndHolds(EqualsText("cdef")));
  EXPECT_THAT(first->Read(1), IsOkAndHolds(IsEmpty()));
  // A directory read is an OS error, not a successful empty file.
  EXPECT_THAT(ReadSourceBytes(*HostReadSource(directory->Path()), 100), StatusIs(absl::StatusCode::kDataLoss));
}

class SequentialSource final : public ReadSource {
 public:
  explicit SequentialSource(std::shared_ptr<ReadBudget> budget) : ReadSource(std::move(budget)) {}

  absl::StatusOr<std::unique_ptr<ReadStream>> Open() const override { return MemoryReadSource("abcdef")->Open(); }
};

TEST_F(ReadSourceTest, RetainedRangesShareMemoryAndNativeOffsetsDoNotReplay) {
  const auto budget = std::make_shared<ReadBudget>(6, 0);
  const auto source = MemoryReadSource("abcdef", budget);
  EXPECT_THAT(source->Size(), IsOkAndHolds(Eq(6)));
  {
    MBO_ASSERT_OK_AND_ASSIGN(const auto block, ReadSourceRange(*source, 2, 4));
    EXPECT_THAT(block.bytes, EqualsText("cdef"));
    EXPECT_THAT(budget->MemoryUsed(), Eq(4));
    EXPECT_THAT(ReadSourceRange(*source, 0, 3), StatusIs(absl::StatusCode::kResourceExhausted));
    EXPECT_THAT(ReadSourceRange(*source, 5, 2), IsOkAndHolds(Field(&ReadBlock::bytes, EqualsText("f"))));
  }
  EXPECT_THAT(budget->MemoryUsed(), Eq(0));
  EXPECT_THAT(ReadSourceRange(*source, 100, 1), IsOkAndHolds(Field(&ReadBlock::bytes, IsEmpty())));
  EXPECT_THAT(ReadSourceRange(*source, 100, 0), IsOkAndHolds(Field(&ReadBlock::bytes, IsEmpty())));
  EXPECT_THAT(ReadSourceRange(*source, 0, (64UZ * 1'024 * 1'024) + 1), StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_THAT(
      ReadSourceRange(*source, std::numeric_limits<std::uint64_t>::max(), 1), StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(ReadSourceTest, SequentialRangesChargeReplayAcrossCursorsAndReleaseFailedReservations) {
  const auto budget = std::make_shared<ReadBudget>(10, 5);
  const SequentialSource source(budget);
  EXPECT_THAT(ReadSourceRange(source, 2, 2), IsOkAndHolds(Field(&ReadBlock::bytes, EqualsText("cd"))));
  EXPECT_THAT(ReadSourceRange(source, 3, 3), IsOkAndHolds(Field(&ReadBlock::bytes, EqualsText("def"))));
  EXPECT_THAT(ReadSourceRange(source, 1, 1), StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_THAT(budget->MemoryUsed(), Eq(0));
  const SequentialSource measured(std::make_shared<ReadBudget>(10, 6));
  EXPECT_THAT(measured.Size(), IsOkAndHolds(Eq(6)));
  EXPECT_THAT(measured.Size(), StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_THAT(BrokenSource().Size(), StatusIs(absl::StatusCode::kDataLoss));
  const SequentialSource beyond(std::make_shared<ReadBudget>(10, 20));
  EXPECT_THAT(ReadSourceRange(beyond, 20, 2), IsOkAndHolds(Field(&ReadBlock::bytes, IsEmpty())));
  EXPECT_THAT(ReadSourceRange(BrokenSource(), 2, 1), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(ReadSourceRange(BrokenSource(), 0, 1), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(ReadSourceRange(*HostReadSource(""), 0, 1), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(ReadSourceTest, HostRangesHaveIndependentNativeOffsets) {
  MBO_ASSERT_OK_AND_ASSIGN(auto directory, TemporaryDirectory::Create(::testing::TempDir() + "range-source", {}));
  MBO_ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(directory->Path() + "/file", {}));
  EXPECT_THAT(output->Write("abcdef"), IsOk());
  const auto source = HostReadSource(output->Path(), std::make_shared<ReadBudget>(10, 0));
  EXPECT_THAT(source->Size(), IsOkAndHolds(Eq(6)));
  EXPECT_THAT(HostReadSource("")->Size(), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(ReadSourceRange(*source, 3, 3), IsOkAndHolds(Field(&ReadBlock::bytes, EqualsText("def"))));
  EXPECT_THAT(ReadSourceRange(*source, 0, 3), IsOkAndHolds(Field(&ReadBlock::bytes, EqualsText("abc"))));
  EXPECT_THAT(ReadSourceRange(*source, 100, 1), IsOkAndHolds(Field(&ReadBlock::bytes, IsEmpty())));
  EXPECT_THAT(source->OpenAt(std::numeric_limits<std::uint64_t>::max()), StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(ReadSourceTest, HostCursorExposesOnlyInitializedBytesAndRetainsEarlierChunks) {
  MBO_ASSERT_OK_AND_ASSIGN(auto directory, TemporaryDirectory::Create(::testing::TempDir() + "host-chunks", {}));
  MBO_ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(directory->Path() + "/file", {}));
  std::string bytes((2UZ * 64 * 1'024) + 17, 'a');
  for (std::size_t at = 0; at < bytes.size(); ++at) {
    bytes.at(at) = static_cast<char>('a' + (at % 26));
  }
  EXPECT_THAT(output->Write(bytes), IsOk());
  const auto source = HostReadSource(output->Path());
  MBO_ASSERT_OK_AND_ASSIGN(auto stream, source->Open());
  EXPECT_THAT(stream->Read(0), IsOkAndHolds(IsEmpty()));
  MBO_ASSERT_OK_AND_ASSIGN(const auto first, stream->Read(1'024UZ * 1'024));
  EXPECT_THAT(first, EqualsText(bytes.substr(0, 64UZ * 1'024)));
  EXPECT_THAT(stream->Read(1'024UZ * 1'024), IsOkAndHolds(EqualsText(bytes.substr(64UZ * 1'024, 64UZ * 1'024))));
  EXPECT_THAT(stream->Read(1'024UZ * 1'024), IsOkAndHolds(EqualsText(bytes.substr(2UZ * 64 * 1'024))));
  EXPECT_THAT(stream->Read(1), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(first, EqualsText(bytes.substr(0, 64UZ * 1'024)));
  MBO_ASSERT_OK_AND_ASSIGN(auto partial, source->OpenAt(bytes.size() - 3));
  EXPECT_THAT(partial->Read(64UZ * 1'024), IsOkAndHolds(EqualsText(bytes.substr(bytes.size() - 3))));
  EXPECT_THAT(partial->Read(1), IsOkAndHolds(IsEmpty()));
  MBO_ASSERT_OK_AND_ASSIGN(auto unreadable, HostReadSource(directory->Path())->Open());
  EXPECT_THAT(unreadable->Read(1), StatusIs(absl::StatusCode::kDataLoss));
}

enum class ProbeReadKind { kOwned, kInto };

struct ProbeReadRequest {
  ProbeReadKind kind;
  std::size_t count;
};

struct ProbeReadState {
  std::string bytes = "abcdefg";
  std::vector<ProbeReadKind> kinds = {ProbeReadKind::kInto, ProbeReadKind::kOwned};
  std::size_t chunk = 2;
  std::size_t fail_call = std::numeric_limits<std::size_t>::max();
  bool oversized = false;
  std::vector<ProbeReadRequest> requests;
};

class ProbeReadStream final : public ReadStream {
 public:
  explicit ProbeReadStream(std::shared_ptr<ProbeReadState> state) : state_(std::move(state)) {}

  bool SupportsReadInto() const noexcept override {
    return state_->kinds.at(state_->requests.size() % state_->kinds.size()) == ProbeReadKind::kInto;
  }

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    MBO_ASSIGN_OR_RETURN(const auto count, Count(max_bytes, ProbeReadKind::kOwned));
    if (state_->oversized) {
      return std::string(max_bytes + 1, '!');
    }
    auto bytes = state_->bytes.substr(offset_, count);
    offset_ += count;
    return bytes;
  }

  absl::StatusOr<std::size_t> ReadInto(std::span<char> buffer) override {
    MBO_ASSIGN_OR_RETURN(const auto count, Count(buffer.size(), ProbeReadKind::kInto));
    if (state_->oversized) {
      return buffer.size() + 1;
    }
    std::ranges::copy(std::string_view(state_->bytes).substr(offset_, count), buffer.begin());
    offset_ += count;
    return count;
  }

 private:
  absl::StatusOr<std::size_t> Count(std::size_t requested, ProbeReadKind kind) {
    const auto call = state_->requests.size();
    state_->requests.push_back({.kind = kind, .count = requested});
    if (call == state_->fail_call) {
      return absl::DataLossError("injected source failure");
    }
    return std::min({requested, state_->chunk, state_->bytes.size() - offset_});
  }

  std::shared_ptr<ProbeReadState> state_;
  std::size_t offset_ = 0;
};

class ProbeReadSource final : public ReadSource {
 public:
  explicit ProbeReadSource(std::shared_ptr<ProbeReadState> state)
      : ReadSource(std::make_shared<ReadBudget>(128, 128)), state_(std::move(state)) {}

  absl::StatusOr<std::unique_ptr<ReadStream>> Open() const override {
    return std::make_unique<ProbeReadStream>(state_);
  }

 private:
  std::shared_ptr<ProbeReadState> state_;
};

TEST_F(ReadSourceTest, DirectRangePreservesShortReadsAndChangingCapabilities) {
  const auto state = std::make_shared<ProbeReadState>();
  state->bytes = "abcdefgh";
  const ProbeReadSource source(state);
  {
    MBO_ASSERT_OK_AND_ASSIGN(const auto block, ReadSourceRange(source, 0, 8));
    EXPECT_THAT(block.bytes, EqualsText("abcdefgh"));
    EXPECT_THAT(source.Budget()->MemoryUsed(), Eq(8));
    EXPECT_THAT(
        state->requests, ElementsAre(
                             FieldsAre(Eq(ProbeReadKind::kInto), Eq(8)), FieldsAre(Eq(ProbeReadKind::kOwned), Eq(6)),
                             FieldsAre(Eq(ProbeReadKind::kInto), Eq(4)), FieldsAre(Eq(ProbeReadKind::kOwned), Eq(2))));
  }
  EXPECT_THAT(source.Budget()->MemoryUsed(), Eq(0));
}

TEST_F(ReadSourceTest, DirectRangeRetainsReservationAndOnlyInitializedPrefixAtEarlyEof) {
  const auto state = std::make_shared<ProbeReadState>();
  state->bytes = "abc";
  const ProbeReadSource source(state);
  MBO_ASSERT_OK_AND_ASSIGN(const auto block, ReadSourceRange(source, 0, 10));
  EXPECT_THAT(block.bytes, EqualsText("abc"));
  EXPECT_THAT(source.Budget()->MemoryUsed(), Eq(10));
  EXPECT_THAT(
      state->requests, ElementsAre(
                           FieldsAre(Eq(ProbeReadKind::kInto), Eq(10)), FieldsAre(Eq(ProbeReadKind::kOwned), Eq(8)),
                           FieldsAre(Eq(ProbeReadKind::kInto), Eq(7))));
}

TEST_F(ReadSourceTest, MaterializationUsesPositiveProbeAtExactLimitWithChangingCapabilities) {
  const auto state = std::make_shared<ProbeReadState>();
  EXPECT_THAT(ReadSourceBytes(ProbeReadSource(state), 7), IsOkAndHolds(EqualsText("abcdefg")));
  EXPECT_THAT(
      state->requests, ElementsAre(
                           FieldsAre(Eq(ProbeReadKind::kInto), Eq(7)), FieldsAre(Eq(ProbeReadKind::kOwned), Eq(5)),
                           FieldsAre(Eq(ProbeReadKind::kInto), Eq(3)), FieldsAre(Eq(ProbeReadKind::kOwned), Eq(1)),
                           FieldsAre(Eq(ProbeReadKind::kInto), Eq(1))));
}

TEST_F(ReadSourceTest, ZeroMaterializationLimitProbesOneByteForEitherCapability) {
  constexpr auto kKinds = std::to_array<ProbeReadKind>({ProbeReadKind::kOwned, ProbeReadKind::kInto});
  for (const auto kind : kKinds) {
    const auto state = std::make_shared<ProbeReadState>();
    state->kinds = {kind};
    EXPECT_THAT(ReadSourceBytes(ProbeReadSource(state), 0), StatusIs(absl::StatusCode::kResourceExhausted));
    EXPECT_THAT(state->requests, ElementsAre(FieldsAre(Eq(kind), Eq(1))));
    state->bytes.clear();
    state->requests.clear();
    EXPECT_THAT(ReadSourceBytes(ProbeReadSource(state), 0), IsOkAndHolds(IsEmpty()));
    EXPECT_THAT(state->requests, ElementsAre(FieldsAre(Eq(kind), Eq(1))));
  }
}

TEST_F(ReadSourceTest, OversizedOwnedAndBorrowedReadsFailBeforeExposingData) {
  constexpr auto kKinds = std::to_array<ProbeReadKind>({ProbeReadKind::kOwned, ProbeReadKind::kInto});
  for (const auto kind : kKinds) {
    const auto state = std::make_shared<ProbeReadState>();
    state->kinds = {kind};
    state->oversized = true;
    const ProbeReadSource source(state);
    EXPECT_THAT(ReadSourceRange(source, 0, 4), StatusIs(absl::StatusCode::kDataLoss));
    EXPECT_THAT(source.Budget()->MemoryUsed(), Eq(0));
    EXPECT_THAT(ReadSourceBytes(source, 4), StatusIs(absl::StatusCode::kDataLoss));
    EXPECT_THAT(ReadSourceBytes(source, 0), StatusIs(absl::StatusCode::kDataLoss));
  }
}

TEST_F(ReadSourceTest, LateReadAndLimitProbeErrorsStayErrorsAndReleaseRangeReservation) {
  constexpr auto kCalls = std::to_array<std::size_t>({0, 1});
  for (const auto call : kCalls) {
    const auto state = std::make_shared<ProbeReadState>();
    state->fail_call = call;
    const ProbeReadSource source(state);
    EXPECT_THAT(ReadSourceRange(source, 0, 4), StatusIs(absl::StatusCode::kDataLoss));
    EXPECT_THAT(source.Budget()->MemoryUsed(), Eq(0));
    state->requests.clear();
    EXPECT_THAT(ReadSourceBytes(source, 4), StatusIs(absl::StatusCode::kDataLoss));
  }
  const auto state = std::make_shared<ProbeReadState>();
  state->bytes = "ab";
  state->fail_call = 1;
  EXPECT_THAT(ReadSourceBytes(ProbeReadSource(state), 2), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(
      state->requests,
      ElementsAre(FieldsAre(Eq(ProbeReadKind::kInto), Eq(2)), FieldsAre(Eq(ProbeReadKind::kOwned), Eq(1))));
}

TEST_F(ReadSourceTest, MaximumMaterializationLimitDoesNotOverflowRequestArithmetic) {
  const auto state = std::make_shared<ProbeReadState>();
  state->bytes = "abc";
  EXPECT_THAT(
      ReadSourceBytes(ProbeReadSource(state), std::numeric_limits<std::size_t>::max()),
      IsOkAndHolds(EqualsText("abc")));
}

TEST_F(ReadSourceTest, OwnedOnlyMaterializationPreservesBothSmallAndMultiChunkResults) {
  constexpr auto kSizes = std::to_array<std::size_t>({10, 512});
  for (const auto size : kSizes) {
    const auto state = std::make_shared<ProbeReadState>();
    state->bytes = std::string(size, 'x');
    state->bytes.front() = 'a';
    state->bytes.back() = 'z';
    state->chunk = 31;
    state->kinds = {ProbeReadKind::kOwned};
    const ProbeReadSource source(state);
    EXPECT_THAT(ReadSourceBytes(source, size), IsOkAndHolds(EqualsText(state->bytes)));
    state->requests.clear();
    EXPECT_THAT(ReadSourceBytes(source, size + 7), IsOkAndHolds(EqualsText(state->bytes)));
    state->requests.clear();
    state->fail_call = 1;
    state->chunk = 2;
    EXPECT_THAT(ReadSourceBytes(source, size), StatusIs(absl::StatusCode::kDataLoss));
  }
}

}  // namespace
}  // namespace xff::vfs
