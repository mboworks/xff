// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/parallel_match.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <string_view>
#include <utility>

#include "absl/time/time.h"
#include "xff/registry/descriptor.h"

namespace xff::engine {
namespace {

inline constexpr std::size_t kProbeChunks = 4;
inline constexpr std::size_t kProbeChunkEntries = 4;
inline constexpr std::size_t kMinRemainingEntries = 2;

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
    mbo::types::OptionalRef<const ExpressionExecution> execution,
    MatchWorkCosts costs)
    : expression_(expression),
      workers_(std::max(workers, std::size_t{1})),
      executor_(executor),
      scores_(scores),
      output_(std::move(output)),
      execution_(execution),
      costs_(costs) {}

ParallelMatch::~ParallelMatch() = default;

const std::vector<ParallelResult>& ParallelMatch::Match(std::vector<CollectedEntry> entries) {
  entries_ = std::move(entries);
  results_.clear();
  results_.resize(entries_.size());
  next_.store(0, std::memory_order_relaxed);
  grain_ = 4;
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
  if (workers_ == 1 || entries_.size() < kProbeChunks + kMinRemainingEntries) {
    ensure_coordinator();
    evaluate_coordinator();
    return results_;
  }
  ensure_coordinator();
  // Observe this batch, even with an already warm executor. Discard the fastest
  // and slowest of four timed chunks so one cold/outlier entry does not by itself
  // justify dispatching a cheap tail. Every sampled result is retained exactly once.
  std::array<std::chrono::nanoseconds, kProbeChunks> timings{};
  const std::size_t chunk_entries =
      std::min(kProbeChunkEntries, (entries_.size() - kMinRemainingEntries) / kProbeChunks);
  const std::size_t probe_entries = kProbeChunks * chunk_entries;
  for (std::size_t chunk = 0; chunk < kProbeChunks; ++chunk) {
    const auto started = std::chrono::steady_clock::now();
    const std::size_t first = chunk * chunk_entries;
    for (std::size_t index = first; index < first + chunk_entries; ++index) {
      results_.at(index) = EvaluateEntry(
          entries_.at(index).AsVisit(), {}, {},
          coordinator_ ? mbo::types::OptionalRef<const ExpressionExecution::Worker>{*coordinator_}
                       : mbo::types::OptionalRef<const ExpressionExecution::Worker>{});
    }
    timings.at(chunk) =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started);
  }
  next_.store(probe_entries, std::memory_order_relaxed);
  const auto plan = PlanMatchWork(
      entries_.size() - probe_entries, workers_, executor_.worker_count(), EstimateMatchWork(timings, chunk_entries),
      costs_);
  if (plan.participants == 1) {
    evaluate_coordinator();
    return results_;
  }
  grain_ = plan.grain;
  executor_.Start(plan.participants - 1);
  const std::size_t count = std::min(plan.participants - 1, executor_.worker_count());
  while (worker_states_.size() < count) {
    worker_states_.push_back(std::make_unique<WorkerState>(*this));
  }
  std::vector<RunTask<void>> tasks;
  tasks.reserve(count);
  for (std::size_t worker = 0; worker < count; ++worker) {
    tasks.push_back(executor_.Submit([this, worker] { EvaluateWorker(worker); }));
  }
  // The caller is part of the allowance, not an extra idle coordinator. Its private
  // evaluator competes for the same independent chunks without touching worker state.
  ensure_coordinator();
  evaluate_coordinator();
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
  // Planned chunks amortize atomic scheduling while retaining multiple balancing waves.
  for (;;) {
    const std::size_t first = next_.fetch_add(grain_, std::memory_order_relaxed);
    if (first >= entries_.size()) {
      return;
    }
    const std::size_t end = std::min(first + grain_, entries_.size());
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
