// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_PARALLEL_MATCH_H_
#define XFF_ENGINE_PARALLEL_MATCH_H_

#include <atomic>
#include <cstddef>
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
#include "xff/engine/collect.h"
#include "xff/engine/evaluate.h"
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
      bool scores,
      std::optional<ParallelContentOutput> output = std::nullopt);
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
  void Run();
  bool Ready(std::size_t generation) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool Finished() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void EvaluateEntries(mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output = {});
  ParallelResult EvaluateEntry(const Visit& visit, mbo::types::OptionalRef<const absl::StatusOr<MatchOutput>> output)
      const;

  const mbo::types::OptionalRef<const parser::Expr> expression_;
  const std::size_t workers_;
  const bool scores_;
  const std::optional<ParallelContentOutput> output_;
  std::vector<std::thread> threads_;
  absl::Mutex mutex_;
  bool stop_ ABSL_GUARDED_BY(mutex_) = false;
  std::size_t generation_ ABSL_GUARDED_BY(mutex_) = 0;
  std::size_t remaining_ ABSL_GUARDED_BY(mutex_) = 0;
  // Published under mutex_, immutable until all workers have completed the batch.
  std::vector<CollectedEntry> entries_;
  std::vector<ParallelResult> results_;
  std::atomic<std::size_t> next_ = 0;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_PARALLEL_MATCH_H_
