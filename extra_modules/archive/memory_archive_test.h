// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ARCHIVE_MEMORY_ARCHIVE_TEST_H_
#define XFF_ARCHIVE_MEMORY_ARCHIVE_TEST_H_

#include <archive.h>
#include <archive_entry.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace xff::archive {

// Fixture-only support, shared by behavior tests and the manual in-memory benchmark.
// Views are borrowed only for MakeMemoryTar; returned bytes retain no caller-owned storage.
struct MemoryArchiveMember {
  std::string_view path;
  std::string_view content = "x";
  bool directory = false;
  std::string_view symlink_target;
};

// A narrow in-memory libarchive adapter; no host paths/handles are opened or mutated.
inline absl::StatusOr<std::string> MakeMemoryTar(std::span<const MemoryArchiveMember> members) {
  const std::unique_ptr<struct ::archive, decltype(&::archive_write_free)> writer(
      ::archive_write_new(), &::archive_write_free);
  if (!writer) {
    return absl::ResourceExhaustedError("cannot allocate fixture archive writer");
  }
  if (::archive_write_set_format_pax_restricted(writer.get()) != ARCHIVE_OK) {
    return absl::DataLossError("cannot configure fixture archive format");
  }
  constexpr std::size_t kTarBlock = 512;
  std::size_t capacity = 16'384;
  for (const auto& member : members) {
    // Allow an entry header, padded data, and a possible extended pathname record.
    capacity += (4 * kTarBlock) + member.content.size() + member.path.size() + member.symlink_target.size();
  }
  std::string bytes(capacity, '\0');
  std::size_t used = 0;
  if (::archive_write_open_memory(writer.get(), bytes.data(), bytes.size(), &used) != ARCHIVE_OK) {
    return absl::DataLossError("cannot open fixture archive memory");
  }
  for (const auto& member : members) {
    const std::unique_ptr<struct ::archive_entry, decltype(&::archive_entry_free)> entry(
        ::archive_entry_new(), &::archive_entry_free);
    if (!entry) {
      return absl::ResourceExhaustedError("cannot allocate fixture archive entry");
    }
    const std::string path(member.path);
    ::archive_entry_set_pathname(entry.get(), path.c_str());
    const bool symlink = !member.symlink_target.empty();
    ::archive_entry_set_filetype(entry.get(), member.directory ? AE_IFDIR : symlink ? AE_IFLNK : AE_IFREG);
    if (symlink) {
      const std::string target(member.symlink_target);
      ::archive_entry_set_symlink(entry.get(), target.c_str());
    }
    ::archive_entry_set_perm(entry.get(), member.directory ? 0755 : 0640);
    ::archive_entry_set_mtime(entry.get(), 1'700'000'000, 0);
    const std::string_view content = (member.directory || symlink) ? std::string_view{} : member.content;
    ::archive_entry_set_size(entry.get(), static_cast<la_int64_t>(content.size()));
    if (::archive_write_header(writer.get(), entry.get()) != ARCHIVE_OK
        || ::archive_write_data(writer.get(), content.data(), content.size())
               != static_cast<la_ssize_t>(content.size())) {
      return absl::DataLossError("cannot write fixture archive member");
    }
  }
  if (::archive_write_close(writer.get()) != ARCHIVE_OK) {
    return absl::DataLossError("cannot finish fixture archive memory");
  }
  bytes.resize(used);
  return bytes;
}

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_MEMORY_ARCHIVE_TEST_H_
