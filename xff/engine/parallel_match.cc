// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/parallel_match.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string_view>
#include <utility>

#include "absl/time/time.h"
#include "xff/registry/descriptor.h"

namespace xff::engine {
namespace {

// A cold executor has a fixed creation and teardown cost. Accumulate enough matcher work to
// repay it; once another run phase has started the executor, ordinary per-batch admission applies.
inline constexpr std::size_t kMinColdParallelEntries = 8'192;
inline constexpr std::size_t kColdProbeEntries = 16;
inline constexpr auto kSlowColdProbe = std::chrono::microseconds(500);

}  // namespace

struct ParallelMatch::WorkerState {
  explicit WorkerState(const ParallelMatch& owner)
      : output(owner.output_ ? std::optional(ForkMatchOutput(owner.output_->output)) : std::nullopt),
        evaluator(owner.execution_ ? std::optional(owner.execution_->MakeWorker()) : std::nullopt) {
    if (owner.expression_ && (!owner.execution_ || !owner.execution_->UsesIndexedMatchers())) {
      matchers.Bind(*owner.expression_);
    }
  }

  std::optional<absl::StatusOr<MatchOutput>> output;
  WorkerMatchers matchers;
  std::optional<ExpressionExecution::Worker> evaluator;
};

bool CanParallelMatch(const parser::Expr& expression) {
  if (expression.kind == parser::Expr::Kind::kPredicate) {
    const auto& descriptor = *expression.descriptor;
    return descriptor.parallel_match && !descriptor.needs_metadata && descriptor.pure
           && descriptor.kind == registry::Kind::kTest && descriptor.safety == registry::Safety::kNone
           && descriptor.control == registry::Control::kNone;
  }
  return (!expression.lhs || CanParallelMatch(*expression.lhs))
         && (!expression.rhs || CanParallelMatch(*expression.rhs));
}

bool HasContentMatch(const parser::Expr& expression) {
  if (expression.kind == parser::Expr::Kind::kPredicate) {
    return expression.descriptor->cost == registry::Cost::kExpensive;
  }
  return (expression.lhs && HasContentMatch(*expression.lhs)) || (expression.rhs && HasContentMatch(*expression.rhs));
}

ParallelMatch::ParallelMatch(
    mbo::types::OptionalRef<const parser::Expr> expression,
    std::size_t workers,
    RunExecutor& executor,
    bool scores,
    std::optional<ParallelContentOutput> output,
    mbo::types::OptionalRef<const ExpressionExecution> execution)
    : expression_(expression),
      workers_(std::max(workers, std::size_t{1})),
      executor_(executor),
      scores_(scores),
      output_(std::move(output)),
      execution_(execution) {}

ParallelMatch::~ParallelMatch() = default;

const std::vector<ParallelResult>& ParallelMatch::Match(std::vector<CollectedEntry> entries) {
  entries_ = std::move(entries);
  results_.clear();
  results_.resize(entries_.size());
  next_.store(0, std::memory_order_relaxed);
  const bool cold = executor_.worker_count() == 0;
  const auto ensure_coordinator = [&] {
    if (execution_ && !coordinator_) {
      coordinator_.emplace(execution_->MakeWorker(ExpressionWorkerRole::kCoordinator));
    }
  };
  const auto evaluate_coordinator = [&] {
    EvaluateEntries(
        {}, {},
        coordinator_ ? mbo::types::OptionalRef<const ExpressionExecution::Worker>{*coordinator_}
                     : mbo::types::OptionalRef<const ExpressionExecution::Worker>{});
  };
  // Very small batches cannot amortize waking the executor.
  if (workers_ == 1 || entries_.size() < 16) {
    ensure_coordinator();
    evaluate_coordinator();
    serial_entries_ += entries_.size();
    return results_;
  }
  std::size_t parallel_entries = entries_.size();
  if (cold && serial_entries_ + entries_.size() < kMinColdParallelEntries) {
    // A count threshold protects fast local storage, but must not suppress useful concurrency on
    // slow filesystems. Measure a small prefix, then either parallelize the rest of this batch or
    // finish inline and retain the evidence for later bounded batches.
    ensure_coordinator();
    const std::size_t probe = std::min(kColdProbeEntries, entries_.size());
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < probe; ++index) {
      results_.at(index) = EvaluateEntry(
          entries_.at(index).AsVisit(), {}, {},
          coordinator_ ? mbo::types::OptionalRef<const ExpressionExecution::Worker>{*coordinator_}
                       : mbo::types::OptionalRef<const ExpressionExecution::Worker>{});
    }
    next_.store(probe, std::memory_order_relaxed);
    if (std::chrono::steady_clock::now() - started < kSlowColdProbe) {
      evaluate_coordinator();
      serial_entries_ += entries_.size();
      return results_;
    }
    parallel_entries -= probe;
  }
  const std::size_t count = std::min(workers_, (parallel_entries + 15) / 16);
  executor_.Start(count);
  while (worker_states_.size() < count) {
    worker_states_.push_back(std::make_unique<WorkerState>(*this));
  }
  std::vector<RunTask<void>> tasks;
  tasks.reserve(count);
  for (std::size_t worker = 0; worker < count; ++worker) {
    tasks.push_back(executor_.Submit([this, worker] { EvaluateWorker(worker); }));
  }
  for (RunTask<void>& task : tasks) {
    task.Get();
  }
  return results_;
}

