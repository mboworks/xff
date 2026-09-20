// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/vfs/read_source.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/vfs/mutations.h"

namespace xff::vfs {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;

struct ReadSourceTest : ::testing::Test {};

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
  auto budget = std::make_shared<ReadBudget>(6, 0);
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
  auto budget = std::make_shared<ReadBudget>(10, 5);
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

}  // namespace
}  // namespace xff::vfs
