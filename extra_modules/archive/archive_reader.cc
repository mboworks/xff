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

#include "xff/archive/archive_reader.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_filters.h"
#include "xff/archive/archive_reader_internal.h"
#include "xff/archive/member_path.h"

namespace xff::archive {
namespace {

// The streaming block size for both the header walk and the content read: one value, so the two
// entry points cannot drift apart.
constexpr std::size_t kBlockSize = std::size_t{64} * 1'024;

// `archive_read_free` for a unique_ptr, so every early return releases the reader.
struct ArchiveDeleter {
  void operator()(struct ::archive* handle) const noexcept {
    if (handle != nullptr) {
      ::archive_read_free(handle);
    }
  }
};

using ArchivePtr = std::unique_ptr<struct ::archive, ArchiveDeleter>;

// A reader with every native filter and every format we WANT enabled; the format is detected from the
// content, so callers never name it.
//
// Deliberately NOT `archive_read_support_format_all`: that set includes `mtree`, a plain-text
// format with no magic number, which happily accepts ordinary text. With it on, `xff notes.txt`
// (the xff family dives a named root by default) reports a bogus member named after the file's
// first word. A false "this is an archive" is far worse than missing an exotic format, so the set
// is spelled out and mtree stays out. `raw` is out for the same reason - it accepts anything.
ArchivePtr NewReader(const internal::FilterEnabler enable_filters = EnableNativeFilters) {
  ArchivePtr handle{::archive_read_new()};
  if (handle == nullptr) {  // LCOV_EXCL_BR_LINE: libarchive allocation failure injection.
    return handle;
  }
  if (!enable_filters(*handle)) {
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
  if (::archive_read_support_format_xar(handle.get()) != ARCHIVE_OK) {
    return nullptr;
  }
  ::archive_read_support_format_zip(handle.get());
  return handle;
}

// The reader's current error text, or a stable fallback when libarchive left none.
std::string LastError(struct ::archive* handle) {
  const char* const message = ::archive_error_string(handle);
  return message == nullptr ? std::string("unknown libarchive error") : std::string(message);
}

// Walks an opened reader's headers into Members. Shared by the memory and file entry points: only
// the open call differs. Never reads member content (headers only), so this stays cheap.
absl::StatusOr<std::vector<Member>> ReadMembers(struct ::archive& handle_ref) {
  struct ::archive* const handle = &handle_ref;
  std::vector<Member> members;
  while (true) {
    struct ::archive_entry* entry = nullptr;
    const int status = ::archive_read_next_header(handle, &entry);
    if (status == ARCHIVE_EOF) {
      return members;
    }
    if (status < ARCHIVE_WARN) {  // LCOV_EXCL_BR_LINE: libarchive header failure injection.
      // Opened as an archive but failed part way: a truncated or corrupt archive, which is a real
      // error the walk must report - distinct from "this file is not an archive at all".
      return absl::DataLossError(absl::StrCat("archive read failed: ", LastError(handle)));
    }
    const char* const path = ::archive_entry_pathname(entry);
    const mode_t filetype = ::archive_entry_filetype(entry);
    Member member{
        .path = path == nullptr ? std::string() : std::string(path),
        .size = ::archive_entry_size(entry),
        .mtime = ::archive_entry_mtime(entry),
        .mode = static_cast<std::uint32_t>(::archive_entry_perm(entry)),
        .is_directory = filetype == AE_IFDIR,
        .is_symlink = filetype == AE_IFLNK,
    };
    if (member.is_symlink) {
      const char* const target = ::archive_entry_symlink(entry);
      member.link_target = target == nullptr ? std::string() : std::string(target);
    }
    members.push_back(std::move(member));
  }
}

// The member-extraction loop over an ALREADY-OPEN reader. Shared by the file and memory entry
// points, which differ only in the open call - so a container inside a container (read out of its
// parent into memory) extracts by exactly the same rules as one on disk.
// The data of the entry the reader is ALREADY positioned on. Split out because the two callers reach an
// entry differently: one matches a member by name, the other (a compressed single file, read through
// libarchive's `raw` format) has exactly one entry whose stored name is meaningless - `raw` reports it
// as NULL - so there is nothing to match and only the bytes matter.
absl::StatusOr<std::string> ReadPositionedEntry(
    struct ::archive& handle_ref,
    struct ::archive_entry& entry_ref,
    std::string_view label,
    std::uint64_t max_bytes) {
  struct ::archive* const handle = &handle_ref;
  struct ::archive_entry* const entry = &entry_ref;
  if (::archive_entry_filetype(entry) != AE_IFREG && ::archive_entry_filetype(entry) != 0) {
    // A directory or symlink has no content. Saying so beats returning an empty string, which a
    // content predicate could not distinguish from a genuinely empty file. (`raw` reports filetype 0,
    // which is a regular stream of bytes.)
    return absl::FailedPreconditionError(absl::StrCat("member is not a regular file: ", label));
  }
  std::string contents;
  // The header's size is a HINT for reserve() only - never a trusted length. A crafted archive can
  // understate it, so the loop below is what actually bounds the read.
  const std::int64_t hint = ::archive_entry_size(entry);
  if (hint > 0) {
    const auto reserve = static_cast<std::uint64_t>(hint);
    contents.reserve(max_bytes != 0 ? std::min<std::uint64_t>(reserve, max_bytes) : reserve);
  }
  std::array<char, kBlockSize> buffer{};
  while (true) {
    const ::ssize_t read = ::archive_read_data(handle, buffer.data(), buffer.size());
    if (read == 0) {
      return contents;  // end of this member's data
    }
    if (read < 0) {  // LCOV_EXCL_BR_LINE: libarchive streaming failure injection.
      return absl::DataLossError(absl::StrCat("reading ", label, " failed: ", LastError(handle)));
    }
    if (max_bytes != 0 && contents.size() + static_cast<std::uint64_t>(read) > max_bytes) {
      return absl::ResourceExhaustedError(absl::StrCat(label, " exceeds the ", max_bytes, " byte limit"));
    }
    contents.append(buffer.data(), static_cast<std::size_t>(read));
  }
}

absl::StatusOr<std::string> ReadMemberOfOpened(
    struct ::archive& handle_ref,
    std::string_view label,
    std::string_view member,
    std::uint64_t max_bytes) {
  struct ::archive* const handle = &handle_ref;
  const std::string_view wanted = NormalizeMemberName(member);
  struct ::archive_entry* entry = nullptr;
  while (true) {
    const int status = ::archive_read_next_header(handle, &entry);
    if (status == ARCHIVE_EOF) {
      return absl::NotFoundError(absl::StrCat("no such member in ", label, ": ", member));
    }
    if (status != ARCHIVE_OK && status != ARCHIVE_WARN) {
      return absl::DataLossError(absl::StrCat("archive read failed: ", LastError(handle)));
    }
    const char* const stored = ::archive_entry_pathname(entry);
    if (stored == nullptr || NormalizeMemberName(stored) != wanted) {
      continue;  // not this one; libarchive skips its data on the next header read
    }
    return ReadPositionedEntry(handle_ref, *entry, member, max_bytes);
  }
}

// The libarchive callbacks borrow this session; the session owns the cursor and callback buffer.
struct SourceSession {
  vfs::SharedReadSource source;
  vfs::ReadBudget::Reservation reservation;
  std::unique_ptr<vfs::ReadStream> stream;
  std::string buffer;
  std::string prefix;
  absl::Status error;
  std::optional<std::uint64_t> size;
  // Declared last so the reader is released before its callback state.
  ArchivePtr handle;

  // XFF_ABI_POINTER: XAR requests absolute heap positions through libarchive's C callback.
  static la_int64_t Seek(struct ::archive*, void* data, la_int64_t offset, int whence) {
    auto& self = *static_cast<SourceSession*>(data);
    if (whence == SEEK_END && offset == 0) {
      if (!self.size) {
        auto size = self.source->Size();
        if (!size.ok()) {
          self.error = size.status();
          return ARCHIVE_FAILED;
        }
        self.size = *size;
      }
      if (*self.size > static_cast<std::uint64_t>(std::numeric_limits<la_int64_t>::max())) {
        self.error = absl::OutOfRangeError("XAR source exceeds signed offset range");
        return ARCHIVE_FAILED;
      }
      offset = static_cast<la_int64_t>(*self.size);
      whence = SEEK_SET;
    }
    if (whence != SEEK_SET || offset < 0) {
      return ARCHIVE_FAILED;
    }
    auto stream = self.source->OpenAt(static_cast<std::uint64_t>(offset));
    if (!stream.ok()) {
      self.error = stream.status();
      return ARCHIVE_FATAL;
    }
    self.stream = std::move(*stream);
    return offset;
  }

  // XFF_ABI_POINTER: libarchive's input callback uses opaque state and a borrowed byte buffer.
  static la_ssize_t Read(struct ::archive*, void* data, const void** buffer) {
    auto& self = *static_cast<SourceSession*>(data);
    auto result = self.stream->Read(kBlockSize);
    if (!result.ok()) {
      self.error = result.status();
      return -1;
    }
    self.buffer = std::move(*result);
    if (self.prefix.size() < 4) {
      self.prefix.append(self.buffer, 0, 4 - self.prefix.size());
    }
    *buffer = self.buffer.data();
    return static_cast<la_ssize_t>(self.buffer.size());
  }
};

absl::StatusOr<std::unique_ptr<SourceSession>> OpenSourceSession(const vfs::SharedReadSource& source) {
  auto session = std::make_unique<SourceSession>();
  session->source = source;
  MBO_ASSIGN_OR_RETURN(session->reservation, source->Budget()->Reserve(kBlockSize));
  MBO_ASSIGN_OR_RETURN(session->stream, source->Open());
  session->handle = NewReader();
  if (!session->handle) {
    return absl::UnavailableError("cannot enable archive readers");
  }
  MBO_ASSIGN_OR_RETURN(const auto prefix, vfs::ReadSourceRange(*source, 0, 4));
  session->prefix = prefix.bytes;
  if (prefix.bytes == "xar!") {
    ::archive_read_set_seek_callback(session->handle.get(), &SourceSession::Seek);
  }
  if (::archive_read_open(session->handle.get(), session.get(), nullptr, &SourceSession::Read, nullptr) != ARCHIVE_OK) {
    MBO_RETURN_IF_ERROR(session->error);
    if (session->prefix == "xar!") {
      return absl::DataLossError(LastError(session->handle.get()));
    }
    return absl::InvalidArgumentError(absl::StrCat("not a readable archive: ", LastError(session->handle.get())));
  }
  if (session->prefix.empty()) {
    return absl::InvalidArgumentError("empty container source");
  }
  return session;
}

class MemberStream final : public vfs::ReadStream {
 public:
  explicit MemberStream(std::unique_ptr<SourceSession> session) : session_(std::move(session)) {}

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    std::string result(std::min(max_bytes, kBlockSize), '\0');
    const auto count = ::archive_read_data(session_->handle.get(), result.data(), result.size());
    if (count < 0) {
      MBO_RETURN_IF_ERROR(session_->error);
      return absl::DataLossError(LastError(session_->handle.get()));
    }
    result.resize(static_cast<std::size_t>(count));
    return result;
  }

 private:
  std::unique_ptr<SourceSession> session_;
};

class MemberSource final : public vfs::ReadSource {
 public:
  MemberSource(vfs::SharedReadSource source, std::string member)
      : ReadSource(source->Budget()), source_(std::move(source)), member_(std::move(member)) {}

