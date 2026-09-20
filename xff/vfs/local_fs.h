// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef XFF_VFS_LOCAL_FS_H_
#define XFF_VFS_LOCAL_FS_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::vfs {

// `FileSystem` over the real local filesystem, backed by POSIX
// `opendir`/`readdir` and `lstat`/`stat`. Birth time is populated from
// `st_birthtime` (macOS/BSD) or `statx(STATX_BTIME)` (Linux) when the kernel
// and filesystem record it; otherwise `Metadata::btime` is left empty.
//
// Stateless: every call uses its own directory handle, so a single instance is
// safe to share across traversal threads.
class LocalFs final : public FileSystem {
 public:
  absl::StatusOr<std::vector<Entry>> ReadDir(std::string_view dir) const override;
  absl::StatusOr<Metadata> Stat(std::string_view path, bool follow_symlinks) const override;
  absl::Status Remove(std::string_view path) const override;
  absl::Status WriteContent(std::string_view path, std::string_view content) const override;
  absl::StatusOr<std::unique_ptr<OutputFile>> OpenOutput(std::string_view path, bool exclusive) const override;

  bool SupportsDirectoryPolicies() const override { return true; }

  absl::Status RemoveControlled(std::string_view path, const MutationPolicy& policy) const override;
  absl::StatusOr<std::unique_ptr<OutputFile>> OpenControlledOutput(
      std::string_view path,
      bool exclusive,
      const MutationPolicy& policy) const override;
  bool Access(std::string_view path, AccessMode mode) const override;
  absl::StatusOr<std::string> ReadLink(std::string_view path) const override;
  absl::StatusOr<std::string> FsType(std::string_view path) const override;
  absl::StatusOr<bool> IsCaseSensitive(std::string_view path) const override;
  absl::StatusOr<std::string> ReadContent(std::string_view path) const override;

  absl::StatusOr<SharedReadSource> ContentSource(std::string_view path) const override {
    return HostReadSource(std::string(path));
  }

  absl::StatusOr<std::string> ReadContentRange(std::string_view path, std::uint64_t offset, std::size_t length)
      const override;
};

}  // namespace xff::vfs

#endif  // XFF_VFS_LOCAL_FS_H_
