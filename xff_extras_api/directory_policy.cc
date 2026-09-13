// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <utility>

#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/vfs/mutations.h"

namespace xff::vfs {
namespace {
// XFF_HOST_IO: resolves the working directory without opening a mutation handle.
absl::StatusOr<std::string> AbsolutePath(std::string_view path) {
  if (path.empty() || path.contains('\0')) {
    return absl::InvalidArgumentError("empty or NUL-containing mutation path");
  }
  const std::filesystem::path input(path);
  for (const auto& part : input) {
    if (part == "..") {
      return absl::PermissionDeniedError("parent traversal is forbidden with directory policy");
    }
  }
  std::error_code error;
  const auto absolute = std::filesystem::absolute(input, error);
  if (error) {
    return absl::ErrnoToStatus(error.value(), "cannot resolve mutation path");
  }
  auto result = absolute.lexically_normal().string();
  while (result.size() > 1 && result.back() == '/') {
    result.pop_back();
  }
  return result;
}

// XFF_HOST_IO: opens only directory components, refusing symlink traversal at each step.
absl::StatusOr<int> Descend(int start, const std::filesystem::path& relative) {
  // POSIX fcntl requires a variadic descriptor argument.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  int current = ::fcntl(start, F_DUPFD_CLOEXEC, 0);
  if (current < 0) {
    return absl::ErrnoToStatus(errno, "cannot retain policy directory");
  }
  for (const auto& part : relative) {
    if (part.empty() || part == ".") {
      continue;
    }
    // POSIX open/openat has a variadic ABI, including read-only directory opens.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    const int next = ::openat(current, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    const int saved_error = errno;
    ::close(current);
    if (next < 0) {
      return absl::ErrnoToStatus(saved_error, "cannot traverse policy directory (symlinks are forbidden)");
    }
    current = next;
  }
  return current;
}

MutationPolicy WithBlocks(MutationPolicy policy, const std::array<bool, 5>& blocks) {
  policy.block_writing |= blocks[0];
  policy.block_overwrite |= blocks[1];
  policy.block_deletion |= blocks[2];
  policy.block_directory_creation |= blocks[3];
  policy.block_directory_deletion |= blocks[4];
  return policy;
}
}  // namespace

DirectoryPolicy::Target::Target(int fd, std::string basename, MutationPolicy permissions)
    : parent_fd(fd), name(std::move(basename)), policy(std::move(permissions)) {}

// XFF_HOST_IO: releases an anchored parent-directory descriptor.
DirectoryPolicy::Target::~Target() {
  if (parent_fd >= 0) {
    ::close(parent_fd);
  }
}

DirectoryPolicy::Target::Target(Target&& other) noexcept
    : parent_fd(std::exchange(other.parent_fd, -1)), name(std::move(other.name)), policy(std::move(other.policy)) {}

DirectoryPolicy::DirectoryPolicy(std::vector<Root> roots) : roots_(std::move(roots)) {}

// XFF_HOST_IO: releases the immutable roots retained for this run.
DirectoryPolicy::~DirectoryPolicy() {
  for (const auto& root : roots_) {
    ::close(root.fd);
  }
}

// XFF_HOST_IO: pins explicitly declared, existing directory roots before evaluation.
absl::StatusOr<std::shared_ptr<const DirectoryPolicy>> DirectoryPolicy::Create(std::vector<DirectoryRule> rules) {
  auto policy = std::shared_ptr<DirectoryPolicy>(new DirectoryPolicy({}));
  // POSIX open/openat has a variadic ABI, including read-only directory opens.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int filesystem_root = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (filesystem_root < 0) {
    return absl::ErrnoToStatus(errno, "cannot open filesystem root");
  }
  const Target anchor(filesystem_root, {}, {});
  for (auto& rule : rules) {
    if (!std::filesystem::path(rule.root).is_absolute()) {
      return absl::InvalidArgumentError("policy roots must be absolute paths");
    }
    MBO_ASSIGN_OR_RETURN(rule.root, AbsolutePath(rule.root));
    if (rule.root == "/") {
      return absl::InvalidArgumentError("filesystem root cannot be a temporary or output root");
    }
    MBO_ASSIGN_OR_RETURN(const int fd, Descend(anchor.parent_fd, std::filesystem::path(rule.root).relative_path()));
    policy->roots_.push_back({.rule = std::move(rule), .fd = fd});
  }
  return policy;
}

// XFF_HOST_IO: protects the pinned root identities, including after an external rename.
absl::StatusOr<DirectoryPolicy::Target> DirectoryPolicy::GuardTarget(Target target) const {
  struct stat candidate{};
  if (::fstatat(target.parent_fd, target.name.c_str(), &candidate, AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno == ENOENT) {
      return target;
    }
    return absl::ErrnoToStatus(errno, "cannot inspect mutation target");
  }
  if (!S_ISDIR(candidate.st_mode)) {
    return target;
  }
  for (const auto& root : roots_) {
    struct stat protected_root{};
    if (::fstat(root.fd, &protected_root) != 0) {
      return absl::ErrnoToStatus(errno, "cannot inspect protected root");
    }
    if (candidate.st_dev == protected_root.st_dev && candidate.st_ino == protected_root.st_ino) {
      return absl::PermissionDeniedError("declared directory root identity is protected");
    }
  }
  return target;
}

// XFF_HOST_IO: selects permissions and an anchored parent using the same no-follow path traversal.
absl::StatusOr<DirectoryPolicy::Target> DirectoryPolicy::Resolve(std::string_view path, const MutationPolicy& ordinary)
    const {
  MBO_ASSIGN_OR_RETURN(const std::string absolute, AbsolutePath(path));
  MutationPolicy selected = ordinary;
  selected.directories.reset();
  int anchor = -1;
  std::size_t prefix_length = 0;
  bool matched = false;
  for (const auto& root : roots_) {
    if (absolute == root.rule.root) {
      return absl::PermissionDeniedError("declared directory root is protected");
    }
    if (!absolute.starts_with(root.rule.root + "/")) {
      continue;
    }
    if (!matched && !ordinary.archive) {
      selected = {.dry_run = ordinary.dry_run};
    }
    matched = true;
    selected = WithBlocks(std::move(selected), root.rule.blocks);
    if (root.rule.root.size() > prefix_length) {
      prefix_length = root.rule.root.size();
      anchor = root.fd;
    }
  }
  if (anchor < 0) {
    if (ordinary.outside_directory_blocks) {
      selected = WithBlocks(std::move(selected), *ordinary.outside_directory_blocks);
    }
    // POSIX open/openat has a variadic ABI, including read-only directory opens.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    const int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
      return absl::ErrnoToStatus(errno, "cannot open filesystem root");
    }
    const Target filesystem_root(fd, {}, {});
    const std::filesystem::path relative = std::filesystem::path(absolute).relative_path();
    MBO_ASSIGN_OR_RETURN(const int parent, Descend(filesystem_root.parent_fd, relative.parent_path()));
    return GuardTarget(Target(parent, relative.filename().string(), std::move(selected)));
  }
  const std::filesystem::path relative = absolute.substr(prefix_length + 1);
  MBO_ASSIGN_OR_RETURN(const int parent, Descend(anchor, relative.parent_path()));
  return GuardTarget(Target(parent, relative.filename().string(), std::move(selected)));
}
}  // namespace xff::vfs
