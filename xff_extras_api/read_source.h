// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_VFS_READ_SOURCE_H_
#define XFF_VFS_READ_SOURCE_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

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
  virtual ~ReadSource() = default;
  virtual absl::StatusOr<std::unique_ptr<ReadStream>> Open() const = 0;
};

using SharedReadSource = std::shared_ptr<const ReadSource>;

SharedReadSource MemoryReadSource(std::string bytes);
// Opens only read-only host handles. No temporary files or subprocesses are involved.
SharedReadSource HostReadSource(std::string path);
// Bounded materialization for legacy consumers; streaming container readers do not use this.
absl::StatusOr<std::string> ReadSourceBytes(const ReadSource& source, std::size_t max_bytes);

}  // namespace xff::vfs
#endif  // XFF_VFS_READ_SOURCE_H_
