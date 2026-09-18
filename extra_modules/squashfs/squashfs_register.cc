// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_backend.h"
#include "xff/archive/member_path.h"
#include "xff/squashfs/squashfs_reader.h"
#include "xff/vfs/filesystem.h"

namespace xff::squashfs {
namespace {

absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenSquashfsContainer(
    std::string_view container,
    std::optional<std::string_view> bytes,
    archive::MemberPathOptions options) {
  MBO_ASSIGN_OR_RETURN(
      SquashfsFileSystem fs, bytes.has_value() ? SquashfsFileSystem::OpenBytes(container, std::string(*bytes), options)
                                               : SquashfsFileSystem::Open(container, options));
  return std::make_unique<SquashfsFileSystem>(std::move(fs));
}

// Registration runs before main; allocation failure is fatal during startup.
// Both spellings name the same intentional static-initialization exception.
// NOLINTNEXTLINE(fuchsia-statically-constructed-objects,cert-err58-cpp,bugprone-throwing-static-initialization)
const archive::ContainerRegistrar kRegisterSquashfs{
    "squashfs",
    &OpenSquashfsContainer,
    {{
        .name = "squashfs",
        .suffixes = {".sfs", ".sqfs", ".sqsh", ".squashfs", ".snap", ".appimage"},
        .detail = "SquashFS images, Snap packages, and AppImage payloads (BSD-licensed libsqsh reader)",
    }},
};

}  // namespace
}  // namespace xff::squashfs
