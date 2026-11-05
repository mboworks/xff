// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_VFS_DARWIN_BULK_H_
#define XFF_VFS_DARWIN_BULK_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "absl/status/statusor.h"
#include "xff/vfs/entry.h"

namespace xff::vfs::internal {

// Darwin's packed native-endian wire format. Constants are checked against the SDK on Darwin.
inline constexpr std::uint32_t kBulkCommonAttributes =
    0x00000001U | 0x00000002U | 0x00000008U | 0x00000200U | 0x00000400U | 0x00000800U | 0x00001000U | 0x00008000U
    | 0x00010000U | 0x00020000U | 0x02000000U | 0x20000000U | 0x80000000U;
inline constexpr std::uint32_t kBulkFileAttributes = 0x00000001U | 0x00000004U | 0x00000200U;

struct DarwinBulkRecord {
  std::string name;
  FileType type = FileType::kUnknown;
  std::optional<Metadata> metadata;
  std::size_t bytes = 0;
};

// Pure bounded decoder. Only complete regular-file observations replace stat; everything else
// carries its name/type and requests ordinary metadata. A malformed record rejects the batch.
absl::StatusOr<DarwinBulkRecord> DecodeDarwinBulkRecord(std::span<const std::byte> bytes);

}  // namespace xff::vfs::internal

#endif  // XFF_VFS_DARWIN_BULK_H_
