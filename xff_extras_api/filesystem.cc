// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/vfs/filesystem.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "mbo/status/status_macros.h"
#include "xff/vfs/mutations.h"

namespace xff::vfs {

absl::Status FileSystem::RemoveControlled(std::string_view path, const MutationPolicy& policy) const {
  if (policy.directories) {
    return absl::PermissionDeniedError("filesystem does not support directory-scoped mutations");
  }
  MBO_ASSIGN_OR_RETURN(const auto metadata, Stat(path, false));
  MBO_RETURN_IF_ERROR(metadata.type == FileType::kDirectory ? policy.DeleteDirectory() : policy.Delete());
  return Remove(path);
}

absl::StatusOr<std::unique_ptr<OutputFile>> FileSystem::OpenControlledOutput(
    std::string_view path,
    bool exclusive,
    const MutationPolicy& policy) const {
  if (policy.directories) {
    return absl::PermissionDeniedError("filesystem does not support directory-scoped mutations");
  }
  MBO_RETURN_IF_ERROR(policy.Write());
  return OpenOutput(path, exclusive || policy.block_overwrite);
}

absl::StatusOr<std::unique_ptr<OutputFile>> FileSystem::OpenOutput(std::string_view, bool) const {
  return absl::UnimplementedError("filesystem does not support output handles");
}

absl::StatusOr<std::string> FileSystem::ReadContentRange(
    std::string_view path,
    std::uint64_t offset,
    std::size_t length) const {
  MBO_ASSIGN_OR_RETURN(const std::string content, ReadContent(path));
  if (offset >= content.size() || length == 0) {
    return std::string();
  }
  const auto begin = static_cast<std::size_t>(offset);
  return content.substr(begin, std::min(length, content.size() - begin));
}

absl::Status FileSystem::WriteContent(std::string_view /*path*/, std::string_view /*content*/) const {
  return absl::UnimplementedError("filesystem backend does not support writing");
}

}  // namespace xff::vfs
