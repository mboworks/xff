// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/content/snapshot.h"

#include <algorithm>
#include <utility>

#include "xff/content/line_match.h"

namespace xff::content {

void Derivation::Include(Derivation other) {
  lines |= other.lines;
  for (auto& digest : other.digests) {
    if (!std::ranges::contains(digests, digest)) {
      digests.push_back(std::move(digest));
    }
  }
}

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
  return DigestValue(fs, path, spec);
}

const std::optional<std::string>& Snapshot::DigestValue(
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

Snapshot Snapshot::TakeDerived(const Derivation& request, const vfs::FileSystem& fs, std::string_view path) && {
  Snapshot derived;
  if (!Loaded()) {
    return derived;
  }
  if (request.lines) {
    static_cast<void>(Lines(fs, path));
    derived.lines_ = std::exchange(lines_, std::nullopt);
  }
  for (const auto& digest : request.digests) {
    static_cast<void>(DigestValue(fs, path, {.algo = digest.algorithm, .encoding = digest.encoding}));
    auto node = digests_.extract(std::pair{digest.algorithm, digest.encoding});
    derived.digests_.insert(std::move(node));
  }
  return derived;
}

void Snapshot::Invalidate() {
  bytes_.reset();
  digests_.clear();
  lines_.reset();
}

}  // namespace xff::content
