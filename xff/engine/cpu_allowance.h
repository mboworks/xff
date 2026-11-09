// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_CPU_ALLOWANCE_H_
#define XFF_ENGINE_CPU_ALLOWANCE_H_

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace xff::engine {

// Pipeline participants borrow at generation/publication boundaries, not per file.
// A producer temporarily returns its unit while blocked on output credit.
class CpuAllowance final {
 public:
  explicit CpuAllowance(std::size_t participants);
  CpuAllowance(const CpuAllowance&) = delete;
  CpuAllowance& operator=(const CpuAllowance&) = delete;

  class Lease final {
   public:
    explicit Lease(CpuAllowance& allowance);
    ~Lease();
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    void Pause();
    void Resume();

   private:
    CpuAllowance& allowance_;
    bool held_ = false;
  };

  struct Usage {
    std::size_t available;
    std::size_t waiters;
  };

  Usage GetUsage() const;

 private:
  void Acquire();
  void Release();

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::size_t available_;
  std::size_t waiters_ = 0;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_CPU_ALLOWANCE_H_
