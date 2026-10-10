// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/match_work.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>

namespace xff::engine {
namespace {

constexpr std::size_t kMaxGrain = 128;
constexpr std::size_t kBalancingWaves = 4;

}  // namespace

MatchWorkEstimate EstimateMatchWork(std::array<std::chrono::nanoseconds, 4> elapsed, std::size_t chunk_entries) {
  if (chunk_entries == 0 || chunk_entries > std::numeric_limits<std::size_t>::max() / 2
      || std::ranges::any_of(elapsed, [](auto duration) { return duration < std::chrono::nanoseconds::zero(); })) {
    return {};
  }
  std::ranges::sort(elapsed);
  const auto sampled = elapsed.at(1) > std::chrono::nanoseconds::max() - elapsed.at(2) ? std::chrono::nanoseconds::max()
                                                                                       : elapsed.at(1) + elapsed.at(2);
  return {.sampled_entries = 2 * chunk_entries, .sampled_elapsed = sampled};
}

MatchWorkPlan PlanMatchWork(
    std::size_t remaining_entries,
    std::size_t participant_limit,
    std::size_t started_workers,
    MatchWorkEstimate estimate,
    MatchWorkCosts costs) {
  const auto zero = std::chrono::nanoseconds::zero();
  const std::size_t capacity = std::min(participant_limit, remaining_entries);
  if (capacity < 2 || estimate.sampled_entries == 0 || estimate.sampled_elapsed <= zero || costs.cold_start < zero
      || costs.worker_start < zero || costs.dispatch < zero || costs.target_chunk < zero
      || costs.minimum_saving < zero) {
    return {};
  }
  // Floating nanoseconds avoid integral overflow when extrapolating a slow sample
  // over a large remaining batch. Counts are clamped before conversion back.
  const double per_entry =
      static_cast<double>(estimate.sampled_elapsed.count()) / static_cast<double>(estimate.sampled_entries);
  const double serial = per_entry * static_cast<double>(remaining_entries);
  const double cold = started_workers == 0 ? static_cast<double>(costs.cold_start.count()) : 0;
  const auto start = static_cast<double>(costs.worker_start.count());
  const auto dispatch = static_cast<double>(costs.dispatch.count());
  MatchWorkPlan best;
  double best_cost = serial;
  const auto consider = [&](std::size_t participants) {
    const std::size_t background = participants - 1;
    const std::size_t added = background > started_workers ? background - started_workers : 0;
    const double predicted =
        (serial / static_cast<double>(participants)) + cold + dispatch + (static_cast<double>(added) * start);
    if (predicted < best_cost || (predicted == best_cost && participants < best.participants)) {
      best.participants = participants;
      best_cost = predicted;
    }
  };
  // Among already started workers, the model improves monotonically. Beyond
  // that boundary, serial/P + P*start is convex with its minimum at sqrt(serial/start).
  // Inspect only boundaries and neighboring integers, even for huge input counts.
  consider(2);
  consider(capacity);
  consider(started_workers >= capacity - 1 ? capacity : std::max(std::size_t{2}, started_workers + 1));
  if (start > 0) {
    const double optimum = std::sqrt(serial / start);
    const std::size_t lower = optimum >= static_cast<double>(capacity) ? capacity
                              : optimum <= 2                           ? std::size_t{2}
                                                                       : static_cast<std::size_t>(optimum);
    consider(lower);
    if (lower < capacity) {
      consider(lower + 1);
    }
  }
  if (best.participants == 1 || serial - best_cost < static_cast<double>(costs.minimum_saving.count())) {
    return {};
  }
  // Aim for useful work between atomic claims, but preserve multiple scheduling
  // waves for uneven entries. A slow item naturally reduces the grain toward one.
  const std::size_t grain_limit =
      std::max(std::size_t{1}, std::min(kMaxGrain, remaining_entries / best.participants / kBalancingWaves));
  const double desired = std::ceil(static_cast<double>(costs.target_chunk.count()) / per_entry);
  best.grain = desired >= static_cast<double>(grain_limit) ? grain_limit
               : desired <= 1                              ? std::size_t{1}
                                                           : static_cast<std::size_t>(desired);
  return best;
}

}  // namespace xff::engine
