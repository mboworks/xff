// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/parallel_match.h"

#include <algorithm>
#include <limits>
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
    mbo::types::OptionalRef<const ExpressionExecution> execution,
    content::Derivation derivation)
    : expression_(expression),
      workers_(std::max(workers, std::size_t{1})),
      scores_(scores),
      output_(std::move(output)),
      execution_(execution),
      derivation_(std::move(derivation)),
      output_credit_(mutex_) {}

ParallelMatch::~ParallelMatch() {
  Cancel();
  {
    const absl::MutexLock lock(mutex_);
    stop_ = true;
  }
  for (auto& thread : threads_) {
    thread.join();
  }
}

ContentSnapshot ParallelMatch::TakeDerived(std::size_t index) {
  return derivation_.Empty() ? ContentSnapshot{} : std::move(derived_.at(index));
}

bool ParallelMatch::Ready(std::size_t generation, std::size_t worker) const {
  return stop_
         || (generation_ != generation && worker < active_workers_
             && (!coordinator_collecting_ || worker + 1 < active_workers_));
}

bool ParallelMatch::Finished() const {
  return remaining_ == 0;
}

const std::vector<ParallelResult>& ParallelMatch::Match(std::vector<OwnedVisit> entries) {
  Start(std::move(entries));
  return Wait();
}

void ParallelMatch::Cancel() {
  cancelled_.store(true, std::memory_order_relaxed);
  output_credit_.Cancel();
  const absl::MutexLock lock(mutex_);
  coordinator_collecting_ = false;
  mutex_.Await(absl::Condition(this, &ParallelMatch::Finished));
  if (credited_output_ && !coordinator_entries_) {
    // Skipped entries have no producer to publish readiness. Wake an ordered
    // consumer only after all active reads and result writes have drained.
    for (auto& completion : ready_) {
      for (auto& state : completion.states) {
        state.store(kReady, std::memory_order_release);
      }
    }
  }
}

const std::vector<ParallelResult>& ParallelMatch::Wait() {
  const absl::MutexLock lock(mutex_);
  coordinator_collecting_ = false;
  mutex_.Await(absl::Condition(this, &ParallelMatch::Finished));
  return results_;
}

void ParallelMatch::PrepareBatch(std::vector<OwnedVisit> entries) {
  cancelled_.store(false, std::memory_order_relaxed);
  entries_ = std::move(entries);
  results_.resize(entries_.size());
  for (auto& slot : output_slots_) {
    slot.Release();
  }
  if (coordinator_output_) {
    coordinator_output_->Release();
  }
  credited_output_ = false;
  observed_output_bytes_ = 0;
  coordinator_entries_ = false;
  if (!derivation_.Empty()) {
    derived_.clear();
    derived_.resize(entries_.size());
  }
  if (output_ && !results_.empty()) {
    const auto limit = std::min(kReusableRecordBytes, kReusableOutputBytes / results_.size());
    // Release oversized old records before evaluating any new entry. Current output can still
    // exceed this bound; only capacities retained from the consumed batch are restricted.
    for (auto& result : results_) {
      if (result.content && result.content->text.capacity() > limit) {
        result.content.reset();
      }
    }
  }
  next_.store(0, std::memory_order_relaxed);
}

void ParallelMatch::Start(std::vector<OwnedVisit> entries, bool pipeline) {
  const absl::MutexLock lock(mutex_, absl::Condition(this, &ParallelMatch::Finished));
  PrepareBatch(std::move(entries));
  // Very small batches cannot amortize waking the pool.
  if (workers_ == 1 || entries_.size() < 16 || (threads_.empty() && entries_.size() < 64)) {
    if (execution_ && !coordinator_) {
      coordinator_.emplace(execution_->MakeWorker(ExpressionWorkerRole::kCoordinator));
    }
    if (pipeline && output_) {
      // There is no worker to unblock a serial renderer. Defer each transaction
      // until consumption, then release it before evaluating the next entry.
      output_credit_.Reset();
      credited_output_ = true;
      coordinator_entries_ = true;
      return;
    }
    EvaluateEntries(
        coordinator_fuzzy_workspace_, {}, {},
        coordinator_ ? mbo::types::OptionalRef<const ExpressionExecution::Worker>{*coordinator_}
                     : mbo::types::OptionalRef<const ExpressionExecution::Worker>{});
    return;
  }
  output_credit_.Reset();
  credited_output_ = pipeline && output_.has_value();
  if (credited_output_) {
    ready_.resize((entries_.size() + kChunkSize - 1) / kChunkSize);
    output_slots_.reserve(std::max(output_slots_.size(), entries_.size()));
    for (std::size_t index = 0; index < entries_.size(); ++index) {
      CompletionState(index).store(kPending, std::memory_order_relaxed);
      if (index == output_slots_.size()) {
        output_slots_.emplace_back(output_credit_, index);
      }
    }
  }
  const std::size_t count = std::min(workers_, (entries_.size() + 15) / 16);
  // An early partial batch must not permanently cap the pool for later full batches.
  if (threads_.size() < count) {
    threads_.reserve(count);
    for (std::size_t index = threads_.size(); index < count; ++index) {
      threads_.emplace_back([this, index] { Run(index); });
    }
  }
  // Retain the creation policy and all useful parallelism. Once a pool has grown, only
  // workers beyond the number of available chunks are redundant for a later short tail.
  active_workers_ = std::min(threads_.size(), (entries_.size() + kChunkSize - 1) / kChunkSize);
  remaining_ = active_workers_;
  coordinator_collecting_ = pipeline && CollectNextBatch() && active_workers_ > 1;
  ++generation_;
}

