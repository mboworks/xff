// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/directory_read_admission.h"

#include <chrono>
#include <cstddef>
#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::engine::walk_internal {
namespace {

using ::testing::IsFalse;
using ::testing::IsTrue;

struct DirectoryReadAdmissionTest : ::testing::Test {};

TEST_F(DirectoryReadAdmissionTest, RejectsUnavailableTimingAndInsufficientParallelism) {
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::nanoseconds(0), 64, 3), IsFalse());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::nanoseconds(-1), 64, 3), IsFalse());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::seconds(1), 0, 3), IsFalse());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::seconds(1), 1, 3), IsFalse());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::seconds(1), 64, 0), IsFalse());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::seconds(1), 64, 1), IsFalse());
}

TEST_F(DirectoryReadAdmissionTest, CheapWideListingsDoNotForceStartup) {
  for (const std::size_t directories : {63, 64, 65, 128}) {
    EXPECT_THAT(UsefulDirectoryReads(std::chrono::microseconds(5), directories, 10), IsFalse());
  }
}

TEST_F(DirectoryReadAdmissionTest, SmallSlowBatchCanRepayStartup) {
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::milliseconds(2), 8, 3), IsTrue());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::milliseconds(2), 8, 10), IsTrue());
}

TEST_F(DirectoryReadAdmissionTest, AccountsForWorkerCountAndIncompleteLastRound) {
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::milliseconds(1), 6, 2), IsFalse());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::milliseconds(1), 6, 3), IsTrue());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::microseconds(600), 8, 100), IsTrue());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::microseconds(570), 8, 100), IsFalse());
}

TEST_F(DirectoryReadAdmissionTest, IncludesExactBudgetBoundaryWithoutMultiplicationOverflow) {
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::nanoseconds(3'999'999), 2, 2), IsFalse());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::milliseconds(4), 2, 2), IsTrue());
  EXPECT_THAT(UsefulDirectoryReads(std::chrono::nanoseconds(1), std::numeric_limits<std::size_t>::max(), 2), IsTrue());
}

}  // namespace
}  // namespace xff::engine::walk_internal
