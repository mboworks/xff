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

#include "xff/archive/archive_pack.h"

#include <archive.h>
#include <archive_entry.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "mbo/status/status_macros.h"
#include "mbo/types/optional_ref.h"

namespace xff::archive {
namespace {

namespace stdfs = ::std::filesystem;

constexpr std::size_t kBlockSize = std::size_t{64} * 1'024;

// std::filesystem::file_time_type has an unspecified epoch, and C++20's file_clock::to_sys is not
// available on every platform this builds on, so the offset is measured once against a known point.
// Both clocks tick in seconds here, which is all an archive header records anyway.
std::int64_t FileClockToUnixEpoch() {
  const auto file_now = stdfs::file_time_type::clock::now().time_since_epoch();
  const auto system_now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::seconds>(system_now).count()
         - std::chrono::duration_cast<std::chrono::seconds>(file_now).count();
}

// Closes before freeing: zip writes its central directory at close time, so a handle that is only
// freed produces a truncated archive rather than an error.
struct WriteDeleter {
  void operator()(struct ::archive* handle) const noexcept {
    if (handle != nullptr) {
      ::archive_write_close(handle);
      ::archive_write_free(handle);
    }
  }
};

struct EntryDeleter {
  void operator()(struct ::archive_entry* entry) const noexcept {
    if (entry != nullptr) {
      ::archive_entry_free(entry);
    }
  }
};

using WritePtr = std::unique_ptr<struct ::archive, WriteDeleter>;
using EntryPtr = std::unique_ptr<struct ::archive_entry, EntryDeleter>;

// One output suffix and what it selects. Longest suffix first, so `.tar.gz` is not read as `.gz`.
//
// The single-word forms are the shortcuts GNU tar recognises for the same thing, and people type
// them: `--pack=x.txz` meaning `x.tar.xz` is not a typo to refuse. Suffix matching folds case, so
// `.tar.z` / `.taz` below accept the conventional `.tar.Z` / `.taZ` spellings too.
struct FormatSuffix {
  std::string_view suffix;
  std::string_view name;  // what PackFormats / the help calls it
  int format;             // ARCHIVE_FORMAT_*
  int filter;             // ARCHIVE_FILTER_*
  // The compressor's level range, both 0 when the format has no compressor. Checked here rather than
  // left to libarchive, which answers an out-of-range level with "Undefined option" and sends the
  // reader looking for a misspelled flag.
  int level_min;
  int level_max;
};

constexpr std::array kFormats = std::to_array<FormatSuffix>({
    {
        .suffix = ".tar.gz",
        .name = "tar.gz",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_GZIP,
        .level_min = 0,
        .level_max = 9,
    },
    {
        .suffix = ".tar.bz2",
        .name = "tar.bz2",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_BZIP2,
        .level_min = 1,
        .level_max = 9,
    },
    {
        .suffix = ".tar.xz",
        .name = "tar.xz",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_XZ,
        .level_min = 0,
        .level_max = 9,
    },
    {
        .suffix = ".tar.zst",
        .name = "tar.zst",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_ZSTD,
        .level_min = 1,
        .level_max = 22,
    },
    {
        .suffix = ".tar.lzma",
        .name = "tar.lzma",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_LZMA,
        .level_min = 0,
        .level_max = 9,
    },
    {
        .suffix = ".tar.lz4",
        .name = "tar.lz4",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_LZ4,
        .level_min = 1,
        .level_max = 9,
    },
    {
        .suffix = ".tar.lz",
        .name = "tar.lz",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_LZIP,
        .level_min = 0,
        .level_max = 9,
    },
    {
        .suffix = ".tar.z",
        .name = "tar.Z",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_COMPRESS,
        .level_min = 0,
        .level_max = 0,
    },
    {
        .suffix = ".tbz2",
        .name = "tbz2",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_BZIP2,
        .level_min = 1,
        .level_max = 9,
    },
    {
        .suffix = ".tzst",
        .name = "tzst",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_ZSTD,
        .level_min = 1,
        .level_max = 22,
    },
    {
        .suffix = ".tbz",
        .name = "tbz",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_BZIP2,
        .level_min = 1,
        .level_max = 9,
    },
    {
        .suffix = ".tz2",
        .name = "tz2",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_BZIP2,
        .level_min = 1,
        .level_max = 9,
    },
    {
        .suffix = ".txz",
        .name = "txz",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_XZ,
        .level_min = 0,
        .level_max = 9,
    },
    {
        .suffix = ".tgz",
        .name = "tgz",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_GZIP,
        .level_min = 0,
        .level_max = 9,
    },
    {
        .suffix = ".tlz",
        .name = "tlz",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_LZMA,
        .level_min = 0,
        .level_max = 9,
    },
    {
        .suffix = ".taz",
        .name = "taZ",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_COMPRESS,
        .level_min = 0,
        .level_max = 0,
    },
    {
        .suffix = ".tar",
        .name = "tar",
        .format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,
        .filter = ARCHIVE_FILTER_NONE,
        .level_min = 0,
        .level_max = 0,
    },
    {
        .suffix = ".zip",
        .name = "zip",
        .format = ARCHIVE_FORMAT_ZIP,
        .filter = ARCHIVE_FILTER_NONE,
        .level_min = 0,
        .level_max = 9,
    },
});

// How an option's value is spelled, which decides both the validation and the translation: libarchive
// spells a boolean by PRESENCE (`name` on, `!name` off) rather than by value, so a bool cannot simply
enum class PackValue {
  kBool,
  kEnum,
  kInt,
};

// One entry of xff's writer vocabulary and what it becomes for libarchive. Alphabetical by xff name,
// which is also the order the help lists them.
struct PackOptionSpec {
  std::string_view name;           // xff's name, the only one a user ever types
  std::string_view writer_option;  // what libarchive calls it
  PackValue kind;
  absl::Span<const std::string_view> values;   // kEnum: the accepted set; empty otherwise
  absl::Span<const std::string_view> formats;  // output names it applies to
  std::string_view detail;                     // one line, rendered into --help=archive
};

// The backing sets for the spans above. Named so the table reads as WHAT applies, and shared where
// two options genuinely mean the same set (zip-only), not merely coincide.
constexpr std::array kZipCompressionValues = std::to_array<std::string_view>({
    "store",
    "deflate",
});
constexpr std::array kZipOnlyFormats = std::to_array<std::string_view>({
    "zip",
});
constexpr std::array kLevelFormats = std::to_array<std::string_view>({
    "tar.gz",
    "tar.bz2",
    "tar.xz",
    "tar.zst",
    "tar.lzma",
    "tar.lz4",
    "tar.lz",
    "tgz",
    "tbz2",
    "tbz",
    "tz2",
    "txz",
    "tzst",
    "tlz",
    "zip",
});
constexpr std::array kThreadsFormats = std::to_array<std::string_view>({
    "tar.xz",
    "tar.zst",
    "txz",
    "tzst",
});
constexpr std::array kGzipHeaderFormats = std::to_array<std::string_view>({
    "tar.gz",
    "tgz",
});

constexpr std::array kPackOptions = std::to_array<PackOptionSpec>({
    {
        .name = "compression",
        .writer_option = "compression",
        .kind = PackValue::kEnum,
        .values = kZipCompressionValues,
        .formats = kZipOnlyFormats,
        .detail = "`store` writes members uncompressed, which is what an archive of already-compressed "
                  "payloads (images, other archives) wants",
    },
    {
        .name = "level",
        .writer_option = "compression-level",
        .kind = PackValue::kInt,
        .values = {},
        .formats = kLevelFormats,
        .detail = "how hard the compressor works, on the scale the format uses (also spelled "
                  "`--pack-level`)",
    },
    {
        .name = "threads",
        .writer_option = "threads",
        .kind = PackValue::kInt,
        .values = {},
        .formats = kThreadsFormats,
        .detail = "compressor threads; `0` lets the compressor pick from the machine",
    },
    {
        .name = "timestamp",
        .writer_option = "timestamp",
        .kind = PackValue::kBool,
        .values = {},
        .formats = kGzipHeaderFormats,
        .detail = "store the modification time in the gzip header; `no` is what makes two runs over "
                  "the same input byte-identical",
    },
    {
        .name = "zip64",
        .writer_option = "zip64",
        .kind = PackValue::kBool,
        .values = {},
        .formats = kZipOnlyFormats,
        .detail = "force the zip64 extensions, which lift the 4 GiB member and archive limits",
    },
});

using OptionalPackOptionSpec = mbo::types::OptionalRef<const PackOptionSpec>;

OptionalPackOptionSpec PackOptionSpecFor(std::string_view name) {
  // The array search produces an iterator. Keep that abstraction even though this implementation's
  // contiguous iterator is a pointer; spelling it as an object pointer would misstate its role.
  // NOLINTNEXTLINE(llvm-qualified-auto,readability-qualified-auto)
  const auto found = absl::c_find_if(kPackOptions, [name](const PackOptionSpec& spec) { return spec.name == name; });
  return found == kPackOptions.end() ? OptionalPackOptionSpec{} : OptionalPackOptionSpec{*found};
}

bool AppliesTo(const PackOptionSpec& spec, std::string_view format) {
  return absl::c_linear_search(spec.formats, format);
}

// Validates one option against the vocabulary AND the chosen format, and renders what libarchive
// should be told. Booleans become a presence/absence spelling, which is how libarchive reads them.
absl::StatusOr<std::string> TranslateOption(const PackSetting& option, const FormatSuffix& format) {
  const OptionalPackOptionSpec spec = PackOptionSpecFor(option.name);
  if (!spec.has_value()) {
    // Joined with a projection rather than copied into a vector first: the names are only needed to
    // build this one message.
    const std::string known_names = absl::StrJoin(
        kPackOptions, ", ", [](std::string* out, const PackOptionSpec& known) { absl::StrAppend(out, known.name); });
    return absl::InvalidArgumentError(
        absl::StrCat("unknown pack option '", option.name, "'; known options are ", known_names));
  }
  if (!AppliesTo(*spec, format.name)) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "pack option '", spec->name, "' does not apply to ", format.name, "; it applies to ",
            absl::StrJoin(spec->formats, ", ")));
  }
  switch (spec->kind) {
    case PackValue::kBool: {
      if (option.value != "yes" && option.value != "no") {
        return absl::InvalidArgumentError(
            absl::StrCat("pack option '", spec->name, "' takes yes or no, got '", option.value, "'"));
      }
      // Presence is the value: libarchive reads `name` as on and `!name` as off.
      return option.value == "yes" ? std::string(spec->writer_option) : absl::StrCat("!", spec->writer_option);
    }
    case PackValue::kEnum: {
      if (!absl::c_linear_search(spec->values, option.value)) {
        return absl::InvalidArgumentError(
            absl::StrCat(
                "pack option '", spec->name, "' takes one of ", absl::StrJoin(spec->values, ", "), ", got '",
                option.value, "'"));
      }
      return absl::StrCat(spec->writer_option, "=", option.value);
    }
    case PackValue::kInt: {
      int parsed = 0;
      if (!absl::SimpleAtoi(option.value, &parsed)) {
        return absl::InvalidArgumentError(
            absl::StrCat("pack option '", spec->name, "' takes a number, got '", option.value, "'"));
      }
      // Only `level` has a range, and it is the FORMAT's, which is why it cannot live in the option
      // table: gzip stops at 9 where zstd goes to 22.
      if (spec->name == "level" && (parsed < format.level_min || parsed > format.level_max)) {
        return absl::InvalidArgumentError(
            absl::StrCat(
                "compression level ", parsed, " is out of range for ", format.name, ", which accepts ",
                format.level_min, "-", format.level_max));
      }
      return absl::StrCat(spec->writer_option, "=", parsed);
    }
  }
  return absl::InternalError("unhandled pack option kind");  // LCOV_EXCL_LINE: exhaustive enum switch above.
}

