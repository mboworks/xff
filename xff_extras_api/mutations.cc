// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/vfs/mutations.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
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

absl::Status AuthorizeScratch(const MutationPolicy& policy) {
  MBO_RETURN_IF_ERROR(policy.Write());
  // Archive writing includes its private staging; ordinary scratch also needs directory creation.
  return policy.archive ? absl::OkStatus() : policy.CreateDirectory();
}

absl::Status RemoveChildren(int fd, std::string_view path, const MutationPolicy& policy, bool owned);

// XFF_HOST_IO: checks each affected directory entry and never follows a symlink while deleting.
absl::Status RemoveAt(
    int parent,
    const std::string& name,
    const std::string& path,
    const MutationPolicy& policy,
    bool owned) {
  struct stat metadata{};
  if (::fstatat(parent, name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
    return absl::ErrnoToStatus(errno, "cannot inspect deletion target");
  }
  const bool directory = S_ISDIR(metadata.st_mode);
  if (!owned) {
    auto effective = policy;
    if (policy.directories) {
      MBO_ASSIGN_OR_RETURN(const auto target, policy.directories->Resolve(path, policy));
      effective = target.policy;
    }
    MBO_RETURN_IF_ERROR(directory ? effective.DeleteDirectory() : effective.Delete());
  }
  if (directory) {
    // POSIX open/openat has a variadic ABI, including read-only directory opens.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    const int fd = ::openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
      return absl::ErrnoToStatus(errno, "cannot open deletion directory");
    }
    const DirectoryPolicy::Target handle(fd, {}, {});
    MBO_RETURN_IF_ERROR(RemoveChildren(handle.parent_fd, path, policy, owned));
  }
  return ::unlinkat(parent, name.c_str(), directory ? AT_REMOVEDIR : 0) == 0
             ? absl::OkStatus()
             : absl::ErrnoToStatus(errno, "cannot remove directory entry");
}

// XFF_HOST_IO: enumerates an anchored directory for per-entry checked removal or owned cleanup.
absl::Status RemoveChildren(int fd, std::string_view path, const MutationPolicy& policy, bool owned) {
  // POSIX fcntl requires a variadic descriptor argument.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int copy = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
  if (copy < 0) {
    return absl::ErrnoToStatus(errno, "cannot retain cleanup directory");
  }
  // XFF_ABI_POINTER: POSIX fdopendir owns a DIR stream.
  DIR* stream = ::fdopendir(copy);
  if (stream == nullptr) {
    ::close(copy);
    return absl::ErrnoToStatus(errno, "cannot enumerate cleanup directory");
  }
  std::vector<std::string> names;
  errno = 0;
  // The directory stream is private to this invocation.
  // XFF_ABI_POINTER: POSIX readdir returns a borrowed directory entry.
  // NOLINTNEXTLINE(concurrency-mt-unsafe): private stream; XFF_ABI_POINTER: POSIX borrowed entry.
  while (const struct dirent* entry = ::readdir(stream)) {
    const std::string_view name(std::data(entry->d_name));
    if (name != "." && name != "..") {
      names.emplace_back(name);
    }
    errno = 0;
  }
  const int read_error = errno;
  ::closedir(stream);
  if (read_error != 0) {
    return absl::ErrnoToStatus(read_error, "cannot read cleanup directory");
  }
  for (const auto& name : names) {
    MBO_RETURN_IF_ERROR(RemoveAt(fd, name, absl::StrCat(path, "/", name), policy, owned));
  }
  return absl::OkStatus();
}

