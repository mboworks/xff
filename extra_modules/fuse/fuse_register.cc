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

// The FUSE extra's identity: linking this translation unit IS what "the fuse extra is compiled in"
// means (epic #183 slice 4a). It fills the xff_extras_api slot the core reads for `--help=extras` /
// the notice line, and registers the notice for THIS EXTRA - which is xff's own Apache-2.0 code.
// libfuse is deliberately NOT registered: xff uses none of its code (see fuse_abi.h), so there is
// nothing to attribute and no third-party license to retain. The target is alwayslink so no file-scope
// registrar is dropped.

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "mbo/status/status_macros.h"
#include "xff/fuse/fuse_backend.h"
#include "xff/fuse/fuse_server.h"
#include "xff/fuse/mount_root.h"
#include "xff/license/notice.h"
#include "xff/vfs/filesystem.h"

namespace xff::fuse {
namespace {

// Guards the shared root below: one process, one tree, however many containers get mounted.
absl::Mutex& RootMutex() {
  static absl::NoDestructor<absl::Mutex> mutex;
  return *mutex;
}

// The run's mount-point tree, SHARED by the mounts that live in it and removed when the last one
// goes. Ownership sits with the mounts on purpose: MountRoot removes its tree in the DESTRUCTOR, so
// parking it in an immortal static would leave every run's directories behind, to be cleaned up
// only by a later run's stale sweep. The weak pointer here owns nothing - it just lets a second
// container join the tree the first one created.
//
// Returns ownership BY VALUE, never a reference into this function's state.
absl::StatusOr<std::shared_ptr<MountRoot>> RunRoot(const vfs::MutationPolicy& policy)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(RootMutex()) {
  static absl::NoDestructor<std::weak_ptr<MountRoot>> live;
  if (std::shared_ptr<MountRoot> root = live->lock(); root != nullptr) {
    return root;
  }
  MBO_ASSIGN_OR_RETURN(MountRoot created, MountRoot::Create({.mutations = policy}));
  auto root = std::make_shared<MountRoot>(std::move(created));
  *live = root;
  return root;
}

// A mounted container: the server plus the mount point, answering member paths.
class ServerMount final : public Mount {
 public:
  ServerMount(std::shared_ptr<MountRoot> root, std::unique_ptr<FuseServer> server)
      : root_(std::move(root)), server_(std::move(server)) {}

  [[nodiscard]] std::string_view MountPoint() const override { return server_->MountPoint(); }

  [[nodiscard]] std::string PathFor(std::string_view member) const override {
    return absl::StrCat(server_->MountPoint(), "/", member);
  }

 private:
  // Declaration order is load-bearing: members are destroyed in reverse, so the server unmounts
  // BEFORE the root removes the directory that mount point lives in.
  std::shared_ptr<MountRoot> root_;
  std::unique_ptr<FuseServer> server_;
};

absl::StatusOr<std::unique_ptr<Mount>> MountThroughFuse(
    std::shared_ptr<const vfs::FileSystem> fs,
    std::string_view container,
    const vfs::MutationPolicy& policy) {
  std::shared_ptr<MountRoot> root;
  std::string mount_point;
  {
    // One critical section for both steps: MountPointFor hands out a fresh directory by mutating
    // the root's counter, so acquiring the root and asking it for a mount point belong together.
    const absl::MutexLock lock(RootMutex());
    MBO_ASSIGN_OR_RETURN(root, RunRoot(policy));
    MBO_ASSIGN_OR_RETURN(mount_point, root->MountPointFor(container));
  }
  MBO_ASSIGN_OR_RETURN(
      std::unique_ptr<FuseServer> server, FuseServer::Mount(std::move(fs), std::string(container), mount_point));
  return std::make_unique<ServerMount>(std::move(root), std::move(server));
}

const MountSupportRegistrar kRegisterFuseMount{};

// Registering the factory is what makes MountContainer work; the slot above is only the "is it
// linked" answer the help and notice surfaces read.
const bool kRegisterFactory = [] {
  RegisterMountFactory(&MountThroughFuse);
  return true;
}();

}  // namespace

const license::Registrar kFuseExtraNotice{{
    .section = "FUSE (@xff_fuse)",
    .section_lead = true,
    .component = "xff FUSE extra (@xff_fuse)",
    .spdx = "Apache-2.0",
    .text = "Copyright M. Boerger, the MBO Works authors. Licensed under the Apache License, Version 2.0.\n"
            "Mounting is implemented against the FUSE 3 lowlevel interface using this extra's own\n"
            "declarations (xff/fuse/fuse_abi.h); no libfuse code is compiled, linked, or shipped. At\n"
            "runtime the host's FUSE implementation is loaded dynamically - libfuse3 on Linux and BSD,\n"
            "macFUSE on macOS - each under its own terms. That is how it interoperates, not a license\n"
            "this binary carries.",
}};

}  // namespace xff::fuse
