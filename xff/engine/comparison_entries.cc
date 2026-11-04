// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/comparison_entries.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace xff::engine {

// XFF_ABI_POINTER: MBO Arena and memcpy return storage; expose only an arena-lifetime view.
std::string_view ComparisonEntries::CopyText(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  return {
      static_cast<const char*>(std::memcpy(text_.Allocate(text.size(), alignof(char)), text.data(), text.size())),
      text.size()};
}

void ComparisonEntries::Add(std::string_view relative, const TreeCompareEntry& entry) {
  if (index_.contains(relative)) {
    return;
  }
  const std::string_view path = CopyText(entry.path);
  const std::string_view key =
      path.ends_with(relative) ? path.substr(path.size() - relative.size()) : CopyText(relative);
  const std::size_t index = entries_.size();
  entries_.push_back(
      TreeCompareEntry{
          .path = path,
          .metadata = entry.metadata,
          .fs = entry.fs,
          .fs_owner = entry.fs_owner,
      });
  index_.emplace(key, index);
  ordered_.emplace_back(key, index);
  sorted_ = false;
}

const std::vector<ComparisonEntries::OrderedEntry>& ComparisonEntries::Ordered() {
  if (!sorted_) {
    std::ranges::sort(ordered_, {}, &OrderedEntry::first);
    sorted_ = true;
  }
  return ordered_;
}

}  // namespace xff::engine