void ParallelMatch::EvaluateWorker(std::size_t worker) {
  const WorkerState& state = *worker_states_.at(worker);
  EvaluateEntries(
      state.output ? mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>>{*state.output}
                   : mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>>{},
      state.matchers,
      state.evaluator ? mbo::types::OptionalRef<const ExpressionExecution::Worker>{*state.evaluator}
                      : mbo::types::OptionalRef<const ExpressionExecution::Worker>{});
}

void ParallelMatch::EvaluateEntries(
    mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output,
    mbo::types::OptionalRef<const WorkerMatchers> matchers,
    mbo::types::OptionalRef<const ExpressionExecution::Worker> evaluator) {
  // Small chunks spread clustered expensive entries while amortizing atomic scheduling.
  constexpr std::size_t kChunk = 4;
  for (;;) {
    const std::size_t first = next_.fetch_add(kChunk, std::memory_order_relaxed);
    if (first >= entries_.size()) {
      return;
    }
    const std::size_t end = std::min(first + kChunk, entries_.size());
    for (std::size_t index = first; index < end; ++index) {
      results_.at(index) = EvaluateEntry(entries_.at(index).AsVisit(), output, matchers, evaluator);
    }
  }
}

ParallelResult ParallelMatch::EvaluateEntry(
    const Visit& visit,
    mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output,
    mbo::types::OptionalRef<const WorkerMatchers> matchers,
    mbo::types::OptionalRef<const ExpressionExecution::Worker> evaluator) const {
  ParallelResult result;
  Control control;
  std::optional<int> fuzzy;
  const auto discard = [](std::string_view) {};
  EvalContext context{
      .visit = visit,
      .emit = discard,
      .fs = *visit.fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .fuzzy_score = scores_ ? mbo::types::OptionalRef{fuzzy} : mbo::types::OptionalRef<std::optional<int>>{},
      .worker_matchers = matchers,
      .control = control,
  };
  result.evaluation = evaluator     ? evaluator->Evaluate(context)
                      : expression_ ? EvaluateDeferred(*expression_, context)
                                    : EvaluationResult{.matched = true};
  if (result.evaluation.matched && output_) {
    auto& content = result.content.emplace();
    // Forking is an optimization: retain the already validated shared matcher if it fails.
    const auto& prepared = output && output->ok() ? **output : output_->output;
    const auto buffer = [&](std::string_view text) { content.text.append(text); };
    context.emit = buffer;
    context.grep = output_->grep;
    context.grep_json = output_->json;
    context.grep_before = output_->before;
    context.grep_after = output_->after;
    if (output_->automatic_filename) {
      context.grep.filename = output_->root_count != 1 || visit.depth != 0;
    }
    if (output_->rg) {
      content.selected = EmitRgOutput(prepared, *output_->rg, context);
    } else {
      EmitMatchOutput(prepared, context);
    }
  }
  return result;
}

}  // namespace xff::engine
