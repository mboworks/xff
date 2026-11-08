// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_PARALLEL_MATCH_H_
#define XFF_ENGINE_PARALLEL_MATCH_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "mbo/types/optional_ref.h"
#include "xff/content/snapshot.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/expression_execution.h"
#include "xff/engine/output_credit.h"
#include "xff/engine/owned_visit.h"
#include "xff/fuzzy/fuzzy.h"
#include "xff/parser/ast.h"

namespace xff::engine {

// Workers evaluate only audited predicates and prepared content searches. They have no host
// output/mutation sinks, captures, or result-set state. The caller emits buffered records in
// entry order after each entry completes. Source bytes die with each worker's evaluator.
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
  std::optional<std::string_view> credited_text;

  std::string_view Text() const { return credited_text.value_or(text); }
};

struct ParallelResult {
  EvaluationResult evaluation;
  std::optional<ContentResult> content;
};

bool CanParallelMatch(const parser::Expr& expression);
bool HasContentMatch(const parser::Expr& expression);

class ParallelMatch final {
 public:
  // Reuse limits apply between batches, not to the current output returned to the caller.
  static constexpr std::size_t kReusableOutputBytes = 1'048'576;
  static constexpr std::size_t kReusableRecordBytes = 65'536;

  ParallelMatch(
      mbo::types::OptionalRef<const parser::Expr> expression,
      std::size_t workers,
      bool scores,
      std::optional<ParallelContentOutput> output = std::nullopt,
      mbo::types::OptionalRef<const ExpressionExecution> execution = {},
      content::Derivation derivation = {});
  ~ParallelMatch();
  ParallelMatch(const ParallelMatch&) = delete;
  ParallelMatch& operator=(const ParallelMatch&) = delete;
  ParallelMatch(ParallelMatch&&) = delete;
  ParallelMatch& operator=(ParallelMatch&&) = delete;

  // Consumes a batch; entry filesystem observers must outlive this call. The
  // owning entries remain available for ordered output until the next call.
  const std::vector<ParallelResult>& Match(std::vector<OwnedVisit> entries);

  // Start one batch on the persistent workers. The coordinator can collect the next
  // batch until ordered consumption establishes the publication barrier. Credited small
  // batches evaluate one entry at a time on that coordinator; Match() remains synchronous.
  void Start(std::vector<OwnedVisit> entries, bool pipeline = false);
  const std::vector<ParallelResult>& Wait();
  // Stop claiming entries and drain every worker before releasing the batch's owners.
  // A subsequent Start() begins a fresh generation.
  void Cancel();
  // Each entry remains producer-owned until AwaitResult() returns. ReleaseResult()
  // ends the borrowed output view and advances the head before the next wait.
  const ParallelResult& AwaitResult(std::size_t index);
  void ReleaseResult(std::size_t index);

  OutputCredit::Usage OutputUsage() const { return output_credit_.GetUsage(); }

  // Observed output controls scheduling only; both policies retain the same byte
  // credit and whole-file publication rules. Unknown output starts without overlap.
  bool CollectNextBatch() const { return output_ && overlap_useful_; }

  const std::vector<OwnedVisit>& Entries() const { return entries_; }

  // Consume one ordered entry's derived caches. No snapshot storage exists without a request.
  ContentSnapshot TakeDerived(std::size_t index);

 private:
  static constexpr std::size_t kChunkSize = 4;
  void PrepareBatch(std::vector<OwnedVisit> entries) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void RenderEntryOutput(
      std::size_t index,
      const ParallelContentOutput& specification,
      EvalContext& context,
      mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output);
  void Run(std::size_t worker);
  bool Ready(std::size_t generation, std::size_t worker) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool Finished() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void EvaluateEntries(
      fuzzy::ScoreWorkspace& workspace,
      mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output = {},
      mbo::types::OptionalRef<const WorkerMatchers> matchers = {},
      mbo::types::OptionalRef<const ExpressionExecution::Worker> evaluator = {});
  // Each worker exclusively owns its claimed slot until publishing its completion.
  void EvaluateEntry(
      std::size_t index,
      fuzzy::ScoreWorkspace& workspace,
      mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output,
      mbo::types::OptionalRef<const WorkerMatchers> matchers,
      mbo::types::OptionalRef<const ExpressionExecution::Worker> evaluator);

  const mbo::types::OptionalRef<const parser::Expr> expression_;
  const std::size_t workers_;
  const bool scores_;
  const std::optional<ParallelContentOutput> output_;
  const mbo::types::OptionalRef<const ExpressionExecution> execution_;
  const content::Derivation derivation_;
  std::optional<ExpressionExecution::Worker> coordinator_;
  fuzzy::ScoreWorkspace coordinator_fuzzy_workspace_;
  std::vector<std::thread> threads_;
  absl::Mutex mutex_;
  bool stop_ ABSL_GUARDED_BY(mutex_) = false;
  std::size_t generation_ ABSL_GUARDED_BY(mutex_) = 0;
  // Fixed for the generation; completion must not change eligibility for delayed participants.
  std::size_t active_workers_ ABSL_GUARDED_BY(mutex_) = 0;
  std::size_t remaining_ ABSL_GUARDED_BY(mutex_) = 0;
  // Reserve the final matcher participant for the coordinator's collection phase.
  // Publication/cancellation releases it; CPU-heavy matching can then use the full pool.
  bool coordinator_collecting_ ABSL_GUARDED_BY(mutex_) = false;
  // Published under mutex_, immutable until all workers have completed the batch.
  std::vector<OwnedVisit> entries_;
  std::vector<ParallelResult> results_;
  std::vector<ContentSnapshot> derived_;
  std::atomic<std::size_t> next_ = 0;
  std::atomic<bool> cancelled_ = false;
  OutputCredit output_credit_;
  std::vector<CreditedOutput> output_slots_;
  std::optional<CreditedOutput> coordinator_output_;
  bool coordinator_entries_ = false;
  static constexpr unsigned char kPending = 0;
  static constexpr unsigned char kWaiting = 1;
  static constexpr unsigned char kReady = 2;

  // A claimed chunk has exactly one producer. Keep different chunks on separate
  // cache lines while avoiding a whole cache line per entry.
  struct alignas(128) Completion {
    std::array<std::atomic<unsigned char>, kChunkSize> states;
  };

  std::atomic<unsigned char>& CompletionState(std::size_t index) {
    return ready_.at(index / kChunkSize).states.at(index % kChunkSize);
  }

  std::deque<Completion> ready_;
  bool credited_output_ = false;
  // Coordinator-owned observations; workers never read or mutate these counters.
  std::size_t observed_output_bytes_ = 0;
  bool overlap_useful_ = false;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_PARALLEL_MATCH_H_
