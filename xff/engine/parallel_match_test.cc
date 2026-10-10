// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/parallel_match.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/expression_execution.h"
#include "xff/matching/regex/regex.h"
#include "xff/parser/parser.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

using ::testing::Contains;
using ::testing::Each;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::SizeIs;

struct EvaluationTrace {
  struct Snapshot {
    std::vector<std::size_t> counts;
    std::set<std::thread::id> contributors;
  };

  explicit EvaluationTrace(std::size_t entries) : counts_(entries) {}

  EvaluationResult Evaluate(EvalContext& context) ABSL_LOCKS_EXCLUDED(mutex_) {
    const std::size_t index = context.visit.metadata.size;
    {
      const absl::MutexLock lock(mutex_);
      ++counts_.at(index);
      contributors_.insert(std::this_thread::get_id());
    }
    // A controlled slow evaluator, not a claim about physical storage latency.
    absl::SleepFor(absl::Milliseconds(2));
    return {.matched = index % 2 == 0};
  }

  Snapshot Read() const ABSL_LOCKS_EXCLUDED(mutex_) {
    const absl::MutexLock lock(mutex_);
    return {.counts = counts_, .contributors = contributors_};
  }

 private:
  // Guards only count and contributor observations; no lock spans the delay.
  mutable absl::Mutex mutex_;
  std::vector<std::size_t> counts_ ABSL_GUARDED_BY(mutex_);
  std::set<std::thread::id> contributors_ ABSL_GUARDED_BY(mutex_);
};

struct TracedWorker final : ExpressionExecution::Worker::State {
  explicit TracedWorker(EvaluationTrace& trace) : trace_(trace) {}

  EvaluationResult Evaluate(EvalContext& context) const override { return trace_.Evaluate(context); }

 private:
  EvaluationTrace& trace_;
};

struct TracedPlan final : ExpressionExecution::Plan {
  explicit TracedPlan(EvaluationTrace& trace) : trace_(trace) {}

  ExpressionExecution::Worker MakeWorker(ExpressionWorkerRole) const override {
    return ExpressionExecution::Worker(std::make_unique<TracedWorker>(trace_));
  }

  bool UsesIndexedMatchers() const override { return true; }

 private:
  EvaluationTrace& trace_;
};

struct ParallelMatchTest : ::testing::Test {
  // Type-only expressions never call this backend. No real paths are visited.
  const vfs::LocalFs fs;

  std::vector<CollectedEntry> Entries(std::size_t count) const {
    std::vector<CollectedEntry> entries;
    entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      entries.push_back({
          .path = std::to_string(index),
          .metadata = {.type = index % 2 == 0 ? vfs::FileType::kRegular : vfs::FileType::kDirectory, .size = index},
          .fs = fs,
      });
    }
    return entries;
  }
};

TEST_F(ParallelMatchTest, RepeatedBatchesRetainEntryOrderAndDoNotReuseOldResults) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-type", "f"}));
  EXPECT_THAT(CanParallelMatch(*command.expression), IsTrue());
  EXPECT_THAT(HasContentMatch(*command.expression), IsFalse());
  RunExecutor executor(4);
  ParallelMatch matcher(*command.expression, 4, executor, false);
  for (const std::size_t count : {10, 1'000, 100, 10, 10'000}) {
    const auto& results = matcher.Match(Entries(count));
    EXPECT_THAT(results, SizeIs(count));
    EXPECT_THAT(matcher.Entries(), SizeIs(count));
    for (std::size_t index = 0; index < count; ++index) {
      EXPECT_THAT(results.at(index).evaluation.matched, Eq(index % 2 == 0));
      EXPECT_THAT(matcher.Entries().at(index).path, Eq(std::to_string(index)));
    }
  }
}

TEST_F(ParallelMatchTest, ZeroWorkersIsClampedAndUnusedPoolStartsNoWork) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-true"}));
  RunExecutor unused_executor(4);
  const ParallelMatch unused(*command.expression, 4, unused_executor, false);
  RunExecutor executor(0);
  ParallelMatch matcher(*command.expression, 0, executor, true);
  EXPECT_THAT(matcher.Match(Entries(100)), SizeIs(100));
  EXPECT_THAT(matcher.Match(Entries(0)), SizeIs(0));
}

TEST_F(ParallelMatchTest, NativeRegexBindingsPreserveOperatorsCaseAndRepeatedBatches) {
  MBO_ASSERT_OK_AND_ASSIGN(
      auto command,
      parser::Parse({"root", "(", "-iregex", "[02468]", "-o", "-regex", "1[0-9]", ")", "!", "-regex", "12"}));
  parser::BindMatchers(command, regex::Grammar::kRe2, parser::CaseMode::kSensitive);
  ASSERT_THAT(CanParallelMatch(*command.expression), IsTrue());
  RunExecutor executor(4);
  ParallelMatch matcher(*command.expression, 4, executor, false);
  for (const std::size_t count : {10, 1'000, 100, 10}) {
    const auto& results = matcher.Match(Entries(count));
    for (std::size_t index = 0; index < count; ++index) {
      EXPECT_THAT(
          results.at(index).evaluation.matched,
          Eq((index < 10 && index % 2 == 0) || (index >= 10 && index < 20 && index != 12)));
    }
  }
}

TEST_F(ParallelMatchTest, UnboundRegexIsANonMatchInWorkers) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-regex", "foo"}));
  RunExecutor executor(4);
  ParallelMatch matcher(*command.expression, 4, executor, false);
  for (const auto& result : matcher.Match(Entries(100))) {
    EXPECT_THAT(result.evaluation.matched, IsFalse());
  }
}

