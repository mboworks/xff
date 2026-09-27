// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/parallel_compare.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "mbo/status/status_macros.h"
#include "xff/vfs/read_source.h"

namespace xff::engine {
namespace {

// A cursor may return a short non-EOF read. Compare complete requested ranges, preserving
// the previous metadata-sized comparison when a file changes during the scan.
absl::StatusOr<std::string> ReadChunk(vfs::ReadStream& stream, std::size_t length) {
  MBO_ASSIGN_OR_RETURN(auto bytes, stream.Read(length));
  if (bytes.empty()) {
    return bytes;
  }
  while (bytes.size() < length) {
    MBO_ASSIGN_OR_RETURN(auto tail, stream.Read(length - bytes.size()));
    if (tail.empty()) {
      break;
    }
    bytes.append(tail);
  }
  return bytes;
}

struct Cursor {
  vfs::ReadBudget::Reservation reservation;
  std::unique_ptr<vfs::ReadStream> stream;
};

absl::StatusOr<Cursor> OpenEntry(const TreeCompareEntry& entry) {
  MBO_ASSIGN_OR_RETURN(const auto source, entry.fs->ContentSource(entry.path));
  MBO_ASSIGN_OR_RETURN(auto reservation, source->Budget()->Reserve(ParallelCompare::kRetainedFileBytes));
  MBO_ASSIGN_OR_RETURN(auto stream, source->Open());
  return Cursor{.reservation = std::move(reservation), .stream = std::move(stream)};
}

absl::StatusOr<ComparisonResult> CompareSmall(
    const TreeCompareEntry& left,
    const TreeCompareEntry& right,
    bool retain) {
  const auto size = static_cast<std::size_t>(left.metadata.size);
  // A single range read avoids allocating a cursor for one-chunk files. One extra byte
  // establishes whether these are complete inputs for optional patch reuse.
  MBO_ASSIGN_OR_RETURN(auto left_bytes, left.fs->ReadContentRange(left.path, 0, size + (retain ? 1 : 0)));
  MBO_ASSIGN_OR_RETURN(auto right_bytes, right.fs->ReadContentRange(right.path, 0, size + (retain ? 1 : 0)));
  ComparisonResult result{
      .same = std::string_view(left_bytes).substr(0, size) == std::string_view(right_bytes).substr(0, size),
  };
  if (!result.same && retain && left_bytes.size() <= size && right_bytes.size() <= size) {
    result.bytes = ComparisonBytes{.left = std::move(left_bytes), .right = std::move(right_bytes)};
  }
  return result;
}

absl::StatusOr<ComparisonResult> CompareRegular(
    const TreeCompareEntry& left,
    const TreeCompareEntry& right,
    bool retain) {
  if (left.metadata.size != right.metadata.size) {
    return ComparisonResult{};
  }
  if (left.metadata.size == 0) {
    return ComparisonResult{.same = true};
  }
  if (left.metadata.size <= ParallelCompare::kRetainedFileBytes) {
    return CompareSmall(left, right, retain);
  }
  MBO_ASSIGN_OR_RETURN(auto left_stream, OpenEntry(left));
  std::optional<Cursor> right_stream;
  constexpr std::size_t kChunk = ParallelCompare::kRetainedFileBytes;
  for (std::uint64_t offset = 0; offset < left.metadata.size; offset += kChunk) {
    const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, left.metadata.size - offset));
    MBO_ASSIGN_OR_RETURN(auto left_bytes, ReadChunk(*left_stream.stream, length));
    if (!right_stream) {
      MBO_ASSIGN_OR_RETURN(auto cursor, OpenEntry(right));
      right_stream.emplace(std::move(cursor));
    }
    MBO_ASSIGN_OR_RETURN(auto right_bytes, ReadChunk(*right_stream->stream, length));
    if (left_bytes != right_bytes) {
      return ComparisonResult{};
    }
    if (left_bytes.size() < length) {
      break;  // Both cursors reached EOF at the same offset.
    }
  }
  return ComparisonResult{.same = true};
}

