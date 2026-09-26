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

#ifndef XFF_CLI_RG_INPUT_H_
#define XFF_CLI_RG_INPUT_H_

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/vfs/filesystem.h"

namespace xff::cli {
// Materialize stdin once; every subsequent access uses the read-only VFS entry.
// XFF_HOST_IO: standard input is the CLI's explicitly requested search/pattern stream.
absl::StatusOr<std::string> ReadRgStdin();

class RgInputFs final : public vfs::FileSystem {
 public:
  RgInputFs(const vfs::FileSystem& host, std::optional<std::string> input)
      : host_(host), input_(input.value_or(std::string())), has_input_(input.has_value()) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override;
  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool follow) const override;
  absl::Status Remove(std::string_view path) const override;
  bool Access(std::string_view path, vfs::AccessMode mode) const override;
  absl::StatusOr<std::string> ReadLink(std::string_view path) const override;
  absl::StatusOr<std::string> FsType(std::string_view path) const override;
  absl::StatusOr<bool> IsCaseSensitive(std::string_view path) const override;
  absl::StatusOr<std::string> ReadContent(std::string_view path) const override;
  absl::StatusOr<vfs::SharedReadSource> ContentSource(std::string_view path) const override;

 private:
  bool IsInput(std::string_view path) const { return has_input_ && path == "-"; }

  const vfs::FileSystem& host_;
  std::string input_;
  bool has_input_;
};
}  // namespace xff::cli
#endif  // XFF_CLI_RG_INPUT_H_
