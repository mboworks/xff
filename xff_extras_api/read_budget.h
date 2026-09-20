// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_VFS_READ_BUDGET_H_
#define XFF_VFS_READ_BUDGET_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace xff::vfs {

// Shared by a source tree and every cursor/cache opened from it. Independent roots can explicitly
// share the same budget. Memory is released with reservations; replay work is cumulative.
class ReadBudget {
  struct State;

 public:
  class Reservation {
   public:
    Reservation() = default;
    ~Reservation();
    Reservation(Reservation&& other) noexcept;
    Reservation& operator=(Reservation&& other) noexcept;
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;

   private:
    friend class ReadBudget;
    Reservation(std::shared_ptr<State> state, std::size_t bytes);
    void Reset();
    std::shared_ptr<State> state_;
    std::size_t bytes_ = 0;
  };

  explicit ReadBudget(
      std::size_t memory_limit = 512UZ * 1'024 * 1'024,
      std::uint64_t replay_limit = 256ULL * 1'024 * 1'024);
  absl::StatusOr<Reservation> Reserve(std::size_t bytes);
  absl::Status ConsumeReplay(std::uint64_t bytes);
  std::size_t MemoryUsed() const;

 private:
  std::shared_ptr<State> state_;
};
}  // namespace xff::vfs
#endif  // XFF_VFS_READ_BUDGET_H_
