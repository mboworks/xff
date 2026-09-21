// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_PARALLEL_MATCH_H_
#define XFF_ENGINE_PARALLEL_MATCH_H_

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "xff/engine/collect.h"
#include "xff/engine/evaluate.h"
#include "xff/parser/ast.h"

namespace xff::engine {

// Bounded batches of independent, audited predicates. Workers have no mutation or output
// sinks, captures, result-set state, or process execution. The caller emits results in input
// order after a batch completes. Construct only for an expression accepted by CanParallelMatch.
bool CanParallelMatch(const parser::Expr& expression);
bool HasContentMatch(const parser::Expr& expression);

class ParallelMatch final {
 public:
  ParallelMatch(const parser::Expr& expression, std::size_t workers, bool scores);
  ~ParallelMatch();
  ParallelMatch(const ParallelMatch&) = delete;
  ParallelMatch& operator=(const ParallelMatch&) = delete;
  ParallelMatch(ParallelMatch&&) = delete;
  ParallelMatch& operator=(ParallelMatch&&) = delete;

  // Consumes a batch; entry filesystem observers must outlive this call. The
  // owning entries remain available for ordered output until the next call.
  const std::vector<EvaluationResult>& Match(std::vector<CollectedEntry> entries);

  const std::vector<CollectedEntry>& Entries() const { return entries_; }

 private:
  void Run();
  bool Ready(std::size_t generation) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool Finished() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void EvaluateEntries();

  const parser::Expr& expression_;
  const std::size_t workers_;
  const bool scores_;
  std::vector<std::thread> threads_;
  absl::Mutex mutex_;
  bool stop_ ABSL_GUARDED_BY(mutex_) = false;
  std::size_t generation_ ABSL_GUARDED_BY(mutex_) = 0;
  std::size_t remaining_ ABSL_GUARDED_BY(mutex_) = 0;
  // Published under mutex_, immutable until all workers have completed the batch.
  std::vector<CollectedEntry> entries_;
  std::vector<EvaluationResult> results_;
  std::atomic<std::size_t> next_ = 0;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_PARALLEL_MATCH_H_
