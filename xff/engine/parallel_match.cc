// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/parallel_match.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

#include "absl/time/time.h"
#include "xff/registry/descriptor.h"

namespace xff::engine {

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
    bool scores,
    std::optional<ParallelContentOutput> output,
    mbo::types::OptionalRef<const ExpressionExecution> execution)
    : expression_(expression),
      workers_(std::max(workers, std::size_t{1})),
      scores_(scores),
      output_(std::move(output)),
      execution_(execution) {}

ParallelMatch::~ParallelMatch() {
  {
    const absl::MutexLock lock(mutex_);
    stop_ = true;
  }
  for (auto& thread : threads_) {
    thread.join();
  }
}

bool ParallelMatch::Ready(std::size_t generation) const {
  return stop_ || generation_ != generation;
}

bool ParallelMatch::Finished() const {
  return remaining_ == 0;
}

const std::vector<ParallelResult>& ParallelMatch::Match(std::vector<CollectedEntry> entries) {
  const absl::MutexLock lock(mutex_);
  entries_ = std::move(entries);
  results_.clear();
  results_.resize(entries_.size());
  next_.store(0, std::memory_order_relaxed);
  // Very small batches cannot amortize waking the pool.
  if (workers_ == 1 || entries_.size() < 16 || (threads_.empty() && entries_.size() < 64)) {
    if (execution_ && !coordinator_) {
      coordinator_.emplace(execution_->MakeWorker());
    }
    EvaluateEntries(
        {}, {},
        coordinator_ ? mbo::types::OptionalRef<const ExpressionExecution::Worker>{*coordinator_}
                     : mbo::types::OptionalRef<const ExpressionExecution::Worker>{});
    return results_;
  }
  const std::size_t count = std::min(workers_, (entries_.size() + 15) / 16);
  // An early partial batch must not permanently cap the pool for later full batches.
  if (threads_.size() < count) {
    threads_.reserve(count);
    for (std::size_t index = threads_.size(); index < count; ++index) {
      threads_.emplace_back([this] { Run(); });
    }
  }
  remaining_ = threads_.size();
  ++generation_;
  mutex_.Await(absl::Condition(this, &ParallelMatch::Finished));
  return results_;
}

void ParallelMatch::Run() {
  const auto local = output_ ? std::optional(ForkMatchOutput(output_->output)) : std::nullopt;
  WorkerMatchers matchers;
  if (expression_ && (!execution_ || !execution_->UsesIndexedMatchers())) {
    matchers.Bind(*expression_);
  }
  const auto evaluator = execution_ ? std::optional(execution_->MakeWorker()) : std::nullopt;
  std::size_t generation = 0;
  for (;;) {
    {
      const auto ready = [&] ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) { return Ready(generation); };
      const absl::MutexLock lock(mutex_, absl::Condition(&ready));
      if (stop_) {
        return;
      }
      generation = generation_;
    }
    EvaluateEntries(
        local ? mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>>{*local}
              : mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>>{},
        matchers,
        evaluator ? mbo::types::OptionalRef<const ExpressionExecution::Worker>{*evaluator}
                  : mbo::types::OptionalRef<const ExpressionExecution::Worker>{});
    {
      const absl::MutexLock lock(mutex_);
      --remaining_;
    }
  }
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
