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
#include "xff/engine/read_measurement.h"
#include "xff/engine/read_observer.h"
#include "xff/vfs/local_fs.h"

namespace {

nlohmann::json Counts(const xff::engine::ReadObservation& value) {
  return {{"attempts", value.attempts}, {"successes", value.successes}, {"bytes", value.bytes}};
}

// XFF_HOST_IO: benchmark executable's diagnostic stream adapter; no path-based I/O.
int Error(std::string_view message) {
  std::cerr << message << '\n';
  return 2;
}

// XFF_HOST_IO: benchmark executable's JSON result stream adapter; no path-based I/O.
void Emit(const nlohmann::json& report) {
  std::cout << report.dump() << '\n';
}

}  // namespace

// XFF_ABI_POINTER: standard process entry point; argv is converted within this adapter.
int main(int argc, char** argv) {
  if (argc < 4) {
    return Error("usage: read_benchmark SCENARIO JOBS ABSOLUTE_ROOT [ABSOLUTE_RIGHT_ROOT]");
  }
  const std::span argument_view(argv, static_cast<std::size_t>(argc));
  const std::vector<std::string> arguments(argument_view.begin(), argument_view.end());
  const auto workload = xff::engine::ParseReadWorkload(arguments.at(1));
  if (!workload.ok()) {
    return Error(workload.status().message());
  }
  std::size_t jobs = 0;
  if (!absl::SimpleAtoi(arguments.at(2), &jobs)) {
    return Error("jobs must be a positive integer");
  }
  std::vector<std::string> roots;
  for (const auto& argument : std::span(arguments).subspan(3)) {
    roots.emplace_back(argument);
  }
  const xff::vfs::LocalFs backend;
  const auto result = xff::engine::MeasureReads(*workload, roots, jobs, backend);
  if (!result.ok()) {
    return Error(result.status().message());
  }
  nlohmann::json report{
      {"workload", arguments.at(1)},
      {"jobs", jobs},
      {"roots", roots},
      {"whole", Counts(result->reads.whole)},
      {"range", Counts(result->reads.range)},
      {"output_bytes", result->output_bytes},
      {"elapsed_seconds", result->elapsed_seconds},
      {"errors", result->errors},
      {"first_output_seconds", nullptr},
      {"measurement", "engine logical VFS bytes; not storage traffic"}};
  if (result->first_output_seconds.has_value()) {
    report["first_output_seconds"] = *result->first_output_seconds;
  }
  Emit(report);
  return result->errors == 0 ? 0 : 1;
}