// XFF_HOST_IO: creates a new inode before replacing a scoped output entry, preserving hard-link targets.
absl::StatusOr<int> OpenScopedOutput(const DirectoryPolicy::Target& target, bool exclusive) {
  MBO_RETURN_IF_ERROR(target.policy.Write());
  // POSIX openat requires a variadic creation mode.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int created = ::openat(target.parent_fd, target.name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
  if (created >= 0) {
    return created;
  }
  if (errno != EEXIST || exclusive || target.policy.block_overwrite) {
    return absl::ErrnoToStatus(errno, "cannot create scoped output");
  }
  MBO_RETURN_IF_ERROR(target.policy.Write(true));
  static std::atomic<std::uint64_t> sequence{0};
  for (int attempt = 0; attempt < 100; ++attempt) {
    const std::string staging = absl::StrCat(".xff-output-", ::getpid(), "-", sequence.fetch_add(1));
    // POSIX openat requires a variadic creation mode.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    const int fd = ::openat(target.parent_fd, staging.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0) {
      if (errno == EEXIST) {
        continue;
      }
      return absl::ErrnoToStatus(errno, "cannot create scoped replacement");
    }
    if (::renameat(target.parent_fd, staging.c_str(), target.parent_fd, target.name.c_str()) == 0) {
      return fd;
    }
    const int saved_error = errno;
    ::close(fd);
    ::unlinkat(target.parent_fd, staging.c_str(), 0);
    return absl::ErrnoToStatus(saved_error, "cannot publish scoped replacement");
  }
  return absl::AlreadyExistsError("cannot reserve scoped replacement");
}
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

absl::Status MutationPolicy::CreateDirectory() const {
  if (block_directory_creation) {
    return absl::PermissionDeniedError("blocked directory creation");
  }
  if (dry_run) {
    return absl::FailedPreconditionError("dry run cannot create directories");
  }
  return absl::OkStatus();
}

absl::Status MutationPolicy::DeleteDirectory() const {
  if (block_directory_deletion) {
    return absl::PermissionDeniedError("blocked directory deletion");
  }
  if (dry_run) {
    return absl::FailedPreconditionError("dry run cannot delete directories");
  }
  return absl::OkStatus();
}

// XFF_HOST_IO: checked named output creation, atomically excluding existing entries when required.
absl::StatusOr<std::unique_ptr<OutputFile>> OpenHostOutput(
    std::string_view path,
    bool exclusive,
    const MutationPolicy& policy) {
  if (policy.directories) {
    MBO_ASSIGN_OR_RETURN(const auto target, policy.directories->Resolve(path, policy));
    MBO_ASSIGN_OR_RETURN(const int fd, OpenScopedOutput(target, exclusive));
    return std::make_unique<HostOutput>(fd);
  }
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
  if (policy.directories) {
    MBO_ASSIGN_OR_RETURN(const auto target, policy.directories->Resolve(path, policy));
    struct stat metadata{};
    if (::fstatat(target.parent_fd, target.name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
      return absl::ErrnoToStatus(errno, "cannot inspect scoped deletion");
    }
    const bool directory = S_ISDIR(metadata.st_mode);
    MBO_RETURN_IF_ERROR(directory ? target.policy.DeleteDirectory() : target.policy.Delete());
    return ::unlinkat(target.parent_fd, target.name.c_str(), directory ? AT_REMOVEDIR : 0) == 0
               ? absl::OkStatus()
               : absl::ErrnoToStatus(errno, "cannot remove scoped entry");
  }
  struct stat metadata{};
  // XFF_HOST_IO: inspects the entry without following its symlink before checked removal.
  if (::lstat(std::string(path).c_str(), &metadata) != 0) {
    return absl::ErrnoToStatus(errno, "cannot inspect deletion target");
  }
  MBO_RETURN_IF_ERROR(S_ISDIR(metadata.st_mode) ? policy.DeleteDirectory() : policy.Delete());
  return ::remove(std::string(path).c_str()) == 0 ? absl::OkStatus()
                                                  : absl::ErrnoToStatus(errno, "cannot remove entry");
}

// XFF_HOST_IO: checked directory creation, used only by the mount-root adapter.
absl::Status CreateHostDirectories(std::string_view path, const MutationPolicy& policy) {
  if (!policy.directories) {
    MBO_RETURN_IF_ERROR(policy.CreateDirectory());
    std::error_code error;
    // XFF_HOST_IO: checked directory creation.
    std::filesystem::create_directories(std::string(path), error);
    return error ? absl::ErrnoToStatus(error.value(), "cannot create directories") : absl::OkStatus();
  }
  auto target = policy.directories->Resolve(path, policy);
  if (!target.ok() && target.status().code() == absl::StatusCode::kNotFound) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty() || parent == path) {
      return target.status();
    }
    MBO_RETURN_IF_ERROR(CreateHostDirectories(parent.string(), policy));
    return CreateHostDirectories(path, policy);
  }
  MBO_ASSIGN_OR_RETURN(const auto destination, std::move(target));
  struct stat metadata{};
  if (::fstatat(destination.parent_fd, destination.name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) == 0) {
    return S_ISDIR(metadata.st_mode) ? absl::OkStatus()
                                     : absl::FailedPreconditionError("directory target is not a directory");
  }
  MBO_RETURN_IF_ERROR(destination.policy.CreateDirectory());
  return ::mkdirat(destination.parent_fd, destination.name.c_str(), 0777) == 0
             ? absl::OkStatus()
             : absl::ErrnoToStatus(errno, "cannot create scoped directory");
}

