// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_RUN_EXECUTOR_H_
#define XFF_ENGINE_RUN_EXECUTOR_H_

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace xff::engine {

// One lazily started worker executor shared by all parallel phases of a run.
// Start grows the pool but never shrinks it; destruction joins the workers once.
class RunExecutor final {
 public:
  explicit RunExecutor(std::size_t max_workers) : max_workers_(max_workers) {}

  ~RunExecutor();

  RunExecutor(const RunExecutor&) = delete;
  RunExecutor& operator=(const RunExecutor&) = delete;
  RunExecutor(RunExecutor&&) = delete;
  RunExecutor& operator=(RunExecutor&&) = delete;

  void Start(std::size_t useful_workers);

  template<typename Job>
  auto Submit(Job job) ABSL_LOCKS_EXCLUDED(mutex_) {
    using Result = std::invoke_result_t<Job>;
    const auto task = std::make_shared<std::packaged_task<Result()>>(std::move(job));
    std::future<Result> future = task->get_future();
    if (threads_.empty()) {
      (*task)();
      return future;
    }
    const absl::MutexLock lock(mutex_);
    queue_.emplace([task] { (*task)(); });
    return future;
  }

  std::size_t worker_count() const { return threads_.size(); }

 private:
  bool Pending() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void Run() ABSL_LOCKS_EXCLUDED(mutex_);

  const std::size_t max_workers_;
  mutable absl::Mutex mutex_;
  std::queue<std::function<void()>> queue_ ABSL_GUARDED_BY(mutex_);
  bool stop_ ABSL_GUARDED_BY(mutex_) = false;
  std::vector<std::thread> threads_;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_RUN_EXECUTOR_H_
