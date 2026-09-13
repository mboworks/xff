// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/vfs/mutations.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <utility>

#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"

namespace xff::vfs {
namespace {
// XFF_HOST_IO: the sole descriptor-write adapter for authorized output handles.
absl::Status WriteDescriptor(int fd, std::string_view content) {
  while (!content.empty()) {
    const ssize_t written = ::write(fd, content.data(), content.size());
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      return absl::ErrnoToStatus(written < 0 ? errno : EIO, "cannot write output");
    }
    content.remove_prefix(static_cast<std::size_t>(written));
  }
  return absl::OkStatus();
}

class HostOutput final : public OutputFile {
 public:
  explicit HostOutput(int fd) : fd_(fd) {}

  HostOutput(const HostOutput&) = delete;
  HostOutput& operator=(const HostOutput&) = delete;
  HostOutput(HostOutput&&) = delete;
  HostOutput& operator=(HostOutput&&) = delete;

  // XFF_HOST_IO: releases an authorized output handle.
  ~HostOutput() override { ::close(fd_); }

  absl::Status Write(std::string_view content) override { return WriteDescriptor(fd_, content); }

 private:
  int fd_;
};
}  // namespace

absl::Status MutationPolicy::Write(bool replaces) const {
  if (block_writing) {
    return absl::PermissionDeniedError("blocked writing");
  }
  if (replaces && block_overwrite) {
    return absl::PermissionDeniedError("blocked overwrite");
  }
  if (dry_run) {
    return absl::FailedPreconditionError("dry run cannot perform a write");
  }
  return absl::OkStatus();
}

absl::Status MutationPolicy::Delete() const {
  if (block_deletion) {
    return absl::PermissionDeniedError("blocked deletion");
  }
  if (dry_run) {
    return absl::FailedPreconditionError("dry run cannot perform deletion");
  }
  return absl::OkStatus();
}

// XFF_HOST_IO: checked named output creation, atomically excluding existing entries when required.
absl::StatusOr<std::unique_ptr<OutputFile>> OpenHostOutput(
    std::string_view path,
    bool exclusive,
    const MutationPolicy& policy) {
  if (auto status = policy.Write(); !status.ok()) {
    return status;
  }
  const std::string name(path);
  // POSIX open requires a variadic mode argument when creating a file.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int fd = ::open(
      name.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | ((exclusive || policy.block_overwrite) ? O_EXCL : O_TRUNC), 0666);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, absl::StrCat("cannot open output ", path));
  }
  return std::make_unique<HostOutput>(fd);
}

// XFF_HOST_IO: checked deletion of one caller-selected entry.
absl::Status RemoveHostEntry(std::string_view path, const MutationPolicy& policy) {
  if (auto status = policy.Delete(); !status.ok()) {
    return status;
  }
  return ::remove(std::string(path).c_str()) == 0 ? absl::OkStatus()
                                                  : absl::ErrnoToStatus(errno, "cannot remove entry");
}

// XFF_HOST_IO: checked directory creation, used only by the mount-root adapter.
absl::Status CreateHostDirectories(std::string_view path, const MutationPolicy& policy) {
  if (auto status = policy.Write(); !status.ok()) {
    return status;
  }
  std::error_code error;
  // XFF_HOST_IO: checked directory creation.
  std::filesystem::create_directories(std::string(path), error);
  return error ? absl::ErrnoToStatus(error.value(), "cannot create directories") : absl::OkStatus();
}

// XFF_HOST_IO: checked recursive removal of explicitly identified abandoned mount roots.
absl::Status RemoveHostTree(std::string_view path, const MutationPolicy& policy) {
  if (auto status = policy.Delete(); !status.ok()) {
    return status;
  }
  std::error_code error;
  // XFF_HOST_IO: checked stale-root removal.
  std::filesystem::remove_all(std::string(path), error);
  return error ? absl::ErrnoToStatus(error.value(), "cannot remove tree") : absl::OkStatus();
}

TemporaryOutput::TemporaryOutput(
    std::unique_ptr<TemporaryDirectory> directory,
    std::string path,
    int fd,
    MutationPolicy policy)
    : path_(std::move(path)), fd_(fd), policy_(policy), directory_(std::move(directory)) {}

// XFF_HOST_IO: creates output within an exclusively owned private scratch directory.
absl::StatusOr<std::unique_ptr<TemporaryOutput>> TemporaryOutput::Create(
    std::string_view prefix,
    MutationPolicy policy) {
  MBO_ASSIGN_OR_RETURN(auto directory, TemporaryDirectory::Create(prefix, policy));
  const std::string path = directory->Path() + "/output";
  // POSIX open requires a variadic mode argument when creating a file.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "cannot create temporary output");
  }
  return std::unique_ptr<TemporaryOutput>(new TemporaryOutput(std::move(directory), path, fd, policy));
}

// XFF_HOST_IO: closes the authorized descriptor; its private directory owns scratch cleanup.
TemporaryOutput::~TemporaryOutput() {
  ::close(fd_);
}

absl::Status TemporaryOutput::Write(std::string_view content) const {
  if (published_) {
    return absl::FailedPreconditionError("cannot write after publication");
  }
  return WriteDescriptor(fd_, content);
}

// XFF_HOST_IO: publication atomically enforces no-overwrite, including symlink collisions.
absl::Status TemporaryOutput::Publish(std::string_view target) {
  if (published_) {
    return absl::FailedPreconditionError("temporary output was already published");
  }
  if (auto status = policy_.Write(); !status.ok()) {
    return status;
  }
  if (::fsync(fd_) != 0) {
    return absl::ErrnoToStatus(errno, "cannot flush temporary output");
  }
  const std::string name(target);
  const int result =
      policy_.block_overwrite ? ::link(path_.c_str(), name.c_str()) : ::rename(path_.c_str(), name.c_str());
  if (result != 0) {
    return absl::ErrnoToStatus(errno, absl::StrCat("cannot publish ", target));
  }
  published_ = true;
  return absl::OkStatus();
}

// XFF_HOST_IO: sets metadata only on the newly created owned handle, never an existing target path.
absl::Status TemporaryOutput::SetPermissions(unsigned int mode) const {
  return ::fchmod(fd_, static_cast<mode_t>(mode)) == 0 ? absl::OkStatus()
                                                       : absl::ErrnoToStatus(errno, "cannot set output permissions");
}

TemporaryDirectory::TemporaryDirectory(std::string path) : path_(std::move(path)) {}

// XFF_HOST_IO: exclusively creates an owned scratch directory after checking writing permission.
absl::StatusOr<std::unique_ptr<TemporaryDirectory>> TemporaryDirectory::Create(
    std::string_view prefix,
    MutationPolicy policy) {
  if (auto status = policy.Write(); !status.ok()) {
    return status;
  }
  std::string path = absl::StrCat(prefix, "-XXXXXX");
  if (::mkdtemp(path.data()) == nullptr) {
    return absl::ErrnoToStatus(errno, "cannot create temporary directory");
  }
  return std::unique_ptr<TemporaryDirectory>(new TemporaryDirectory(std::move(path)));
}

// XFF_HOST_IO: recursive cleanup is limited to the scratch directory minted by Create.
TemporaryDirectory::~TemporaryDirectory() {
  std::error_code error;
  // XFF_HOST_IO: cleanup of owned scratch storage.
  std::filesystem::remove_all(path_, error);
}
}  // namespace xff::vfs
