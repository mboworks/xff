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
struct ReadAheadBudget;

template<typename Result>
struct TaskState final {
  using Stored = std::conditional_t<std::is_void_v<Result>, std::monostate, Result>;
  std::optional<Stored> result;
  absl::Notification completed;
};
}  // namespace executor_detail

enum class RunTaskClass { kForeground, kReadAhead };

struct ReadAheadStats {
  // Unfinished reads. Completed results keep their byte charge, not an execution slot.
  std::size_t in_use = 0;
  // Pending/finished request bookkeeping plus retained speculative listing storage.
  std::size_t retained_bytes = 0;
  std::size_t peak_in_use = 0;
  std::size_t peak_retained_bytes = 0;
};

// An owned speculative-read reservation. Retain replaces its total byte charge,
// never below the initial bookkeeping charge; a failed charge must discard the
// additional storage instead of publishing it. Finish releases the unfinished-read
// slot without releasing bytes. Destruction releases any remaining slot and bytes.
// The reservation can outlive its executor, but only one owner may mutate it.
class ReadAheadReservation final {
 public:
  ~ReadAheadReservation();
  ReadAheadReservation(ReadAheadReservation&&) noexcept;
  ReadAheadReservation& operator=(ReadAheadReservation&&) noexcept;
  ReadAheadReservation(const ReadAheadReservation&) = delete;
  ReadAheadReservation& operator=(const ReadAheadReservation&) = delete;

  [[nodiscard]] bool Retain(std::size_t bytes);
  // Idempotent. Completed cached results must not occupy execution slots.
  void Finish();

 private:
  friend class RunExecutor;
  struct Impl;
  explicit ReadAheadReservation(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

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

// One lazily started worker executor shared by all parallel phases of a command.
// max_workers counts background threads; command owners reserve one participant
// for the calling coordinator and therefore pass N-1 for a --jobs=N allowance.
// Start grows the pool but never shrinks it; destruction joins the workers once.
// Start, Submit and worker_count are called only by the coordinator. Jobs stay
// leaves and must not submit or wait on work in this executor.
class RunExecutor final {
 public:
  static constexpr std::size_t kReadAheadBytes = 8 * 1'024 * 1'024;

  explicit RunExecutor(std::size_t max_workers, std::size_t read_ahead_bytes = kReadAheadBytes);

  ~RunExecutor();

  RunExecutor(const RunExecutor&) = delete;
  RunExecutor& operator=(const RunExecutor&) = delete;
  RunExecutor(RunExecutor&&) = delete;
  RunExecutor& operator=(RunExecutor&&) = delete;

  void Start(std::size_t useful_workers);

  // Coordinator-only admission and inspection. All walks sharing this executor use
  // the same byte allowance and at most twice the started worker count in unfinished reads.
  // Initial bytes charge the request's bookkeeping/path before it can enter the queue.
  // In-flight VFS calls and the coordinator's required listing are not retained lookahead.
  [[nodiscard]] std::optional<ReadAheadReservation> ReserveReadAhead(std::size_t initial_bytes = 0);
  ReadAheadStats ReadAheadUsage() const;

  std::size_t ReadAheadLimit() const { return read_ahead_bytes_; }

  template<typename Job>
  auto Submit(Job job, RunTaskClass task_class = RunTaskClass::kForeground) ABSL_LOCKS_EXCLUDED(mutex_) {
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
    auto& queues = queues_.value();
    if (task_class == RunTaskClass::kReadAhead) {
      queues.read_ahead.emplace(std::move(task));
    } else {
      queues.foreground.emplace(std::move(task));
    }
    return result;
  }

  std::size_t worker_count() const { return threads_.size(); }

 private:
  bool Pending() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void Run() ABSL_LOCKS_EXCLUDED(mutex_);

  struct DispatchQueues {
    std::queue<absl::AnyInvocable<void()>> foreground;
    std::queue<absl::AnyInvocable<void()>> read_ahead;
    bool prefer_read_ahead = false;
  };

  const std::size_t max_workers_;
  const std::size_t read_ahead_bytes_;
  // Initialized lazily by the coordinator before publishing the first reservation.
  // Workers access only their shared reservation state, never this owning handle.
  std::shared_ptr<executor_detail::ReadAheadBudget> read_ahead_budget_;
  // The mutex guards both queues, dispatch preference and shutdown. Jobs run unlocked;
  // the coordinator alone grows threads_ and inspects their count. Dispatch and
  // shared-budget mutexes are never held simultaneously by this implementation.
  mutable absl::Mutex mutex_;
  // std::queue's default deque allocates even while empty. Inline commands never
  // construct these queues; Start initializes them before publishing any worker.
  std::optional<DispatchQueues> queues_ ABSL_GUARDED_BY(mutex_);
  bool stop_ ABSL_GUARDED_BY(mutex_) = false;
  std::vector<std::thread> threads_;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_RUN_EXECUTOR_H_
