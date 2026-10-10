// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_MATCH_WORK_H_
#define XFF_ENGINE_MATCH_WORK_H_

#include <array>
#include <chrono>
#include <cstddef>

namespace xff::engine {

struct MatchWorkEstimate {
  std::size_t sampled_entries = 0;
  std::chrono::nanoseconds sampled_elapsed = {};
};

// All four chunks were evaluated once. Use the middle two elapsed observations
// to avoid letting one already-completed cold/outlier chunk justify a cheap tail.
// Invalid dimensions/times return an empty estimate; duration addition saturates.
MatchWorkEstimate EstimateMatchWork(std::array<std::chrono::nanoseconds, 4> elapsed, std::size_t chunk_entries);

// Experimental planning allowances, not portable measurements or hard latency bounds.
// The cold allowance covers the observed default Linux/x86 calibration cliff; native
// macOS and actual matcher setup/dispatch still require acceptance measurements.
struct MatchWorkCosts {
  std::chrono::nanoseconds cold_start = std::chrono::microseconds(3'500);
  std::chrono::nanoseconds worker_start = std::chrono::microseconds(50);
  std::chrono::nanoseconds dispatch = std::chrono::microseconds(50);
  std::chrono::nanoseconds target_chunk = std::chrono::microseconds(32);
  std::chrono::nanoseconds minimum_saving = std::chrono::microseconds(100);
};

struct MatchWorkPlan {
  std::size_t participants = 1;  // Includes the calling coordinator.
  std::size_t grain = 4;
};

// Plan only the work still available after sampling. Existing background threads
// are reusable, but waking them and growing the pool are not free. Invalid or
// insufficient observations fall back to the caller. There is no completed-work
// counter and no assumption of independent work beyond this owned batch.
MatchWorkPlan PlanMatchWork(
    std::size_t remaining_entries,
    std::size_t participant_limit,
    std::size_t started_workers,
    MatchWorkEstimate estimate,
    MatchWorkCosts costs = {});

}  // namespace xff::engine

#endif  // XFF_ENGINE_MATCH_WORK_H_
