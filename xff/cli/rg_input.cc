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

#include "xff/cli/rg_input.h"

#include <iostream>
#include <iterator>

#include "absl/status/status.h"

namespace xff::cli {
// XFF_HOST_IO: explicit CLI standard-input stream adapter.
absl::StatusOr<std::string> ReadRgStdin() {
  std::string input{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
  if (std::cin.bad()) {
    return absl::UnknownError("cannot read standard input");
  }
  return input;
}

absl::StatusOr<std::vector<vfs::Entry>> RgInputFs::ReadDir(std::string_view path) const {
  if (IsInput(path)) {
    return absl::FailedPreconditionError("standard input is not a directory");
  }
  return host_.ReadDir(path);
}

absl::StatusOr<vfs::Metadata> RgInputFs::Stat(std::string_view path, bool follow) const {
  if (IsInput(path)) {
    return vfs::Metadata{.type = vfs::FileType::kRegular, .size = input_.size()};
  }
  return host_.Stat(path, follow);
}

absl::Status RgInputFs::Remove(std::string_view /*path*/) const {
  return absl::PermissionDeniedError("rg input is read-only");
}

bool RgInputFs::Access(std::string_view path, vfs::AccessMode mode) const {
  return IsInput(path) ? mode == vfs::AccessMode::kRead : host_.Access(path, mode);
}

absl::StatusOr<std::string> RgInputFs::ReadLink(std::string_view path) const {
  if (IsInput(path)) {
    return absl::InvalidArgumentError("standard input is not a link");
  }
  return host_.ReadLink(path);
}

absl::StatusOr<std::string> RgInputFs::FsType(std::string_view path) const {
  if (IsInput(path)) {
    return "stream";
  }
  return host_.FsType(path);
}

absl::StatusOr<bool> RgInputFs::IsCaseSensitive(std::string_view path) const {
  if (IsInput(path)) {
    return true;
  }
  return host_.IsCaseSensitive(path);
}

absl::StatusOr<std::string> RgInputFs::ReadContent(std::string_view path) const {
  if (IsInput(path)) {
    return input_;
  }
  return host_.ReadContent(path);
}

absl::StatusOr<vfs::SharedReadSource> RgInputFs::ContentSource(std::string_view path) const {
  if (IsInput(path)) {
    return FileSystem::ContentSource(path);
  }
  return host_.ContentSource(path);
}
}  // namespace xff::cli
