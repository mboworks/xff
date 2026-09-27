// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_REGISTRY_MODE_H_
#define XFF_REGISTRY_MODE_H_

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace xff::registry {
// Command-line vocabulary, independent of configuration presets and build-time extras.
enum class Mode : std::uint8_t { kFind = 1, kXff = 2, kRg = 4 };
enum class Modes : std::uint8_t { kNone = 0, kFind = 1, kXff = 2, kRg = 4, kNative = 3, kAll = 7 };

constexpr Modes operator|(Modes lhs, Modes rhs) {
  return static_cast<Modes>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

constexpr bool Supports(Modes modes, Mode mode) {
  return (std::to_underlying(modes) & std::to_underlying(mode)) != 0;
}

constexpr bool Overlaps(Modes lhs, Modes rhs) {
  return (std::to_underlying(lhs) & std::to_underlying(rhs)) != 0;
}

inline constexpr auto kModes = std::to_array<Mode>({Mode::kFind, Mode::kXff, Mode::kRg});

constexpr std::string_view ModeName(Mode mode) {
  switch (mode) {
    case Mode::kFind: return "find";
    case Mode::kXff: return "xff";
    case Mode::kRg: return "rg";
  }
  return {};
}
}  // namespace xff::registry
#endif  // XFF_REGISTRY_MODE_H_
