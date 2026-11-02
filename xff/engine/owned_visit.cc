// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/owned_visit.h"

#include <utility>

namespace xff::engine {

OwnedVisit::OwnedVisit(const Visit& visit)
    : depth(visit.depth),
      metadata(visit.metadata),
      fs(visit.fs),
      fs_owner(visit.fs_owner),
      root_index(visit.root_index),
      path_size_(visit.path.size()) {
  const bool shared_name = visit.path.ends_with(visit.name);
  const bool shared_root = visit.path.starts_with(visit.root);
  bytes_.reserve(visit.path.size() + (shared_name ? 0 : visit.name.size()) + (shared_root ? 0 : visit.root.size()));
  bytes_.append(visit.path);
  name_ = {.offset = shared_name ? path_size_ - visit.name.size() : bytes_.size(), .size = visit.name.size()};
  if (!shared_name) {
    bytes_.append(visit.name);
  }
  root_ = {.offset = shared_root ? 0 : bytes_.size(), .size = visit.root.size()};
  if (!shared_root) {
    bytes_.append(visit.root);
  }
}

OwnedVisit::OwnedVisit(OwnedVisit&& other) noexcept
    : depth(other.depth),
      metadata(other.metadata),
      fs(other.fs),
      fs_owner(std::move(other.fs_owner)),
      root_index(other.root_index),
      bytes_(std::move(other.bytes_)),
      path_size_(std::exchange(other.path_size_, 0)),
      name_(std::exchange(other.name_, {})),
      root_(std::exchange(other.root_, {})) {
  other.fs.reset();
}

std::string_view OwnedVisit::Path() const {
  return std::string_view(bytes_).substr(0, path_size_);
}

std::string_view OwnedVisit::Name() const {
  return std::string_view(bytes_).substr(name_.offset, name_.size);
}

std::string_view OwnedVisit::Root() const {
  return std::string_view(bytes_).substr(root_.offset, root_.size);
}

std::size_t OwnedVisit::LogicalTextBytes() const {
  return path_size_ + name_.size + root_.size;
}

Visit OwnedVisit::AsVisit() const {
  return {
      .path = Path(),
      .name = Name(),
      .root = Root(),
      .depth = depth,
      .metadata = metadata,
      .fs = fs,
      .fs_owner = fs_owner,
      .root_index = root_index,
  };
}

}  // namespace xff::engine
