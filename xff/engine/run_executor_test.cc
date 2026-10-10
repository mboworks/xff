// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/run_executor.h"

#include <atomic>
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

struct BlockingCapture final {
  BlockingCapture(absl::Notification& destruction_started, absl::Notification& release)
      : destruction_started(destruction_started), release(release) {}

  BlockingCapture(const BlockingCapture&) = delete;
  BlockingCapture& operator=(const BlockingCapture&) = delete;
  BlockingCapture(BlockingCapture&&) = delete;
  BlockingCapture& operator=(BlockingCapture&&) = delete;

  absl::Notification& destruction_started;
  absl::Notification& release;

  ~BlockingCapture() {
    destruction_started.Notify();
    release.WaitForNotificationWithTimeout(absl::Seconds(5));
  }
};

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

TEST_F(RunExecutorTest, ClaimableTasksRunInlineAndSupportMoveOnlyResults) {
  RunExecutor executor(1);
  const auto coordinator = std::this_thread::get_id();
  auto task = executor.SubmitClaimable([value = std::make_unique<int>(42)] mutable { return std::move(value); });
  EXPECT_THAT(task.Ready(), IsTrue());
  const auto result = task.Get();
  EXPECT_THAT(*result, Eq(42));
  EXPECT_THAT(executor.SubmitClaimable([] { return std::this_thread::get_id(); }).Get(), Eq(coordinator));
  executor.SubmitClaimable([] {}).Wait();
  EXPECT_THAT(executor.worker_count(), Eq(0));
  EXPECT_THAT(executor.QueuedTaskCount(), Eq(0));
}

TEST_F(RunExecutorTest, ClaimableCompletionWaitsForCallbackCaptureDestruction) {
  RunExecutor executor(1);
  executor.Start(1);
  absl::Notification destruction_started;
  absl::Notification release;
  // Construct in place: a temporary guard would notify during its own destruction.
  auto guard = std::make_unique<BlockingCapture>(destruction_started, release);
  auto task = executor.SubmitClaimable([guard = std::move(guard)] { return guard ? 42 : 0; });
  const bool destroying = destruction_started.WaitForNotificationWithTimeout(absl::Seconds(5));
  const bool published_before_destruction = task.Ready();
  release.Notify();
  EXPECT_THAT(task.Get(), Eq(42));
  EXPECT_THAT(destroying, IsTrue());
  EXPECT_THAT(published_before_destruction, IsFalse());
}

