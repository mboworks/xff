// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/run_executor.h"

#include <algorithm>

namespace xff::engine {

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
  threads_.reserve(count);
  while (threads_.size() < count) {
    threads_.emplace_back([this] { Run(); });
  }
}

bool RunExecutor::Pending() const {
  return stop_ || !queue_.empty();
}

void RunExecutor::Run() {
  for (;;) {
    std::function<void()> job;
    {
      const absl::MutexLock lock(mutex_, absl::Condition(this, &RunExecutor::Pending));
      if (stop_ && queue_.empty()) {
        return;
      }
      job = std::move(queue_.front());
      queue_.pop();
    }
    job();
  }
}

}  // namespace xff::engine
