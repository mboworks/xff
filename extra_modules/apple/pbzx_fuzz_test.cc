// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string>

#include "xff/apple/pbzx.h"
#include "xff/vfs/read_source.h"

// XFF_ABI_POINTER: libFuzzer supplies arbitrary bytes through its C entry point.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > 64UZ * 1'024) {
    return 0;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // XFF_ABI_POINTER: codec byte buffer.
  const auto* chars = reinterpret_cast<const char*>(data);
  const auto decoded = xff::apple::DecodePbzx(xff::vfs::MemoryReadSource(std::string(chars, size)));
  if (decoded.ok()) {
    (void)xff::vfs::ReadSourceBytes(**decoded, 1'024UZ * 1'024);
  }
  return 0;
}
