// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_VFS_MUTATIONS_H_
#define XFF_VFS_MUTATIONS_H_

#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/vfs/filesystem.h"

namespace xff::vfs {

// Immutable permissions passed down to every mutating host adapter. A dry-run adapter fails
// closed; previews belong to the caller and never acquire a mutation handle.
struct MutationPolicy {
  bool block_deletion = false;
  bool block_writing = false;
  bool block_overwrite = false;
  bool dry_run = false;
  absl::Status Write(bool replaces = false) const;
  absl::Status Delete() const;
};

absl::StatusOr<std::unique_ptr<OutputFile>> OpenHostOutput(
    std::string_view path,
    bool exclusive,
    const MutationPolicy& policy);
absl::Status CreateHostDirectories(std::string_view path, const MutationPolicy& policy);
absl::Status RemoveHostTree(std::string_view path, const MutationPolicy& policy);
absl::Status RemoveHostEntry(std::string_view path, const MutationPolicy& policy);

// A capability handle for a uniquely created temporary file. Its own cleanup is allowed even
// when user-directed deletion is blocked. Fd is a narrow adapter for libarchive's descriptor API.
class TemporaryDirectory;

class TemporaryOutput {
 public:
  static absl::StatusOr<std::unique_ptr<TemporaryOutput>> Create(std::string_view prefix, MutationPolicy policy);
  ~TemporaryOutput();
  TemporaryOutput(const TemporaryOutput&) = delete;
  TemporaryOutput& operator=(const TemporaryOutput&) = delete;

  int Fd() const { return fd_; }

  const std::string& Path() const { return path_; }

  absl::Status Write(std::string_view content);
  absl::Status Publish(std::string_view target);
  absl::Status SetPermissions(unsigned int mode) const;

 private:
  TemporaryOutput(std::unique_ptr<TemporaryDirectory> directory, std::string path, int fd, MutationPolicy policy);
  std::string path_;
  int fd_;
  MutationPolicy policy_;
  std::unique_ptr<TemporaryDirectory> directory_;
  bool published_ = false;
};

// Owned scratch directory; recursive cleanup cannot be used on caller-chosen existing paths.
class TemporaryDirectory {
 public:
  static absl::StatusOr<std::unique_ptr<TemporaryDirectory>> Create(std::string_view prefix, MutationPolicy policy);
  ~TemporaryDirectory();
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  const std::string& Path() const { return path_; }

 private:
  explicit TemporaryDirectory(std::string path);
  std::string path_;
};

}  // namespace xff::vfs
#endif  // XFF_VFS_MUTATIONS_H_
