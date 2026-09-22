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

#ifndef XFF_VFS_FILESYSTEM_H_
#define XFF_VFS_FILESYSTEM_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/read_source.h"

namespace xff::vfs {

struct MutationPolicy;

// Basic stat fields suffice unless a consumer explicitly needs creation time.
// Backends may supply additional fields when they are available at no extra cost.
enum class MetadataFields { kBasic, kBirthTime };

// Permission to probe with `FileSystem::Access` (find's -readable/-writable/
// -executable). Platform-neutral; LocalFs maps these to R_OK/W_OK/X_OK.
enum class AccessMode { kRead, kWrite, kExecute };

// An owned output stream. The backend enforces exclusive creation when requested; retaining
// this handle prevents subsequent records from reopening a path that an attacker could replace.
class OutputFile {
 public:
  OutputFile() = default;
  virtual ~OutputFile() = default;
  OutputFile(const OutputFile&) = delete;
  OutputFile& operator=(const OutputFile&) = delete;
  OutputFile(OutputFile&&) = delete;
  OutputFile& operator=(OutputFile&&) = delete;
  virtual absl::Status Write(std::string_view content) = 0;
};

// Abstraction over a source of files. `LocalFs` (the real filesystem) is the
// only backend today; archive and remote backends slot in behind this same
// interface as read-only virtual entries (design.md "Virtual entries").
//
// Implementations must be safe to call concurrently from multiple threads, as
// the engine traverses in parallel (design.md "Determinism" / "Parallel exec").
class FileSystem {
 public:
  FileSystem() = default;
  virtual ~FileSystem() = default;
  FileSystem(const FileSystem&) = default;
  FileSystem& operator=(const FileSystem&) = default;
  FileSystem(FileSystem&&) noexcept = default;
  FileSystem& operator=(FileSystem&&) noexcept = default;

  // Lists the direct children of `dir`: no recursion, and excluding `.`/`..`.
  // Order is unspecified (the engine imposes `--sort` when determinism is
  // requested). A per-directory failure (not a directory, permission denied,
  // ...) is returned as an error; the engine continues traversal and reflects
  // it in the exit code (design.md "Exit-code model"), rather than aborting.
  virtual absl::StatusOr<std::vector<Entry>> ReadDir(std::string_view dir) const = 0;

  // Returns metadata for `path`. When `path` is a symlink, `follow_symlinks`
  // selects `stat` (follow the link, like `-L`) vs `lstat` (the link itself,
  // like the default `-P`).
  virtual absl::StatusOr<Metadata> Stat(std::string_view path, bool follow_symlinks) const = 0;

  // Field-aware lookup. The default preserves complete metadata for existing backends.
  virtual absl::StatusOr<Metadata> StatFields(std::string_view path, bool follow_symlinks, MetadataFields fields) const;

  // Removes `path` (a file, symlink, or empty directory; find's -delete relies
  // on -depth to empty directories first). Read-only backends (archive/remote)
  // return an error. The object is not mutated, only the underlying source.
  virtual absl::Status Remove(std::string_view path) const = 0;

  // Writes `content` to `path`, replacing any existing file. Read-only backends return an error.
  virtual absl::Status WriteContent(std::string_view path, std::string_view content) const;

  // Opens named output, atomically rejecting any existing directory entry when exclusive is true.
  // Read-only backends fail; there is no host-filesystem fallback.
  virtual absl::StatusOr<std::unique_ptr<OutputFile>> OpenOutput(std::string_view path, bool exclusive) const;

  virtual bool SupportsDirectoryPolicies() const { return false; }

  // Policy-aware mutation entry points. Backends without anchored path support reject scopes.
  virtual absl::Status RemoveControlled(std::string_view path, const MutationPolicy& policy) const;
  virtual absl::StatusOr<std::unique_ptr<OutputFile>> OpenControlledOutput(
      std::string_view path,
      bool exclusive,
      const MutationPolicy& policy) const;

  // True if `path` is accessible to the current (effective) user for `mode`
  // (find's -readable/-writable/-executable). Resolves symlinks and reflects
  // the real permission check (ownership, groups, ACLs), not just mode bits.
  // A missing/unreadable path is false.
  virtual bool Access(std::string_view path, AccessMode mode) const = 0;

  // Reads the target path of the symlink at `path` (find's -lname/-ilname),
  // without resolving it. An error if `path` is not a symlink or cannot be read.
  virtual absl::StatusOr<std::string> ReadLink(std::string_view path) const = 0;

  // The filesystem type name at `path` (find's -fstype), e.g. "ext2/ext3",
  // "apfs", "tmpfs", "nfs". An error if `path` cannot be queried (`statfs`).
  virtual absl::StatusOr<std::string> FsType(std::string_view path) const = 0;

  // Whether name lookups on the volume holding `path` distinguish case: true =
  // case-sensitive (ext4 / xfs and most Linux filesystems, where `foo` and `FOO`
  // are distinct files), false = case-folding (APFS / HFS+ in their default
  // configuration, NTFS, exFAT, where they are the same file). Backs xff's
  // FS-native name matching (`--exact` opts out): the default xff style matches
  // `-name` the way the filesystem itself would, so a lookup that the OS would
  // satisfy case-insensitively also matches here. Returns an error when the
  // volume cannot be probed; the caller falls back to the conservative
  // case-sensitive (byte-exact) behaviour, which is always find-faithful.
  virtual absl::StatusOr<bool> IsCaseSensitive(std::string_view path) const = 0;

  // Reads the entire byte content of the regular file at `path` (xff's content
  // predicates -content / -icontent / -rxc / -irxc). Returns an error when the
  // path cannot be opened or read; content search treats that as a non-match
  // rather than aborting the walk. Reading the whole file is what makes those
  // predicates Cost::kExpensive. Only meaningful for regular files; the caller is
  // expected to gate on the entry type.
  virtual absl::StatusOr<std::string> ReadContent(std::string_view path) const = 0;

  // Restartable owned content. Legacy backends may materialize; archive/host backends stream.
  virtual absl::StatusOr<SharedReadSource> ContentSource(std::string_view path) const;

  // Contextual supplement to the suffix gate, e.g. an extensionless package Payload.
  virtual bool ContainerCandidate(std::string_view /*path*/) const { return false; }

  // Reads at most `length` bytes of the regular file at `path`, starting at
  // `offset`. A range extending beyond EOF is shortened; an offset at or beyond
  // EOF and a zero length both return an empty string. This seam lets format
  // probes avoid materializing an entire candidate when the backend can seek.
  //
  // The default preserves correctness for virtual backends that cannot seek
  // within an encoded member. Backends with direct random access should
  // override it so the underlying read is bounded as well as the result.
  virtual absl::StatusOr<std::string> ReadContentRange(std::string_view path, std::uint64_t offset, std::size_t length)
      const;
};

}  // namespace xff::vfs

#endif  // XFF_VFS_FILESYSTEM_H_
