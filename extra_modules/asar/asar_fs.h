// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ASAR_ASAR_FS_H_
#define XFF_ASAR_ASAR_FS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::asar {

class AsarParser;

// A read-only VFS over one Electron ASAR. The JSON header describes the directory tree, while packed
// contents remain in the archive and `unpacked` contents live beside it in `<archive>.unpacked/`.
// Integrity records are metadata on their file, never synthetic entries, and are verified on reads.
class AsarFileSystem : public vfs::FileSystem {
 public:
  static absl::StatusOr<AsarFileSystem> Open(std::string_view container, archive::MemberPathOptions options = {});
  static absl::StatusOr<AsarFileSystem> OpenBytes(
      std::string_view container,
      std::string bytes,
      archive::MemberPathOptions options = {});

  ~AsarFileSystem() override = default;
  AsarFileSystem(const AsarFileSystem&) = delete;
  AsarFileSystem& operator=(const AsarFileSystem&) = delete;
  AsarFileSystem(AsarFileSystem&& other) noexcept;
  AsarFileSystem& operator=(AsarFileSystem&& other) noexcept = delete;

  [[nodiscard]] absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override;
  [[nodiscard]] absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool follow_symlinks) const override;
  [[nodiscard]] absl::Status Remove(std::string_view path) const override;
  [[nodiscard]] bool Access(std::string_view path, vfs::AccessMode mode) const override;
  [[nodiscard]] absl::StatusOr<std::string> ReadLink(std::string_view path) const override;
  [[nodiscard]] absl::StatusOr<std::string> FsType(std::string_view path) const override;
  [[nodiscard]] absl::StatusOr<bool> IsCaseSensitive(std::string_view path) const override;
  [[nodiscard]] absl::StatusOr<std::string> ReadContent(std::string_view path) const override;

 private:
  friend class AsarParser;

  struct Integrity {
    std::string hash;
    std::uint64_t block_size = 0;
    std::vector<std::string> blocks;
  };

  struct Node {
    vfs::Metadata metadata;
    std::uint64_t offset = 0;
    bool unpacked = false;
    std::string link_target;
    std::optional<Integrity> integrity;
  };

  AsarFileSystem(std::string container, archive::MemberPathOptions options)
      : container_(std::move(container)), options_(options) {}

  static absl::StatusOr<AsarFileSystem> Parse(
      std::string container,
      std::string bytes,
      const std::string& header_bytes,
      std::uint64_t archive_size,
      archive::MemberPathOptions options);
  [[nodiscard]] std::optional<std::string> MemberKeyOf(std::string_view path) const;
  [[nodiscard]] absl::StatusOr<std::string> ReadNodeContent(std::string_view key, const Node& node) const;

  std::string container_;
  std::string bytes_;
  std::uint64_t data_offset_ = 0;
  archive::MemberPathOptions options_;
  std::map<std::string, Node, std::less<>> nodes_;
  mutable absl::Mutex cache_mutex_;
  mutable std::map<std::string, std::string, std::less<>> content_cache_ ABSL_GUARDED_BY(cache_mutex_);
};

}  // namespace xff::asar

#endif  // XFF_ASAR_ASAR_FS_H_
