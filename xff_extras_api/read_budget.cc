// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/vfs/read_budget.h"

#include <mutex>
#include <utility>

namespace xff::vfs {
struct ReadBudget::State {
  std::mutex mutex;
  std::size_t limit;
  std::size_t used = 0;
  std::uint64_t replay_remaining;
};

ReadBudget::ReadBudget(std::size_t memory_limit, std::uint64_t replay_limit) : state_(std::make_shared<State>()) {
  state_->limit = memory_limit;
  state_->replay_remaining = replay_limit;
}

ReadBudget::Reservation::Reservation(std::shared_ptr<State> state, std::size_t bytes)
    : state_(std::move(state)), bytes_(bytes) {}

ReadBudget::Reservation::~Reservation() {
  Reset();
}

ReadBudget::Reservation::Reservation(Reservation&& other) noexcept
    : state_(std::move(other.state_)), bytes_(std::exchange(other.bytes_, 0)) {}

ReadBudget::Reservation& ReadBudget::Reservation::operator=(Reservation&& other) noexcept {
  if (this != &other) {
    Reset();
    state_ = std::move(other.state_);
    bytes_ = std::exchange(other.bytes_, 0);
  }
  return *this;
}

void ReadBudget::Reservation::Reset() {
  if (state_) {
    const std::scoped_lock lock(state_->mutex);
    state_->used -= bytes_;
  }
  state_.reset();
  bytes_ = 0;
}

absl::StatusOr<ReadBudget::Reservation> ReadBudget::Reserve(std::size_t bytes) {
  const std::scoped_lock lock(state_->mutex);
  if (bytes > state_->limit - state_->used) {
    return absl::ResourceExhaustedError("shared container memory budget exceeded");
  }
  state_->used += bytes;
  return Reservation(state_, bytes);
}

absl::Status ReadBudget::ConsumeReplay(std::uint64_t bytes) {
  const std::scoped_lock lock(state_->mutex);
  if (bytes > state_->replay_remaining) {
    return absl::ResourceExhaustedError("shared container replay budget exceeded");
  }
  state_->replay_remaining -= bytes;
  return absl::OkStatus();
}

std::size_t ReadBudget::MemoryUsed() const {
  const std::scoped_lock lock(state_->mutex);
  return state_->used;
}
}  // namespace xff::vfs