absl::StatusOr<ComparisonResult> ComparePair(const ComparisonPair& pair, bool retain) {
  if (!pair.left || !pair.right || pair.left->metadata.type != pair.right->metadata.type) {
    return ComparisonResult{};
  }
  if (pair.left->metadata.type == vfs::FileType::kRegular) {
    return CompareRegular(*pair.left, *pair.right, retain);
  }
  if (pair.left->metadata.type == vfs::FileType::kSymlink) {
    MBO_ASSIGN_OR_RETURN(const auto left, pair.left->fs->ReadLink(pair.left->path));
    MBO_ASSIGN_OR_RETURN(const auto right, pair.right->fs->ReadLink(pair.right->path));
    return ComparisonResult{.same = left == right};
  }
  return ComparisonResult{.same = true};
}

}  // namespace

ParallelCompare::ParallelCompare(std::size_t workers, bool retain_patch_inputs)
    : workers_(std::max(workers, std::size_t{1})), retain_patch_inputs_(retain_patch_inputs) {}

ParallelCompare::~ParallelCompare() {
  {
    const absl::MutexLock lock(mutex_);
    stop_ = true;
  }
  for (auto& thread : threads_) {
    thread.join();
  }
}

bool ParallelCompare::UseWorkers() const {
  std::size_t reads = 0;
  std::uint64_t bytes = 0;
  for (const auto& pair : inputs_) {
    if ((pair.left && (pair.left->fs_owner || pair.left->metadata.source == vfs::Source::kArchiveMember))
        || (pair.right && (pair.right->fs_owner || pair.right->metadata.source == vfs::Source::kArchiveMember))) {
      return false;
    }
    if (pair.left && pair.right && pair.left->metadata.type == vfs::FileType::kRegular
        && pair.right->metadata.type == vfs::FileType::kRegular && pair.left->metadata.size == pair.right->metadata.size
        && pair.left->metadata.size > kRetainedFileBytes) {
      ++reads;
      bytes = std::min<std::uint64_t>(
          4 * kRetainedFileBytes, bytes + std::min<std::uint64_t>(4 * kRetainedFileBytes, pair.left->metadata.size));
    }
  }
  return workers_ > 1 && reads > 1 && bytes >= 4 * kRetainedFileBytes;
}

bool ParallelCompare::Ready(std::size_t generation) const {
  return stop_ || generation_ != generation;
}

bool ParallelCompare::Finished() const {
  return remaining_ == 0;
}

const std::vector<absl::StatusOr<ComparisonResult>>& ParallelCompare::Compare(std::vector<ComparisonPair> inputs) {
  const absl::MutexLock lock(mutex_);
  inputs_ = std::move(inputs);
  results_.clear();
  results_.resize(inputs_.size());
  next_.store(0, std::memory_order_relaxed);
  if (!UseWorkers()) {
    EvaluateEntries();
    return results_;
  }
  const auto count = std::min(workers_, inputs_.size());
  threads_.reserve(count);
  while (threads_.size() < count) {
    threads_.emplace_back([this] { Run(); });
  }
  remaining_ = threads_.size();
  ++generation_;
  mutex_.Await(absl::Condition(this, &ParallelCompare::Finished));
  return results_;
}

void ParallelCompare::Run() {
  std::size_t generation = 0;
  for (;;) {
    {
      const auto ready = [&]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) { return Ready(generation); };
      const absl::MutexLock lock(mutex_, absl::Condition(&ready));
      if (stop_) {
        return;
      }
      generation = generation_;
    }
    EvaluateEntries();
    {
      const absl::MutexLock lock(mutex_);
      --remaining_;
    }
  }
}

void ParallelCompare::EvaluateEntries() {
  for (;;) {
    const auto index = next_.fetch_add(1, std::memory_order_relaxed);
    if (index >= inputs_.size()) {
      return;
    }
    results_.at(index) = ComparePair(inputs_.at(index), retain_patch_inputs_);
  }
}

}  // namespace xff::engine
