// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/output_credit.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "absl/synchronization/mutex.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/engine/cpu_allowance.h"

namespace xff::engine {
namespace {

using ::mbo::testing::EqualsText;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct OutputCreditTest : ::testing::Test {
  static void CheckCpuCreditWake(bool cancel) {
    OutputCredit credit;
    CpuAllowance cpu(1);
    EXPECT_THAT(credit.Acquire(2, OutputCredit::kSharedBytes), IsTrue());
    bool admitted = false;
    std::atomic<bool> finished = false;
    std::jthread producer([&] {
      CpuAllowance::Lease lease(cpu);
      admitted = credit.Acquire(1, 1, lease);
      finished.store(true);
    });
    EXPECT_THAT(AwaitBlocked(credit), IsTrue());
    std::optional<CpuAllowance::Lease> coordinator;
    coordinator.emplace(cpu);
    if (cancel) {
      credit.Cancel();
    } else {
      credit.Advance(1);
    }
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (cpu.GetUsage().waiters == 0 && std::chrono::steady_clock::now() < end) {
      std::this_thread::yield();
    }
    EXPECT_THAT(cpu.GetUsage().waiters, Eq(1));
    EXPECT_THAT(finished.load(), IsFalse());
    // A resumed producer cannot retain the credit mutex while waiting for CPU.
    EXPECT_THAT(credit.GetUsage().excess, Eq(OutputCredit::kSharedBytes + (cancel ? 0UZ : 1UZ)));
    coordinator.reset();
    producer.join();
    EXPECT_THAT(finished.load(), IsTrue());
    if (cancel) {
      EXPECT_THAT(admitted, IsFalse());
    } else {
      EXPECT_THAT(admitted, IsTrue());
    }
    credit.Release(OutputCredit::kSharedBytes + (cancel ? 0UZ : 1UZ));
    EXPECT_THAT(credit.GetUsage().excess, Eq(0));
    EXPECT_THAT(cpu.GetUsage().available, Eq(1));
  }

  static bool AwaitBlocked(const OutputCredit& credit) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (credit.GetUsage().waiters == 0 && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    return credit.GetUsage().waiters != 0;
  }
};

TEST_F(OutputCreditTest, BlockedOutputReturnsCpuAndReacquiresItAfterDroppingTheCreditMutex) {
  CheckCpuCreditWake(false);
}

TEST_F(OutputCreditTest, CancelledOutputReacquiresCpuBeforeDrainingTheProducer) {
  CheckCpuCreditWake(true);
}

TEST_F(OutputCreditTest, ContiguousGrowthAndHeadingPreserveTextAndReleaseEveryAllocation) {
  OutputCredit credit;
  {
    CreditedOutput output(credit, 0);
    output.Append("selected line\n");
    output.Prepend("heading\n");
    EXPECT_THAT(output.View(), EqualsText("heading\nselected line\n"));
    const std::string text(4 * OutputCredit::kSlotGrant, 'x');
    output.Append(text);
    EXPECT_THAT(output.View(), EqualsText("heading\nselected line\n" + text));
    EXPECT_THAT(credit.GetUsage().excess, Ge(text.size() - OutputCredit::kSlotGrant));
    EXPECT_THAT(credit.GetUsage().peak_excess, Ge(credit.GetUsage().excess));
    CreditedOutput moved(std::move(output));
    EXPECT_THAT(moved.View(), EqualsText("heading\nselected line\n" + text));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): defined reusable source contract.
    output.Append("reused");
    EXPECT_THAT(output.View(), EqualsText("reused"));
    moved.Clear();
    EXPECT_THAT(moved.Empty(), IsTrue());
  }
  EXPECT_THAT(credit.GetUsage().excess, Eq(0));
}

TEST_F(OutputCreditTest, EmptyAndCancelledTransactionsRetainNoPublishedText) {
  OutputCredit credit;
  CreditedOutput output(credit, 1);
  output.Append("");
  output.Prepend("");
  EXPECT_THAT(output.Empty(), IsTrue());
  credit.Cancel();
  output.Append(std::string(OutputCredit::kSlotGrant * 2, 'x'));
  EXPECT_THAT(output.Cancelled(), IsTrue());
  output.Append("discarded");
  output.Prepend("discarded heading");
  EXPECT_THAT(output.Empty(), IsTrue());
  EXPECT_THAT(credit.GetUsage().excess, Eq(0));
}

TEST_F(OutputCreditTest, HeapGrowthReservesOldAndNewCapacityUntilTheOldAllocationIsReleased) {
  OutputCredit credit;
  {
    CreditedOutput output(credit, 0);
    const std::string text(OutputCredit::kSlotGrant, 'x');
    output.Append(text);
    EXPECT_THAT(credit.GetUsage().excess, Eq(0));
    output.Prepend("heading\n");
    EXPECT_THAT(output.View(), EqualsText("heading\n" + text));
    EXPECT_THAT(credit.GetUsage().excess, Eq(output.Capacity() - OutputCredit::kSlotGrant));
    EXPECT_THAT(credit.GetUsage().peak_excess, Eq(output.Capacity()));
  }
  EXPECT_THAT(credit.GetUsage().excess, Eq(0));
}

TEST_F(OutputCreditTest, ReuseKeepsOnlyHeapStorageCoveredByTheSlotGrant) {
  OutputCredit credit;
  CreditedOutput output(credit, 0);
  output.Append(std::string(512, 'x'));
  const auto capacity = output.Capacity();
  output.Release();
  EXPECT_THAT(output.Empty(), IsTrue());
  EXPECT_THAT(output.Capacity(), Eq(capacity));
  credit.Advance(1);
  output.Begin(1);
  output.Append("new transaction");
  EXPECT_THAT(output.View(), EqualsText("new transaction"));
  output.Append(std::string(2 * OutputCredit::kSlotGrant, 'y'));
  output.Release();
  EXPECT_THAT(output.Capacity(), Eq(CreditedOutput::kInlineBytes));
  EXPECT_THAT(credit.GetUsage().excess, Eq(0));
}

TEST_F(OutputCreditTest, InlineMovesAndReusedSlotsDoNotCopyUninitializedBytes) {
  OutputCredit credit;
  CreditedOutput output(credit, 0);
  output.Append("test");
  const CreditedOutput moved(std::move(output));
  EXPECT_THAT(moved.View(), EqualsText("test"));
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): defined empty source contract.
  EXPECT_THAT(output.Empty(), IsTrue());
  EXPECT_THAT(credit.GetUsage().excess, Eq(0));
}