using OptionalFormatSuffix = mbo::types::OptionalRef<const FormatSuffix>;

OptionalFormatSuffix FormatEntryFor(std::string_view path) {
  const std::string lower = absl::AsciiStrToLower(path);
  for (const FormatSuffix& candidate : kFormats) {
    if (std::string_view(lower).ends_with(candidate.suffix)) {
      return OptionalFormatSuffix{candidate};
    }
  }
  return {};
}

// Writes one entry's header and, for a regular file, its bytes. The size has to be known up front
// (tar puts it in the header), so it is taken from the stat rather than from the read.
// XFF_HOST_IO: classify direct archive sources without following symlinks before destination planning.
absl::StatusOr<PackFile> DescribePackSource(const PackEntry& entry) {
  std::error_code error;
  const auto status = stdfs::symlink_status(stdfs::path(entry.source), error);
  if (error || !stdfs::exists(status)) {
    return absl::NotFoundError(absl::StrCat("cannot stat '", entry.source, "': ", error.message()));
  }
  return PackFile{.source = entry.source, .name = entry.name, .is_directory = stdfs::is_directory(status)};
}

absl::StatusOr<std::vector<PackFile>> PlanPackSources(
    const std::vector<PackEntry>& entries,
    PackDuplicatePolicy duplicates) {
  std::vector<PackFile> files;
  files.reserve(entries.size());
  absl::btree_map<std::string, absl::Status> unreadable;
  for (const auto& entry : entries) {
    auto file = DescribePackSource(entry);
    if (file.ok()) {
      files.push_back(*std::move(file));
    } else {
      unreadable.emplace(entry.source, file.status());
      files.push_back({.source = entry.source, .name = entry.name});
    }
  }
  MBO_ASSIGN_OR_RETURN(auto planned, PlanPackFiles(files, duplicates));
  for (const auto& file : planned) {
    if (const auto error = unreadable.find(file.source); error != unreadable.end()) {
      return error->second;
    }
  }
  return planned;
}