  absl::StatusOr<std::unique_ptr<vfs::ReadStream>> Open() const override {
    MBO_ASSIGN_OR_RETURN(auto session, OpenSourceSession(source_));
    // XFF_ABI_POINTER: archive_read_next_header returns a borrowed C entry.
    struct ::archive_entry* entry = nullptr;
    while (true) {
      const int status = ::archive_read_next_header(session->handle.get(), &entry);
      if (status == ARCHIVE_EOF) {
        return absl::NotFoundError(absl::StrCat("no such archive member: ", member_));
      }
      if (status < ARCHIVE_WARN) {
        MBO_RETURN_IF_ERROR(session->error);
        return absl::DataLossError(LastError(session->handle.get()));
      }
      // XFF_ABI_POINTER: archive_entry_pathname returns a borrowed NUL-terminated name.
      const char* stored = ::archive_entry_pathname(entry);
      if (stored == nullptr || NormalizeMemberName(stored) != NormalizeMemberName(member_)) {
        continue;
      }
      if (::archive_entry_filetype(entry) != AE_IFREG) {
        return absl::FailedPreconditionError("archive member is not a regular file");
      }
      return std::make_unique<MemberStream>(std::move(session));
    }
  }

 private:
  vfs::SharedReadSource source_;
  std::string member_;
};

}  // namespace

// The compression suffixes a SINGLE file can carry, paired with nothing: the member's name is simply
// the container's with the suffix removed. Only the whole-file codecs appear - `.tgz` and friends are
// tar shorthands, so they are archives libarchive reads by itself and must not land here.
constexpr std::array kSingleFileSuffixes = std::to_array<std::string_view>({
    ".bz2",
    ".gz",
    ".lz",
    ".lz4",
    ".lzma",
    ".xz",
    ".Z",
    ".zst",
});

std::optional<std::string> CompressionSuffixStripped(std::string_view name) {
  for (const std::string_view suffix : kSingleFileSuffixes) {
    if (name.size() > suffix.size() && absl::EndsWithIgnoreCase(name, suffix)) {
      const std::string_view stem = name.substr(0, name.size() - suffix.size());
      return stem.empty() ? std::nullopt : std::optional<std::string>(stem);
    }
  }
  return std::nullopt;
}

absl::StatusOr<std::string> internal::ReadCompressedSingleFileWithFilterEnabler(
    std::string_view path,
    const std::uint64_t max_bytes,
    const FilterEnabler enable_filters) {
  const std::string_view::size_type slash = path.rfind('/');
  const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
  if (!CompressionSuffixStripped(name).has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("not a compressed single file by name: ", path));
  }
  // `raw` is registered on THIS reader only, never on the shared one: it bids on anything, so it may
  // only ever see a file whose name already claimed to be compressed.
  const ArchivePtr handle{::archive_read_new()};
  if (handle == nullptr) {  // LCOV_EXCL_BR_LINE: libarchive allocation failure injection.
    return absl::ResourceExhaustedError("cannot allocate a libarchive reader");
  }
  if (!enable_filters(*handle)) {
    return absl::UnavailableError("cannot enable the linked compression filters");
  }
  ::archive_read_support_format_raw(handle.get());
  const std::string path_string(path);
  if (::archive_read_open_filename(handle.get(), path_string.c_str(), kBlockSize) != ARCHIVE_OK) {  // LCOV_EXCL_BR_LINE
    return absl::InvalidArgumentError(absl::StrCat("not readable: ", LastError(handle.get())));
  }
  struct ::archive_entry* entry = nullptr;
  if (::archive_read_next_header(handle.get(), &entry) != ARCHIVE_OK) {  // LCOV_EXCL_BR_LINE
    return absl::InvalidArgumentError(absl::StrCat("not a compressed single file: ", LastError(handle.get())));
  }
  // The confirmation the name alone cannot give: a real codec has to have been applied. A text file
  // called `notes.gz` reaches here and is refused, because its filter is `none`.
  if (::archive_filter_count(handle.get()) < 2 || ::archive_filter_code(handle.get(), 0) == ARCHIVE_FILTER_NONE) {
    return absl::InvalidArgumentError(absl::StrCat("no compression filter applied: ", path));
  }
  return ReadPositionedEntry(*handle, *entry, path, max_bytes);
}