// XFF_HOST_IO: checked recursive removal of explicitly identified abandoned mount roots.
absl::Status RemoveHostTree(std::string_view path, const MutationPolicy& policy) {
  if (policy.directories) {
    MBO_ASSIGN_OR_RETURN(const auto target, policy.directories->Resolve(path, policy));
    return RemoveAt(target.parent_fd, target.name, std::string(path), policy, false);
  }
  const std::filesystem::path name(path);
  const auto parent = name.parent_path().empty() ? std::filesystem::path(".") : name.parent_path();
  // POSIX open/openat has a variadic ABI, including read-only directory opens.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "cannot open deletion parent");
  }
  const DirectoryPolicy::Target target(fd, name.filename().string(), policy);
  return RemoveAt(target.parent_fd, target.name, std::string(path), policy, false);
}

TemporaryOutput::TemporaryOutput(
    std::unique_ptr<TemporaryDirectory> directory,
    std::string path,
    int fd,
    MutationPolicy policy)
    : path_(std::move(path)), fd_(fd), policy_(std::move(policy)), directory_(std::move(directory)) {}

// XFF_HOST_IO: creates output within an exclusively owned private scratch directory.
absl::StatusOr<std::unique_ptr<TemporaryOutput>> TemporaryOutput::Create(
    std::string_view prefix,
    MutationPolicy policy) {
  MBO_ASSIGN_OR_RETURN(auto directory, TemporaryDirectory::Create(prefix, policy));
  const std::string path = directory->Path() + "/output";
  // POSIX open requires a variadic mode argument when creating a file.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int fd = ::openat(directory->Fd(), "output", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "cannot create temporary output");
  }
  return std::unique_ptr<TemporaryOutput>(new TemporaryOutput(std::move(directory), path, fd, std::move(policy)));
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
  if (::fsync(fd_) != 0) {
    return absl::ErrnoToStatus(errno, "cannot flush temporary output");
  }
  int result = 0;
  if (policy_.directories) {
    MBO_ASSIGN_OR_RETURN(const auto destination, policy_.directories->Resolve(target, policy_));
    MBO_RETURN_IF_ERROR(destination.policy.Write());
    result = destination.policy.block_overwrite
                 ? ::linkat(directory_->Fd(), "output", destination.parent_fd, destination.name.c_str(), 0)
                 : ::renameat(directory_->Fd(), "output", destination.parent_fd, destination.name.c_str());
  } else {
    MBO_RETURN_IF_ERROR(policy_.Write());
    const std::string name(target);
    result = policy_.block_overwrite ? ::link(path_.c_str(), name.c_str()) : ::rename(path_.c_str(), name.c_str());
  }
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