absl::Status WriteOne(struct ::archive& writer_ref, const PackFile& entry) {
  struct ::archive* const writer = &writer_ref;
  std::error_code error;
  const stdfs::path source(entry.source);
  static const std::int64_t kFileClockToUnixEpoch = FileClockToUnixEpoch();
  const stdfs::file_status status = stdfs::symlink_status(source, error);
  if (error) {
    return absl::NotFoundError(absl::StrCat("cannot stat '", entry.source, "': ", error.message()));
  }
  const EntryPtr header{::archive_entry_new()};
  if (header == nullptr) {  // LCOV_EXCL_BR_LINE: libarchive allocation failure injection.
    return absl::UnavailableError("out of memory building an archive entry");
  }
  ::archive_entry_set_pathname(header.get(), entry.name.c_str());
  // Masked as filesystem perms rather than as an int, which keeps the bit operation off a signed
  // type; `perms::all` is the 0777 the archive header wants.
  ::archive_entry_set_perm(header.get(), static_cast<::mode_t>(status.permissions() & stdfs::perms::all));
  // The modification time travels with the file; without it every member unpacks stamped 1970, which
  // breaks the make / rsync comparisons an archive of source files exists for. Ownership deliberately
  // does NOT travel: it is stored as 0:0, the reproducible-archive convention, so unpacking as root
  // cannot hand files to whoever happened to own them on the machine that packed them.
  if (const stdfs::file_time_type when = stdfs::last_write_time(source, error); !error) {
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch()).count() + kFileClockToUnixEpoch;
    ::archive_entry_set_mtime(header.get(), static_cast<::time_t>(seconds), 0);
  }
  error.clear();
  if (stdfs::is_symlink(status)) {
    const stdfs::path target = stdfs::read_symlink(source, error);
    if (error) {
      return absl::NotFoundError(absl::StrCat("cannot read the link '", entry.source, "': ", error.message()));
    }
    ::archive_entry_set_filetype(header.get(), AE_IFLNK);
    ::archive_entry_set_symlink(header.get(), target.string().c_str());
  } else if (stdfs::is_directory(status)) {
    ::archive_entry_set_filetype(header.get(), AE_IFDIR);
  } else {
    const std::uintmax_t size = stdfs::file_size(source, error);
    if (error) {
      return absl::NotFoundError(absl::StrCat("cannot size '", entry.source, "': ", error.message()));
    }
    ::archive_entry_set_filetype(header.get(), AE_IFREG);
    ::archive_entry_set_size(header.get(), static_cast<::la_int64_t>(size));
  }
  if (::archive_write_header(writer, header.get()) < ARCHIVE_WARN) {  // LCOV_EXCL_BR_LINE
    return absl::UnavailableError(
        absl::StrCat("cannot write the entry '", entry.name, "': ", ::archive_error_string(writer)));
  }
  if (!stdfs::is_regular_file(status)) {
    return absl::OkStatus();  // a directory or link has a header and no content
  }
  // Streamed in blocks: an archive may hold files far larger than memory. A stream rather than
  // stdio because it closes itself on every path out of here, including the error returns below.
  // XFF_HOST_IO: archive pack consumes the explicitly selected host input file.
  std::ifstream file(source, std::ios::binary);
  if (!file.is_open()) {
    return absl::NotFoundError(absl::StrCat("cannot open '", entry.source, "'"));
  }
  std::array<char, kBlockSize> buffer{};
  while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0) {
    const std::streamsize read = file.gcount();
    if (read <= 0) {
      break;
    }
    if (::archive_write_data(writer, buffer.data(), static_cast<std::size_t>(read)) < 0) {  // LCOV_EXCL_BR_LINE
      return absl::UnavailableError(absl::StrCat("cannot write '", entry.name, "': ", ::archive_error_string(writer)));
    }
  }
  if (file.bad()) {
    return absl::DataLossError(absl::StrCat("read failed part way through '", entry.source, "'"));
  }
  return absl::OkStatus();
}

}  // namespace

