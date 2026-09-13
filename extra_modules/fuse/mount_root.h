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

#ifndef XFF_FUSE_MOUNT_ROOT_H_
#define XFF_FUSE_MOUNT_ROOT_H_

// The mount-point lifecycle of the FUSE extra (epic #183, slice 2): one directory tree per RUN,
// one mount point per container, gone when the run is.
//
// The layout is `<base>/xff/<pid>/<container-slug>/`, where `<base>` is `$XDG_RUNTIME_DIR` when set
// (a tmpfs on Linux, wiped at logout) and the temporary directory otherwise. The pid in the path is
// what makes CRASHES recoverable: a later run can tell an abandoned root from a live one by asking
// the kernel whether that pid still exists, which is what the stale sweep does. Unmounting itself is
// NOT this class's job - the server (slice 3) owns its mounts and hands the sweep an unmounter - so
// everything here is plain filesystem work, testable without FUSE.

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "xff/vfs/mutations.h"

namespace xff::fuse {

// Options exist for tests: `base_override` replaces the `$XDG_RUNTIME_DIR` / tempdir choice so a
// test owns the whole tree it creates and sweeps.
struct MountRootOptions {
  std::string base_override;
  vfs::MutationPolicy mutations;
};

class MountRoot {
 public:
  // Creates the per-run root for THIS process. The parent `<base>/xff/` is shared across runs and
  // never removed; the `<pid>` level is this run's own.
  static absl::StatusOr<MountRoot> Create(const MountRootOptions& options = {});

  // Movable (returned by value), not copyable: exactly one owner removes the tree.
  MountRoot(MountRoot&& other) noexcept;
  MountRoot& operator=(MountRoot&& other) noexcept;
  MountRoot(const MountRoot&) = delete;
  MountRoot& operator=(const MountRoot&) = delete;

  // Removes the per-run tree (best effort). Mounts must already be unmounted by their owners: a
  // still-mounted directory makes its subtree busy, and the removal deliberately leaves it for the
  // next run's stale sweep rather than fighting the kernel here.
  ~MountRoot();

  // This run's root directory.
  [[nodiscard]] std::string_view path() const { return path_; }

  // A fresh mount-point directory for `container` under the root. The name is the container's
  // basename plus a counter when two containers share one basename - readable in `mount` output and
  // in `{}` paths, unique within the run.
  absl::StatusOr<std::string> MountPointFor(std::string_view container);

 private:
  MountRoot(std::unique_ptr<vfs::TemporaryDirectory> root, vfs::MutationPolicy policy)
      : path_(root->Path()), root_(std::move(root)), policy_(std::move(policy)) {}

  std::string path_;  // empty in a moved-from instance, which then owns nothing
  std::unique_ptr<vfs::TemporaryDirectory> root_;
  vfs::MutationPolicy policy_;
  std::size_t next_ = 0;
};

// The abandoned per-run roots under `<base>/xff/`: every `<pid>` sibling whose process no longer
// exists. Reported rather than removed so the caller controls the order (unmount first, then
// remove) and the policy.
[[nodiscard]] std::vector<std::string> StaleRoots(const MountRootOptions& options = {});

// Removes every stale root, calling `unmount` for each of its immediate subdirectories first (the
// mount points; a no-op unmounter is fine when the platform has nothing mounted). Returns how many
// roots are gone. Failures are skipped: a busy mount stays for the NEXT sweep, never an error loop.
std::size_t SweepStaleRoots(
    absl::FunctionRef<void(std::string_view mount_point)> unmount,
    const MountRootOptions& options = {});

}  // namespace xff::fuse

#endif  // XFF_FUSE_MOUNT_ROOT_H_