absl::StatusOr<std::string> ReadCompressedSingleFile(std::string_view path, const std::uint64_t max_bytes) {
  return internal::ReadCompressedSingleFileWithFilterEnabler(path, max_bytes, EnableNativeFilters);
}

absl::StatusOr<std::vector<Member>> internal::ListMembersWithFilterEnabler(
    std::string_view bytes,
    const FilterEnabler enable_filters) {
  // libarchive opens zero bytes happily and reports EOF at once, which would make an empty file
  // look like a valid archive holding nothing. No format has an empty representation, so an empty
  // input is simply not an archive - and the walk must keep treating such a file as a plain file.
  if (bytes.empty()) {
    return absl::InvalidArgumentError("not a readable archive: empty input");
  }
  const ArchivePtr handle = NewReader(enable_filters);
  if (handle == nullptr) {  // LCOV_EXCL_BR_LINE: libarchive allocation failure injection.
    return absl::ResourceExhaustedError("cannot allocate a libarchive reader");
  }
  if (::archive_read_open_memory(handle.get(), bytes.data(), bytes.size()) != ARCHIVE_OK) {
    return absl::InvalidArgumentError(absl::StrCat("not a readable archive: ", LastError(handle.get())));
  }
  return ReadMembers(*handle);
}

absl::StatusOr<std::vector<Member>> ListMembers(std::string_view bytes) {
  return internal::ListMembersWithFilterEnabler(bytes, EnableNativeFilters);
}

