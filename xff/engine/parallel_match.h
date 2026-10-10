// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_PARALLEL_MATCH_H_
#define XFF_ENGINE_PARALLEL_MATCH_H_

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "mbo/types/optional_ref.h"
#include "xff/engine/collect.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/expression_execution.h"
#include "xff/engine/match_work.h"
#include "xff/engine/run_executor.h"
#include "xff/parser/ast.h"

namespace xff::engine {

// Workers evaluate only audited predicates and prepared content searches. They have no host
// output/mutation sinks, captures, or result-set state. The caller emits buffered records in
// entry order after the batch completes. Source bytes die with each worker's evaluator.
struct ParallelContentOutput {
  const MatchOutput& output;
  mbo::types::OptionalRef<const parser::RgSearch> rg;
  GrepOptions grep;
  bool json = false;
  bool automatic_filename = false;
  std::size_t root_count = 0;
  std::size_t before = 0;
  std::size_t after = 0;
};

struct ContentResult {
  absl::StatusOr<bool> selected = true;
  std::string text;
};

struct ParallelResult {
  EvaluationResult evaluation;
  std::optional<ContentResult> content;
};

bool CanParallelMatch(const parser::Expr& expression);
bool HasContentMatch(const parser::Expr& expression);

class ParallelMatch final {
 public:
  ParallelMatch(
      mbo::types::OptionalRef<const parser::Expr> expression,
      std::size_t workers,
      RunExecutor& executor,
      bool scores,
      std::optional<ParallelContentOutput> output = std::nullopt,
      mbo::types::OptionalRef<const ExpressionExecution> execution = {},
      MatchWorkCosts costs = {});
  ~ParallelMatch();
  ParallelMatch(const ParallelMatch&) = delete;
  ParallelMatch& operator=(const ParallelMatch&) = delete;
  ParallelMatch(ParallelMatch&&) = delete;
  ParallelMatch& operator=(ParallelMatch&&) = delete;

  // Consumes a batch; entry filesystem observers must outlive this call. The
  // owning entries remain available for ordered output until the next call.
  const std::vector<ParallelResult>& Match(std::vector<CollectedEntry> entries);

  const std::vector<CollectedEntry>& Entries() const { return entries_; }

 private:
  struct WorkerState;
  void EvaluateWorker(std::size_t worker);
  void EvaluateEntries(
      mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output = {},
      mbo::types::OptionalRef<const WorkerMatchers> matchers = {},
      mbo::types::OptionalRef<const ExpressionExecution::Worker> evaluator = {});
  ParallelResult EvaluateEntry(
      const Visit& visit,
      mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output,
      mbo::types::OptionalRef<const WorkerMatchers> matchers,
      mbo::types::OptionalRef<const ExpressionExecution::Worker> evaluator) const;

  const mbo::types::OptionalRef<const parser::Expr> expression_;
  const std::size_t workers_;
  RunExecutor& executor_;
  const bool scores_;
  const std::optional<ParallelContentOutput> output_;
  const mbo::types::OptionalRef<const ExpressionExecution> execution_;
  const MatchWorkCosts costs_;
  std::optional<ExpressionExecution::Worker> coordinator_;
  std::vector<std::unique_ptr<WorkerState>> worker_states_;
  std::vector<CollectedEntry> entries_;
  std::vector<ParallelResult> results_;
  std::atomic<std::size_t> next_ = 0;
  // Published before dispatch and unchanged until all jobs join.
  std::size_t grain_ = 4;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_PARALLEL_MATCH_H_
