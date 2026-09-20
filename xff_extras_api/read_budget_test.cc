// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/vfs/read_budget.h"

#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::vfs {
namespace {
using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::Eq;

struct ReadBudgetTest : ::testing::Test {};

TEST_F(ReadBudgetTest, ChargesAreSharedAndReleasedByMoveAssignmentAndDestruction) {
  ReadBudget budget(10, 10);
  {
    MBO_ASSERT_OK_AND_ASSIGN(auto first, budget.Reserve(6));
    MBO_ASSERT_OK_AND_ASSIGN(auto second, budget.Reserve(4));
    EXPECT_THAT(budget.MemoryUsed(), Eq(10));
    EXPECT_THAT(budget.Reserve(1), StatusIs(absl::StatusCode::kResourceExhausted));
    auto moved = std::move(first);
    second = std::move(moved);
    EXPECT_THAT(budget.MemoryUsed(), Eq(6));
    EXPECT_THAT(budget.Reserve(0), IsOk());
    EXPECT_THAT(
        budget.Reserve(std::numeric_limits<std::size_t>::max()), StatusIs(absl::StatusCode::kResourceExhausted));
  }
  EXPECT_THAT(budget.MemoryUsed(), Eq(0));
  EXPECT_THAT(budget.Reserve(10), IsOk());
}

TEST_F(ReadBudgetTest, ReservationsOutliveTheirBudgetHandle) {
  ReadBudget::Reservation reservation;
  {
    ReadBudget budget(1);
    MBO_ASSERT_OK_AND_ASSIGN(reservation, budget.Reserve(1));
  }
  reservation = {};
}

TEST_F(ReadBudgetTest, ReplayIsCumulativeAndOverflowSafe) {
  ReadBudget budget(0, 10);
  EXPECT_THAT(budget.ConsumeReplay(6), IsOk());
  EXPECT_THAT(budget.ConsumeReplay(5), StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_THAT(budget.ConsumeReplay(4), IsOk());
  EXPECT_THAT(budget.ConsumeReplay(0), IsOk());
  EXPECT_THAT(
      budget.ConsumeReplay(std::numeric_limits<std::uint64_t>::max()), StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST_F(ReadBudgetTest, ConcurrentReservationsRemainWithinLimit) {
  ReadBudget budget(8);
  std::vector<std::jthread> workers;
  workers.reserve(8);
  for (int index = 0; index < 8; ++index) {
    workers.emplace_back([&budget] {
      for (int repeat = 0; repeat < 100; ++repeat) {
        EXPECT_THAT(budget.Reserve(1), IsOk());
      }
    });
  }
  workers.clear();
  EXPECT_THAT(budget.MemoryUsed(), Eq(0));
}
}  // namespace
}  // namespace xff::vfs