absl::StatusOr<std::vector<Member>> ListMembersOfFile(std::string_view path) {
  const ArchivePtr handle = NewReader();
  if (handle == nullptr) {  // LCOV_EXCL_BR_LINE: libarchive allocation failure injection.
    return absl::ResourceExhaustedError("cannot allocate a libarchive reader");
  }
  // 64 KiB blocks: large enough to keep syscalls down on big archives, small enough that listing a
  // tiny one costs nothing. The C API needs a NUL-terminated path.
  const std::string path_string(path);
  if (::archive_read_open_filename(handle.get(), path_string.c_str(), kBlockSize) != ARCHIVE_OK) {
    return absl::InvalidArgumentError(absl::StrCat("not a readable archive: ", LastError(handle.get())));
  }
  return ReadMembers(*handle);
}

absl::StatusOr<std::string> ReadMemberOfFile(std::string_view path, std::string_view member, std::uint64_t max_bytes) {
  const ArchivePtr handle = NewReader();
  if (handle == nullptr) {  // LCOV_EXCL_BR_LINE: libarchive allocation failure injection.
    return absl::ResourceExhaustedError("cannot allocate a libarchive reader");
  }
  const std::string path_string(path);
  if (::archive_read_open_filename(handle.get(), path_string.c_str(), kBlockSize) != ARCHIVE_OK) {
    return absl::InvalidArgumentError(absl::StrCat("not a readable archive: ", LastError(handle.get())));
  }
  return ReadMemberOfOpened(*handle, path, member, max_bytes);
}

