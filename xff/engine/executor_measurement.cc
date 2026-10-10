// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/executor_measurement.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "xff/engine/run_executor.h"

namespace xff::engine {
namespace {

using Clock = std::chrono::steady_clock;

double Elapsed(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

struct Chunk {
  std::size_t items = 0;
  std::uint64_t checksum = 0;
  std::thread::id participant = std::this_thread::get_id();
};

// Runtime-dependent unsigned mixing keeps the work observable in the returned
// checksum. Zero units measures dispatch with only the seed and accumulation.
Chunk Work(std::size_t first, std::size_t end, std::size_t units) {
  Chunk result;
  for (std::size_t index = first; index < end; ++index) {
    std::uint64_t value = index + 1;
    for (std::size_t unit = 0; unit < units; ++unit) {
      value ^= value << 7U;
      value ^= value >> 9U;
      value ^= value << 8U;
    }
    result.checksum += value;
    ++result.items;
  }
  return result;
}

class Accumulator final {
 public:
  void Add(const Chunk& chunk) {
    phase_.items += chunk.items;
    phase_.checksum += chunk.checksum;
    if (chunk.items != 0) {
      participants_.insert(chunk.participant);
    }
  }

  void Queued() { ++phase_.queued_jobs; }

  ExecutorProbePhase Finish(double seconds) && {
    phase_.seconds = seconds;
    phase_.observed_participants = participants_.size();
    phase_.caller_participated = participants_.contains(std::this_thread::get_id());
    return phase_;
  }

 private:
  ExecutorProbePhase phase_;
  std::set<std::thread::id> participants_;
};

struct PendingChunk {
  RunTask<Chunk> result;
  RunTask<void> wrapper;
  std::shared_ptr<RunWork> work;

  Chunk Get() {
    if (work) {
      work->Run();
    }
    auto chunk = result.Get();
    if (wrapper.Valid()) {
      wrapper.Get();
    }
    return chunk;
  }
};

PendingChunk Queue(
    RunExecutor& executor,
    ExecutorProbeDispatch dispatch,
    std::size_t first,
    std::size_t end,
    std::size_t units) {
  const auto job = [first, end, units] { return Work(first, end, units); };
  if (dispatch != ExecutorProbeDispatch::kClaimable) {
    return {.result = executor.Submit(job)};
  }
  RunPromise<Chunk> promise;
  auto result = promise.Task();
  const auto work = std::make_shared<RunWork>([job, promise = std::move(promise)] mutable { promise.SetValue(job()); });
  auto wrapper = executor.Submit([work] { work->Run(); }, RunTaskClass::kReadAhead);
  return {.result = std::move(result), .wrapper = std::move(wrapper), .work = work};
}

ExecutorProbePhase FirstDispatch(RunExecutor& executor, std::size_t participants) {
  Accumulator total;
  std::vector<PendingChunk> pending;
  pending.reserve(participants - 1);
  const auto start = Clock::now();
  for (std::size_t index = 0; index + 1 < participants; ++index) {
    pending.push_back(Queue(executor, ExecutorProbeDispatch::kLeaf, index, index + 1, 0));
    total.Queued();
  }
  total.Add(Work(participants - 1, participants, 0));
  for (auto& task : pending) {
    total.Add(task.Get());
  }
  return std::move(total).Finish(Elapsed(start));
}

ExecutorProbePhase LeafSweeps(RunExecutor& executor, const ExecutorProbeConfig& config) {
  Accumulator total;
  std::vector<PendingChunk> pending;
  pending.reserve(config.participants - 1);
  const auto start = Clock::now();
  for (std::size_t sweep = 0; sweep < config.sweeps; ++sweep) {
    std::size_t next = 0;
    while (next < config.items) {
      pending.clear();
      const std::size_t caller_first = next;
      const std::size_t caller_end = next + std::min(config.grain, config.items - next);
      next = caller_end;
      for (std::size_t worker = 1; worker < config.participants && next < config.items; ++worker) {
        const std::size_t end = next + std::min(config.grain, config.items - next);
        pending.push_back(Queue(executor, config.dispatch, next, end, config.work_units));
        total.Queued();
        next = end;
      }
      total.Add(Work(caller_first, caller_end, config.work_units));
      for (auto& task : pending) {
        total.Add(task.Get());
      }
    }
  }
  return std::move(total).Finish(Elapsed(start));
}

ExecutorProbePhase DrainSweeps(RunExecutor& executor, const ExecutorProbeConfig& config) {
  Accumulator total;
  std::vector<RunTask<Chunk>> pending;
  pending.reserve(config.participants - 1);
  // Only this atomic assigns chunk indices. Each drain owns its local checksum;
  // only the coordinator merges returned chunks and changes total/pending.
  std::atomic<std::size_t> next = 0;
  const std::size_t chunks = ((config.items - 1) / config.grain) + 1;
  const auto drain = [&] {
    Chunk result;
    for (;;) {
      const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
      if (index >= chunks) {
        return result;
      }
      const std::size_t first = index * config.grain;
      const auto chunk = Work(first, first + std::min(config.grain, config.items - first), config.work_units);
      result.items += chunk.items;
      result.checksum += chunk.checksum;
    }
  };
  const auto start = Clock::now();
  for (std::size_t sweep = 0; sweep < config.sweeps; ++sweep) {
    next.store(0, std::memory_order_relaxed);
    pending.clear();
    for (std::size_t worker = 1; worker < config.participants; ++worker) {
      pending.push_back(executor.Submit(drain));
      total.Queued();
    }
    total.Add(drain());
    for (auto& task : pending) {
      total.Add(task.Get());
    }
  }
  return std::move(total).Finish(Elapsed(start));
}

}  // namespace

absl::StatusOr<ExecutorProbeDispatch> ParseExecutorProbeDispatch(std::string_view name) {
  if (name == "leaf") {
    return ExecutorProbeDispatch::kLeaf;
  }
  if (name == "claimable") {
    return ExecutorProbeDispatch::kClaimable;
  }
  if (name == "drain") {
    return ExecutorProbeDispatch::kDrain;
  }
  return absl::InvalidArgumentError("dispatch must be leaf, claimable or drain");
}

absl::StatusOr<ExecutorProbeMeasurement> MeasureExecutor(const ExecutorProbeConfig& config) {
  if (config.participants == 0 || config.participants > 64 || config.items == 0 || config.sweeps == 0
      || config.grain == 0 || config.items > std::numeric_limits<std::size_t>::max() / config.sweeps
      || config.sweeps > std::numeric_limits<std::size_t>::max() / config.participants
      || config.items > std::numeric_limits<std::size_t>::max() - config.participants) {
    return absl::InvalidArgumentError("participants must be 1..64; items/sweeps/grain positive and representable");
  }
  if (config.dispatch != ExecutorProbeDispatch::kLeaf && config.dispatch != ExecutorProbeDispatch::kClaimable
      && config.dispatch != ExecutorProbeDispatch::kDrain) {
    return absl::InvalidArgumentError("unknown dispatch");
  }
  auto executor = std::make_unique<RunExecutor>(config.participants - 1);
  ExecutorProbeMeasurement result;
  auto start = Clock::now();
  executor->Start(config.participants - 1);
  result.start_seconds = Elapsed(start);
  result.background_workers = executor->worker_count();
  result.first_dispatch = FirstDispatch(*executor, config.participants);
  result.warmed =
      config.dispatch == ExecutorProbeDispatch::kDrain ? DrainSweeps(*executor, config) : LeafSweeps(*executor, config);
  start = Clock::now();
  executor.reset();
  result.teardown_seconds = Elapsed(start);
  return result;
}

}  // namespace xff::engine
