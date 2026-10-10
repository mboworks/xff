// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/run_executor.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace xff::engine {

namespace executor_detail {
struct ReadAheadBudget final {
  explicit ReadAheadBudget(std::size_t bytes) : limit(bytes) {}

  const std::size_t limit;
  // The mutex guards slot and byte accounting, including peaks. A reservation
  // exclusively owns its own charge; its shared budget outlives all reservations.
  absl::Mutex mutex;
  ReadAheadStats usage ABSL_GUARDED_BY(mutex);
};
}  // namespace executor_detail

struct ReadAheadReservation::Impl final {
  Impl(std::shared_ptr<executor_detail::ReadAheadBudget> state, std::size_t initial_bytes)
      : budget(std::move(state)), minimum_bytes(initial_bytes), bytes(initial_bytes) {}

  ~Impl() {
    const absl::MutexLock lock(budget->mutex);
    if (unfinished) {
      --budget->usage.in_use;
    }
    budget->usage.retained_bytes -= bytes;
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  const std::shared_ptr<executor_detail::ReadAheadBudget> budget;
  const std::size_t minimum_bytes;
  std::size_t bytes = 0;
  bool unfinished = true;
};

ReadAheadReservation::ReadAheadReservation(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ReadAheadReservation::~ReadAheadReservation() = default;
ReadAheadReservation::ReadAheadReservation(ReadAheadReservation&&) noexcept = default;
ReadAheadReservation& ReadAheadReservation::operator=(ReadAheadReservation&&) noexcept = default;

bool ReadAheadReservation::Retain(std::size_t bytes) {
  const absl::MutexLock lock(impl_->budget->mutex);
  auto& usage = impl_->budget->usage;
  const std::size_t others = usage.retained_bytes - impl_->bytes;
  if (bytes < impl_->minimum_bytes || bytes > impl_->budget->limit - others) {
    return false;
  }
  usage.retained_bytes = others + bytes;
  usage.peak_retained_bytes = std::max(usage.peak_retained_bytes, usage.retained_bytes);
  impl_->bytes = bytes;
  return true;
}

void ReadAheadReservation::Finish() {
  const absl::MutexLock lock(impl_->budget->mutex);
  if (impl_->unfinished) {
    --impl_->budget->usage.in_use;
    impl_->unfinished = false;
  }
}

RunExecutor::RunExecutor(std::size_t max_workers, std::size_t read_ahead_bytes)
    : max_workers_(max_workers), read_ahead_bytes_(read_ahead_bytes) {}

RunExecutor::~RunExecutor() {
  {
    const absl::MutexLock lock(mutex_);
    stop_ = true;
  }
  for (std::thread& thread : threads_) {
    thread.join();
  }
}

void RunExecutor::Start(std::size_t useful_workers) {
  const std::size_t count = std::min(max_workers_, useful_workers);
  if (count == 0) {
    return;
  }
  if (threads_.empty()) {
    const absl::MutexLock lock(mutex_);
    queues_.emplace();
  }
  threads_.reserve(count);
  while (threads_.size() < count) {
    threads_.emplace_back([this] { Run(); });
  }
}

bool RunExecutor::Pending() const {
  return stop_ || (queues_ && (!queues_->foreground.empty() || !queues_->read_ahead.empty()));
}

std::optional<ReadAheadReservation> RunExecutor::ReserveReadAhead(std::size_t initial_bytes) {
  if (threads_.empty()) {
    return std::nullopt;
  }
  if (!read_ahead_budget_) {
    read_ahead_budget_ = std::make_shared<executor_detail::ReadAheadBudget>(read_ahead_bytes_);
  }
  const absl::MutexLock lock(read_ahead_budget_->mutex);
  const auto capacity = std::min(threads_.size(), std::numeric_limits<std::size_t>::max() / 2) * 2;
  if (read_ahead_budget_->usage.in_use >= capacity
      || initial_bytes > read_ahead_bytes_ - read_ahead_budget_->usage.retained_bytes) {
    return std::nullopt;
  }
  auto impl = std::make_unique<ReadAheadReservation::Impl>(read_ahead_budget_, initial_bytes);
  ++read_ahead_budget_->usage.in_use;
  read_ahead_budget_->usage.retained_bytes += initial_bytes;
  read_ahead_budget_->usage.peak_retained_bytes =
      std::max(read_ahead_budget_->usage.peak_retained_bytes, read_ahead_budget_->usage.retained_bytes);
  read_ahead_budget_->usage.peak_in_use =
      std::max(read_ahead_budget_->usage.peak_in_use, read_ahead_budget_->usage.in_use);
  return ReadAheadReservation(std::move(impl));
}

ReadAheadStats RunExecutor::ReadAheadUsage() const {
  if (!read_ahead_budget_) {
    return {};
  }
  const absl::MutexLock lock(read_ahead_budget_->mutex);
  return read_ahead_budget_->usage;
}

void RunExecutor::Run() {
  for (;;) {
    absl::AnyInvocable<void()> job;
    {
      const absl::MutexLock lock(mutex_, absl::Condition(this, &RunExecutor::Pending));
      if (!queues_) {
        return;  // An unstarted executor has no queued work to dispatch.
      }
      auto& queues = queues_.value();
      if (stop_ && queues.foreground.empty() && queues.read_ahead.empty()) {
        return;
      }
      // Alternating classes prevents an older speculative frontier from sitting
      // ahead of all foreground work, without starving speculation in a busy pool.
      const bool read_ahead = !queues.read_ahead.empty() && (queues.foreground.empty() || queues.prefer_read_ahead);
      auto& queue = read_ahead ? queues.read_ahead : queues.foreground;
      job = std::move(queue.front());
      queue.pop();
      queues.prefer_read_ahead = !read_ahead;
    }
    job();
  }
}

}  // namespace xff::engine
