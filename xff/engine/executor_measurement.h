// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_EXECUTOR_MEASUREMENT_H_
#define XFF_ENGINE_EXECUTOR_MEASUREMENT_H_

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "absl/status/statusor.h"

namespace xff::engine {

enum class ExecutorProbeDispatch { kLeaf, kClaimable, kDrain };

absl::StatusOr<ExecutorProbeDispatch> ParseExecutorProbeDispatch(std::string_view name);

struct ExecutorProbeConfig {
  ExecutorProbeDispatch dispatch = ExecutorProbeDispatch::kLeaf;
  std::size_t participants = 1;
  std::size_t items = 1'024;
  std::size_t sweeps = 16;
  std::size_t grain = 1;
  std::size_t work_units = 0;
};

struct ExecutorProbePhase {
  double seconds = 0;
  std::size_t items = 0;
  std::size_t queued_jobs = 0;
  std::size_t observed_participants = 0;
  bool caller_participated = false;
  std::uint64_t checksum = 0;
};

struct ExecutorProbeMeasurement {
  std::size_t background_workers = 0;
  double start_seconds = 0;
  ExecutorProbePhase first_dispatch;
  ExecutorProbePhase warmed;
  double teardown_seconds = 0;
};

// Diagnostic only: explicitly start N-1 workers, dispatch one cold batch, then
// sweep synthetic checksum work with a warm pool. Cold results require a fresh
// process: earlier Abseil contention may already have calibrated its clock.
// Phase times include probe bookkeeping, not just isolated queue operations.
// No VFS, traversal/frontier, storage or runtime admission policy is measured.
absl::StatusOr<ExecutorProbeMeasurement> MeasureExecutor(const ExecutorProbeConfig& config);

}  // namespace xff::engine

#endif  // XFF_ENGINE_EXECUTOR_MEASUREMENT_H_
