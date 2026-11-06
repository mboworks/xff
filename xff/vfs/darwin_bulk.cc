// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/vfs/darwin_bulk.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/time/time.h"

#if defined(__APPLE__)
# include <sys/attr.h>
# include <sys/stat.h>
# include <sys/vnode.h>
#endif

namespace xff::vfs::internal {
namespace {

#if defined(__APPLE__)
static_assert(
    kBulkCommonAttributes
    == (ATTR_CMN_NAME | ATTR_CMN_DEVID | ATTR_CMN_OBJTYPE | ATTR_CMN_CRTIME | ATTR_CMN_MODTIME | ATTR_CMN_CHGTIME
        | ATTR_CMN_ACCTIME | ATTR_CMN_OWNERID | ATTR_CMN_GRPID | ATTR_CMN_ACCESSMASK | ATTR_CMN_FILEID | ATTR_CMN_ERROR
        | ATTR_CMN_RETURNED_ATTRS));
static_assert(kBulkFileAttributes == (ATTR_FILE_LINKCOUNT | ATTR_FILE_ALLOCSIZE | ATTR_FILE_DATALENGTH));
static_assert(sizeof(dev_t) == 4 && sizeof(uid_t) == 4 && sizeof(gid_t) == 4 && sizeof(off_t) == 8);
static_assert(sizeof(timespec) == 16 && sizeof(attribute_set_t) == 20 && sizeof(attrreference_t) == 8);
static_assert(sizeof(mode_t) == 2 && sizeof(nlink_t) == 2);
static_assert(VREG == 1 && VDIR == 2 && VLNK == 5 && S_IFREG == 0100000 && S_IFLNK == 0120000);
#endif

class Cursor final {
 public:
  explicit Cursor(std::span<const std::byte> bytes) : bytes_(bytes) {}

  template<typename T>
  T Read() {
    if (sizeof(T) > bytes_.size() - position_) {
      valid_ = false;
      return T{};
    }
    T value;
    std::memcpy(&value, bytes_.subspan(position_, sizeof(T)).data(), sizeof(T));
    position_ += sizeof(T);
    return value;
  }

  bool Valid() const { return valid_; }

  std::size_t Position() const { return position_; }

 private:
  std::span<const std::byte> bytes_;
  std::size_t position_ = 0;
  bool valid_ = true;
};

struct NativeTime {
  std::int64_t seconds;
  std::int64_t nanoseconds;

  bool Valid() const { return nanoseconds >= 0 && nanoseconds < 1'000'000'000; }

  absl::Time Value() const { return absl::FromUnixSeconds(seconds) + absl::Nanoseconds(nanoseconds); }
};

FileType NativeType(std::uint32_t type) {
  switch (type) {
    case 1: return FileType::kRegular;
    case 2: return FileType::kDirectory;
    case 3: return FileType::kBlockDevice;
    case 4: return FileType::kCharDevice;
    case 5: return FileType::kSymlink;
    case 6: return FileType::kSocket;
    case 7: return FileType::kFifo;
    default: return FileType::kUnknown;
  }
}

absl::Status InvalidRecord() {
  return absl::DataLossError("invalid Darwin bulk metadata record");
}

}  // namespace

absl::StatusOr<DarwinBulkRecord> DecodeDarwinBulkRecord(std::span<const std::byte> bytes) {
  Cursor prefix(bytes);
  const auto length = prefix.Read<std::uint32_t>();
  if (!prefix.Valid() || length < sizeof(length) || length > bytes.size() || length % 8 != 0) {
    return InvalidRecord();
  }
  bytes = bytes.first(length);
  Cursor cursor(bytes);
  static_cast<void>(cursor.Read<std::uint32_t>());
  const auto common = cursor.Read<std::uint32_t>();
  static_cast<void>(cursor.Read<std::uint32_t>());  // Volume attributes.
  static_cast<void>(cursor.Read<std::uint32_t>());  // Directory attributes.
  const auto file = cursor.Read<std::uint32_t>();
  static_cast<void>(cursor.Read<std::uint32_t>());  // Fork attributes.
  const auto error = cursor.Read<std::uint32_t>();
  const auto name_position = cursor.Position();
  const auto offset = cursor.Read<std::int32_t>();
  const auto name_length = cursor.Read<std::uint32_t>();
  const auto device = cursor.Read<std::int32_t>();
  const auto type = cursor.Read<std::uint32_t>();
  const auto birth = cursor.Read<NativeTime>();
  const auto modified = cursor.Read<NativeTime>();
  const auto changed = cursor.Read<NativeTime>();
  const auto accessed = cursor.Read<NativeTime>();
  const auto uid = cursor.Read<std::uint32_t>();
  const auto gid = cursor.Read<std::uint32_t>();
  const auto permissions = cursor.Read<std::uint32_t>();
  const auto inode = cursor.Read<std::uint64_t>();
  // Darwin omits file-specific slots for directories even with PACK_INVAL_ATTRS.
  const auto links = type == 2 ? 0 : cursor.Read<std::uint32_t>();
  const auto allocated = type == 2 ? 0 : cursor.Read<std::int64_t>();
  const auto size = type == 2 ? 0 : cursor.Read<std::int64_t>();
  if (!cursor.Valid() || offset < 0 || name_length == 0 || (common & 1U) == 0) {
    return InvalidRecord();
  }
  const auto name_start = name_position + static_cast<std::uint32_t>(offset);
  if (name_start < cursor.Position() || name_start > bytes.size() || name_length > bytes.size() - name_start) {
    return InvalidRecord();
  }
  const auto name_bytes = bytes.subspan(name_start, name_length);
  if (name_bytes.back() != std::byte{0}) {
    return InvalidRecord();
  }
  std::string name(
      std::from_range, name_bytes.first(name_bytes.size() - 1)
                           | std::views::transform([](std::byte value) { return std::to_integer<char>(value); }));
  if (name.empty() || name.find_first_of(std::string_view("/\0", 2)) != std::string_view::npos) {
    return InvalidRecord();
  }
  DarwinBulkRecord record{.name = std::move(name), .type = NativeType(type), .bytes = length};
  if (error == 0 && (type == 1 || type == 5) && (common & kBulkCommonAttributes) == kBulkCommonAttributes
      && (file & kBulkFileAttributes) == kBulkFileAttributes && size >= 0 && allocated >= 0 && allocated % 512 == 0
      && links <= std::numeric_limits<std::uint16_t>::max() && birth.Valid() && modified.Valid() && changed.Valid()
      && accessed.Valid()) {
    record.metadata = Metadata{
        .type = NativeType(type),
        .size = static_cast<std::uint64_t>(size),
        .blocks = static_cast<std::uint64_t>(allocated / 512),
        .mode = (permissions & 07777U) | (type == 5 ? 0120000U : 0100000U),
        .nlink = static_cast<std::uint64_t>(links),
        .uid = uid,
        .gid = gid,
        .ino = inode,
        .dev = static_cast<std::uint64_t>(device),
        .atime = accessed.Value(),
        .mtime = modified.Value(),
        .ctime = changed.Value(),
        .btime = birth.Value(),
    };
  }
  return record;
}

}  // namespace xff::vfs::internal
