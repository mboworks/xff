// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xff/archive/archive_writer.h"

#include <archive.h>
#include <archive_entry.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_filters.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/mutations.h"

namespace xff::archive {
namespace {

namespace stdfs = ::std::filesystem;

constexpr std::size_t kBlockSize = std::size_t{64} * 1'024;

// `archive_read_free` / `archive_write_free` for a unique_ptr, so every early return releases the
// handle - and, for the writer, closes it first, since a format like zip writes its directory at
// close time.
struct ReadDeleter {
  void operator()(struct ::archive* handle) const noexcept {
    if (handle != nullptr) {
      ::archive_read_free(handle);
    }
  }
};

struct WriteDeleter {
  void operator()(struct ::archive* handle) const noexcept {
    if (handle != nullptr) {
      ::archive_write_free(handle);
    }
  }
};

using ReadPtr = std::unique_ptr<struct ::archive, ReadDeleter>;
using WritePtr = std::unique_ptr<struct ::archive, WriteDeleter>;

std::string LastError(struct ::archive* handle) {
  const char* message = ::archive_error_string(handle);
  return message == nullptr ? std::string("unknown libarchive error") : std::string(message);
}

// The same format set the reader registers, and for the same reason: `mtree` and `raw` bid on
// anything, so a text file must not present as an archive here either.
ReadPtr OpenReader(const std::string& path) {
  ReadPtr handle{::archive_read_new()};
  if (handle == nullptr) {  // LCOV_EXCL_BR_LINE: libarchive allocation failure injection.
    return handle;
  }
  if (!EnableNativeFilters(*handle)) {  // LCOV_EXCL_BR_LINE: compiled-in filter registration failure.
    return nullptr;
  }
  ::archive_read_support_format_7zip(handle.get());
  ::archive_read_support_format_ar(handle.get());
  ::archive_read_support_format_cab(handle.get());
  ::archive_read_support_format_cpio(handle.get());
  ::archive_read_support_format_empty(handle.get());
  ::archive_read_support_format_iso9660(handle.get());
  ::archive_read_support_format_lha(handle.get());
  ::archive_read_support_format_rar(handle.get());
  ::archive_read_support_format_rar5(handle.get());
  ::archive_read_support_format_tar(handle.get());
  ::archive_read_support_format_warc(handle.get());
  ::archive_read_support_format_xar(handle.get());
  ::archive_read_support_format_zip(handle.get());
  if (::archive_read_open_filename(handle.get(), path.c_str(), kBlockSize) != ARCHIVE_OK) {  // LCOV_EXCL_BR_LINE
    return nullptr;
  }
  return handle;
}

// Copies one member's data across. Block-wise rather than whole-member, so rewriting a container
// holding a huge member does not need it in memory. `archive_write_data` and not the _block variant:
// the latter belongs to the write-to-DISK API and answers "not supported" on an archive writer.
absl::Status CopyData(struct ::archive& reader_ref, struct ::archive& writer_ref) {
  struct ::archive* const reader = &reader_ref;
  struct ::archive* const writer = &writer_ref;
  for (;;) {
    const void* block = nullptr;
    std::size_t size = 0;
    std::int64_t offset = 0;
    const int status = ::archive_read_data_block(reader, &block, &size, &offset);
    if (status == ARCHIVE_EOF) {
      return absl::OkStatus();
    }
    if (status < ARCHIVE_WARN) {  // LCOV_EXCL_BR_LINE: libarchive streaming failure injection.
      return absl::DataLossError(LastError(reader));
    }
    if (::archive_write_data(writer, block, size) < 0) {  // LCOV_EXCL_BR_LINE: libarchive write failure injection.
      return absl::UnavailableError(LastError(writer));
    }
  }
}

// The rewrite itself, into an already-created writer. Split out so the caller owns the temporary
// file's lifetime: whatever happens here, an unfinished temporary is removed rather than renamed.
class RemovalTracker final {
 public:
  explicit RemovalTracker(const std::vector<std::string>& requested)
      : requested_(requested), removed_(requested.size(), false) {}

  bool ShouldRemove(std::string_view name) {
    bool drop = false;
    for (std::size_t i = 0; i < requested_.size(); ++i) {
      if (NormalizeMemberName(requested_[i]) == name) {
        drop = true;
        removed_[i] = true;
      }
    }
    return drop;
  }

