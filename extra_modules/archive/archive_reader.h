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

#ifndef XFF_ARCHIVE_ARCHIVE_READER_H_
#define XFF_ARCHIVE_ARCHIVE_READER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/vfs/read_source.h"

namespace xff::archive {

// One member of an archive, as the reader sees it. Deliberately a plain description, not a VFS
// entry: the VFS backend (a later slice) maps these onto vfs::Metadata, so this layer stays a thin,
// testable shell over libarchive with no knowledge of the core's types.
struct Member {
  std::string path;           // member path as stored (`dir/file.txt`), never the container's path
  std::int64_t size = 0;      // UNCOMPRESSED (logical) size in bytes
  std::int64_t mtime = 0;     // modification time, seconds since the Unix epoch (0 when unset)
  std::uint32_t mode = 0;     // POSIX permission bits as stored
  bool is_directory = false;  // a directory member (an explicit entry, not merely a path prefix)
  bool is_symlink = false;    // a symlink member; `link_target` is what it points at
  std::string link_target;
};

// Lists the members of the archive in `bytes` (a whole in-memory archive). libarchive detects the
// format and the compression filter by content, so tar / zip / cpio / ar and the gz / bz2 / xz /
// zstd / lz4 filters all work through this one entry point.
//
// Returns InvalidArgumentError when the data is not an archive libarchive can open, or DataLossError
// when it opens but fails part way (a truncated or corrupt archive), so a caller can tell "not an
// archive" from "a broken archive" - the walk treats the first as a plain file and reports the
// second. Never reads member CONTENT, only the headers, so listing a huge archive stays cheap.
// A COMPRESSED SINGLE FILE - `notes.txt.gz`, not a `.tar.gz` - decompressed whole, or nullopt when the
// file is not one. This is the case libarchive's `raw` format exists for, and the case the reader
// deliberately does not register it for: `raw` accepts ANY bytes, so with it on every text file in a
// tree would present as a one-member archive. Here the decision is made from the NAME (a known
// compression suffix) and then confirmed by libarchive actually applying a filter other than `none`, so
// a `.gz` that is really text is still refused.
//
// The single member's name is the container's, minus the compression suffix (`notes.txt.gz` holds
// `notes.txt`), which is what gzip itself does with `-d`. Nothing else can be recovered: a bare
// compressed stream carries no member list.
[[nodiscard]] std::optional<std::string> CompressionSuffixStripped(std::string_view name);

// The decompressed content of a compressed single file at `path`, or InvalidArgument when it is not one
// (no compression suffix, or no filter applied). `max_bytes` (0 = unlimited) caps the output, the
// decompression-bomb guard every read here shares.
absl::StatusOr<std::string> ReadCompressedSingleFile(std::string_view path, std::uint64_t max_bytes = 0);

absl::StatusOr<std::vector<Member>> ListMembers(std::string_view bytes);

// Lists the members of the archive file at `path`, streaming it from disk rather than requiring the
// whole archive in memory (the form the walk uses). Same error contract as ListMembers.
absl::StatusOr<std::vector<Member>> ListMembersOfFile(std::string_view path);

// Reads ONE member's uncompressed content out of the archive file at `path`. This is what lets the
// content predicates (`-grep`, `-content`, `{hash}`) work on members; listing alone cannot, since it
// deliberately never touches member data.
//
// `member` is matched against the stored path in normalized form: a leading `./` is ignored on either
// side, and so is a trailing `/`, because tar writes the same member several ways (`dir/x` vs
// `./dir/x`, and a directory as `dir/`). So a lookup for `dir` FINDS the directory and is told it has
// no content, rather than misreporting "no such member". Streaming, single pass, stopping at the
// match, so reading an early member of a huge archive does not decompress the rest.
//
// Errors, all distinguishable on purpose: InvalidArgument when `path` is not an archive libarchive
// can open, NotFound when the archive has no such member, FailedPrecondition when the member exists
// but is not a regular file (a directory or symlink has no content to read), DataLoss when the
// archive opens but the read fails part way, and ResourceExhausted when `max_bytes` (0 = unlimited)
// would be exceeded - a decompression-bomb guard, since a small member header can promise a huge
// expansion.
absl::StatusOr<std::string> ReadMemberOfFile(
    std::string_view path,
    std::string_view member,
    std::uint64_t max_bytes = 0);

// ReadMemberOfFile over bytes already in memory, with identical semantics and errors. This is what
// a container INSIDE a container needs: its own bytes come out of its parent, so there is no path to
// open. Empty input is not an archive, as in ListMembers.
absl::StatusOr<std::string> ReadMember(std::string_view bytes, std::string_view member, std::uint64_t max_bytes = 0);

// Streaming sources retain their parent chain and do not materialize nested containers.
absl::StatusOr<std::vector<Member>> ListMembersOfSource(const vfs::SharedReadSource& source);
vfs::SharedReadSource MemberReadSource(vfs::SharedReadSource source, std::string member);

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_ARCHIVE_READER_H_
