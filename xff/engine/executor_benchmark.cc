// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/numbers.h"
#include "nlohmann/json.hpp"
#include "xff/engine/executor_measurement.h"

namespace {

using ::xff::engine::ExecutorProbePhase;

nlohmann::json Phase(const ExecutorProbePhase& phase) {
  return {
      {"seconds", phase.seconds},
      {"items", phase.items},
      {"queued_jobs", phase.queued_jobs},
      {"observed_participants", phase.observed_participants},
      {"caller_participated", phase.caller_participated},
      {"checksum", phase.checksum},
  };
}

// XFF_HOST_IO: diagnostic executable's result/error stream adapter; no path I/O.
int Error(std::string_view message) {
  std::cerr << message << '\n';
  return 2;
}

// XFF_HOST_IO: diagnostic executable's result stream adapter; no path I/O.
void Emit(const nlohmann::json& report) {
  std::cout << report.dump() << '\n';
}

}  // namespace

// XFF_ABI_POINTER: standard process entry point; argv is converted inside this adapter.
int main(int argc, char** argv) {
  if (argc != 7) {
    return Error("usage: executor_benchmark leaf|claimable|drain PARTICIPANTS ITEMS SWEEPS GRAIN WORK_UNITS");
  }
  const std::span argument_view(argv, static_cast<std::size_t>(argc));
  const std::vector<std::string> arguments(argument_view.begin(), argument_view.end());
  const auto dispatch = xff::engine::ParseExecutorProbeDispatch(arguments.at(1));
  if (!dispatch.ok()) {
    return Error(dispatch.status().message());
  }
  xff::engine::ExecutorProbeConfig config{.dispatch = *dispatch};
  if (!absl::SimpleAtoi(arguments.at(2), &config.participants) || !absl::SimpleAtoi(arguments.at(3), &config.items)
      || !absl::SimpleAtoi(arguments.at(4), &config.sweeps) || !absl::SimpleAtoi(arguments.at(5), &config.grain)
      || !absl::SimpleAtoi(arguments.at(6), &config.work_units)) {
    return Error("probe dimensions must be unsigned integers");
  }
  const auto result = xff::engine::MeasureExecutor(config);
  if (!result.ok()) {
    return Error(result.status().message());
  }
  Emit({
      {"dispatch", arguments.at(1)},
      {"participants", config.participants},
      {"items", config.items},
      {"sweeps", config.sweeps},
      {"grain", config.grain},
      {"work_units", config.work_units},
      {"background_workers", result->background_workers},
      {"start_seconds", result->start_seconds},
      {"first_dispatch", Phase(result->first_dispatch)},
      {"warmed", Phase(result->warmed)},
      {"teardown_seconds", result->teardown_seconds},
      {"measurement", "synthetic executor phases including bookkeeping; fresh process required for cold costs"},
  });
  return 0;
}
