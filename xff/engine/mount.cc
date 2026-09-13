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

#include "xff/engine/mount.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "xff/archive/member_path.h"
#include "xff/fuse/fuse_backend.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

std::optional<std::string> MountedContainers::PathFor(
    std::shared_ptr<const vfs::FileSystem> fs,
    std::string_view member_path) {
  if (!armed_) {
    return std::nullopt;
  }
  // Split ONCE, at the container boundary: only that boundary uses the separator, and the member's
  // own directories keep ordinary slashes (pinned by the mini.tar contract in archive_fs_test), so
  // the remainder joins onto the mount point unchanged.
  const std::optional<archive::MemberPathParts> parts = archive::SplitMemberPath(member_path);
  if (!parts.has_value()) {
    return std::nullopt;  // not a member path: an ordinary file needs no mount
  }
  const std::string container(parts->container);
  auto found = mounts_.find(container);
  if (found == mounts_.end()) {
    absl::StatusOr<std::unique_ptr<fuse::Mount>> mount = fuse::MountContainer(std::move(fs), container, policy_);
    if (!mount.ok()) {
      if (degrade_reason_.empty()) {
        // Once per run: every member of every container would otherwise repeat the same sentence.
        degrade_reason_ = absl::StrCat(mount.status().message(), " (extracting instead)");
      }
      return std::nullopt;
    }
    found = mounts_.emplace(container, *std::move(mount)).first;
  }
  return found->second->PathFor(parts->member);
}

}  // namespace xff::engine
