// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/run_executor.h"

#include <limits>
#include <memory>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "absl/synchronization/barrier.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::engine {
namespace {
using ::testing::_;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::Ne;
using ::testing::Not;
using ::testing::Optional;
using ::testing::SizeIs;

struct RunExecutorTest : ::testing::Test {};

TEST_F(RunExecutorTest, RunsInlineUntilStartedAndRetainsWorkers) {
  RunExecutor executor(3);
  const std::thread::id coordinator = std::this_thread::get_id();
  EXPECT_THAT(executor.Submit([] { return std::this_thread::get_id(); }).Get(), Eq(coordinator));

  executor.Start(2);
  EXPECT_THAT(executor.worker_count(), Eq(2));
  EXPECT_THAT(executor.Submit([] { return std::this_thread::get_id(); }).Get(), Ne(coordinator));

  executor.Start(3);
  EXPECT_THAT(executor.worker_count(), Eq(3));
  executor.Start(1);
  EXPECT_THAT(executor.worker_count(), Eq(3));
  executor.Start(100);
  EXPECT_THAT(executor.worker_count(), Eq(3));
}

TEST_F(RunExecutorTest, ReusesTheSameBoundedThreadsAcrossPhases) {
  RunExecutor executor(3);
  executor.Start(3);
  const auto phase = [&] {
    absl::Barrier started(3);
    std::vector<RunTask<std::thread::id>> tasks;
    tasks.reserve(3);
    for (int worker = 0; worker < 3; ++worker) {
      tasks.push_back(executor.Submit([&] {
        started.Block();
        return std::this_thread::get_id();
      }));
    }
    std::set<std::thread::id> threads;
    for (auto& task : tasks) {
      threads.insert(task.Get());
    }
    return threads;
  };
  const auto first = phase();
  EXPECT_THAT(first, SizeIs(3));
  EXPECT_THAT(first, Not(Contains(std::this_thread::get_id())));
  EXPECT_THAT(phase(), Eq(first));
}

TEST_F(RunExecutorTest, SupportsMoveOnlyJobsAndResults) {
  RunExecutor executor(1);
  executor.Start(1);
  auto task = executor.Submit([value = std::make_unique<int>(42)] mutable { return std::move(value); });
  const auto result = task.Get();
  EXPECT_THAT(*result, Eq(42));
  EXPECT_THAT(RunTask<void>{}.Valid(), IsFalse());
  executor.Submit([] {}).Get();
}

TEST_F(RunExecutorTest, ReadAheadSlotsAndBytesAreSharedAndReleasedByTheirOwner) {
  RunExecutor executor(2, 100);
  EXPECT_THAT(executor.ReserveReadAhead(), Eq(std::nullopt));
  executor.Start(1);
  auto first = executor.ReserveReadAhead();
  auto second = executor.ReserveReadAhead();
  ASSERT_THAT(first, Optional(_));
  ASSERT_THAT(second, Optional(_));
  // The assertions report absence; this guard also proves engagement to static analysis.
  if (!first || !second) {
    return;
  }
  EXPECT_THAT(executor.ReserveReadAhead(), Eq(std::nullopt));
  EXPECT_THAT(first.value().Retain(60), IsTrue());
  EXPECT_THAT(second.value().Retain(41), IsFalse());
  EXPECT_THAT(second.value().Retain(40), IsTrue());
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(100));
  EXPECT_THAT(first.value().Retain(std::numeric_limits<std::size_t>::max()), IsFalse());
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(100));
  first.reset();
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(40));
  EXPECT_THAT(executor.ReadAheadUsage().in_use, Eq(1));
  executor.Start(2);
  std::vector<ReadAheadReservation> additional;
  additional.reserve(3);
  for (int index = 0; index < 3; ++index) {
    auto reservation = executor.ReserveReadAhead();
    ASSERT_THAT(reservation, Optional(_));
    if (!reservation) {
      return;
    }
    additional.push_back(std::move(reservation.value()));
  }
  EXPECT_THAT(executor.ReserveReadAhead(), Eq(std::nullopt));
  EXPECT_THAT(executor.ReadAheadUsage().peak_in_use, Eq(4));
  EXPECT_THAT(executor.ReadAheadUsage().peak_retained_bytes, Eq(100));
  additional.clear();
  second.reset();
  EXPECT_THAT(executor.ReadAheadUsage().in_use, Eq(0));
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(0));
}

TEST_F(RunExecutorTest, ConcurrentReadRetainersCannotExceedTheirSharedByteAllowance) {
  RunExecutor executor(3, 100);
  executor.Start(3);
  absl::Barrier ready(3);
  std::vector<RunTask<bool>> tasks;
  tasks.reserve(3);
  for (int index = 0; index < 3; ++index) {
    auto reservation = executor.ReserveReadAhead();
    ASSERT_THAT(reservation, Optional(_));
    tasks.push_back(executor.Submit([&ready, reservation = std::move(reservation)] mutable {
      ready.Block();
      const bool retained = reservation.value().Retain(40);
      reservation.reset();
      return retained;
    }));
  }
  // Each job releases its charge at completion, so successes are not required to
  // overlap. The measured peak must always fit, regardless of worker ordering.
  for (auto& task : tasks) {
    task.Get();
  }
  EXPECT_THAT(executor.ReadAheadUsage().peak_retained_bytes, Le(100));
  EXPECT_THAT(executor.ReadAheadUsage().in_use, Eq(0));
}

