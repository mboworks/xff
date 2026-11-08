// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_OUTPUT_CREDIT_H_
#define XFF_ENGINE_OUTPUT_CREDIT_H_

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>

#include "absl/synchronization/mutex.h"
#include "mbo/types/optional_ref.h"

namespace xff::engine {

// Per-slot initial grants cover ordinary records without synchronized accounting.
// Excess allocations share a byte allowance. Only the ordered head may exceed it,
// so a late-validating transaction always has room to finish. The allowance is
// retained output storage, not a bound on active source/decoder or process memory.
class OutputCredit final {
 public:
  static constexpr std::size_t kSlotGrant = 4'096;
  static constexpr std::size_t kSharedBytes = 1'048'576;

  OutputCredit() = default;

  explicit OutputCredit(absl::Mutex& progress) : progress_(progress) {}

  struct Usage {
    std::size_t excess = 0;
    std::size_t peak_excess = 0;
    std::size_t waiters = 0;
  };

  void Reset();
  bool Acquire(std::size_t index, std::size_t bytes);
  void Release(std::size_t bytes);
  void Advance(std::size_t next);
  void Cancel();
  Usage GetUsage() const;

  bool Blocked() const { return blocked_.load() != 0; }

 private:
  void Wake();

  mutable std::mutex mutex_;
  Usage usage_;
  std::atomic<std::size_t> head_ = 0;
  std::atomic<std::size_t> blocked_ = 0;
  std::condition_variable condition_;
  mbo::types::OptionalRef<absl::Mutex> progress_;
  std::atomic<bool> cancelled_ = false;
};

// One worker owns this contiguous transaction until release-publishing completion.
// The coordinator borrows its view, then releases storage before advancing the head.
// Old and new allocations both hold credit during growth. Nothing is truncated or
// published before read/binary validation. Cancellation discards subsequent appends.
class CreditedOutput final {
 public:
  static constexpr std::size_t kInlineBytes = 64;

  CreditedOutput(OutputCredit& credit, std::size_t index) : credit_(credit), index_(index) {}

  ~CreditedOutput();
  CreditedOutput(const CreditedOutput&) = delete;
  CreditedOutput& operator=(const CreditedOutput&) = delete;
  CreditedOutput(CreditedOutput&& other) noexcept;
  CreditedOutput& operator=(CreditedOutput&&) = delete;

  void Begin(std::size_t index);
  void Release();
  void Append(std::string_view text);
  void Prepend(std::string_view text);
  void Clear();

  bool Empty() const { return size_ == 0; }

  std::size_t Size() const { return size_; }

  std::size_t Capacity() const { return capacity_; }

  bool Cancelled() const { return cancelled_; }

  std::string_view View() const;

 private:
  bool Grow(std::size_t size);
  std::span<char> Storage();

  OutputCredit& credit_;
  std::size_t index_;
  // The bytes are initialized only by append/prepend; do not zero the full slot array.
  std::array<char, kInlineBytes> inline_;

  struct StorageDeleter {
    // XFF_ABI_POINTER: std::unique_ptr's required deleter for raw operator-new storage.
    void operator()(char* storage) const noexcept;
  };

  using HeapStorage = std::unique_ptr<char, StorageDeleter>;
  HeapStorage heap_;
  std::size_t capacity_ = kInlineBytes;
  std::size_t size_ = 0;
  std::size_t excess_ = 0;
  bool cancelled_ = false;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_OUTPUT_CREDIT_H_
