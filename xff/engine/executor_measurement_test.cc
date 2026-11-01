// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/executor_measurement.h"

#include <array>
#include <cstddef>
#include <limits>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::engine {
namespace {
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::IsTrue;
using ::testing::Le;

struct ExecutorMeasurementTest : ::testing::Test {
  static constexpr std::array kDispatches = {
      ExecutorProbeDispatch::kLeaf,
      ExecutorProbeDispatch::kClaimable,
      ExecutorProbeDispatch::kDrain,
  };
};

TEST_F(ExecutorMeasurementTest, ParsesOnlyDiagnosticDispatches) {
  EXPECT_THAT(ParseExecutorProbeDispatch("leaf"), IsOkAndHolds(ExecutorProbeDispatch::kLeaf));
  EXPECT_THAT(ParseExecutorProbeDispatch("claimable"), IsOkAndHolds(ExecutorProbeDispatch::kClaimable));
  EXPECT_THAT(ParseExecutorProbeDispatch("drain"), IsOkAndHolds(ExecutorProbeDispatch::kDrain));
  EXPECT_THAT(ParseExecutorProbeDispatch("unknown"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ExecutorMeasurementTest, CountsEveryPartialChunkAndKeepsTheCallerInTheBudget) {
  for (const auto dispatch : kDispatches) {
    SCOPED_TRACE(static_cast<int>(dispatch));
    for (const std::size_t participants : {1, 2, 3, 10}) {
      SCOPED_TRACE(participants);
      for (const std::size_t grain : {1, 16, 128}) {
        SCOPED_TRACE(grain);
        ASSERT_OK_AND_ASSIGN(
            const auto result,
            MeasureExecutor(
                {.dispatch = dispatch, .participants = participants, .items = 513, .sweeps = 2, .grain = grain}));
        EXPECT_THAT(result.background_workers, Eq(participants - 1));
        EXPECT_THAT(result.first_dispatch.items, Eq(participants));
        EXPECT_THAT(result.first_dispatch.queued_jobs, Eq(participants - 1));
        EXPECT_THAT(result.first_dispatch.checksum, Eq(participants * (participants + 1) / 2));
        EXPECT_THAT(result.first_dispatch.observed_participants, Le(participants));
        EXPECT_THAT(result.first_dispatch.caller_participated, IsTrue());
        EXPECT_THAT(result.warmed.items, Eq(1'026));
        EXPECT_THAT(result.warmed.checksum, Eq(263'682));
        EXPECT_THAT(result.warmed.observed_participants, Le(participants));
        // Drain workers may claim every chunk before their coordinator does.
        // Caller participation is forced by leaf sweeps, not assumed for drains.
        if (dispatch != ExecutorProbeDispatch::kDrain) {
          EXPECT_THAT(result.warmed.caller_participated, IsTrue());
        }
        if (participants == 1) {
          EXPECT_THAT(result.warmed.queued_jobs, Eq(0));
        }
        EXPECT_THAT(result.start_seconds, Ge(0.0));
        EXPECT_THAT(result.first_dispatch.seconds, Ge(0.0));
        EXPECT_THAT(result.warmed.seconds, Ge(0.0));
        EXPECT_THAT(result.teardown_seconds, Ge(0.0));
      }
    }
  }
}

TEST_F(ExecutorMeasurementTest, RuntimeWorkHasTheIndependentlyKnownUnsignedChecksum) {
  for (const auto dispatch : kDispatches) {
    ASSERT_OK_AND_ASSIGN(
        const auto result,
        MeasureExecutor(
            {.dispatch = dispatch, .participants = 3, .items = 2, .sweeps = 3, .grain = 1, .work_units = 1}));
    EXPECT_THAT(result.warmed.items, Eq(6));
    EXPECT_THAT(result.warmed.checksum, Eq(3 * (33'153 + 66'306)));
  }
}

TEST_F(ExecutorMeasurementTest, RejectsInvalidOrUnrepresentableWork) {
  const auto invalid = [](const ExecutorProbeConfig& config) {
    EXPECT_THAT(MeasureExecutor(config), StatusIs(absl::StatusCode::kInvalidArgument));
  };
  invalid({.participants = 0});
  invalid({.participants = 65});
  invalid({.items = 0});
  invalid({.sweeps = 0});
  invalid({.grain = 0});
  invalid({.items = std::numeric_limits<std::size_t>::max(), .sweeps = 2});
  invalid({.participants = 3, .items = 1, .sweeps = std::numeric_limits<std::size_t>::max()});
  invalid({.dispatch = static_cast<ExecutorProbeDispatch>(99)});
}

}  // namespace
}  // namespace xff::engine