TemporaryDirectory::TemporaryDirectory(std::string path, int parent_fd, std::string name, int fd)
    : path_(std::move(path)), parent_fd_(parent_fd), name_(std::move(name)), fd_(fd) {}

// XFF_HOST_IO: exclusively creates private staging storage for an authorized write.
absl::StatusOr<std::unique_ptr<TemporaryDirectory>> TemporaryDirectory::Create(
    std::string_view prefix,
    const MutationPolicy& policy) {
  if (policy.directories) {
    MBO_ASSIGN_OR_RETURN(const auto target, policy.directories->Resolve(prefix, policy));
    MBO_RETURN_IF_ERROR(AuthorizeScratch(target.policy));
    return CreateScoped(prefix, target);
  }
  MBO_RETURN_IF_ERROR(AuthorizeScratch(policy));
  std::string path = absl::StrCat(prefix, "-XXXXXX");
  if (::mkdtemp(path.data()) == nullptr) {
    return absl::ErrnoToStatus(errno, "cannot create temporary directory");
  }
  const std::filesystem::path location(path);
  const auto parent_path = location.parent_path().empty() ? std::filesystem::path(".") : location.parent_path();
  // POSIX open/openat has a variadic ABI, including read-only directory opens.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int parent = ::open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  // POSIX open/openat has a variadic ABI.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0 || parent < 0) {
    if (fd >= 0) {
      ::close(fd);
    }
    if (parent >= 0) {
      ::close(parent);
    }
    return absl::ErrnoToStatus(errno, "cannot retain scratch directory");
  }
  return std::unique_ptr<TemporaryDirectory>(new TemporaryDirectory(path, parent, location.filename().string(), fd));
}

// XFF_HOST_IO: reserves and retains private staging storage beneath an already checked parent.
absl::StatusOr<std::unique_ptr<TemporaryDirectory>> TemporaryDirectory::CreateScoped(
    std::string_view prefix,
    const DirectoryPolicy::Target& target) {
  static std::atomic<std::uint64_t> sequence{0};
  for (int attempt = 0; attempt < 100; ++attempt) {
    const std::string name = absl::StrCat(target.name, "-", ::getpid(), "-", sequence.fetch_add(1));
    if (::mkdirat(target.parent_fd, name.c_str(), 0700) != 0) {
      if (errno == EEXIST) {
        continue;
      }
      return absl::ErrnoToStatus(errno, "cannot create scoped scratch directory");
    }
    // POSIX open/openat has a variadic ABI, including read-only directory opens.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    const int fd = ::openat(target.parent_fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    // POSIX fcntl requires a variadic descriptor argument.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    const int parent = ::fcntl(target.parent_fd, F_DUPFD_CLOEXEC, 0);
    if (fd < 0 || parent < 0) {
      if (fd >= 0) {
        ::close(fd);
      }
      if (parent >= 0) {
        ::close(parent);
      }
      return absl::ErrnoToStatus(errno, "cannot retain scratch directory");
    }
    return std::unique_ptr<TemporaryDirectory>(
        new TemporaryDirectory((std::filesystem::path(prefix).parent_path() / name).string(), parent, name, fd));
  }
  return absl::AlreadyExistsError("cannot reserve scratch directory");
}

// XFF_HOST_IO: cleanup is confined to the retained private directory and never follows child links.
TemporaryDirectory::~TemporaryDirectory() {
  (void)RemoveChildren(fd_, path_, {}, true);
  struct stat held{};
  struct stat named{};
  if (::fstat(fd_, &held) == 0 && ::fstatat(parent_fd_, name_.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0
      && held.st_dev == named.st_dev && held.st_ino == named.st_ino) {
    ::unlinkat(parent_fd_, name_.c_str(), AT_REMOVEDIR);
  }
  ::close(fd_);
  ::close(parent_fd_);
}
}  // namespace xff::vfs
