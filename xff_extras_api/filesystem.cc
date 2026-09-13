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

namespace xff::vfs {

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
