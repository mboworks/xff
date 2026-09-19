// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_READ_OBSERVER_H_
#define XFF_ENGINE_READ_OBSERVER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

// Logical calls and bytes delivered through one observed VFS boundary, not storage traffic.
struct ReadObservation {
  std::uint64_t attempts = 0;
  std::uint64_t successes = 0;
  std::uint64_t bytes = 0;
};

struct ReadObservations {
  ReadObservation whole;
  ReadObservation range;
};

// Benchmark-only, read-only observer. The backend must outlive this object. Snapshot after all
// readers have joined; individual counters are atomic, but a live snapshot is not transactional.
// Range reads delegate directly, so a backend's fallback whole-file read is not double-counted.
class ReadObserver final : public vfs::FileSystem {
 public:
  explicit ReadObserver(const vfs::FileSystem& backend) : backend_(backend) {}

  ReadObservations Snapshot() const;
  absl::StatusOr<std::string> ReadContent(std::string_view path) const override;
  absl::StatusOr<std::string> ReadContentRange(std::string_view path, std::uint64_t offset, std::size_t length)
      const override;

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override;
  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool follow) const override;
  bool Access(std::string_view path, vfs::AccessMode mode) const override;
  absl::StatusOr<std::string> ReadLink(std::string_view path) const override;
  absl::StatusOr<std::string> FsType(std::string_view path) const override;
  absl::StatusOr<bool> IsCaseSensitive(std::string_view path) const override;

  absl::Status Remove(std::string_view) const override;
  absl::Status WriteContent(std::string_view, std::string_view) const override;
  absl::StatusOr<std::unique_ptr<vfs::OutputFile>> OpenOutput(std::string_view, bool) const override;
  absl::Status RemoveControlled(std::string_view, const vfs::MutationPolicy&) const override;
  absl::StatusOr<std::unique_ptr<vfs::OutputFile>> OpenControlledOutput(
      std::string_view,
      bool,
      const vfs::MutationPolicy&) const override;

 private:
  struct Counters {
    std::atomic<std::uint64_t> attempts = 0;
    std::atomic<std::uint64_t> successes = 0;
    std::atomic<std::uint64_t> bytes = 0;
    ReadObservation Snapshot() const;
    absl::StatusOr<std::string> Complete(absl::StatusOr<std::string> result);
  };

  const vfs::FileSystem& backend_;
  mutable Counters whole_;
  mutable Counters range_;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_READ_OBSERVER_H_