TEST_F(OutputCreditTest, OrderedHeadCanFinishWhenLaterTransactionsExhaustSharedCredit) {
  OutputCredit credit;
  EXPECT_THAT(credit.Acquire(1, OutputCredit::kSharedBytes), IsTrue());
  // A later entry cannot take another byte, but head admission must never wait.
  EXPECT_THAT(credit.Acquire(0, 2 * OutputCredit::kSharedBytes), IsTrue());
  EXPECT_THAT(credit.GetUsage().peak_excess, Eq(3 * OutputCredit::kSharedBytes));
  credit.Release(2 * OutputCredit::kSharedBytes);
  credit.Release(OutputCredit::kSharedBytes);
  EXPECT_THAT(credit.GetUsage().excess, Eq(0));
}

TEST_F(OutputCreditTest, AdvancingTheHeadWakesAnOversizedLaterTransaction) {
  OutputCredit credit;
  std::atomic<bool> entered = false;
  bool admitted = false;
  std::jthread producer([&] {
    entered.store(true);
    entered.notify_one();
    admitted = credit.Acquire(1, 2 * OutputCredit::kSharedBytes);
  });
  entered.wait(false);
  EXPECT_THAT(AwaitBlocked(credit), IsTrue());
  credit.Advance(1);
  producer.join();
  EXPECT_THAT(admitted, IsTrue());
  credit.Release(2 * OutputCredit::kSharedBytes);
}

TEST_F(OutputCreditTest, BackpressureWakesARegisteredPublicationWindow) {
  absl::Mutex progress;
  OutputCredit credit(progress);
  std::atomic<bool> entered = false;
  std::atomic<bool> finished = false;
  std::atomic<bool> stop = false;
  bool observed = false;
  std::jthread coordinator([&] {
    const absl::MutexLock lock(progress);
    entered.store(true);
    entered.notify_one();
    const auto ready = [&] { return credit.Blocked() || stop.load(); };
    progress.Await(absl::Condition(&ready));
    observed = credit.Blocked();
    finished.store(true);
  });
  entered.wait(false);
  bool admitted = true;
  std::jthread producer([&] { admitted = credit.Acquire(1, 2 * OutputCredit::kSharedBytes); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!finished.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  EXPECT_THAT(finished.load(), IsTrue());
  stop.store(true);
  { const absl::MutexLock notify(progress); }
  credit.Cancel();
  coordinator.join();
  producer.join();
  EXPECT_THAT(observed, IsTrue());
  EXPECT_THAT(admitted, IsFalse());
}

TEST_F(OutputCreditTest, CancellationWakesBlockedReservationsAndResetAllowsFreshWork) {
  OutputCredit credit;
  std::atomic<bool> entered = false;
  bool admitted = true;
  std::jthread producer([&] {
    entered.store(true);
    entered.notify_one();
    admitted = credit.Acquire(1, 2 * OutputCredit::kSharedBytes);
  });
  entered.wait(false);
  EXPECT_THAT(AwaitBlocked(credit), IsTrue());
  credit.Cancel();
  producer.join();
  EXPECT_THAT(admitted, IsFalse());
  credit.Reset();
  EXPECT_THAT(credit.Acquire(1, 4), IsTrue());
  credit.Release(4);
  EXPECT_THAT(credit.GetUsage().excess, Eq(0));
}

}  // namespace
}  // namespace xff::engine
