// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_RUN_EXECUTOR_H_
#define XFF_ENGINE_RUN_EXECUTOR_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"

namespace xff::engine {

namespace executor_detail {
template<typename Result>
struct TaskState final {
  using Stored = std::conditional_t<std::is_void_v<Result>, std::monostate, Result>;
  std::optional<Stored> result;
  absl::Notification completed;
};
}  // namespace executor_detail

template<typename Result>
class RunPromise;

// A single-consumer result. Wait, Ready and Get require a valid task; Get consumes
// the value once. Completion publishes through an Abseil notification; no worker
// may touch the value after signaling completion.
template<typename Result>
class RunTask final {
 public:
  RunTask() = default;
  RunTask(RunTask&&) = default;
  RunTask& operator=(RunTask&&) = default;
  RunTask(const RunTask&) = delete;
  RunTask& operator=(const RunTask&) = delete;

  bool Valid() const { return state_ != nullptr; }

  bool Ready() const { return state_->completed.HasBeenNotified(); }

  void Wait() const { state_->completed.WaitForNotification(); }

  Result Get() {
    Wait();
    if constexpr (!std::is_void_v<Result>) {
      return std::move(*state_->result);
    }
  }

 private:
  friend class RunPromise<Result>;

  explicit RunTask(std::shared_ptr<executor_detail::TaskState<Result>> state) : state_(std::move(state)) {}

  std::shared_ptr<executor_detail::TaskState<Result>> state_;
};

// The producer side of a RunTask, used when a drain job produces indexed results.
// Obtain one Task and call SetValue exactly once before releasing the producer.
template<typename Result>
class RunPromise final {
 public:
  using Stored = typename executor_detail::TaskState<Result>::Stored;

  RunTask<Result> Task() const { return RunTask<Result>(state_); }

  void SetValue(Stored result)
  requires(!std::is_void_v<Result>)
  {
    state_->result.emplace(std::move(result));
    state_->completed.Notify();
  }

  void SetValue()
  requires(std::is_void_v<Result>)
  {
    state_->completed.Notify();
  }

 private:
  std::shared_ptr<executor_detail::TaskState<Result>> state_ = std::make_shared<executor_detail::TaskState<Result>>();
};

// One lazily started worker executor shared by all parallel phases of a run.
// Start grows the pool but never shrinks it; destruction joins the workers once.
// Start, Submit and worker_count are called only by the coordinator. Jobs stay
// leaves and must not submit or wait on work in this executor.
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
    RunPromise<Result> promise;
    auto result = promise.Task();
    absl::AnyInvocable<void()> task = [promise = std::move(promise), job = std::move(job)]() mutable {
      if constexpr (std::is_void_v<Result>) {
        job();
        promise.SetValue();
      } else {
        promise.SetValue(job());
      }
    };
    if (threads_.empty()) {
      task();
      return result;
    }
    const absl::MutexLock lock(mutex_);
    queue_.emplace(std::move(task));
    return result;
  }

  std::size_t worker_count() const { return threads_.size(); }

 private:
  bool Pending() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void Run() ABSL_LOCKS_EXCLUDED(mutex_);

  const std::size_t max_workers_;
  mutable absl::Mutex mutex_;
  std::queue<absl::AnyInvocable<void()>> queue_ ABSL_GUARDED_BY(mutex_);
  bool stop_ ABSL_GUARDED_BY(mutex_) = false;
  std::vector<std::thread> threads_;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_RUN_EXECUTOR_H_
