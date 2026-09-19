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

// Plugs this extra into the core's archive seam. Linking this target is the ONLY thing that makes
// `--archive` able to look inside a container; a build without it reports no archive support.
//
// Kept in its own translation unit, exactly like the PCRE2 backend's registrar: the registration is a
// static-init side effect in an `alwayslink` target, so the linker cannot drop it, and separating it
// keeps `archive_fs_cc` itself free of global state (a test can construct an ArchiveFileSystem without
// the process-wide slot being touched).

#include "xff/archive/archive_register.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_backend.h"
#include "xff/archive/archive_extension.h"
#include "xff/archive/archive_fs.h"
#include "xff/archive/archive_pack.h"
#include "xff/archive/archive_reader.h"
#include "xff/archive/archive_writer.h"
#include "xff/archive/member_path.h"
#include "xff/archive/phar_writer.h"
#include "xff/vfs/filesystem.h"

namespace xff::archive {
// Adapts ArchiveFileSystem::Open (which returns the filesystem BY VALUE, so it stays usable without
// heap allocation in its own tests) to the seam's owning-pointer contract. The reader's status is
// returned unchanged, which is what keeps "not an archive" (InvalidArgument) apart from "corrupt
// archive" (DataLoss) all the way out to the walk.
absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenArchiveContainer(
    std::string_view container,
    std::optional<std::string_view> bytes,
    MemberPathOptions options) {
  // With bytes, the container is nested and `container` is only the label its members render under;
  // without them it is a real path. Both index identically once opened.
  MBO_ASSIGN_OR_RETURN(
      ArchiveFileSystem archive_fs, bytes.has_value()
                                        ? ArchiveFileSystem::OpenBytes(container, std::string(*bytes), options)
                                        : ArchiveFileSystem::Open(container, options));
  return std::make_unique<ArchiveFileSystem>(std::move(archive_fs));
}

namespace {

struct ReadFormatSpec {
  std::string_view name;
  absl::Span<const std::string_view> suffixes;
  std::string_view detail;
};

constexpr std::array k7zSuffixes = std::to_array<std::string_view>({".7z"});
constexpr std::array kArSuffixes = std::to_array<std::string_view>({".ar", ".deb"});
constexpr std::array kCabSuffixes = std::to_array<std::string_view>({".cab"});
constexpr std::array kCpioSuffixes = std::to_array<std::string_view>({".cpio", ".rpm"});
constexpr std::array kIsoSuffixes = std::to_array<std::string_view>({".iso"});
constexpr std::array kLhaSuffixes = std::to_array<std::string_view>({".lha", ".lzh"});
constexpr std::array kRarSuffixes = std::to_array<std::string_view>({".rar"});
constexpr std::array kTarSuffixes = std::to_array<std::string_view>({
    ".tar",    ".tar.gz", ".tgz", ".taz",    ".crate",    ".gem",     ".tar.bz2", ".tbz", ".tbz2",    ".tz2",
    ".tar.xz", ".txz",    ".tlz", ".tar.lz", ".tar.lzma", ".tar.lz4", ".tar.Z",   ".taZ", ".tar.zst", ".tzst",
});
constexpr std::array kWarcSuffixes = std::to_array<std::string_view>({".warc"});
constexpr std::array kXarSuffixes = std::to_array<std::string_view>({".xar"});
constexpr std::array kZipSuffixes = std::to_array<std::string_view>({
    ".zip",  ".jar",  ".war",   ".ear", ".whl", ".egg", ".apk",  ".aab",  ".cbz",  ".crx", ".docx",
    ".epub", ".jmod", ".nupkg", ".odp", ".ods", ".odt", ".pptx", ".vsix", ".xlsx", ".xpi",
});
constexpr std::array kPharSuffixes = std::to_array<std::string_view>({".phar"});
constexpr std::array kSingleFileSuffixes = std::to_array<std::string_view>({
    ".gz",
    ".bz2",
    ".xz",
    ".zst",
    ".zstd",
    ".lz",
    ".lz4",
    ".lzma",
    ".Z",
});

constexpr std::array kNativeReadFormats = std::to_array<ReadFormatSpec>({
    {.name = "7z", .suffixes = k7zSuffixes, .detail = "7-Zip archives"},
    {.name = "ar", .suffixes = kArSuffixes, .detail = "Unix ar archives; a `.deb` is one"},
    {.name = "cab", .suffixes = kCabSuffixes, .detail = "Microsoft cabinet archives"},
    {.name = "cpio", .suffixes = kCpioSuffixes, .detail = "cpio archives; an `.rpm`'s payload is one"},
    {.name = "iso9660", .suffixes = kIsoSuffixes, .detail = "ISO 9660 disc images"},
    {.name = "lha", .suffixes = kLhaSuffixes, .detail = "LHA/LZH archives"},
    {.name = "rar", .suffixes = kRarSuffixes, .detail = "RAR 4 and RAR 5 archives"},
    {
        .name = "tar",
        .suffixes = kTarSuffixes,
        .detail = "tar archives, plain or through any compression filter; `.crate` and `.gem` are tars",
    },
    {.name = "warc", .suffixes = kWarcSuffixes, .detail = "web archives"},
    {.name = "xar", .suffixes = kXarSuffixes, .detail = "xar archives"},
    {
        .name = "zip",
        .suffixes = kZipSuffixes,
        .detail = "zip archives and the package formats that are zips underneath",
    },
    {.name = "phar", .suffixes = kPharSuffixes, .detail = "PHP phar archives (xff's own reader)"},
    {
        .name = "file",
        .suffixes = kSingleFileSuffixes,
        .detail = "a compressed SINGLE file (`notes.txt.gz`): one member, decompressed at open",
    },
});

// The formats this reader understands, for the --help=archive table and the seam's name gate.
// Reading is SNIFF-based (archive_reader.cc's curated libarchive set + the phar parser + the
// compressed-single-file probe); the suffixes are what the name gate dives on and what the docs
// show. Package extensions ride their underlying format (.jar is a zip, .crate a tar.gz, .deb an
// ar, .rpm a cpio). Keep in step with NewReader() - archive_register_test pins BOTH directions
// against LooksLikeContainerName, so a drift fails the build.
std::vector<ReadFormatInfo> ReadFormats() {
  std::vector<ReadFormatInfo> formats;
  formats.reserve(kNativeReadFormats.size());
  for (const ReadFormatSpec& spec : kNativeReadFormats) {
    formats.push_back({
        .name = std::string(spec.name),
        .suffixes = std::vector<std::string>(spec.suffixes.begin(), spec.suffixes.end()),
        .detail = std::string(spec.detail),
    });
  }
  std::vector<ReadFormatInfo> extensions = CompressionExtensionReadFormats();
  for (ReadFormatInfo& extension : extensions) {
    const auto found = std::find_if(formats.begin(), formats.end(), [&extension](const ReadFormatInfo& format) {
      return format.name == extension.name;
    });
    if (found == formats.end()) {
      formats.push_back(std::move(extension));
      continue;
    }
    found->suffixes.insert(
        found->suffixes.end(), std::make_move_iterator(extension.suffixes.begin()),
        std::make_move_iterator(extension.suffixes.end()));
    if (!extension.detail.empty()) {
      found->detail = absl::StrCat(found->detail, "; ", extension.detail);
    }
  }
  return formats;
}

}  // namespace

namespace {

// The write half, registered separately because it answers for FEWER containers than the opener: a
// phar or a compressed single file opens here and cannot be rewritten, and the writer says so.
absl::Status RemoveArchiveMembers(
    std::string_view container,
    const std::vector<std::string>& members,
    const vfs::MutationPolicy& policy) {
  // A tar-based or zip-based phar is an ordinary tar / zip that libarchive would happily rewrite - and
  // the result would be a container PHP rejects, because such a phar keeps its signature in a MEMBER
  // computed over everything else. Refusing beats silently breaking it, and the check is cheap: the
  // member list is read from headers only.
  if (const absl::StatusOr<std::vector<Member>> listed = ListMembersOfFile(container);
      listed.ok() && IsSignedTarOrZipPhar(*listed)) {
    return absl::UnimplementedError(
        absl::StrCat(
            "xff will not rewrite ", container,
            ": it is a tar-based or zip-based phar whose signature is a member (.phar/signature.bin),"
            " and removing anything would leave that signature stale, so PHP would reject the result"));
  }
  // Not const: it is returned by value, and a const local cannot move out of the function.
  absl::Status libarchive = RemoveMembersOfFile(container, members, policy);
  if (!absl::IsInvalidArgument(libarchive)) {
    return libarchive;
  }
  // libarchive said "not an archive", which for a container xff DIVED into means another reader opened
  // it. The native phar is the one such format xff can also write, so it gets its own attempt; its own
  // InvalidArgument then means neither reader owns this file as a rewritable archive.
  absl::Status phar = RemovePharMembersOfFile(container, members, policy);  // not const: see above
  if (!absl::IsInvalidArgument(phar)) {
    return phar;
  }
  // What is left is a container read through a path that has no write side at all: a compressed single
  // file (removing its one member means deleting the file, a different request), or a phar inside a
  // whole-file compression, which would have to be decompressed and recompressed around the rewrite.
  return absl::UnimplementedError(
      absl::StrCat(
          "xff can read ", container,
          " but not rewrite it: a compressed single file has no member list to rewrite, and a"
          " whole-file-compressed container would have to be recompressed around the change"));
}

}  // namespace

// The create half. A third registration rather than a mode on the second because the capabilities
// differ: this backend can CREATE a tar or zip it could never rewrite in place.
absl::Status PackNativeArchiveContainer(
    std::string_view path,
    const std::vector<PackFile>& files,
    const PackOptions& options) {
  std::vector<PackEntry> entries;
  entries.reserve(files.size());
  for (const PackFile& file : files) {
    entries.push_back(PackEntry{.source = file.source, .name = file.name});
  }
  PackSettings settings;
  settings.duplicates = options.duplicates;
  settings.mutations = options.mutations;
  settings.options.reserve(options.options.size());
  for (const PackOption& option : options.options) {
    settings.options.push_back({.name = option.name, .value = option.value});
  }
  return PackFiles(path, entries, settings);
}

namespace {

absl::Status PackArchiveContainer(
    std::string_view path,
    const std::vector<PackFile>& files,
    const PackOptions& options) {
  if (!CompressionExtensionPackFormatFor(path).empty()) {
    return PackCompressionExtension(path, files, options);
  }
  return PackNativeArchiveContainer(path, files, options);
}

// The vocabulary travels with the packer, so the CLI's pre-walk check and `--help=archive` both read
// the writer's own table rather than a copy that could drift from it.
std::vector<PackOptionInfo> PackVocabulary() {
  std::vector<PackOptionInfo> vocabulary;
  for (const PackOptionDoc& doc : PackOptionDocs()) {
    vocabulary.push_back(
        {.name = doc.name, .value_syntax = doc.value_syntax, .formats = doc.formats, .detail = doc.detail});
  }
  std::vector<PackOptionInfo> extensions = CompressionExtensionPackVocabulary();
  vocabulary.insert(
      vocabulary.end(), std::make_move_iterator(extensions.begin()), std::make_move_iterator(extensions.end()));
  return vocabulary;
}

std::vector<std::string> AggregatePackFormats() {
  std::vector<std::string> formats = PackFormats();
  std::vector<std::string> extensions = CompressionExtensionPackFormats();
  formats.insert(formats.end(), std::make_move_iterator(extensions.begin()), std::make_move_iterator(extensions.end()));
  return formats;
}

}  // namespace

void RegisterArchiveBackend() {
  RegisterContainerReader("archive", &OpenArchiveContainer, ReadFormats());
  RegisterContainerMemberRemover(&RemoveArchiveMembers);
  RegisterContainerPacker(&PackArchiveContainer, AggregatePackFormats(), PackVocabulary());
}

namespace {

struct ArchiveRegistrar {
  ArchiveRegistrar() { RegisterArchiveBackend(); }
};

// Registration runs before main; allocation failure is fatal during startup.
// Both spellings name the same intentional static-initialization exception.
// NOLINTNEXTLINE(fuchsia-statically-constructed-objects,cert-err58-cpp,bugprone-throwing-static-initialization)
const ArchiveRegistrar kRegisterArchive;

}  // namespace

}  // namespace xff::archive
