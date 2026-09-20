// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lzma.h"

namespace xff::apple {
namespace {
using ::testing::Eq;
using ::testing::Message;

std::uint64_t ReferenceCrc(std::span<const std::uint8_t> bytes, std::uint64_t crc) {
  crc = ~crc;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ ((0ULL - (crc & 1U)) & 0xC96C5795D7870F42ULL);
    }
  }
  return ~crc;
}

struct XzCrc64Test : ::testing::Test {};

TEST_F(XzCrc64Test, MatchesKnownCheckValue) {
  constexpr auto kBytes = std::to_array<std::uint8_t>({'1', '2', '3', '4', '5', '6', '7', '8', '9'});
  EXPECT_THAT(lzma_crc64(kBytes.data(), kBytes.size(), 0), Eq(0x995DC9BBDF1939FAULL));
}

TEST_F(XzCrc64Test, ReadsOnlySuppliedBytesAtEveryAlignmentAndTailLength) {
  constexpr auto kSeeds = std::to_array<std::uint64_t>({0, 0x123456789ABCDEF0ULL});
  for (std::size_t size = 0; size <= 256; ++size) {
    for (std::size_t offset = 0; offset < 16; ++offset) {
      SCOPED_TRACE(Message() << "size=" << size << " offset=" << offset);
      // The allocation ends exactly at the supplied range, exposing vector tail overreads to ASan.
      std::vector<std::uint8_t> storage(size + offset);
      std::uint8_t value = 0;
      for (auto& byte : storage) {
        byte = value++;
      }
      const auto bytes = std::span<const std::uint8_t>(storage).subspan(offset);
      for (const auto seed : kSeeds) {
        EXPECT_THAT(lzma_crc64(bytes.data(), bytes.size(), seed), Eq(ReferenceCrc(bytes, seed)));
        const auto prefix = bytes.first(size / 2);
        const auto suffix = bytes.subspan(size / 2);
        const auto partial = lzma_crc64(prefix.data(), prefix.size(), seed);
        EXPECT_THAT(lzma_crc64(suffix.data(), suffix.size(), partial), Eq(ReferenceCrc(bytes, seed)));
      }
    }
  }
}
}  // namespace
}  // namespace xff::apple
