// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_VFS_MUTATIONS_H_
#define XFF_VFS_MUTATIONS_H_

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/vfs/filesystem.h"

namespace xff::vfs {

struct DirectoryRule {
  std::string root;
  // File writing, overwrite, deletion, directory creation, directory deletion.
  std::array<bool, 5> blocks = {};
};

class DirectoryPolicy;

// Immutable permissions passed down to every mutating host adapter. A dry-run adapter fails
// closed; previews belong to the caller and never acquire a mutation handle.
struct MutationPolicy {
  bool block_deletion = false;
  bool block_writing = false;
  bool block_overwrite = false;
  bool dry_run = false;
  bool block_directory_creation = false;
  bool block_directory_deletion = false;
  bool archive = false;
  std::shared_ptr<const DirectoryPolicy> directories;
  std::string temporary_root;
  // Additional physical-path restrictions outside all declared roots (used by archive output).
  std::optional<std::array<bool, 5>> outside_directory_blocks;
  absl::Status Write(bool replaces = false) const;
  absl::Status Delete() const;
  absl::Status CreateDirectory() const;
  absl::Status DeleteDirectory() const;
};

// Roots are opened once before traversal. Scoped host operations retain directory descriptors;
// no symlink traversal or lexical parent traversal can acquire a scoped exception.
class DirectoryPolicy {
 public:
  static absl::StatusOr<std::shared_ptr<const DirectoryPolicy>> Create(std::vector<DirectoryRule> rules);
  ~DirectoryPolicy();
  DirectoryPolicy(const DirectoryPolicy&) = delete;
  DirectoryPolicy& operator=(const DirectoryPolicy&) = delete;
  DirectoryPolicy(DirectoryPolicy&&) = delete;
  DirectoryPolicy& operator=(DirectoryPolicy&&) = delete;

  struct Target {
    int parent_fd = -1;
    std::string name;
    MutationPolicy policy;
    Target(int fd, std::string basename, MutationPolicy permissions);
    ~Target();
    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;
    Target(Target&& other) noexcept;
    Target& operator=(Target&&) = delete;
  };

  absl::StatusOr<Target> Resolve(std::string_view path, const MutationPolicy& ordinary) const;

 private:
  struct Root {
    DirectoryRule rule;
    int fd = -1;
  };

  absl::StatusOr<Target> GuardTarget(Target target) const;
  explicit DirectoryPolicy(std::vector<Root> roots);
  std::vector<Root> roots_;
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
  TemporaryOutput(TemporaryOutput&&) = delete;
  TemporaryOutput& operator=(TemporaryOutput&&) = delete;

  int Fd() const { return fd_; }

  const std::string& Path() const { return path_; }

  absl::Status Write(std::string_view content) const;
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
  static absl::StatusOr<std::unique_ptr<TemporaryDirectory>> Create(
      std::string_view prefix,
      const MutationPolicy& policy);
  ~TemporaryDirectory();
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
  TemporaryDirectory(TemporaryDirectory&&) = delete;
  TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

  const std::string& Path() const { return path_; }

 private:
  static absl::StatusOr<std::unique_ptr<TemporaryDirectory>> CreateScoped(
      std::string_view prefix,
      const DirectoryPolicy::Target& target);
  TemporaryDirectory(std::string path, int parent_fd, std::string name, int fd);
  std::string path_;
  int parent_fd_;
  std::string name_;
  int fd_;

 public:
  // Narrow adapter for opening files and publishing from this owned directory.
  int Fd() const { return fd_; }
};

}  // namespace xff::vfs
#endif  // XFF_VFS_MUTATIONS_H_
