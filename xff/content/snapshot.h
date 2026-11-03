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
#include <vector>

#include "absl/status/statusor.h"
#include "xff/hash/hash.h"
#include "xff/vfs/filesystem.h"

namespace xff::content {

// Immutable after run preparation. Algorithm names own their bytes independently of templates.
struct DigestRequest {
  std::string algorithm;
  hash::Encoding encoding = hash::Encoding::kHex;
  bool operator==(const DigestRequest&) const = default;
};

struct Derivation {
  bool lines = false;
  std::vector<DigestRequest> digests;

  bool Empty() const { return !lines && digests.empty(); }

  void Include(Derivation other);
};

// Lazily owned bytes and requested derived values for exactly one entry. A snapshot belongs
// to one evaluator; it has no locks and is never shared between workers. Read errors are cached.
// Invalidate at effect boundaries and before using a different entry. Views expire on invalidation.
class Snapshot final {
 public:
  absl::StatusOr<std::string_view> Read(const vfs::FileSystem& fs, std::string_view path) const;
  std::optional<std::string> Digest(const vfs::FileSystem& fs, std::string_view path, const hash::AlgoEncoding& spec)
      const;
  // Borrow a cached digest without copying its string. Inserting other digests preserves the
  // view; invalidating, transferring, moving or destroying this snapshot ends the borrow.
  // Read failures and unsupported algorithms remain absent, as with the owning Digest form.
  std::optional<std::string_view> DigestView(
      const vfs::FileSystem& fs,
      std::string_view path,
      const hash::AlgoEncoding& spec) const;
  std::optional<std::size_t> Lines(const vfs::FileSystem& fs, std::string_view path) const;
  void Invalidate();

  // Extract only requested derived caches from already loaded entry bytes. This never initiates
  // a VFS read and the returned snapshot owns no raw content, including on a cached read error.
  Snapshot TakeDerived(const Derivation& request, const vfs::FileSystem& fs, std::string_view path) &&;

  bool Loaded() const { return bytes_.has_value(); }

 private:
  const std::optional<std::string>& DigestValue(
      const vfs::FileSystem& fs,
      std::string_view path,
      const hash::AlgoEncoding& spec) const;

  mutable std::optional<absl::StatusOr<std::string>> bytes_;
  mutable std::map<std::pair<std::string, hash::Encoding>, std::optional<std::string>> digests_;
  mutable std::optional<std::optional<std::size_t>> lines_;
};

}  // namespace xff::content

#endif  // XFF_CONTENT_SNAPSHOT_H_
