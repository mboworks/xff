// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_REGISTRY_SPELLING_INDEX_H_
#define XFF_REGISTRY_SPELLING_INDEX_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "xff/registry/mode.h"

namespace xff::registry {
// Build exact spelling indexes at compile time. Runtime parsing never walks the
// registry or invents aliases; mode membership is part of each lookup key.
struct IndexedSpelling {
  std::string_view name;
  std::size_t entry = 0;
};

constexpr std::size_t SpellingModeIndex(registry::Mode mode) {
  switch (mode) {
    case registry::Mode::kFind: return 0;
    case registry::Mode::kXff: return 1;
    case registry::Mode::kRg: return 2;
  }
  std::unreachable();
}

template<std::size_t Capacity>
struct ModeSpellings {
  std::array<IndexedSpelling, Capacity> entries{};
  std::size_t size = 0;

  consteval void Add(std::string_view name, std::size_t entry) {
    if (!name.empty()) {
      entries.at(size++) = {.name = name, .entry = entry};
    }
  }

  consteval bool Sort() {
    auto active = std::span(entries).first(size);
    std::ranges::sort(active, {}, &IndexedSpelling::name);
    for (std::size_t i = 1; i < size; ++i) {
      if (entries.at(i - 1).name == entries.at(i).name && entries.at(i - 1).entry != entries.at(i).entry) {
        return false;
      }
    }
    return true;
  }

  std::optional<std::size_t> Find(std::string_view name) const {
    const auto active = std::span(entries).first(size);
    const auto found = std::ranges::lower_bound(active, name, {}, &IndexedSpelling::name);
    return found != active.end() && found->name == name ? std::optional(found->entry) : std::nullopt;
  }
};

template<std::size_t Capacity>
struct SpellingIndex {
  std::array<ModeSpellings<Capacity>, registry::kModes.size()> modes;
  bool valid = true;

  consteval void Add(registry::Mode mode, std::string_view name, std::size_t entry) {
    modes.at(SpellingModeIndex(mode)).Add(name, entry);
  }

  consteval void Sort() {
    for (auto& mode : modes) {
      valid = mode.Sort() && valid;
    }
  }

  std::optional<std::size_t> Find(std::string_view name, registry::Mode mode) const {
    return modes.at(SpellingModeIndex(mode)).Find(name);
  }
};

}  // namespace xff::registry
#endif  // XFF_REGISTRY_SPELLING_INDEX_H_
