// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_CONTENT_SNAPSHOT_H_
#define XFF_CONTENT_SNAPSHOT_H_

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "xff/hash/hash.h"
#include "xff/vfs/filesystem.h"

namespace xff::content {

// Lazily owned bytes and requested derived values for exactly one entry. A snapshot belongs
// to one evaluator; it has no locks and is never shared between workers. Read errors are cached.
// Invalidate at effect boundaries and before using a different entry. Views expire on invalidation.
class Snapshot final {
 public:
  absl::StatusOr<std::string_view> Read(const vfs::FileSystem& fs, std::string_view path) const;
  std::optional<std::string> Digest(const vfs::FileSystem& fs, std::string_view path, const hash::AlgoEncoding& spec)
      const;
  std::optional<std::size_t> Lines(const vfs::FileSystem& fs, std::string_view path) const;
  void Invalidate();

  bool Loaded() const { return bytes_.has_value(); }

 private:
  mutable std::optional<absl::StatusOr<std::string>> bytes_;
  mutable std::map<std::pair<std::string, hash::Encoding>, std::optional<std::string>> digests_;
  mutable std::optional<std::optional<std::size_t>> lines_;
};

}  // namespace xff::content

#endif  // XFF_CONTENT_SNAPSHOT_H_
