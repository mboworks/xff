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

#include "xff/fuse/mount_root.h"

// kill() is POSIX, not part of <csignal>.
#include <signal.h>  // NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers)
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"

namespace xff::fuse {
namespace {

namespace stdfs = ::std::filesystem;

// `<base>/xff/`, the level shared across runs. Reads the environment once per call; this runs at
// startup and in tests, never on the walk's hot path.
std::string SharedBase(const MountRootOptions& options) {
  if (!options.base_override.empty()) {
    return absl::StrCat(options.base_override, "/xff");
  }
  // NOLINTNEXTLINE(concurrency-mt-unsafe): startup-only, before the walk spawns workers.
  const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
  if (runtime_dir != nullptr && *runtime_dir != '\0') {
    return absl::StrCat(runtime_dir, "/xff");
  }
  std::error_code error;
  const stdfs::path temp = stdfs::temp_directory_path(error);
  return absl::StrCat(error ? "/tmp" : temp.string(), "/xff");
}

// Whether the process that owns a per-run root is still alive. Signal 0 probes without signalling;
// EPERM means "exists, not ours", which is just as alive.
bool PidLives(pid_t pid) {
  return ::kill(pid, 0) == 0 || errno == EPERM;
}

// The pid encoded in a root's directory name, or -1 for a name that is not a pid (never staled: an
// unknown directory under our shared base is not ours to remove).
pid_t PidOfRootName(std::string_view name) {
  name = name.substr(0, name.find('-'));
  int pid = 0;
  if (!absl::SimpleAtoi(name, &pid) || pid <= 0) {
    return -1;
  }
  return pid;
}

}  // namespace

absl::StatusOr<MountRoot> MountRoot::Create(const MountRootOptions& options) {
  const std::string base = SharedBase(options);
  MBO_RETURN_IF_ERROR(vfs::CreateHostDirectories(base, options.mutations));
  MBO_ASSIGN_OR_RETURN(
      auto root, vfs::TemporaryDirectory::Create(absl::StrCat(base, "/", ::getpid()), options.mutations));
  return MountRoot(std::move(root), options.mutations);
}

MountRoot::MountRoot(MountRoot&& other) noexcept = default;

MountRoot& MountRoot::operator=(MountRoot&& other) noexcept {
  if (this != &other) {
    path_ = std::exchange(other.path_, {});
    root_ = std::move(other.root_);
    policy_ = other.policy_;
    next_ = other.next_;
  }
  return *this;
}

MountRoot::~MountRoot() = default;

absl::StatusOr<std::string> MountRoot::MountPointFor(std::string_view container) {
  const std::string_view::size_type slash = container.rfind('/');
  const std::string_view name = slash == std::string_view::npos ? container : container.substr(slash + 1);
  // The counter disambiguates two containers sharing a basename; the first keeps the plain name so
  // the common case reads clean in `mount` output and in rendered `{}` paths.
  std::string point = next_ == 0 ? absl::StrCat(path_, "/", name) : absl::StrCat(path_, "/", name, ".", next_);
  ++next_;
  if (name.empty() || name == "." || name == "..") {
    return absl::InvalidArgumentError("invalid container basename for mount point");
  }
  MBO_RETURN_IF_ERROR(vfs::CreateHostDirectories(point, policy_));
  return point;
}

std::vector<std::string> StaleRoots(const MountRootOptions& options) {
  std::vector<std::string> stale;
  std::error_code error;
  for (const stdfs::directory_entry& entry : stdfs::directory_iterator(SharedBase(options), error)) {
    if (!entry.is_directory(error)) {
      continue;
    }
    const pid_t pid = PidOfRootName(entry.path().filename().string());
    if (pid > 0 && !PidLives(pid)) {
      stale.push_back(entry.path().string());
    }
  }
  return stale;
}

std::size_t SweepStaleRoots(
    absl::FunctionRef<void(std::string_view mount_point)> unmount,
    const MountRootOptions& options) {
  if (!options.mutations.Delete().ok()) {
    return 0;
  }
  std::size_t removed = 0;
  for (const std::string& root : StaleRoots(options)) {
    std::error_code error;
    for (const stdfs::directory_entry& entry : stdfs::directory_iterator(root, error)) {
      if (entry.is_directory(error)) {
        unmount(entry.path().string());
      }
    }
    if (vfs::RemoveHostTree(root, options.mutations).ok()) {
      ++removed;
    }
  }
  return removed;
}

}  // namespace xff::fuse
