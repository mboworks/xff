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

#include "xff/archive/archive_fs.h"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_extension.h"
#include "xff/archive/archive_reader.h"
#include "xff/archive/member_path.h"
#include "xff/archive/phar_reader.h"
#include "xff/vfs/entry.h"

namespace xff::archive {
namespace {

// A stored member path, normalized for indexing: no leading "./", no trailing slash. The trailing
// slash is how tar marks a directory, so it is information - the caller records the type before
// stripping it, and an ABSOLUTE member keeps its leading slash (see member_path.h: hiding it would
// hide the Zip-Slip red flag).
std::string IndexKey(std::string_view stored) {
  std::string_view key = stored;
  while (key.starts_with("./")) {
    key.remove_prefix(2);
  }
  while (key.size() > 1 && key.ends_with('/')) {
    key.remove_suffix(1);
  }
  return std::string(key);
}

std::string_view ParentOf(std::string_view key) {
  const std::string_view::size_type slash = key.rfind('/');
  return slash == std::string_view::npos ? std::string_view{} : key.substr(0, slash);
}

std::string_view NameOf(std::string_view key) {
  const std::string_view::size_type slash = key.rfind('/');
  return slash == std::string_view::npos ? key : key.substr(slash + 1);
}

vfs::Metadata DirectoryMetadata() {
  return vfs::Metadata{
      .type = vfs::FileType::kDirectory,
      .source = vfs::Source::kArchiveMember,
      // 0555: readable and traversable, never writable - members cannot be modified.
      .mode = 0555,
  };
}

// A synthetic device id for one container, so that every member of it shares a device and members of
// two containers do not. Derived from the container path rather than counted, so the value is stable
// across runs (a walk that prints it, or compares it between invocations, sees no churn).
//
// The high bit is set to keep it clear of real device numbers, which are small: a member must never
// compare equal to a file on a real filesystem.
std::uint64_t SyntheticDevice(std::string_view container) {
  constexpr std::uint64_t kVirtualDeviceBit = std::uint64_t{1} << 63U;
  return kVirtualDeviceBit | absl::HashOf(container);
}

}  // namespace

absl::StatusOr<ArchiveFileSystem> ArchiveFileSystem::OpenSource(
    std::string_view container,
    vfs::SharedReadSource source,
    MemberPathOptions options) {
  MBO_ASSIGN_OR_RETURN(auto probe, source->Open());
  std::string prefix;
  while (prefix.size() < 4) {
    MBO_ASSIGN_OR_RETURN(const std::string bytes, probe->Read(4 - prefix.size()));
    if (bytes.empty()) {
      break;
    }
    prefix += bytes;
  }
  MBO_ASSIGN_OR_RETURN(auto decoded, DecodeSource(source));
  if (prefix == "pbzx" && decoded == source) {
    return absl::UnimplementedError("PBZX package payload requires the apple extension");
  }
  auto members = ListMembersOfSource(decoded);
  if (absl::IsInvalidArgument(members.status()) && decoded != source) {
    return absl::DataLossError("decoded package payload is not a supported archive");
  }
  MBO_RETURN_IF_ERROR(members.status());
  MBO_ASSIGN_OR_RETURN(auto fs, Index(std::string(container), {}, *members, options));
  source = std::move(decoded);
  fs.source_ = std::move(source);
  fs.package_ = prefix == "xar!"
                && (absl::EndsWithIgnoreCase(container, ".pkg") || absl::EndsWithIgnoreCase(container, ".mpkg")
                    || absl::EndsWithIgnoreCase(container, ".xip"));
  return fs;
}

absl::StatusOr<vfs::SharedReadSource> ArchiveFileSystem::ContentSource(std::string_view path) const {
  if (!source_) {
    return FileSystem::ContentSource(path);
  }
  MBO_ASSIGN_OR_RETURN(const auto metadata, Stat(path, false));
  if (metadata.type != vfs::FileType::kRegular) {
    return absl::FailedPreconditionError("archive member is not a regular file");
  }
  const auto key = MemberKeyOf(path);
  if (!key) {
    return absl::NotFoundError("not an archive member");
  }
  return MemberReadSource(source_, *key);
}

bool ArchiveFileSystem::ContainerCandidate(std::string_view path) const {
  const auto key = MemberKeyOf(path);
  if (!key) {
    return false;
  }
  const std::string_view name = NameOf(*key);
  const bool payload = name == "Payload" || name == "Scripts" || name == "Content";
  return payload && package_;
}

absl::StatusOr<ArchiveFileSystem> ArchiveFileSystem::Open(std::string_view container, MemberPathOptions options) {
  // The reader's status passes through unchanged: "not a readable archive" and "corrupt archive" are
  // different answers and the caller decides what to do with each.
  // libarchive first, the phar reader second. A native phar is a PHP stub followed by a manifest
  // libarchive knows nothing about, so it answers InvalidArgument ("not an archive") - and without this
  // fallback the reader that DOES understand it is unreachable from a real run, which is exactly what
  // happened: every native fixture listed zero members while the tar/zip-based ones worked. Only
  // InvalidArgument falls through: a corrupt archive (DataLoss) is an answer, not a reason to guess
  // again with a different parser.
  auto streamed = OpenSource(container, vfs::HostReadSource(std::string(container)), options);
  if (streamed.ok() || !absl::IsInvalidArgument(streamed.status())) {
    return streamed;
  }
  absl::StatusOr<std::vector<Member>> members = ListMembersOfFile(container);
  bool phar = false;
  if (absl::IsInvalidArgument(members.status())) {
    absl::StatusOr<std::vector<Member>> phar_members = ListPharMembersOfFile(container);
    if (phar_members.ok()) {
      members = std::move(phar_members);
      phar = true;
    } else if (!absl::IsInvalidArgument(phar_members.status())) {
      return phar_members.status();  // it IS a phar, and a broken one: say so
    }
  }
  if (absl::IsInvalidArgument(members.status())) {
    if (CompressionExtensionFor(container).has_value()) {
      MBO_ASSIGN_OR_RETURN(std::string content, DecodeCompressionExtension(container, std::nullopt));
      return OpenDecodedExtension(container, std::move(content), options);
    }
    // Third and last: a compressed single file. Its InvalidArgument means "not that either", which
    // falls through to report the original member-list error below.
    absl::StatusOr<ArchiveFileSystem> single = OpenCompressedSingle(container, options);
    if (single.ok() || !absl::IsInvalidArgument(single.status())) {
      return single;
    }
  }
  MBO_RETURN_IF_ERROR(members.status());
  MBO_ASSIGN_OR_RETURN(ArchiveFileSystem fs, Index(std::string(container), std::string(), *members, options));
  fs.phar_ = phar;
  // Not single_: the only path that sets it returns from inside the branch above, so reaching here
  // means this container HAS a member list and the field keeps its false default.
  return fs;
}

absl::StatusOr<ArchiveFileSystem> ArchiveFileSystem::OpenCompressedSingle(
    std::string_view container,
    MemberPathOptions options) {
  // A compressed single file has no member list to read, so the content is decompressed once here
  // and the filesystem holds it - which also makes the size it reports the real, uncompressed one.
  MBO_ASSIGN_OR_RETURN(std::string content, ReadCompressedSingleFile(container));
  return OpenDecodedExtension(container, std::move(content), options);
}

absl::StatusOr<ArchiveFileSystem> ArchiveFileSystem::OpenDecodedExtension(
    std::string_view container,
    std::string content,
    MemberPathOptions options) {
  // What the decompressed bytes turn out to BE decides this. `Phar::compress()` wraps a whole phar,
  // and `.tar.gz` is a whole tar, so a container can hide inside the compression: OpenBytes asks
  // libarchive and then the phar reader about the decompressed bytes, and when either claims them
  // the members are the real answer. Only when nothing claims them is this a single compressed FILE.
  absl::StatusOr<ArchiveFileSystem> inner = OpenBytesImpl(container, content, options, /*allow_extensions=*/false);
  if (inner.ok() || !absl::IsInvalidArgument(inner.status())) {
    return inner;
  }
  const std::string_view::size_type slash = container.rfind('/');
  const std::string_view name = slash == std::string_view::npos ? container : container.substr(slash + 1);
  std::optional<std::string> stem = CompressionSuffixStripped(name);
  if (!stem.has_value()) {
    stem = CompressionExtensionStem(name);
  }
  MBO_ASSIGN_OR_RETURN(
      ArchiveFileSystem fs, Index(
                                std::string(container), std::string(),
                                {Member{
                                    .path = stem.value_or(std::string(name)),
                                    .size = static_cast<std::int64_t>(content.size()),
                                    .mode = 0444,
                                }},
                                options));
  fs.single_file_content_ = std::move(content);
  fs.single_ = true;
  return fs;
}

absl::StatusOr<ArchiveFileSystem> ArchiveFileSystem::OpenBytes(
    std::string_view container,
    std::string bytes,
    MemberPathOptions options) {
  auto streamed = OpenSource(container, vfs::MemoryReadSource(bytes), options);
  if (streamed.ok() || !absl::IsInvalidArgument(streamed.status())) {
    return streamed;
  }
  return OpenBytesImpl(container, std::move(bytes), options, /*allow_extensions=*/true);
}

absl::StatusOr<ArchiveFileSystem> ArchiveFileSystem::OpenBytesImpl(
    std::string_view container,
    std::string bytes,
    MemberPathOptions options,
    bool allow_extensions) {
  // A container INSIDE a container: its bytes came out of its parent, so there is no path to open
  // and the filesystem keeps them for as long as it lives (member reads stream from them).
  absl::StatusOr<std::vector<Member>> members = ListMembers(bytes);
  bool phar = false;
  if (absl::IsInvalidArgument(members.status())) {
    absl::StatusOr<std::vector<Member>> phar_members = ListPharMembers(bytes);
    if (phar_members.ok()) {
      members = std::move(phar_members);
      phar = true;
    } else if (!absl::IsInvalidArgument(phar_members.status())) {
      return phar_members.status();
    }
  }
  if (allow_extensions && absl::IsInvalidArgument(members.status()) && CompressionExtensionFor(container).has_value()) {
    MBO_ASSIGN_OR_RETURN(std::string content, DecodeCompressionExtension(container, bytes));
    return OpenDecodedExtension(container, std::move(content), options);
  }
  MBO_RETURN_IF_ERROR(members.status());
  MBO_ASSIGN_OR_RETURN(ArchiveFileSystem fs, Index(std::string(container), std::move(bytes), *members, options));
  fs.phar_ = phar;
  return fs;
}

// The shared indexing pass: whatever the source, a member list becomes nodes the same way.
absl::StatusOr<ArchiveFileSystem> ArchiveFileSystem::Index(
    std::string container,
    std::string bytes,
    const std::vector<Member>& members,
    MemberPathOptions options) {
  ArchiveFileSystem fs(std::move(container), options);
  fs.bytes_ = std::move(bytes);
  const std::uint64_t device = SyntheticDevice(fs.container_);
  // Inode numbers are handed out in index order, starting at 1 so that 0 stays "unset". They have to
  // be DISTINCT per member: the walk's loop detector keys on (dev, ino) and reports "filesystem loop
  // detected" the second time it sees a pair, so members all reporting {0, 0} would make the second
  // directory in any archive look like a cycle. Synthesizing them also makes `-xdev` do the right
  // thing - one device per container, so a walk that started on a real filesystem stops at it.
  std::uint64_t next_ino = 1;
  for (const Member& member : members) {
    const bool is_dir = member.path.ends_with('/') || member.is_directory;
    const std::string key = IndexKey(member.path);
    if (key.empty()) {
      continue;  // the archive's own root, which the container path already names
    }
    Node node;
    // The reader reports the type explicitly, so trust it rather than inferring from link_target.
    node.metadata.type = is_dir              ? vfs::FileType::kDirectory
                         : member.is_symlink ? vfs::FileType::kSymlink
                                             : vfs::FileType::kRegular;
    node.metadata.size = static_cast<std::uint64_t>(member.size < 0 ? 0 : member.size);
    node.metadata.mode = member.mode != 0 ? member.mode : (is_dir ? 0555U : 0444U);
    node.metadata.mtime = absl::FromUnixSeconds(member.mtime);
    node.metadata.dev = device;
    node.metadata.ino = next_ino++;
    node.metadata.nlink = 1;
    // What makes -delete and the exec family refuse this entry rather than act on a path no process
    // can open. The listing's Entry already says read_only; this is the same fact where the walk and
    // the evaluator can see it.
    node.metadata.source = vfs::Source::kArchiveMember;
    node.link_target = member.link_target;
    fs.nodes_[key] = std::move(node);

    // Synthesize every missing ancestor: tar streams routinely omit directory entries, and a walk
    // that trusted the stored list would never descend into them.
    for (std::string_view parent = ParentOf(key); !parent.empty(); parent = ParentOf(parent)) {
      const std::string parent_key(parent);
      if (fs.nodes_.contains(parent_key)) {
        break;  // this ancestor exists, so all further ancestors do too
      }
      Node parent_node{.metadata = DirectoryMetadata(), .synthesized = true};
      parent_node.metadata.dev = device;
      parent_node.metadata.ino = next_ino++;
      parent_node.metadata.nlink = 1;
      fs.nodes_[parent_key] = std::move(parent_node);
    }
  }
  return fs;
}

std::optional<std::string> ArchiveFileSystem::MemberKeyOf(std::string_view path) const {
  if (path == container_) {
    return std::string{};  // the container itself: the archive's root directory
  }
  // The probing split, with the answer already known: this filesystem's container is the only one it
  // can be. A plain first-separator split would be wrong for a NESTED container, whose own path
  // contains a separator (`outer.tar!inner.tar`) and would be attributed to the outer archive.
  const std::optional<MemberPathParts> parts =
      SplitMemberPath(path, options_, [this](std::string_view candidate) { return candidate == container_; });
  if (!parts.has_value()) {
    return std::nullopt;
  }
  return IndexKey(parts->member);
}

absl::StatusOr<std::vector<vfs::Entry>> ArchiveFileSystem::ReadDir(std::string_view dir) const {
  const std::optional<std::string> key = MemberKeyOf(dir);
  if (!key.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("not a path in ", container_, ": ", dir));
  }
  if (!key->empty()) {
    const auto found = nodes_.find(*key);
    if (found == nodes_.end()) {
      return absl::NotFoundError(absl::StrCat("no such member: ", *key));
    }
    if (found->second.metadata.type != vfs::FileType::kDirectory) {
      return absl::InvalidArgumentError(absl::StrCat("not a directory: ", *key));
    }
  }
  std::vector<vfs::Entry> entries;
  for (const auto& [member_key, node] : nodes_) {
    if (ParentOf(member_key) != *key) {
      continue;  // only direct children; the walk recurses for the rest
    }
    entries.push_back(
        vfs::Entry{
            .path = JoinMemberPath(container_, member_key, options_),
            .name = std::string(NameOf(member_key)),
            .type = node.metadata.type,
            // Both fields matter downstream: kArchiveMember tags the origin, and read_only is what
            // makes -delete and the exec family refuse rather than silently skip.
            .source = vfs::Source::kArchiveMember,
            .read_only = true,
        });
  }
  return entries;
}

