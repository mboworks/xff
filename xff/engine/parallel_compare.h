// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_PARALLEL_COMPARE_H_
#define XFF_ENGINE_PARALLEL_COMPARE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "mbo/types/optional_ref.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

// Only fields consumed by comparison and patch generation; matching and summaries
// consume the full metadata during traversal, before comparison entries are retained.
struct ComparisonMetadata {
  vfs::FileType type = vfs::FileType::kUnknown;
  vfs::Source source = vfs::Source::kLocalFs;
  std::uint64_t size = 0;
  std::uint32_t mode = 0;
};

// Text and filesystem observers must outlive this record and comparison results.
// ComparisonEntries owns retained path bytes for the two-tree coordinator.
struct TreeCompareEntry {
  std::string_view path;
  ComparisonMetadata metadata;
  mbo::types::OptionalRef<const vfs::FileSystem> fs;
  std::shared_ptr<const vfs::FileSystem> fs_owner;
};

struct ComparisonPair {
  std::string_view path;
  mbo::types::OptionalRef<const TreeCompareEntry> left;
  mbo::types::OptionalRef<const TreeCompareEntry> right;
};

struct ComparisonBytes {
  std::string left;
  std::string right;
};

struct ComparisonResult {
  bool same = false;
  // Only complete small differing inputs are retained for optional serial patch generation.
  std::optional<ComparisonBytes> bytes;
};

// Bounded ordered batches of read-only comparisons. Input entries and their filesystem observers
// must outlive Compare and consumption of the returned results/Inputs. Owned archive sources stay serial.
// Callers submit at most kBatchSize pairs.
// Workers publish values only; errors, patches and summaries are emitted by the coordinator.
class ParallelCompare final {
 public:
  static constexpr std::size_t kBatchSize = 64;
  static constexpr std::size_t kRetainedFileBytes = 65'536;

  explicit ParallelCompare(std::size_t workers, bool retain_patch_inputs = false);
  ~ParallelCompare();
  ParallelCompare(const ParallelCompare&) = delete;
  ParallelCompare& operator=(const ParallelCompare&) = delete;
  ParallelCompare(ParallelCompare&&) = delete;
  ParallelCompare& operator=(ParallelCompare&&) = delete;

  const std::vector<absl::StatusOr<ComparisonResult>>& Compare(std::vector<ComparisonPair> inputs);

  const std::vector<ComparisonPair>& Inputs() const { return inputs_; }

 private:
  bool UseWorkers() const;
  bool Ready(std::size_t generation) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool Finished() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void Run();
  void EvaluateEntries();

  const std::size_t workers_;
  const bool retain_patch_inputs_;
  std::vector<std::thread> threads_;
  absl::Mutex mutex_;
  bool stop_ ABSL_GUARDED_BY(mutex_) = false;
  std::size_t generation_ ABSL_GUARDED_BY(mutex_) = 0;
  std::size_t remaining_ ABSL_GUARDED_BY(mutex_) = 0;
  std::vector<ComparisonPair> inputs_;
  std::vector<absl::StatusOr<ComparisonResult>> results_;
  std::atomic<std::size_t> next_ = 0;
};

}  // namespace xff::engine
#endif  // XFF_ENGINE_PARALLEL_COMPARE_H_
