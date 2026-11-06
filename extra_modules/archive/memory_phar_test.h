// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ARCHIVE_MEMORY_PHAR_TEST_H_
#define XFF_ARCHIVE_MEMORY_PHAR_TEST_H_

#include <zlib.h>

#include <array>
#include <cstdint>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace xff::archive {

inline std::string MemoryPharUint32(std::uint32_t value) {
  const auto bytes = std::to_array<char>({
      static_cast<char>(value & 0xFFU),
      static_cast<char>((value >> 8U) & 0xFFU),
      static_cast<char>((value >> 16U) & 0xFFU),
      static_cast<char>((value >> 24U) & 0xFFU),
  });
  return std::string(std::from_range, bytes);
}

// XFF_ABI_POINTER: zlib's CRC32 API observes immutable fixture bytes.
inline std::uint32_t MemoryPharCrc32(std::string_view content) {
  return static_cast<std::uint32_t>(::crc32_z(
      0,
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const ::Bytef*>(content.data()), content.size()));
}

// One uncompressed native phar member, built without filesystem, PHP, or subprocess access.
inline absl::StatusOr<std::string> MakeMemoryPhar(std::string_view name, std::string_view content) {
  constexpr std::uint32_t kFixedManifestBytes = 46;
  constexpr std::uint32_t kMaximum = std::numeric_limits<std::uint32_t>::max();
  if (name.size() > kMaximum - kFixedManifestBytes || content.size() > kMaximum) {
    return absl::InvalidArgumentError("phar fixture exceeds its 32-bit length fields");
  }
  const std::string manifest = absl::StrCat(
      MemoryPharUint32(1), "\x11\x10", MemoryPharUint32(0), MemoryPharUint32(0), MemoryPharUint32(0),
      MemoryPharUint32(static_cast<std::uint32_t>(name.size())), name,
      MemoryPharUint32(static_cast<std::uint32_t>(content.size())), MemoryPharUint32(1'700'000'000),
      MemoryPharUint32(static_cast<std::uint32_t>(content.size())), MemoryPharUint32(MemoryPharCrc32(content)),
      MemoryPharUint32(0640), MemoryPharUint32(0));
  return absl::StrCat(
      "<?php __HALT_COMPILER(); ?>\n", MemoryPharUint32(static_cast<std::uint32_t>(manifest.size())), manifest,
      content);
}

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_MEMORY_PHAR_TEST_H_