absl::StatusOr<vfs::Metadata> ArchiveFileSystem::Stat(std::string_view path, bool /*follow_symlinks*/) const {
  const std::optional<std::string> key = MemberKeyOf(path);
  if (!key.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("not a path in ", container_, ": ", path));
  }
  if (key->empty()) {
    // The container presents as a directory here so a walk can enter it. Its real-filesystem
    // identity (size, mode, times of the archive FILE) comes from the local backend, not this one.
    return DirectoryMetadata();
  }
  const auto found = nodes_.find(*key);
  if (found == nodes_.end()) {
    return absl::NotFoundError(absl::StrCat("no such member: ", *key));
  }
  return found->second.metadata;
}

absl::Status ArchiveFileSystem::Remove(std::string_view path) const {
  // A refusal, never a silent success: an archive member cannot be deleted in place, and reporting
  // success would make `-delete` look like it worked.
  return absl::PermissionDeniedError(absl::StrCat("archive members are read-only, cannot remove: ", path));
}

bool ArchiveFileSystem::Access(std::string_view path, vfs::AccessMode mode) const {
  if (mode == vfs::AccessMode::kWrite) {
    return false;  // read-only, unconditionally
  }
  const std::optional<std::string> key = MemberKeyOf(path);
  if (!key.has_value()) {
    return false;
  }
  if (key->empty()) {
    return true;  // the archive root is readable and traversable
  }
  const auto found = nodes_.find(*key);
  if (found == nodes_.end()) {
    return false;
  }
  const std::uint32_t bits = found->second.metadata.mode;
  // Any of user/group/other suffices: xff reports what the archive stored, and a member has no
  // meaningful owner on the reading machine.
  return mode == vfs::AccessMode::kRead ? (bits & 0444U) != 0 : (bits & 0111U) != 0;
}

