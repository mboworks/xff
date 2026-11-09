// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_OWNED_VISIT_H_
#define XFF_ENGINE_OWNED_VISIT_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "mbo/types/optional_ref.h"
#include "xff/engine/walk.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

// An independent snapshot of opaque path/name/root bytes and entry observations. Text shares
// one owned allocation where the original values overlap, without encoding/path normalization.
// Offsets, rather than stored views, survive short-string moves and vector relocation.
class OwnedVisit final {
 public:
  OwnedVisit() = default;
  ~OwnedVisit() = default;
  explicit OwnedVisit(const Visit& visit);
  OwnedVisit(const OwnedVisit&) = default;
  OwnedVisit(OwnedVisit&& other) noexcept;
  OwnedVisit& operator=(const OwnedVisit&) = delete;
  OwnedVisit& operator=(OwnedVisit&&) = delete;

  const vfs::Metadata& Metadata() const { return metadata_; }

  std::string_view Path() const;
  std::string_view Name() const;
  std::string_view Root() const;
  // Logical lengths remain unchanged even when their physical byte ranges overlap.
  std::size_t LogicalTextBytes() const;

  // Retained text capacity, including the terminator and inline storage.
  std::size_t TextStorageBytes() const { return bytes_.capacity() + 1; }

  Visit AsVisit() const;

 private:
  int depth_ = 0;
  vfs::Metadata metadata_;
  mbo::types::OptionalRef<const vfs::FileSystem> fs_;
  std::shared_ptr<const vfs::FileSystem> fs_owner_;
  std::size_t root_index_ = 0;

  struct Slice {
    std::size_t offset = 0;
    std::size_t size = 0;
  };

  std::string bytes_;
  std::size_t path_size_ = 0;
  Slice name_;
  Slice root_;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_OWNED_VISIT_H_
