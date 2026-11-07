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
  // Keep unused inline storage unconstructed, including when an enclosing context is
  // value-initialized. A defaulted constructor could zero-initialize the whole snapshot.
  Snapshot() : state_(std::nullopt) {}

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

  bool Loaded() const { return state_ && state_->bytes.has_value(); }

 private:
  const std::optional<std::string>& DigestValue(
      const vfs::FileSystem& fs,
      std::string_view path,
      const hash::AlgoEncoding& spec) const;

  struct State {
    std::optional<absl::StatusOr<std::string>> bytes;
    std::map<std::pair<std::string, hash::Encoding>, std::optional<std::string>> digests;
    std::optional<std::optional<std::size_t>> lines;
  };

  State& Storage() const { return state_ ? *state_ : state_.emplace(); }

  // Reserve cache storage with the evaluator, but construct it only for a content consumer.
  mutable std::optional<State> state_;
};

}  // namespace xff::content

#endif  // XFF_CONTENT_SNAPSHOT_H_