TEST_F(RunExecutorTest, CallerClaimedBatchesBoundQueueRetentionAndLeaveUnrelatedWorkQueued) {
  RunExecutor executor(1);
  executor.Start(1);
  absl::Notification started;
  absl::Notification release;
  auto occupied = executor.Submit([&] {
    started.Notify();
    return release.WaitForNotificationWithTimeout(absl::Seconds(10));
  });
  const bool running = started.WaitForNotificationWithTimeout(absl::Seconds(5));
  std::atomic<int> unrelated_calls = 0;
  auto unrelated = executor.Submit([&] { unrelated_calls.fetch_add(1); });
  std::size_t own_calls = 0;
  const auto coordinator = std::this_thread::get_id();
  for (std::size_t batch = 0; batch < 1'024; ++batch) {
    SCOPED_TRACE(batch);
    const auto task_class = batch % 2 == 0 ? RunTaskClass::kForeground : RunTaskClass::kReadAhead;
    auto task = executor.SubmitClaimable(
        [&] {
          ++own_calls;
          return std::this_thread::get_id();
        },
        task_class);
    EXPECT_THAT(task.Get(), Eq(coordinator));
    EXPECT_THAT(task.Ready(), IsTrue());
    // The single unrelated ordinary job plus this batch's claimed wrapper.
    EXPECT_THAT(executor.QueuedTaskCount(), Le(2));
  }
  const int unrelated_before_release = unrelated_calls.load();
  release.Notify();
  EXPECT_THAT(occupied.Get(), IsTrue());
  unrelated.Get();
  EXPECT_THAT(running, IsTrue());
  EXPECT_THAT(own_calls, Eq(1'024));
  EXPECT_THAT(unrelated_before_release, Eq(0));
  EXPECT_THAT(unrelated_calls.load(), Eq(1));
  EXPECT_THAT(executor.Submit([] { return 42; }).Get(), Eq(42));
}

TEST_F(RunExecutorTest, AWorkerClaimedTaskIsJoinedAndNeverExecutedTwice) {
  RunExecutor executor(1);
  executor.Start(1);
  absl::Notification started;
  absl::Notification release;
  std::atomic<int> calls = 0;
  auto task = executor.SubmitClaimable([&] {
    calls.fetch_add(1);
    started.Notify();
    return release.WaitForNotificationWithTimeout(absl::Seconds(5)) ? 42 : 0;
  });
  const bool running = started.WaitForNotificationWithTimeout(absl::Seconds(5));
  const bool ready_while_worker_runs = task.Ready();
  release.Notify();
  task.Wait();
  EXPECT_THAT(task.Get(), Eq(42));
  EXPECT_THAT(running, IsTrue());
  EXPECT_THAT(ready_while_worker_runs, IsFalse());
  EXPECT_THAT(calls.load(), Eq(1));
}

TEST_F(RunExecutorTest, ClaimableResultsCanOutliveTheExecutor) {
  auto task = [] {
    RunExecutor executor(1);
    executor.Start(1);
    return executor.SubmitClaimable([] { return std::make_unique<int>(42); });
  }();
  EXPECT_THAT(task.Ready(), IsTrue());
  const auto result = task.Get();
  EXPECT_THAT(*result, Eq(42));
}

TEST_F(RunExecutorTest, RacingTypedCallerAndWorkerClaimsPublishMoveOnlyResultsOnce) {
  RunExecutor executor(1);
  executor.Start(1);
  std::atomic<std::size_t> calls = 0;
  for (std::size_t round = 0; round < 128; ++round) {
    SCOPED_TRACE(round);
    absl::Barrier released(2);
    auto occupied = executor.Submit([&] { released.Block(); });
    auto task = executor.SubmitClaimable([&] {
      calls.fetch_add(1);
      return std::make_unique<std::size_t>(round);
    });
    // Release the worker and claim the queued typed result concurrently. Separate
    // controls force worker-owned and coordinator-owned execution deterministically.
    released.Block();
    const auto result = task.Get();
    occupied.Get();
    EXPECT_THAT(*result, Eq(round));
    EXPECT_THAT(calls.load(), Eq(round + 1));
  }
}

TEST_F(RunExecutorTest, WorkerDispatchedClaimableQueuesPreserveClassAlternation) {
  RunExecutor executor(1);
  executor.Start(1);
  absl::Notification started;
  absl::Notification release;
  std::vector<int> order;
  auto initial = executor.Submit(
      [&] {
        started.Notify();
        release.WaitForNotificationWithTimeout(absl::Seconds(5));
        order.push_back(0);
      },
      RunTaskClass::kReadAhead);
  const bool running = started.WaitForNotificationWithTimeout(absl::Seconds(5));
  std::vector<RunClaimableTask<void>> tasks;
  tasks.reserve(5);
  tasks.push_back(executor.SubmitClaimable([&] { order.push_back(1); }, RunTaskClass::kReadAhead));
  tasks.push_back(executor.SubmitClaimable([&] { order.push_back(2); }, RunTaskClass::kReadAhead));
  tasks.push_back(executor.SubmitClaimable([&] { order.push_back(3); }));
  tasks.push_back(executor.SubmitClaimable([&] { order.push_back(4); }));
  tasks.push_back(executor.SubmitClaimable([&] { order.push_back(5); }));
  auto finished = executor.Submit([&] { order.push_back(6); });
  release.Notify();
  initial.Get();
  // Wait for the trailing ordinary marker before invoking typed Get: this verifies
  // actual worker dispatch order, not coordinator pumping or caller claim order.
  finished.Get();
  for (auto& task : tasks) {
    task.Get();
  }
  EXPECT_THAT(running, IsTrue());
  EXPECT_THAT(order, ElementsAre(0, 3, 1, 4, 2, 5, 6));
}

TEST_F(RunExecutorTest, CallerClaimsRequiredWorkWhileItsQueuedWrapperWaits) {
  RunExecutor executor(1);
  executor.Start(1);
  absl::Notification started;
  absl::Notification release;
  auto occupied = executor.Submit([&] {
    started.Notify();
    release.WaitForNotification();
  });
  const bool running = started.WaitForNotificationWithTimeout(absl::Seconds(5));
  std::size_t calls = 0;
  RunPromise<int> promise;
  auto result = promise.Task();
  const auto work =
      std::make_shared<RunWork>([&calls, value = std::make_unique<int>(42), promise = std::move(promise)] mutable {
        ++calls;
        promise.SetValue(*value);
      });
  auto queued = executor.Submit([work] { work->Run(); }, RunTaskClass::kReadAhead);
  work->Run();
  const bool ready_before_release = result.Ready();
  release.Notify();
  occupied.Get();
  queued.Get();
  work->Run();
  EXPECT_THAT(running, IsTrue());
  EXPECT_THAT(ready_before_release, IsTrue());
  EXPECT_THAT(result.Get(), Eq(42));
  EXPECT_THAT(calls, Eq(1));
}

TEST_F(RunExecutorTest, CallerDoesNotDuplicateOrWaitInsideWorkerClaimedWork) {
  RunExecutor executor(1);
  executor.Start(1);
  absl::Notification started;
  absl::Notification release;
  std::atomic<int> calls = 0;
  RunPromise<int> promise;
  auto result = promise.Task();
  const auto work = std::make_shared<RunWork>([&calls, &started, &release, promise = std::move(promise)] mutable {
    calls.fetch_add(1);
    started.Notify();
    // Bound a broken claim implementation so it reports failure rather than hanging.
    promise.SetValue(release.WaitForNotificationWithTimeout(absl::Seconds(5)) ? 42 : 0);
  });
  auto dispatched = executor.Submit([work] { work->Run(); });
  const bool running = started.WaitForNotificationWithTimeout(absl::Seconds(5));
  work->Run();
  const bool ready_before_release = result.Ready();
  release.Notify();
  dispatched.Get();
  EXPECT_THAT(running, IsTrue());
  EXPECT_THAT(ready_before_release, IsFalse());
  EXPECT_THAT(result.Get(), Eq(42));
  EXPECT_THAT(calls.load(), Eq(1));
}

TEST_F(RunExecutorTest, SimultaneousCallerAndWorkerClaimsExecuteMoveOnlyWorkOnce) {
  RunExecutor executor(2);
  executor.Start(2);
  absl::Barrier ready(3);
  std::atomic<int> calls = 0;
  RunPromise<int> promise;
  auto result = promise.Task();
  const auto work =
      std::make_shared<RunWork>([&calls, value = std::make_unique<int>(42), promise = std::move(promise)] mutable {
        calls.fetch_add(1);
        promise.SetValue(*value);
      });
  const auto claim = [&] {
    ready.Block();
    work->Run();
  };
  auto first = executor.Submit(claim);
  auto second = executor.Submit(claim);
  claim();
  first.Get();
  second.Get();
  EXPECT_THAT(result.Get(), Eq(42));
  EXPECT_THAT(calls.load(), Eq(1));
}

TEST_F(RunExecutorTest, WorkerWrapperDoesNotWaitForCallerClaimedWork) {
  RunExecutor executor(1);
  executor.Start(1);
  absl::Notification started;
  absl::Notification release;
  RunPromise<int> promise;
  auto result = promise.Task();
  const auto work = std::make_shared<RunWork>([&started, &release, promise = std::move(promise)] mutable {
    started.Notify();
    // A queued wrapper may finish while its caller-owned read is still running.
    // The coordinator owns that inline lifetime, not the wrapper's completion.
    promise.SetValue(release.WaitForNotificationWithTimeout(absl::Seconds(5)) ? 42 : 0);
  });
  auto wrapper = executor.Submit([&started, &release, work] {
    const bool caller_started = started.WaitForNotificationWithTimeout(absl::Seconds(5));
    work->Run();
    release.Notify();
    return caller_started;
  });
  work->Run();
  EXPECT_THAT(wrapper.Get(), IsTrue());
  EXPECT_THAT(result.Get(), Eq(42));
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