std::string FormatFromName(std::string_view path) {
  const OptionalFormatSuffix found = FormatEntryFor(path);
  return found.has_value() ? std::string(found->name) : std::string();
}

std::vector<PackOptionDoc> PackOptionDocs() {
  std::vector<PackOptionDoc> docs;
  docs.reserve(kPackOptions.size());
  for (const PackOptionSpec& spec : kPackOptions) {
    std::string syntax;
    switch (spec.kind) {
      case PackValue::kBool: syntax = "yes|no"; break;
      case PackValue::kEnum: syntax = absl::StrJoin(spec.values, "|"); break;
      case PackValue::kInt: syntax = "N"; break;
    }
    docs.push_back({
        .name = std::string(spec.name),
        .value_syntax = std::move(syntax),
        .formats = spec.formats,
        .detail = std::string(spec.detail),
    });
  }
  return docs;
}

std::vector<std::string> PackFormats() {
  std::vector<std::string> names;
  names.reserve(kFormats.size());
  for (const FormatSuffix& format : kFormats) {
    names.emplace_back(format.name);
  }
  return names;
}

absl::Status PackFiles(std::string_view path, const std::vector<PackEntry>& entries, const PackSettings& options) {
  const OptionalFormatSuffix format = FormatEntryFor(path);
  if (!format.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "cannot tell the archive format from '", path, "'; expected one ending in .",
            absl::StrJoin(PackFormats(), ", .")));
  }
  MBO_ASSIGN_OR_RETURN(const auto planned, PlanPackSources(entries, options.duplicates));
  // Written beside the target and renamed over it, so a failure part way leaves no half archive -
  // and an existing file survives an attempt that fails. Same contract as the member rewrite.
  MBO_ASSIGN_OR_RETURN(
      const auto temporary, vfs::TemporaryOutput::Create(absl::StrCat(path, ".xff-pack"), options.mutations));
  {
    const WritePtr writer{::archive_write_new()};
    if (writer == nullptr) {  // LCOV_EXCL_BR_LINE: libarchive allocation failure injection.
      return absl::UnavailableError("out of memory creating the archive writer");
    }
    if (::archive_write_set_format(writer.get(), format->format) != ARCHIVE_OK
        || (format->filter != ARCHIVE_FILTER_NONE
            && ::archive_write_add_filter(writer.get(), format->filter) != ARCHIVE_OK)) {
      return absl::InvalidArgumentError(
          absl::StrCat("this build cannot write ", format->name, ": ", ::archive_error_string(writer.get())));
    }
    // Options are translated and applied before anything is written, so a rejected one costs no file.
    // Last value for a name wins, which is what lets a caller append rather than rewrite its list.
    absl::btree_map<std::string, std::string> resolved;
    for (const PackSetting& option : options.options) {
      resolved[option.name] = option.value;
    }
    for (const auto& [name, value] : resolved) {
      MBO_ASSIGN_OR_RETURN(
          const std::string setting, TranslateOption(PackSetting{.name = name, .value = value}, *format));
      if (::archive_write_set_options(writer.get(), setting.c_str()) != ARCHIVE_OK) {  // LCOV_EXCL_BR_LINE
        return absl::InvalidArgumentError(
            absl::StrCat(
                "this build cannot set '", name, "=", value, "' on ", format->name, ": ",
                ::archive_error_string(writer.get())));
      }
    }
    if (::archive_write_open_fd(writer.get(), temporary->Fd()) != ARCHIVE_OK) {
      return absl::UnavailableError(absl::StrCat("cannot open archive output: ", ::archive_error_string(writer.get())));
    }
    for (const PackFile& entry : planned) {
      MBO_RETURN_IF_ERROR(WriteOne(*writer, entry));
    }
    if (::archive_write_close(writer.get()) != ARCHIVE_OK) {
      return absl::UnavailableError(absl::StrCat("cannot finish archive: ", ::archive_error_string(writer.get())));
    }
  }
  return temporary->Publish(path);
}

}  // namespace xff::archive
