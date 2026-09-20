// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/vfs/read_source.h"

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"

namespace xff::vfs {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
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
}  // namespace
}  // namespace xff::vfs