const ParallelResult& ParallelMatch::AwaitResult(std::size_t index) {
  if (index == 0) {
    const absl::MutexLock lock(mutex_);
    coordinator_collecting_ = false;
  }
  if (coordinator_entries_) {
    if (!cancelled_.load(std::memory_order_acquire)) {
      EvaluateEntry(
          index, coordinator_fuzzy_workspace_, {}, {},
          coordinator_ ? mbo::types::OptionalRef<const ExpressionExecution::Worker>{*coordinator_}
                       : mbo::types::OptionalRef<const ExpressionExecution::Worker>{});
    }
  } else if (credited_output_) {
    const auto await = [&](std::size_t target, bool allow_backpressure) {
      auto& state = CompletionState(target);
      if (state.load(std::memory_order_acquire) != kReady) {
        auto expected = kPending;
        // A publication window may already have registered this same target.
        static_cast<void>(state.compare_exchange_strong(expected, kWaiting, std::memory_order_acq_rel));
        const auto completed = [&] {
          return state.load(std::memory_order_acquire) == kReady || (allow_backpressure && output_credit_.Blocked());
        };
        const absl::MutexLock lock(mutex_, absl::Condition(&completed));
      }
    };
    // Amortize wakeups for ordinary short records. If an arbitrary large later
    // record needs credit, bypass the window and drain the current head instead.
    static constexpr std::size_t kPublicationWindow = 64;
    const auto window = CollectNextBatch() ? kPublicationWindow : entries_.size();
    if (index % window == 0) {
      await(std::min(index + window, entries_.size()) - 1, true);
    }
    await(index, false);
  } else if (index == 0) {
    Wait();
  }
  if (cancelled_.load(std::memory_order_acquire)) {
    static const ParallelResult kCancelled{.evaluation = {.matched = false}};
    return kCancelled;
  }
  return results_.at(index);
}

void ParallelMatch::ReleaseResult(std::size_t index) {
  if (credited_output_) {
    if (auto& content = results_.at(index).content; content) {
      observed_output_bytes_ +=
          std::min(content->Text().size(), std::numeric_limits<std::size_t>::max() - observed_output_bytes_);
      content->credited_text.reset();
      content->selected = true;
    }
    if (coordinator_entries_) {
      if (coordinator_output_) {
        coordinator_output_->Release();
      }
    } else {
      output_slots_.at(index).Release();
    }
    output_credit_.Advance(index + 1);
    if (index + 1 == entries_.size()) {
      // A 256-entry batch with at least 1 KiB of output per visited entry has
      // enough publication work to try overlap. This threshold is experimental;
      // it does not inspect file metadata or introduce a clock into the hot path.
      static constexpr std::size_t kOutputBytesPerEntry = 1'024;
      overlap_useful_ = observed_output_bytes_ / entries_.size() >= kOutputBytesPerEntry;
    }
  }
}