absl::StatusOr<std::string> ArchiveFileSystem::ReadLink(std::string_view path) const {
  const std::optional<std::string> key = MemberKeyOf(path);
  if (!key.has_value() || key->empty()) {
    return absl::InvalidArgumentError(absl::StrCat("not a member path: ", path));
  }
  const auto found = nodes_.find(*key);
  if (found == nodes_.end()) {
    return absl::NotFoundError(absl::StrCat("no such member: ", *key));
  }
  if (found->second.metadata.type != vfs::FileType::kSymlink) {
    return absl::InvalidArgumentError(absl::StrCat("not a symlink: ", *key));
  }
  return found->second.link_target;
}

absl::StatusOr<std::string> ArchiveFileSystem::FsType(std::string_view /*path*/) const {
  return std::string("archive");
}

absl::StatusOr<bool> ArchiveFileSystem::IsCaseSensitive(std::string_view /*path*/) const {
  // Stored member names are bytes, compared byte-wise, whatever the host filesystem does.
  return true;
}

absl::StatusOr<std::string> ArchiveFileSystem::ReadContent(std::string_view path) const {
  // What makes -grep / -content / {hash} work on a member: the bytes come out of the container, one
  // streamed pass per read. The index is consulted FIRST so this filesystem's own errors (not mine,
  // no such member, has no content) are answered from what it already knows, and libarchive is only
  // asked to extract something that is really there.
  const std::optional<std::string> key = MemberKeyOf(path);
  if (!key.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("not a path in ", container_, ": ", path));
  }
  if (key->empty()) {
    // The container itself: as a directory here (see Stat), it has no content of its own. Reading
    // the archive FILE is the local backend's job, and the walk asks that one for it.
    return absl::FailedPreconditionError(absl::StrCat("not a regular file: ", container_));
  }
  const auto found = nodes_.find(*key);
  if (found == nodes_.end()) {
    return absl::NotFoundError(absl::StrCat("no such member: ", *key));
  }
  if (found->second.metadata.type != vfs::FileType::kRegular) {
    return absl::FailedPreconditionError(absl::StrCat("not a regular file: ", *key));
  }
  // Whichever reader indexed this container also extracts from it: a phar's data lies at offsets its
  // own manifest gives, which libarchive cannot compute.
  if (single_) {
    return single_file_content_;  // one member, already decompressed when the container was opened
  }
  if (std::optional<std::string> cached = member_cache_->Get(*key); cached.has_value()) {
    return *std::move(cached);
  }
  absl::StatusOr<std::string> content;
  if (source_) {
    content = vfs::ReadSourceBytes(*MemberReadSource(source_, *key), std::numeric_limits<std::size_t>::max());
  } else if (phar_) {
    content = bytes_.empty() ? ReadPharMemberOfFile(container_, *key) : ReadPharMember(bytes_, *key);
  } else {
    content = bytes_.empty() ? ReadMemberOfFile(container_, *key) : ReadMember(bytes_, *key);
  }
  if (content.ok()) {
    member_cache_->Put(*key, *content);
  }
  return content;
}

}  // namespace xff::archive
