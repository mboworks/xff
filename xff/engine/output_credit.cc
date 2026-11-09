// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/output_credit.h"

#include <algorithm>
#include <new>
#include <utility>

namespace xff::engine {

void OutputCredit::Reset() {
  const std::scoped_lock lock(mutex_);
  usage_ = {};
  head_.store(0);
  cancelled_.store(false, std::memory_order_relaxed);
}

bool OutputCredit::Acquire(std::size_t index, std::size_t bytes, mbo::types::OptionalRef<CpuAllowance::Lease> cpu) {
  if (bytes == 0) {
    return true;
  }
  std::unique_lock lock(mutex_);
  const auto available = [&] {
    return cancelled_.load() || index == head_.load()
           || (bytes <= kSharedBytes && usage_.excess <= kSharedBytes - bytes);
  };
  bool paused = false;
  if (!available()) {
    // The coordinator needs a CPU unit to return output credit. Never park while
    // holding that unit; reacquire it only after dropping the credit mutex.
    if (cpu) {
      cpu->Pause();
      paused = true;
    }
    blocked_.fetch_add(1);
    if (progress_) {
      // A coordinator batching publication must wake and drain the ordered head
      // when credit fills. Do not nest the credit and progress mutexes.
      lock.unlock();
      { const absl::MutexLock notify(*progress_); }
      lock.lock();
    }
    condition_.wait(lock, available);
    blocked_.fetch_sub(1);
  }
  const bool admitted = !cancelled_.load();
  if (admitted) {
    usage_.excess += bytes;
    usage_.peak_excess = std::max(usage_.peak_excess, usage_.excess);
  }
  lock.unlock();
  if (paused) {
    cpu->Resume();
  }
  return admitted;
}

void OutputCredit::Wake() {
  if (blocked_.load() != 0) {
    // Serialize with a producer's predicate check and release-to-wait. A producer
    // registering later observes the already advanced head/cancellation instead.
    { const std::scoped_lock lock(mutex_); }
    condition_.notify_all();
  }
}

void OutputCredit::Release(std::size_t bytes) {
  if (bytes != 0) {
    {
      const std::scoped_lock lock(mutex_);
      usage_.excess -= bytes;
    }
    if (blocked_.load() != 0) {
      condition_.notify_all();
    }
  }
}

void OutputCredit::Advance(std::size_t next) {
  head_.store(next);
  Wake();
}

void OutputCredit::Cancel() {
  cancelled_.store(true);
  Wake();
}

OutputCredit::Usage OutputCredit::GetUsage() const {
  const std::scoped_lock lock(mutex_);
  return {
      .excess = usage_.excess,
      .peak_excess = usage_.peak_excess,
      .waiters = blocked_.load(),
  };
}

// XFF_ABI_POINTER: std::unique_ptr's required deleter for raw operator-new storage.
void CreditedOutput::StorageDeleter::operator()(char* storage) const noexcept {
  ::operator delete(storage);
}

CreditedOutput::~CreditedOutput() {
  heap_.reset();
  credit_.Release(excess_);
}

CreditedOutput::CreditedOutput(CreditedOutput&& other) noexcept
    : credit_(other.credit_),
      index_(other.index_),
      heap_(std::move(other.heap_)),
      capacity_(std::exchange(other.capacity_, kInlineBytes)),
      size_(std::exchange(other.size_, 0)),
      excess_(std::exchange(other.excess_, 0)),
      cancelled_(other.cancelled_),
      cpu_(other.cpu_) {
  other.cpu_.reset();
  if (!heap_) {
    std::ranges::copy(std::span(other.inline_).first(size_), inline_.begin());
  }
}

std::span<char> CreditedOutput::Storage() {
  return heap_ ? std::span<char>(heap_.get(), capacity_) : std::span<char>(inline_);
}

std::string_view CreditedOutput::View() const {
  return heap_ ? std::string_view(heap_.get(), size_) : std::string_view(inline_.data(), size_);
}

bool CreditedOutput::Grow(std::size_t size) {
  if (cancelled_) {
    return false;
  }
  if (size <= capacity_) {
    return true;
  }
  const auto next = std::max(size, 2 * capacity_);
  // The grant covers both old and new allocations. Inline bytes live in the slot
  // and are already charged as slot storage, rather than heap output credit.
  const auto old = heap_ ? capacity_ : 0;
  const auto combined = old + next;
  const auto combined_excess = combined > OutputCredit::kSlotGrant ? combined - OutputCredit::kSlotGrant : 0;
  const auto additional = combined_excess - excess_;
  if (!credit_.Acquire(index_, additional, cpu_)) {
    cancelled_ = true;
    size_ = 0;
    return false;
  }
  // XFF_ABI_POINTER: consume operator new's storage with unique_ptr's character-pointer ABI.
  auto replacement = HeapStorage(static_cast<char*>(::operator new(next)));
  std::ranges::copy(Storage().first(size_), std::span(replacement.get(), next).begin());
  heap_ = std::move(replacement);
  capacity_ = next;
  const auto retained = next > OutputCredit::kSlotGrant ? next - OutputCredit::kSlotGrant : 0;
  credit_.Release(combined_excess - retained);
  excess_ = retained;
  return true;
}

void CreditedOutput::Release() {
  size_ = 0;
  cancelled_ = false;
  // Keep only the allocation already covered by this slot's initial grant.
  // Larger transactions return every excess byte before the ordered head advances.
  if (excess_ != 0) {
    heap_.reset();
    capacity_ = kInlineBytes;
    credit_.Release(std::exchange(excess_, 0));
  }
}

void CreditedOutput::Begin(std::size_t index) {
  Release();
  index_ = index;
}

void CreditedOutput::Append(std::string_view text) {
  if (!text.empty() && Grow(size_ + text.size())) {
    std::ranges::copy(text, Storage().subspan(size_).begin());
    size_ += text.size();
  }
}

void CreditedOutput::Prepend(std::string_view text) {
  if (!text.empty() && Grow(size_ + text.size())) {
    const auto storage = Storage();
    std::ranges::copy_backward(storage.first(size_), storage.subspan(0, size_ + text.size()).end());
    std::ranges::copy(text, storage.begin());
    size_ += text.size();
  }
}

void CreditedOutput::Clear() {
  size_ = 0;
}

}  // namespace xff::engine
