// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_DIRECTORY_READ_ADMISSION_H_
#define XFF_ENGINE_DIRECTORY_READ_ADMISSION_H_

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace xff::engine::walk_internal {

// A conservative cold-start work budget, including pool synchronization. This is an admission
// estimate, not a deadline or a promise that sibling reads have identical cost. An existing pool
// does not need to amortize startup again. Keep the arithmetic overflow-safe for large listings.
constexpr bool UsefulDirectoryReads(std::chrono::nanoseconds sample, std::size_t directories, std::size_t workers) {
  if (sample.count() <= 0 || directories < 2 || workers < 2) {
    return false;
  }
  workers = std::min(workers, directories);
  const std::size_t rounds = (directories / workers) + (directories % workers != 0 ? 1 : 0);
  const std::size_t saved_reads = directories - rounds;
  constexpr auto kStartupBudget = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(4));
  return static_cast<std::uint64_t>(sample.count())
         >= ((static_cast<std::uint64_t>(kStartupBudget.count()) - 1) / saved_reads) + 1;
}

}  // namespace xff::engine::walk_internal

#endif  // XFF_ENGINE_DIRECTORY_READ_ADMISSION_H_