void ParallelMatch::Run(std::size_t worker) {
  const auto local = output_ ? std::optional(ForkMatchOutput(output_->output)) : std::nullopt;
  WorkerMatchers matchers;
  if (expression_ && (!execution_ || !execution_->UsesIndexedMatchers())) {
    matchers.Bind(*expression_);
  }
  const auto evaluator = execution_ ? std::optional(execution_->MakeWorker()) : std::nullopt;
  fuzzy::ScoreWorkspace fuzzy_workspace;
  std::size_t generation = 0;
  for (;;) {
    {
      const auto ready = [&] ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) { return Ready(generation, worker); };
      const absl::MutexLock lock(mutex_, absl::Condition(&ready));
      if (stop_) {
        return;
      }
      generation = generation_;
    }
    EvaluateEntries(
        fuzzy_workspace,
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
    fuzzy::ScoreWorkspace& workspace,
    mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output,
    mbo::types::OptionalRef<const WorkerMatchers> matchers,
    mbo::types::OptionalRef<const ExpressionExecution::Worker> evaluator) {
  // Small chunks spread clustered expensive entries while amortizing atomic scheduling.
  for (;;) {
    const std::size_t first = next_.fetch_add(kChunkSize, std::memory_order_relaxed);
    if (first >= entries_.size() || cancelled_.load(std::memory_order_relaxed)) {
      return;
    }
    const std::size_t end = std::min(first + kChunkSize, entries_.size());
    for (std::size_t index = first; index < end; ++index) {
      EvaluateEntry(index, workspace, output, matchers, evaluator);
      if (credited_output_) {
        auto& state = CompletionState(index);
        if (state.exchange(kReady, std::memory_order_release) == kWaiting) {
          // Acquiring/releasing the condition's mutex prevents a lost wake if the
          // coordinator is between registering its wait and actually parking.
          const absl::MutexLock lock(mutex_);
        }
      }
    }
  }
}

void ParallelMatch::RenderEntryOutput(
    std::size_t index,
    const ParallelContentOutput& specification,
    EvalContext& context,
    mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output) {
  auto& result = results_.at(index);
  auto& content = result.content ? *result.content : result.content.emplace();
  content.selected = true;
  // Forking is an optimization: retain the already validated shared matcher if it fails.
  const auto& prepared = output && output->ok() ? **output : specification.output;
  const auto sink = context.emit;
  const auto buffer = [&](std::string_view text) { content.text.append(text); };
  context.emit = buffer;
  context.grep = specification.grep;
  context.grep_json = specification.json;
  context.grep_before = specification.before;
  context.grep_after = specification.after;
  if (specification.automatic_filename) {
    context.grep.filename = specification.root_count != 1 || context.visit.depth != 0;
  }
  if (credited_output_) {
    auto& records = coordinator_entries_ ? (coordinator_output_ ? *coordinator_output_
                                                                : coordinator_output_.emplace(output_credit_, index))
                                         : output_slots_.at(index);
    if (coordinator_entries_) {
      records.Begin(index);
    }
    if (specification.rg) {
      content.selected = RenderRgOutput(prepared, *specification.rg, context, records);
    } else {
      static_cast<void>(RenderMatchOutput(prepared, context, records));
    }
    content.credited_text = records.View();
  } else if (specification.rg) {
    auto rendered = RenderRgOutput(prepared, *specification.rg, context, std::move(content.text));
    if (rendered.ok()) {
      content.selected = rendered->selected;
      content.text = std::move(rendered->text);
    } else {
      content.selected = rendered.status();
      content.text.clear();
    }
  } else {
    EmitMatchOutput(prepared, context);
  }
  context.emit = sink;
}

void ParallelMatch::EvaluateEntry(
    std::size_t index,
    fuzzy::ScoreWorkspace& workspace,
    mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output,
    mbo::types::OptionalRef<const WorkerMatchers> matchers,
    mbo::types::OptionalRef<const ExpressionExecution::Worker> evaluator) {
  const Visit visit = entries_.at(index).AsVisit();
  auto& result = results_.at(index);
  if (result.content) {
    result.content->text.clear();
    result.content->credited_text.reset();
  }
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
      .fuzzy_workspace = workspace,
      .worker_matchers = matchers,
      .control = control,
  };
  result.evaluation = evaluator     ? evaluator->Evaluate(context)
                      : expression_ ? EvaluateDeferred(*expression_, context)
                                    : EvaluationResult{.matched = true};
  if (result.evaluation.matched && output_) {
    RenderEntryOutput(index, *output_, context, output);
  } else {
    result.content.reset();
  }
  if (!derivation_.Empty() && result.evaluation.matched
      && (!result.content || (result.content->selected.ok() && *result.content->selected))) {
    derived_.at(index) = std::move(context.content).TakeDerived(derivation_, *visit.fs, visit.path);
  }
}

}  // namespace xff::engine
