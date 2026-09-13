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

#ifndef XFF_ENGINE_MOUNT_H_
#define XFF_ENGINE_MOUNT_H_

// `--archive-mount`: give an archive member a real path by MOUNTING its container, rather than by
// copying the member out of it (`--archive-extract`, extract.h).
//
// Both answer the same question - "what path can a child process open for this member?" - and they
// differ in what the child then sees. An extracted member is a copy: a formatter edits the copy and
// reports success while the archive is untouched. A mounted member is the container itself, served
// read-only, so `{}` names a path INSIDE the archive and a tool that only reads (a compiler, a
// checksum, `grep`) needs no copy at all. Writes still cannot reach the container - the mount has
// no write callbacks - so mounting arms nothing extraction did not.
//
// One mount per CONTAINER (not per member), kept for the run: mounting is a kernel operation, and
// members of one archive are usually visited together.
//
// Mounting is a per-MACHINE capability: the fuse extra may not be linked, and even when it is, the
// machine may have no runtime fuse3 or no permission to mount. That is a DEGRADE, never a failure -
// `PathFor` returns nothing, the caller falls back to extraction, and `DegradeReason` carries the
// one-line explanation the run reports once.

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "xff/fuse/fuse_backend.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

class MountedContainers {
 public:
  // `armed` is `--archive-mount`; a disarmed instance mounts nothing and answers nothing, so the
  // caller needs no second flag check.
  explicit MountedContainers(bool armed = false, vfs::MutationPolicy policy = {}) : armed_(armed), policy_(policy) {}

  ~MountedContainers() = default;

  MountedContainers(const MountedContainers&) = delete;
  MountedContainers& operator=(const MountedContainers&) = delete;
  MountedContainers(MountedContainers&&) = delete;
  MountedContainers& operator=(MountedContainers&&) = delete;

  // The real path of `member_path` (a full VFS path, `container<separator>member`) under its
  // container's mount, mounting the container on first use. Nothing when mounting is disarmed,
  // when `member_path` names no member, or when this machine cannot mount - the caller then
  // extracts instead.
  // `fs` is the container's filesystem as SHARED ownership (Visit::fs_owner), because the mount
  // keeps serving it after the dive that opened the container has ended.
  std::optional<std::string> PathFor(std::shared_ptr<const vfs::FileSystem> fs, std::string_view member_path);

  // Why mounting did not happen, empty until a mount is actually attempted and fails. Recorded
  // once (the first failure), because every later member of every container would repeat it.
  [[nodiscard]] std::string_view DegradeReason() const { return degrade_reason_; }

  [[nodiscard]] bool Armed() const { return armed_; }

 private:
  bool armed_ = false;
  vfs::MutationPolicy policy_;

  // Container path -> its mount. Insert-only for the run; a Mount unmounts when destroyed. The
  // mount holds its own share of the reader (see MountFactory), so nothing is tracked beside it.
  absl::flat_hash_map<std::string, std::unique_ptr<fuse::Mount>> mounts_;
  std::string degrade_reason_;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_MOUNT_H_
