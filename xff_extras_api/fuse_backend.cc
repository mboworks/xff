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

#include "xff/fuse/fuse_backend.h"

#include <memory>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/vfs/filesystem.h"

namespace xff::fuse {
namespace {

// The slots: written only during static init (see the header's contract), read afterwards.
bool& LinkedSlot() {
  static bool linked = false;
  return linked;
}

MountFactory& FactorySlot() {
  static MountFactory& factory = *new MountFactory();
  return factory;
}

}  // namespace

void RegisterMountSupport() {
  LinkedSlot() = true;
}

bool MountSupportAvailable() {
  return LinkedSlot();
}

void RegisterMountFactory(MountFactory factory) {
  FactorySlot() = std::move(factory);
}

absl::StatusOr<std::unique_ptr<Mount>> MountContainer(
    std::shared_ptr<const vfs::FileSystem> fs,
    std::string_view container,
    const vfs::MutationPolicy& policy) {
  if (auto status = policy.Write(); !status.ok()) {
    return status;
  }
  if (fs == nullptr) {
    return absl::InvalidArgumentError("cannot mount a null filesystem");
  }
  if (!FactorySlot()) {
    return absl::UnimplementedError("this binary has no FUSE mount support (rebuild with --//xff:xff_fuse)");
  }
  return FactorySlot()(std::move(fs), container, policy);
}

}  // namespace xff::fuse
