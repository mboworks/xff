// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/read_measurement.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/registry/descriptor.h"

namespace xff::engine {

absl::StatusOr<ReadWorkload> ParseReadWorkload(std::string_view name) {
  constexpr auto kNames = std::to_array<std::pair<std::string_view, ReadWorkload>>(
      {{"listing", ReadWorkload::kListing},
       {"summary", ReadWorkload::kSummary},
       {"hash", ReadWorkload::kHash},
       {"hash_twice", ReadWorkload::kHashTwice},
       {"hash_lines", ReadWorkload::kHashLines},
       {"compare", ReadWorkload::kCompare}});
  for (const auto& [spelling, workload] : kNames) {
    if (name == spelling) {
      return workload;
    }
  }
  return absl::InvalidArgumentError("Unknown read workload");
}

absl::StatusOr<ReadMeasurement> MeasureReads(
    ReadWorkload workload,
    const std::vector<std::string>& roots,
    std::size_t jobs,
    const vfs::FileSystem& backend) {
  if (jobs == 0 || roots.size() != (workload == ReadWorkload::kCompare ? 2U : 1U)) {
    return absl::InvalidArgumentError("Read workload requires positive jobs and the expected root count");
  }
  for (const auto& root : roots) {
    if (!root.starts_with('/')) {
      return absl::InvalidArgumentError("Read workload roots must be absolute");
    }
  }
  std::vector<std::string> arguments{"--safe", absl::StrCat("--jobs=", jobs)};
  arguments.insert(arguments.end(), roots.begin(), roots.end());
  arguments.insert(arguments.end(), {"-type", "f"});
  switch (workload) {
    case ReadWorkload::kListing: break;
    case ReadWorkload::kSummary: arguments.emplace_back("--summary=ext"); break;
    case ReadWorkload::kHash: arguments.emplace_back("--template={hash}"); break;
    case ReadWorkload::kHashTwice: arguments.emplace_back("--template={hash} {hash}"); break;
    case ReadWorkload::kHashLines: arguments.emplace_back("--template={hash} {lines}"); break;
    case ReadWorkload::kCompare: arguments.emplace_back("--compare=summary"); break;
  }
  MBO_ASSIGN_OR_RETURN(const auto command, parser::Parse(arguments));
  const ReadObserver observer(backend);
  ReadMeasurement measurement;
  std::mutex output_mutex;
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  const auto result = RunFind(
      command, observer,
      [&](std::string_view text) {
        if (text.empty()) {
          return;
        }
        const std::scoped_lock lock(output_mutex);
        if (!measurement.first_output_seconds.has_value()) {
          measurement.first_output_seconds = std::chrono::duration<double>(Clock::now() - start).count();
        }
        measurement.output_bytes += text.size();
      },
      [](std::string_view, absl::Status) {}, registry::Style::kXff);
  measurement.elapsed_seconds = std::chrono::duration<double>(Clock::now() - start).count();
  measurement.reads = observer.Snapshot();
  measurement.errors = result.errors;
  return measurement;
}

}  // namespace xff::engine
