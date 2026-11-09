// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/cpu_allowance.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <latch>
#include <thread>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::engine {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Le;

struct CpuAllowanceTest : ::testing::Test {
  static bool AwaitWaiters(const CpuAllowance& allowance, std::size_t count) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (allowance.GetUsage().waiters < count && std::chrono::steady_clock::now() < end) {
      std::this_thread::yield();
    }
    return allowance.GetUsage().waiters >= count;
  }
};

TEST_F(CpuAllowanceTest, ZeroUnitsRetainsOneUsableParticipantAndLeaseRestoresIt) {
  CpuAllowance allowance(0);
  EXPECT_THAT(allowance.GetUsage().available, Eq(1));
  {
    CpuAllowance::Lease lease(allowance);
    EXPECT_THAT(allowance.GetUsage().available, Eq(0));
    lease.Pause();
    lease.Pause();
    EXPECT_THAT(allowance.GetUsage().available, Eq(1));
    lease.Resume();
    lease.Resume();
    EXPECT_THAT(allowance.GetUsage().available, Eq(0));
  }
  EXPECT_THAT(allowance.GetUsage().available, Eq(1));
}

TEST_F(CpuAllowanceTest, ABlockedProducerReturnsItsUnitForTheCoordinatorAndCanResume) {
  CpuAllowance allowance(1);
  CpuAllowance::Lease producer(allowance);
  std::atomic<bool> entered = false;
  std::latch leave(1);
  std::jthread coordinator([&] {
    const CpuAllowance::Lease lease(allowance);
    entered.store(true);
    leave.wait();
  });
  EXPECT_THAT(AwaitWaiters(allowance, 1), IsTrue());
  EXPECT_THAT(entered.load(), IsFalse());
  producer.Pause();
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!entered.load() && std::chrono::steady_clock::now() < end) {
    std::this_thread::yield();
  }
  EXPECT_THAT(entered.load(), IsTrue());
  leave.count_down();
  coordinator.join();
  producer.Resume();
  EXPECT_THAT(allowance.GetUsage().available, Eq(0));
}

TEST_F(CpuAllowanceTest, ConcurrentOwnersStayWithinTheSharedAllowance) {
  CpuAllowance allowance(3);
  std::latch start(8);
  std::latch leave(1);
  std::atomic<std::size_t> active = 0;
  std::atomic<std::size_t> peak = 0;
  std::vector<std::jthread> participants;
  participants.reserve(8);
  for (std::size_t index = 0; index < 8; ++index) {
    participants.emplace_back([&] {
      start.arrive_and_wait();
      const CpuAllowance::Lease lease(allowance);
      const auto count = active.fetch_add(1) + 1;
      auto previous = peak.load();
      while (previous < count && !peak.compare_exchange_weak(previous, count)) {}
      leave.wait();
      active.fetch_sub(1);
    });
  }
  EXPECT_THAT(AwaitWaiters(allowance, 5), IsTrue());
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (active.load() < 3 && std::chrono::steady_clock::now() < end) {
    std::this_thread::yield();
  }
  EXPECT_THAT(active.load(), Eq(3));
  EXPECT_THAT(allowance.GetUsage().available, Eq(0));
  leave.count_down();
  for (auto& participant : participants) {
    participant.join();
  }
  EXPECT_THAT(peak.load(), Le(3));
  EXPECT_THAT(active.load(), Eq(0));
  EXPECT_THAT(allowance.GetUsage().available, Eq(3));
}

}  // namespace
}  // namespace xff::engine