absl::StatusOr<std::string> ReadMember(std::string_view bytes, std::string_view member, std::uint64_t max_bytes) {
  // Same guard as ListMembers: libarchive reports EOF on zero bytes, which would make an empty
  // input look like an archive with no members rather than the plain file it is.
  if (bytes.empty()) {
    return absl::InvalidArgumentError("not an archive: empty input");
  }
  const ArchivePtr handle = NewReader();
  if (handle == nullptr) {
    return absl::ResourceExhaustedError("cannot allocate a libarchive reader");
  }
  if (::archive_read_open_memory(handle.get(), bytes.data(), bytes.size()) != ARCHIVE_OK) {  // LCOV_EXCL_BR_LINE
    return absl::InvalidArgumentError(absl::StrCat("not a readable archive: ", LastError(handle.get())));
  }
  return ReadMemberOfOpened(*handle, "<memory>", member, max_bytes);
}

absl::StatusOr<std::vector<Member>> ListMembersOfSource(const vfs::SharedReadSource& source) {
  MBO_ASSIGN_OR_RETURN(auto session, OpenSourceSession(source));
  auto members = ReadMembers(*session->handle);
  MBO_RETURN_IF_ERROR(session->error);
  return members;
}

vfs::SharedReadSource MemberReadSource(vfs::SharedReadSource source, std::string member) {
  return std::make_shared<MemberSource>(std::move(source), std::move(member));
}

}  // namespace xff::archive
