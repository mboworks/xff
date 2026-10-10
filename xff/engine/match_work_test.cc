// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/match_work.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::engine {
namespace {

using ::std::chrono::microseconds;
using ::std::chrono::nanoseconds;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::FieldsAre;
using ::testing::Ge;
using ::testing::Le;
using ::testing::Message;

struct MatchWorkTest : ::testing::Test {};

TEST_F(MatchWorkTest, OneSlowCompletedChunkDoesNotInflateRemainingEstimate) {
  EXPECT_THAT(
      EstimateMatchWork({nanoseconds(400), microseconds(80'000), nanoseconds(800), nanoseconds(400)}, 4),
      FieldsAre(8, nanoseconds(1'200)));
  EXPECT_THAT(
      EstimateMatchWork({nanoseconds(400), nanoseconds(800), nanoseconds(800), nanoseconds(400)}, 4),
      FieldsAre(8, nanoseconds(1'200)));
}

TEST_F(MatchWorkTest, InvalidSampleChunksAndSaturatingDurationAdditionAreSafe) {
  EXPECT_THAT(EstimateMatchWork({}, 0), FieldsAre(0, nanoseconds::zero()));
  EXPECT_THAT(EstimateMatchWork({}, std::numeric_limits<std::size_t>::max()), FieldsAre(0, nanoseconds::zero()));
  EXPECT_THAT(EstimateMatchWork({nanoseconds(-1), {}, {}, {}}, 4), FieldsAre(0, nanoseconds::zero()));
  EXPECT_THAT(
      EstimateMatchWork({nanoseconds::max(), nanoseconds::max(), nanoseconds::max(), nanoseconds::max()}, 4),
      FieldsAre(8, nanoseconds::max()));
}

TEST_F(MatchWorkTest, NoIndependentRemainingWorkStaysOnCallerEvenWithWarmPool) {
  const MatchWorkEstimate estimate{.sampled_entries = 16, .sampled_elapsed = microseconds(16'000)};
  constexpr std::array kRemaining{std::size_t{0}, std::size_t{1}};
  for (const auto remaining : kRemaining) {
    EXPECT_THAT(PlanMatchWork(remaining, 10, 0, estimate), FieldsAre(1, 4));
    EXPECT_THAT(PlanMatchWork(remaining, 10, 9, estimate), FieldsAre(1, 4));
  }
}

TEST_F(MatchWorkTest, FewExpensiveRemainingItemsCanRepayColdStartup) {
  EXPECT_THAT(
      PlanMatchWork(2, 10, 0, {.sampled_entries = 16, .sampled_elapsed = microseconds(160'000)}), FieldsAre(2, 1));
  EXPECT_THAT(
      PlanMatchWork(2, 10, 0, {.sampled_entries = 16, .sampled_elapsed = microseconds(16'000)}), FieldsAre(1, 4));
  EXPECT_THAT(
      PlanMatchWork(2, 10, 9, {.sampled_entries = 16, .sampled_elapsed = microseconds(16'000)}), FieldsAre(2, 1));
}

TEST_F(MatchWorkTest, ColdStartupCanOutweighUsefulWarmWork) {
  const MatchWorkEstimate estimate{.sampled_entries = 16, .sampled_elapsed = microseconds(160)};
  EXPECT_THAT(PlanMatchWork(256, 10, 0, estimate), FieldsAre(1, 4));
  EXPECT_THAT(PlanMatchWork(256, 3, 2, estimate), FieldsAre(3, 4));
  EXPECT_THAT(PlanMatchWork(256, 10, 9, estimate), FieldsAre(10, 4));
}

TEST_F(MatchWorkTest, EnoughRemainingCostRepaysColdStartupAndHonorsParticipantCap) {
  const MatchWorkEstimate estimate{.sampled_entries = 16, .sampled_elapsed = microseconds(160)};
  EXPECT_THAT(PlanMatchWork(1'024, 3, 0, estimate), FieldsAre(3, 4));
  EXPECT_THAT(PlanMatchWork(1'024, 10, 0, estimate), FieldsAre(10, 4));
  EXPECT_THAT(PlanMatchWork(1'024, 1, 0, estimate), FieldsAre(1, 4));
  EXPECT_THAT(PlanMatchWork(1'024, 0, 0, estimate), FieldsAre(1, 4));
}

TEST_F(MatchWorkTest, ReusingThreadsStillMustRepayDispatchAndSavingMargin) {
  const MatchWorkEstimate estimate{.sampled_entries = 16, .sampled_elapsed = microseconds(1)};
  EXPECT_THAT(PlanMatchWork(1'024, 10, 9, estimate), FieldsAre(1, 4));
}

TEST_F(MatchWorkTest, NewWorkersNeedEnoughAdditionalBenefit) {
  const MatchWorkEstimate estimate{.sampled_entries = 16, .sampled_elapsed = microseconds(16)};
  EXPECT_THAT(PlanMatchWork(256, 10, 2, estimate).participants, Eq(3));
  const MatchWorkEstimate slower{.sampled_entries = 16, .sampled_elapsed = microseconds(160)};
  EXPECT_THAT(PlanMatchWork(256, 10, 1, slower).participants, Eq(7));
}

TEST_F(MatchWorkTest, GrainReflectsPerEntryCostAndPreservesBalancingWaves) {
  const MatchWorkCosts free_dispatch{
      .cold_start = {},
      .worker_start = {},
      .dispatch = {},
      .target_chunk = microseconds(32),
      .minimum_saving = {},
  };
  EXPECT_THAT(
      PlanMatchWork(8'192, 3, 0, {.sampled_entries = 16, .sampled_elapsed = microseconds(1)}, free_dispatch),
      FieldsAre(3, 128));
  EXPECT_THAT(
      PlanMatchWork(32, 3, 0, {.sampled_entries = 16, .sampled_elapsed = microseconds(1)}, free_dispatch),
      FieldsAre(3, 2));
  EXPECT_THAT(
      PlanMatchWork(512, 3, 0, {.sampled_entries = 16, .sampled_elapsed = microseconds(1'600)}, free_dispatch),
      FieldsAre(3, 1));
}

TEST_F(MatchWorkTest, SavingMarginBoundaryUsesRemainingCost) {
  MatchWorkCosts costs{
      .cold_start = {},
      .worker_start = {},
      .dispatch = microseconds(500),
      .target_chunk = microseconds(32),
      .minimum_saving = microseconds(300),
  };
  const MatchWorkEstimate estimate{.sampled_entries = 16, .sampled_elapsed = microseconds(800)};
  EXPECT_THAT(PlanMatchWork(32, 2, 0, estimate, costs), FieldsAre(2, 1));
  costs.dispatch += nanoseconds(1);
  EXPECT_THAT(PlanMatchWork(32, 2, 0, estimate, costs), FieldsAre(1, 4));
}

TEST_F(MatchWorkTest, InvalidObservationsAndCostsFallBackToCaller) {
  const MatchWorkEstimate valid{.sampled_entries = 16, .sampled_elapsed = microseconds(160)};
  EXPECT_THAT(PlanMatchWork(1'024, 3, 0, {}), FieldsAre(1, 4));
  EXPECT_THAT(PlanMatchWork(1'024, 3, 0, {.sampled_elapsed = microseconds(160)}), FieldsAre(1, 4));
  EXPECT_THAT(PlanMatchWork(1'024, 3, 0, {.sampled_entries = 16}), FieldsAre(1, 4));
  EXPECT_THAT(PlanMatchWork(1'024, 3, 0, {.sampled_entries = 16, .sampled_elapsed = nanoseconds(-1)}), FieldsAre(1, 4));
  const std::array invalid{
      MatchWorkCosts{.cold_start = nanoseconds(-1)},     MatchWorkCosts{.worker_start = nanoseconds(-1)},
      MatchWorkCosts{.dispatch = nanoseconds(-1)},       MatchWorkCosts{.target_chunk = nanoseconds(-1)},
      MatchWorkCosts{.minimum_saving = nanoseconds(-1)},
  };
  for (const auto costs : invalid) {
    EXPECT_THAT(PlanMatchWork(1'024, 3, 0, valid, costs), FieldsAre(1, 4));
  }
}

TEST_F(MatchWorkTest, MaximumCountsDoNotOverflowDurationOrSearchWorkerCountsLinearly) {
  constexpr auto kMaximum = std::numeric_limits<std::size_t>::max();
  const auto plan =
      PlanMatchWork(kMaximum, kMaximum, kMaximum, {.sampled_entries = 1, .sampled_elapsed = nanoseconds::max()});
  EXPECT_THAT(plan.participants, Eq(kMaximum));
  EXPECT_THAT(plan.grain, Eq(1));
  const auto cold = PlanMatchWork(kMaximum, kMaximum, 0, {.sampled_entries = 1, .sampled_elapsed = nanoseconds::max()});
  EXPECT_THAT(cold.participants, AllOf(Ge(2), Le(kMaximum)));
  EXPECT_THAT(cold.grain, AllOf(Ge(1), Le(128)));
}

TEST_F(MatchWorkTest, ConstantTimeSearchAgreesWithExhaustiveBoundedCostModel) {
  const MatchWorkCosts costs;
  constexpr std::array kRemaining{2UZ, 4UZ, 16UZ, 256UZ, 8'192UZ};
  constexpr std::array kLimits{2UZ, 3UZ, 10UZ};
  constexpr std::array kStarted{0UZ, 1UZ, 9UZ};
  constexpr std::array kElapsed{microseconds(1), microseconds(160), microseconds(1'600)};
  for (const auto remaining : kRemaining) {
    for (const auto limit : kLimits) {
      for (const auto started : kStarted) {
        for (const auto elapsed : kElapsed) {
          SCOPED_TRACE(Message() << remaining << ":" << limit << ":" << started << ":" << elapsed.count());
          const double serial = static_cast<double>(nanoseconds(elapsed).count()) * static_cast<double>(remaining) / 16;
          double best_cost = serial;
          std::size_t expected = 1;
          const std::size_t capacity = remaining < limit ? remaining : limit;
          for (std::size_t participants = 2; participants <= capacity; ++participants) {
            const std::size_t added = participants - 1 > started ? participants - 1 - started : 0;
            const double predicted = (serial / static_cast<double>(participants))
                                     + static_cast<double>(started == 0 ? costs.cold_start.count() : 0)
                                     + static_cast<double>(costs.dispatch.count())
                                     + (static_cast<double>(added) * static_cast<double>(costs.worker_start.count()));
            if (predicted < best_cost) {
              expected = participants;
              best_cost = predicted;
            }
          }
          if (serial - best_cost < static_cast<double>(costs.minimum_saving.count())) {
            expected = 1;
          }
          EXPECT_THAT(
              PlanMatchWork(remaining, limit, started, {.sampled_entries = 16, .sampled_elapsed = elapsed})
                  .participants,
              Eq(expected));
        }
      }
    }
  }
}

}  // namespace
}  // namespace xff::engine
