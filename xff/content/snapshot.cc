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
  auto& bytes = Storage().bytes;
  if (!bytes) {
    bytes = fs.ReadContent(path);
  }
  if (!bytes->ok()) {
    return bytes->status();
  }
  return std::string_view(**bytes);
}

std::optional<std::string> Snapshot::Digest(
    const vfs::FileSystem& fs,
    std::string_view path,
    const hash::AlgoEncoding& spec) const {
  return DigestValue(fs, path, spec);
}

std::optional<std::string_view> Snapshot::DigestView(
    const vfs::FileSystem& fs,
    std::string_view path,
    const hash::AlgoEncoding& spec) const {
  const auto& value = DigestValue(fs, path, spec);
  return value ? std::optional<std::string_view>(*value) : std::nullopt;
}

const std::optional<std::string>& Snapshot::DigestValue(
    const vfs::FileSystem& fs,
    std::string_view path,
    const hash::AlgoEncoding& spec) const {
  auto [entry, inserted] = Storage().digests.try_emplace(std::pair{std::string(spec.algo), spec.encoding});
  if (inserted) {
    const auto bytes = Read(fs, path);
    if (bytes.ok()) {
      entry->second = hash::HashData(spec.algo, *bytes, spec.encoding);
    }
  }
  return entry->second;
}

std::optional<std::size_t> Snapshot::Lines(const vfs::FileSystem& fs, std::string_view path) const {
  auto& lines = Storage().lines;
  if (!lines) {
    const auto bytes = Read(fs, path);
    lines.emplace(bytes.ok() ? ContentLineCount(*bytes) : std::nullopt);
  }
  return *lines;
}

Snapshot Snapshot::TakeDerived(const Derivation& request, const vfs::FileSystem& fs, std::string_view path) && {
  Snapshot derived;
  if (!Loaded() || request.Empty()) {
    return derived;
  }
  auto& source = Storage();
  auto& target = derived.Storage();
  if (request.lines) {
    static_cast<void>(Lines(fs, path));
    target.lines = std::exchange(source.lines, std::nullopt);
  }
  for (const auto& digest : request.digests) {
    static_cast<void>(DigestValue(fs, path, {.algo = digest.algorithm, .encoding = digest.encoding}));
    auto node = source.digests.extract(std::pair{digest.algorithm, digest.encoding});
    target.digests.insert(std::move(node));
  }
  return derived;
}

void Snapshot::Invalidate() {
  state_.reset();
}

}  // namespace xff::content
