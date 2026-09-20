// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_VFS_READ_SOURCE_H_
#define XFF_VFS_READ_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "xff/vfs/read_budget.h"

namespace xff::vfs {

// An owned sequential cursor. Empty data means EOF; failures are never reported as EOF.
// Cursors are independent and single-threaded. Read must return at most max_bytes bytes.
class ReadStream {
 public:
  virtual ~ReadStream() = default;
  virtual absl::StatusOr<std::string> Read(std::size_t max_bytes) = 0;
};

// An immutable, restartable source. Open can be called concurrently; each cursor owns everything
// it needs, including any parent container, without retaining a borrowed FileSystem reference.
class ReadSource {
 public:
  explicit ReadSource(std::shared_ptr<ReadBudget> budget = std::make_shared<ReadBudget>())
      : budget_(std::move(budget)) {}

  virtual ~ReadSource() = default;
  virtual absl::StatusOr<std::unique_ptr<ReadStream>> Open() const = 0;
  // Native sources seek directly; sequential sources replay within the shared work budget.
  // Positions beyond EOF yield an empty stream. Failures remain errors.
  virtual absl::StatusOr<std::unique_ptr<ReadStream>> OpenAt(std::uint64_t offset) const;
  // Native sources use metadata; other sources scan within the shared replay budget.
  virtual absl::StatusOr<std::uint64_t> Size() const;

  const std::shared_ptr<ReadBudget>& Budget() const { return budget_; }

 private:
  std::shared_ptr<ReadBudget> budget_;
};

using SharedReadSource = std::shared_ptr<const ReadSource>;

SharedReadSource MemoryReadSource(
    std::string bytes,
    std::shared_ptr<ReadBudget> budget = std::make_shared<ReadBudget>());
// Opens only read-only host handles. No temporary files or subprocesses are involved.
SharedReadSource HostReadSource(std::string path, std::shared_ptr<ReadBudget> budget = std::make_shared<ReadBudget>());

// A retained range keeps its memory reservation until the block is destroyed.
struct ReadBlock {
  ReadBudget::Reservation reservation;
  std::string bytes;
};

// At most 64 MiB per request, possibly shorter at EOF. No implicit disk spooling.
absl::StatusOr<ReadBlock> ReadSourceRange(const ReadSource& source, std::uint64_t offset, std::size_t count);

// Bounded materialization for legacy consumers; streaming container readers do not use this.
absl::StatusOr<std::string> ReadSourceBytes(const ReadSource& source, std::size_t max_bytes);

}  // namespace xff::vfs
#endif  // XFF_VFS_READ_SOURCE_H_