  // NotFound names every requested member the rewrite never saw.
  absl::Status CheckAll(std::string_view path) const {
    if (absl::c_all_of(removed_, [](bool found) { return found; })) {
      return absl::OkStatus();
    }
    std::vector<std::string> missing;
    for (std::size_t i = 0; i < requested_.size(); ++i) {
      if (!removed_[i]) {
        missing.push_back(requested_[i]);
      }
    }
    return absl::NotFoundError(absl::StrCat("no such member in ", path, ": ", absl::StrJoin(missing, ", ")));
  }

 private:
  const std::vector<std::string>& requested_;
  std::vector<bool> removed_;
};

absl::Status RewriteWithout(struct ::archive& reader_ref, struct ::archive& writer_ref, RemovalTracker& removals) {
  struct ::archive* const reader = &reader_ref;
  struct ::archive* const writer = &writer_ref;
  for (;;) {
    struct ::archive_entry* entry = nullptr;
    const int status = ::archive_read_next_header(reader, &entry);
    if (status == ARCHIVE_EOF) {
      return absl::OkStatus();
    }
    if (status < ARCHIVE_WARN) {  // LCOV_EXCL_BR_LINE: libarchive header failure injection.
      return absl::DataLossError(LastError(reader));
    }
    const char* stored = ::archive_entry_pathname(entry);
    const std::string_view name = NormalizeMemberName(stored == nullptr ? std::string_view() : stored);
    if (removals.ShouldRemove(name)) {
      ::archive_read_data_skip(reader);
      continue;
    }
    if (::archive_write_header(writer, entry) < ARCHIVE_WARN) {  // LCOV_EXCL_BR_LINE
      return absl::UnavailableError(LastError(writer));
    }
    if (::archive_entry_size(entry) > 0) {
      const absl::Status copied = CopyData(reader_ref, writer_ref);
      if (!copied.ok()) {
        return copied;
      }
    }
  }
}

// Points `writer` at `temporary` with the same format and compression the reader found, which is what
// keeps a rewrite from quietly changing the container. Split out of RemoveMembersOfFile: it is the one
// cohesive step in it that answers a different question ("can this be written back at all?"), and
// leaving it inline put that function past the complexity the style guide allows.
absl::Status MatchWriterToReader(
    struct ::archive& reader_ref,
    struct ::archive& writer_ref,
    const vfs::TemporaryOutput& temporary,
    std::string_view path) {
  struct ::archive* const reader = &reader_ref;
  struct ::archive* const writer = &writer_ref;
  // Reading a format does not imply writing it: libarchive reads 7-Zip, RAR, ISO and cab, and writes
  // only some of those. Refusing here (rather than producing a tar named `.7z`) is the point.
  if (::archive_write_set_format(writer, ::archive_format(reader)) != ARCHIVE_OK) {  // LCOV_EXCL_BR_LINE
    return absl::UnimplementedError(
        absl::StrCat(
            "this build cannot write the ", ::archive_format_name(reader),
            " format back, so a member cannot be removed from ", path));
  }
  // Filter 0 is the outermost compression the reader applied, and the innermost is always `none`.
  // Carrying it over is what keeps a `.tar.gz` gzipped rather than silently expanding it.
  for (int i = ::archive_filter_count(reader) - 2; i >= 0; --i) {
    if (::archive_write_add_filter(writer, ::archive_filter_code(reader, i)) != ARCHIVE_OK) {  // LCOV_EXCL_BR_LINE
      return absl::UnimplementedError(
          absl::StrCat(
              "this build cannot write the ", ::archive_filter_name(reader, i),
              " compression back, so a member cannot be removed from ", path));
    }
  }
  if (::archive_write_open_fd(writer, temporary.Fd()) != ARCHIVE_OK) {  // LCOV_EXCL_BR_LINE
    return absl::UnavailableError(absl::StrCat("cannot write ", temporary.Path(), ": ", LastError(writer)));
  }
  return absl::OkStatus();
}

// The peeked first member: dropped when listed (marking it in `removed`), copied through otherwise.
// Split out of RemoveMembersOfFile so the peek-then-loop shape stays readable there.
absl::Status TransferFirstMember(
    struct ::archive& reader_ref,
    struct ::archive& writer_ref,
    struct ::archive_entry& first_ref,
    RemovalTracker& removals) {
  struct ::archive* const reader = &reader_ref;
  struct ::archive* const writer = &writer_ref;
  struct ::archive_entry* const first = &first_ref;
  const char* stored = ::archive_entry_pathname(first);
  const std::string_view name = NormalizeMemberName(stored == nullptr ? std::string_view() : stored);
  if (removals.ShouldRemove(name)) {
    ::archive_read_data_skip(reader);
    return absl::OkStatus();
  }
  if (::archive_write_header(writer, first) < ARCHIVE_WARN) {  // LCOV_EXCL_BR_LINE
    return absl::UnavailableError(LastError(writer));
  }
  if (::archive_entry_size(first) > 0) {
    return CopyData(reader_ref, writer_ref);
  }
  return absl::OkStatus();
}

absl::Status RewriteMembers(
    ::archive& reader,
    ::archive_entry& first,
    vfs::TemporaryOutput& temporary,
    std::string_view path,
    const std::vector<std::string>& members) {
  const WritePtr writer{::archive_write_new()};
  if (writer == nullptr) {  // LCOV_EXCL_BR_LINE: libarchive allocation failure injection.
    return absl::UnavailableError("cannot create a libarchive writer");
  }
  MBO_RETURN_IF_ERROR(MatchWriterToReader(reader, *writer, temporary, path));
  RemovalTracker removals(members);
  // The peeked header is the first member, so handle it before the loop takes over the rest.
  MBO_RETURN_IF_ERROR(TransferFirstMember(reader, *writer, first, removals));
  MBO_RETURN_IF_ERROR(RewriteWithout(reader, *writer, removals));
  MBO_RETURN_IF_ERROR(removals.CheckAll(path));
  if (::archive_write_close(writer.get()) != ARCHIVE_OK) {
    return absl::UnavailableError(LastError(writer.get()));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status RemoveMembersOfFile(
    std::string_view path,
    const std::vector<std::string>& members,
    const vfs::MutationPolicy& policy) {
  MBO_RETURN_IF_ERROR(policy.Delete());
  MBO_RETURN_IF_ERROR(policy.Write(true));
  if (members.empty()) {
    return absl::OkStatus();
  }
  const std::string path_string(path);
  const ReadPtr reader = OpenReader(path_string);
  if (reader == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat("not an archive this build can open: ", path));
  }
  // The format and filter are read off the ORIGINAL, which needs a header, so peek at the first
  // member before the writer is set up. An archive with no members has nothing to remove anyway.
  struct ::archive_entry* first = nullptr;
  const int peeked = ::archive_read_next_header(reader.get(), &first);
  if ((static_cast<unsigned>(::archive_format(reader.get())) & static_cast<unsigned>(ARCHIVE_FORMAT_BASE_MASK))
      == ARCHIVE_FORMAT_XAR) {
    return absl::UnimplementedError("XAR/PKG/XIP containers are read-only; rewriting can invalidate signatures");
  }
  if (peeked == ARCHIVE_EOF) {
    return absl::NotFoundError(absl::StrCat("no such member in ", path, ": ", absl::StrJoin(members, ", ")));
  }
  if (peeked < ARCHIVE_WARN) {
    return absl::DataLossError(LastError(reader.get()));
  }
  const stdfs::path target(path_string);
  MBO_ASSIGN_OR_RETURN(const auto temporary, vfs::TemporaryOutput::Create(absl::StrCat(path, ".xff-rewrite"), policy));
  MBO_RETURN_IF_ERROR(RewriteMembers(*reader, *first, *temporary, path, members));
  std::error_code error;
  // XFF_HOST_IO: reads original permissions before publishing the owned replacement.
  const auto mode = stdfs::status(target, error).permissions();
  if (error) {
    return absl::UnavailableError(error.message());
  }
  MBO_RETURN_IF_ERROR(temporary->SetPermissions(static_cast<unsigned int>(mode)));
  return temporary->Publish(path);
}

}  // namespace xff::archive
