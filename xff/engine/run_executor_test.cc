// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/run_executor.h"

#include <memory>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "absl/synchronization/barrier.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::engine {
namespace {
using ::testing::Contains;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::Ne;
using ::testing::Not;
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
  auto task = executor.Submit([value = std::make_unique<int>(42)]() mutable { return std::move(value); });
  auto result = task.Get();
  EXPECT_THAT(*result, Eq(42));
  EXPECT_THAT(RunTask<void>{}.Valid(), IsFalse());
  executor.Submit([] {}).Get();
}

}  // namespace
}  // namespace xff::engine
