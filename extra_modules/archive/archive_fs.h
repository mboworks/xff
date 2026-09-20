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

#ifndef XFF_ARCHIVE_ARCHIVE_FS_H_
#define XFF_ARCHIVE_ARCHIVE_FS_H_

// A read-only `vfs::FileSystem` over ONE archive container, so the whole expression grammar
// (`-name`, `-size`, `-type`, the printing actions) matches an archive's members at virtual paths
// like `foo.tgz!inner/x` - the spelling being whatever --archive-separator / --archive-prefix say
// (`member_path.h`).
//
// It implements the seam from @xff_extras_api, never the xff core, so the archive extra stays a
// standalone module the core knows nothing about.
//
// Three properties are decisions rather than implementation details:
//
//  * Every member is `read_only` with `source == kArchiveMember`. That is what makes `-delete` and
//    the exec family REFUSE members instead of silently skipping them.
//  * Implicit parent directories are SYNTHESIZED. Tar streams routinely store `a/b/c.txt` with no
//    entry for `a/` or `a/b/`, so a walk that trusted the stored list would find nothing to descend.
//  * The CONTAINER keeps its real-filesystem identity: this backend answers only for the container
//    path itself (as a directory, so a walk can enter it) and for member paths under it. The real
//    file stays a real file to the rest of xff, which is what "dual identity" means.
//
// `ReadContent` is deliberately unimplemented in this slice: the reader lists members but does not
// extract data yet, so content predicates (`-grep`, `-content`, `{hash}`) get a clear Unimplemented
// rather than silence or an empty string. The next slice adds member extraction.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/archive/archive_reader.h"
#include "xff/archive/member_cache.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::archive {

class ArchiveFileSystem : public vfs::FileSystem {
 public:
  // Opens `container` (a real filesystem path) and indexes its members. Returns the reader's error
  // unchanged when the file is not a readable archive, so "not an archive" and "corrupt archive"
  // stay distinguishable to the caller.
  static absl::StatusOr<ArchiveFileSystem> Open(std::string_view container, MemberPathOptions options = {});

  // Opens a container whose BYTES are already in hand rather than on disk - a container nested in
  // another one, whose parent handed over its content. `container` is only the label the member
  // paths are rendered with; the bytes are kept for the filesystem's lifetime, since every member
  // read streams from them.
  static absl::StatusOr<ArchiveFileSystem> OpenBytes(
      std::string_view container,
      std::string bytes,
      MemberPathOptions options = {});

  static absl::StatusOr<ArchiveFileSystem> OpenSource(
      std::string_view container,
      vfs::SharedReadSource source,
      MemberPathOptions options = {});
  absl::StatusOr<vfs::SharedReadSource> ContentSource(std::string_view path) const override;
  bool ContainerCandidate(std::string_view path) const override;

  [[nodiscard]] absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override;
  [[nodiscard]] absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool follow_symlinks) const override;
  [[nodiscard]] absl::Status Remove(std::string_view path) const override;
  [[nodiscard]] bool Access(std::string_view path, vfs::AccessMode mode) const override;
  [[nodiscard]] absl::StatusOr<std::string> ReadLink(std::string_view path) const override;
  [[nodiscard]] absl::StatusOr<std::string> FsType(std::string_view path) const override;
  [[nodiscard]] absl::StatusOr<bool> IsCaseSensitive(std::string_view path) const override;
  [[nodiscard]] absl::StatusOr<std::string> ReadContent(std::string_view path) const override;

  // The container this filesystem was opened on, as given.
  [[nodiscard]] std::string_view container() const { return container_; }

 private:
  // One indexed node: a member as stored, or a directory synthesized from a member's parents.
  struct Node {
    vfs::Metadata metadata;
    std::string link_target;
    bool synthesized = false;  // no stored entry of its own; inferred from a child's path
  };

  ArchiveFileSystem(std::string container, MemberPathOptions options)
      : container_(std::move(container)), options_(options) {}

  // The third probe in Open's chain: a COMPRESSED SINGLE FILE (`notes.txt.gz`, not `.tar.gz`).
  // InvalidArgument keeps Open's own convention: "not mine, try the next explanation"; any other
  // error is an answer. On success the filesystem holds the decompressed content.
  static absl::StatusOr<ArchiveFileSystem> OpenCompressedSingle(std::string_view container, MemberPathOptions options);

  static absl::StatusOr<ArchiveFileSystem> OpenBytesImpl(
      std::string_view container,
      std::string bytes,
      MemberPathOptions options,
      bool allow_extensions);
  static absl::StatusOr<ArchiveFileSystem> OpenDecodedExtension(
      std::string_view container,
      std::string content,
      MemberPathOptions options);

  // Builds the node index from a member list, whatever the source. `bytes` is empty for a
  // path-backed container.
  static absl::StatusOr<ArchiveFileSystem> Index(
      std::string container,
      std::string bytes,
      const std::vector<Member>& members,
      MemberPathOptions options);

  // Resolves an incoming path to a member key: empty means the container itself (the archive's
  // root), nullopt means the path does not belong to this filesystem at all.
  [[nodiscard]] std::optional<std::string> MemberKeyOf(std::string_view path) const;

  vfs::SharedReadSource source_;
  bool package_ = false;
  std::string container_;
  // The container's own bytes, for a nested container; empty when `container_` is a real path and
  // reads stream from the file instead.
  std::string bytes_;
  // Which reader owns this container: the phar reader when libarchive declined it and the phar parser
  // accepted it. Member reads must go back to the same one, since a phar's data offsets come from its
  // manifest rather than from any format libarchive knows.
  bool phar_ = false;
  // A compressed SINGLE file (`notes.txt.gz`): one member, whose content is decompressed once at open
  // and kept here. There is no member list in such a container and no second read to make.
  bool single_ = false;
  std::string single_file_content_;
  // Extracted-member content, so a composed expression (`-grep` + `{hash}` + `-cmp`) does not
  // decompress the same member once per predicate. Behind a pointer because `absl::Mutex` is not
  // movable and this filesystem is returned by value; mutable because a read is logically const.
  mutable std::unique_ptr<MemberCache> member_cache_ = std::make_unique<MemberCache>();
  MemberPathOptions options_;
  // Keyed by member path as stored, without a trailing slash. Ordered so ReadDir output is stable
  // without a sort, which keeps a walk's ordering reproducible.
  std::map<std::string, Node, std::less<>> nodes_;
};

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_ARCHIVE_FS_H_