TEST_F(ParallelMatchTest, PreparedCoordinatorAndWorkersRetainRegexStateAcrossPoolTransitions) {
  ASSERT_OK_AND_ASSIGN(auto command, parser::Parse({"root", "-regex", "[02468]+"}));
  parser::BindMatchers(command, regex::Grammar::kRe2, parser::CaseMode::kSensitive);
  ASSERT_OK_AND_ASSIGN(const auto execution, PrepareExpressionExecution(*command.expression));
  RunExecutor executor_pool(4);
  const MatchWorkCosts free_dispatch{
      .cold_start = {},
      .worker_start = {},
      .dispatch = {},
      .minimum_saving = {},
  };
  ParallelMatch matcher(*command.expression, 4, executor_pool, false, std::nullopt, execution, free_dispatch);
  // Small batches run on the coordinator, large batches activate private worker state;
  // returning to the coordinator must not reuse a worker's previous match or scratch.
  constexpr std::array kBatches{1UZ, 128UZ, 5UZ, 512UZ, 8'192UZ, 5UZ};
  for (const std::size_t count : kBatches) {
    const auto& results = matcher.Match(Entries(count));
    ASSERT_THAT(results, SizeIs(count));
    for (std::size_t index = 0; index < count; ++index) {
      const auto path = std::to_string(index);
      EXPECT_THAT(results.at(index).evaluation.matched, Eq(path.find_first_of("13579") == std::string::npos));
    }
  }
  EXPECT_THAT(executor_pool.worker_count(), Eq(3));
}

TEST_F(ParallelMatchTest, RepeatedAdmittedBatchesKeepCallerInclusiveBudget) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-true"}));
  RunExecutor executor(4);
  const MatchWorkCosts free_dispatch{
      .cold_start = {},
      .worker_start = {},
      .dispatch = {},
      .minimum_saving = {},
  };
  ParallelMatch matcher(*command.expression, 4, executor, false, std::nullopt, {}, free_dispatch);
  for (int batch = 0; batch < 7; ++batch) {
    EXPECT_THAT(matcher.Match(Entries(1'024)), SizeIs(1'024));
    EXPECT_THAT(executor.worker_count(), Eq(3));
  }
  EXPECT_THAT(matcher.Match(Entries(1'024)), SizeIs(1'024));
  EXPECT_THAT(executor.worker_count(), Eq(3));
}

TEST_F(ParallelMatchTest, CompletedSmallBatchesDoNotAdmitSmallTail) {
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-true"}));
  RunExecutor executor(9);
  ParallelMatch matcher(*command.expression, 10, executor, false);
  constexpr std::size_t kHistoryBatches = 512;
  for (std::size_t batch = 0; batch < kHistoryBatches; ++batch) {
    EXPECT_THAT(matcher.Match(Entries(16)), SizeIs(16));
  }
  EXPECT_THAT(matcher.Match(Entries(32)), SizeIs(32));
  EXPECT_THAT(executor.worker_count(), Eq(0));
}

TEST_F(ParallelMatchTest, SlowRemainingWorkUsesCallerAndReusesBoundedWorkers) {
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-true"}));
  constexpr std::size_t kEntries = 64;
  EvaluationTrace trace(kEntries);
  const ExpressionExecution execution(std::make_unique<TracedPlan>(trace));
  RunExecutor executor(2);
  ParallelMatch matcher(*command.expression, 3, executor, false, std::nullopt, execution);
  constexpr std::size_t kBatches = 2;
  for (std::size_t batch = 0; batch < kBatches; ++batch) {
    const auto& results = matcher.Match(Entries(kEntries));
    ASSERT_THAT(results, SizeIs(kEntries));
    for (std::size_t index = 0; index < kEntries; ++index) {
      EXPECT_THAT(results.at(index).evaluation.matched, Eq(index % 2 == 0));
    }
    EXPECT_THAT(executor.worker_count(), Eq(2));
    const auto observed = trace.Read();
    EXPECT_THAT(observed.counts, Each(Eq(batch + 1)));
    EXPECT_THAT(observed.contributors, SizeIs(3));
    EXPECT_THAT(observed.contributors, Contains(std::this_thread::get_id()));
  }
}

TEST_F(ParallelMatchTest, StatefulMetadataAndActionExpressionsCannotEnterWorkers) {
  const std::vector<std::vector<std::string>> expressions{
      {"root", "-first", "1"},
      {"root", "-size", "1c"},
      {"root", "-delete"},
      {"root", "-exec", "echo", "{}", ";"},
      {"root", "-type", "f", "-print"},
      {"root", "-true", "-o", "-quit"},
  };
  for (const auto& arguments : expressions) {
    MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse(arguments));
    EXPECT_THAT(CanParallelMatch(*command.expression), IsFalse());
  }
  MBO_ASSERT_OK_AND_ASSIGN(const auto content, parser::Parse({"root", "-type", "f", "-content", "needle"}));
  EXPECT_THAT(CanParallelMatch(*content.expression), IsTrue());
  EXPECT_THAT(HasContentMatch(*content.expression), IsTrue());
}

}  // namespace
}  // namespace xff::engine
