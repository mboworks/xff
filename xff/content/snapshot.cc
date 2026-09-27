// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/content/snapshot.h"

#include "xff/content/line_match.h"

namespace xff::content {

absl::StatusOr<std::string_view> Snapshot::Read(const vfs::FileSystem& fs, std::string_view path) const {
  if (!bytes_) {
    bytes_ = fs.ReadContent(path);
  }
  if (!bytes_->ok()) {
    return bytes_->status();
  }
  return std::string_view(**bytes_);
}

std::optional<std::string> Snapshot::Digest(
    const vfs::FileSystem& fs,
    std::string_view path,
    const hash::AlgoEncoding& spec) const {
  auto [entry, inserted] = digests_.try_emplace(std::pair{std::string(spec.algo), spec.encoding});
  if (inserted) {
    const auto bytes = Read(fs, path);
    if (bytes.ok()) {
      entry->second = hash::HashData(spec.algo, *bytes, spec.encoding);
    }
  }
  return entry->second;
}

std::optional<std::size_t> Snapshot::Lines(const vfs::FileSystem& fs, std::string_view path) const {
  if (!lines_) {
    const auto bytes = Read(fs, path);
    lines_.emplace(bytes.ok() ? ContentLineCount(*bytes) : std::nullopt);
  }
  return *lines_;
}

void Snapshot::Invalidate() {
  bytes_.reset();
  digests_.clear();
  lines_.reset();
}

}  // namespace xff::content
