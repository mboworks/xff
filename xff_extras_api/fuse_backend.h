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

#ifndef XFF_FUSE_FUSE_BACKEND_H_
#define XFF_FUSE_FUSE_BACKEND_H_

// The FUSE extra's registration slot (epic #183): the core asks "is the mount extra LINKED into
// this binary" through here, the same question the regex slot answers with `Pcre2Available()`.
// Linked is a build-time fact and is deliberately distinct from MOUNTABLE: whether the machine's
// runtime fuse3 library exists is probed per run by the extra itself, and its absence is a degrade
// (extraction fallback), never a missing feature. `--help=extras` and the notice line report the
// build-time fact, so they read the slot.
//
// The mount entry points themselves stay in the extra (@xff_fuse); this seam will grow the typed
// mount factory when the CLI flag lands (#183 slice 4b).

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/mutations.h"

namespace xff::fuse {

// One mounted container: a directory on the real filesystem whose contents ARE the container's, so
// a child process opens an ordinary path and never learns an archive was involved.
class Mount {
 public:
  Mount() = default;
  virtual ~Mount() = default;

  Mount(const Mount&) = delete;
  Mount& operator=(const Mount&) = delete;
  Mount(Mount&&) = delete;
  Mount& operator=(Mount&&) = delete;

  // Where the container is mounted.
  [[nodiscard]] virtual std::string_view MountPoint() const = 0;

  // The real path of `member`, which is the member's name INSIDE the container (what remains after
  // the container boundary - see member_path.h). Inner directories keep their slashes, so this is
  // a plain join: the split happens once, at the boundary, and never again.
  [[nodiscard]] virtual std::string PathFor(std::string_view member) const = 0;
};

// Mounts `fs` (an open container filesystem) whose root path is `container`. Unavailable when this
// machine has no runtime fuse3 - the caller degrades to extraction rather than failing the run.
//
// A mount SERVES `fs` for as long as it exists, on its own thread, so it must OWN a share of it -
// the shared_ptr is the whole point of this signature. Passing `const vfs::FileSystem&` compiled
// just as well and cost a day: the walk held the container's reader in a dive-scoped unique_ptr
// while a mount outlived the dive, and every callback then served freed memory. A garbage reply is
// what makes the kernel abort the connection, so it surfaced as ECONNABORTED on one architecture,
// intermittently, and moved when unrelated lines were deleted. Take ownership; do not document it.
using MountFactory = std::function<absl::StatusOr<std::unique_ptr<
    Mount>>(std::shared_ptr<const vfs::FileSystem> fs, std::string_view container, const vfs::MutationPolicy& policy)>;

// Records that the FUSE mount extra is linked in. Called once, at static-init, from the extra's
// registration translation unit (which must be alwayslink so the registrar is not dropped).
// Static-init only; not thread-safe.
void RegisterMountSupport();

// Self-registers on construction. Declare one at namespace scope in the extra's registration TU:
//   const xff::fuse::MountSupportRegistrar kRegisterFuseMount{};
struct MountSupportRegistrar {
  MountSupportRegistrar() { RegisterMountSupport(); }
};

// Registers the process-wide mount factory. Called once, at static-init, from the extra's
// registration translation unit. Static-init only; not thread-safe.
void RegisterMountFactory(MountFactory factory);

// Mounts through the registered factory, or returns Unimplemented when no fuse extra is linked
// (the lean build), so the caller reports "not built in" rather than guessing.
absl::StatusOr<std::unique_ptr<Mount>> MountContainer(
    std::shared_ptr<const vfs::FileSystem> fs,
    std::string_view container,
    const vfs::MutationPolicy& policy = {});

// Whether the FUSE mount extra is compiled into this binary. False in the lean build; true when
// @xff_fuse's registration TU is linked (--//xff:xff_fuse / --//xff:xff_all).
[[nodiscard]] bool MountSupportAvailable();

}  // namespace xff::fuse

#endif  // XFF_FUSE_FUSE_BACKEND_H_
