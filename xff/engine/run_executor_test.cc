// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/run_executor.h"

#include <future>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::engine {
namespace {
using ::testing::Eq;
using ::testing::Ne;

struct RunExecutorTest : ::testing::Test {};

TEST_F(RunExecutorTest, RunsInlineUntilStartedAndRetainsWorkers) {
  RunExecutor executor(3);
  const std::thread::id coordinator = std::this_thread::get_id();
  EXPECT_THAT(executor.Submit([] { return std::this_thread::get_id(); }).get(), Eq(coordinator));

  executor.Start(2);
  EXPECT_THAT(executor.worker_count(), Eq(2));
  EXPECT_THAT(executor.Submit([] { return std::this_thread::get_id(); }).get(), Ne(coordinator));

  executor.Start(3);
  EXPECT_THAT(executor.worker_count(), Eq(3));
}

}  // namespace
}  // namespace xff::engine
