// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_READ_MEASUREMENT_H_
#define XFF_ENGINE_READ_MEASUREMENT_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/engine/read_observer.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

enum class ReadWorkload { kListing, kSummary, kHash, kHashTwice, kHashLines, kCompare };
absl::StatusOr<ReadWorkload> ParseReadWorkload(std::string_view name);

struct ReadMeasurement {
  ReadObservations reads;
  std::uint64_t output_bytes = 0;
  std::optional<double> first_output_seconds;
  double elapsed_seconds = 0;
  int errors = 0;
};

// Executes the broad/deep benchmark suite's read-only workloads over the supplied backend.
// Roots must be absolute (so they cannot be parsed as options), one per ordinary workload and
// two for comparison. This measures engine execution, excluding CLI startup/config loading.
absl::StatusOr<ReadMeasurement> MeasureReads(
    ReadWorkload workload,
    const std::vector<std::string>& roots,
    std::size_t jobs,
    const vfs::FileSystem& backend);

}  // namespace xff::engine

#endif  // XFF_ENGINE_READ_MEASUREMENT_H_