TEST_F(RunExecutorTest, CompletedListingsRetainBytesWithoutBlockingDeeperReadSlots) {
  RunExecutor executor(1, 100);
  executor.Start(1);
  auto first = executor.ReserveReadAhead(20);
  auto second = executor.ReserveReadAhead(20);
  ASSERT_THAT(first, Optional(_));
  ASSERT_THAT(second, Optional(_));
  if (!first || !second) {
    return;
  }
  EXPECT_THAT(executor.ReserveReadAhead(1), Eq(std::nullopt));
  first.value().Finish();
  second.value().Finish();
  EXPECT_THAT(executor.ReadAheadUsage().in_use, Eq(0));
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(40));
  EXPECT_THAT(executor.ReserveReadAhead(61), Eq(std::nullopt));
  auto deeper = executor.ReserveReadAhead(60);
  ASSERT_THAT(deeper, Optional(_));
  EXPECT_THAT(executor.ReadAheadUsage().in_use, Eq(1));
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(100));
  deeper.reset();
  first.reset();
  second.reset();
  EXPECT_THAT(executor.ReadAheadUsage().in_use, Eq(0));
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(0));
}

TEST_F(RunExecutorTest, FinishingIsIdempotentAndCannotDropTheBookkeepingCharge) {
  RunExecutor executor(1, 100);
  executor.Start(1);
  auto reservation = executor.ReserveReadAhead(20);
  ASSERT_THAT(reservation, Optional(_));
  if (!reservation) {
    return;
  }
  EXPECT_THAT(reservation.value().Retain(19), IsFalse());
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(20));
  EXPECT_THAT(reservation.value().Retain(100), IsTrue());
  reservation.value().Finish();
  reservation.value().Finish();
  EXPECT_THAT(executor.ReadAheadUsage().in_use, Eq(0));
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(100));
  EXPECT_THAT(reservation.value().Retain(0), IsFalse());
  EXPECT_THAT(reservation.value().Retain(20), IsTrue());
  auto unfinished = executor.ReserveReadAhead(80);
  ASSERT_THAT(unfinished, Optional(_));
  reservation.reset();
  EXPECT_THAT(executor.ReadAheadUsage().in_use, Eq(1));
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(80));
  unfinished.reset();
  EXPECT_THAT(executor.ReadAheadUsage().in_use, Eq(0));
  EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(0));
}

TEST_F(RunExecutorTest, ForegroundAndReadAheadQueuesAlternateWithoutStarvation) {
  RunExecutor executor(1);
  executor.Start(1);
  absl::Notification started;
  absl::Notification release;
  std::vector<int> order;
  auto initial = executor.Submit(
      [&] {
        started.Notify();
        release.WaitForNotification();
        order.push_back(0);
      },
      RunTaskClass::kReadAhead);
  const bool running = started.WaitForNotificationWithTimeout(absl::Seconds(5));
  std::vector<RunTask<void>> tasks;
  tasks.reserve(5);
  tasks.push_back(executor.Submit([&] { order.push_back(1); }, RunTaskClass::kReadAhead));
  tasks.push_back(executor.Submit([&] { order.push_back(2); }, RunTaskClass::kReadAhead));
  tasks.push_back(executor.Submit([&] { order.push_back(3); }));
  tasks.push_back(executor.Submit([&] { order.push_back(4); }));
  tasks.push_back(executor.Submit([&] { order.push_back(5); }));
  release.Notify();
  initial.Get();
  for (auto& task : tasks) {
    task.Get();
  }
  EXPECT_THAT(running, IsTrue());
  EXPECT_THAT(order, ElementsAre(0, 3, 1, 4, 2, 5));
}

TEST_F(RunExecutorTest, AReservationCanMoveAcrossThreadsAndOutliveItsExecutor) {
  std::optional<ReadAheadReservation> retained;
  {
    RunExecutor executor(1, 100);
    executor.Start(1);
    auto reservation = executor.ReserveReadAhead();
    ASSERT_THAT(reservation, Optional(_));
    if (!reservation) {
      return;
    }
    auto task = executor.Submit([reservation = std::move(reservation.value())] mutable {
      EXPECT_THAT(reservation.Retain(50), IsTrue());
      return std::move(reservation);
    });
    retained.emplace(task.Get());
    EXPECT_THAT(executor.ReadAheadUsage().in_use, Eq(1));
    EXPECT_THAT(executor.ReadAheadUsage().retained_bytes, Eq(50));
  }
  EXPECT_THAT(retained.value().Retain(100), IsTrue());
  EXPECT_THAT(retained.value().Retain(101), IsFalse());
}

}  // namespace
}  // namespace xff::engine
