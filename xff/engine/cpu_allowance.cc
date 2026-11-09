// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/cpu_allowance.h"

#include <algorithm>

namespace xff::engine {

CpuAllowance::CpuAllowance(std::size_t participants) : available_(std::max(participants, std::size_t{1})) {}

void CpuAllowance::Acquire() {
  std::unique_lock lock(mutex_);
  if (available_ == 0) {
    ++waiters_;
    condition_.wait(lock, [&] { return available_ != 0; });
    --waiters_;
  }
  --available_;
}

void CpuAllowance::Release() {
  bool waiting = false;
  {
    const std::scoped_lock lock(mutex_);
    ++available_;
    waiting = waiters_ != 0;
  }
  if (waiting) {
    condition_.notify_one();
  }
}

CpuAllowance::Usage CpuAllowance::GetUsage() const {
  const std::scoped_lock lock(mutex_);
  return {.available = available_, .waiters = waiters_};
}

CpuAllowance::Lease::Lease(CpuAllowance& allowance) : allowance_(allowance) {
  Resume();
}

CpuAllowance::Lease::~Lease() {
  Pause();
}

void CpuAllowance::Lease::Pause() {
  if (held_) {
    held_ = false;
    allowance_.Release();
  }
}

void CpuAllowance::Lease::Resume() {
  if (!held_) {
    allowance_.Acquire();
    held_ = true;
  }
}

}  // namespace xff::engine
