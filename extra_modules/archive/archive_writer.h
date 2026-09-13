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

#ifndef XFF_ARCHIVE_ARCHIVE_WRITER_H_
#define XFF_ARCHIVE_ARCHIVE_WRITER_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "xff/vfs/mutations.h"

namespace xff::archive {

// Rewrites the archive at `path` without `members`, which is the only WRITE this reader-shaped
// library has. There is no such thing as removing a member in place: an archive is a stream of
// header+data records, so the file is written again from its remaining members.
//
// The new archive keeps the original's FORMAT and compression FILTER, read off the original rather
// than guessed, so a `.tar.gz` stays a gzipped tar and a `.zip` stays a zip. Everything else about
// each surviving member - its stored name, mode, times, link target - is copied through untouched.
//
// All or nothing: the rewrite goes to a temporary file beside the original and is renamed over it
// only after it completes, so an interrupted or failing run leaves `path` exactly as it was. The
// original's permission bits are carried over to the replacement.
//
// `members` are member names, matched the way the reader matches them (a leading `./` and a trailing
// `/` are ignored on both sides), so the names xff printed are the names that work here.
//
// Errors: InvalidArgument when `path` is not an archive this build can open; Unimplemented when it
// can be read but not written back (7-Zip, RAR, ISO and the like, which libarchive reads only) -
// distinct, because the second is a property of the format rather than of the file; NotFound when a
// name in `members` is not in the archive (nothing is written then, so a typo cannot silently do
// half the job); DataLoss when the read fails part way; Unavailable when the temporary file or the
// rename fails.
[[nodiscard]] absl::Status RemoveMembersOfFile(
    std::string_view path,
    const std::vector<std::string>& members,
    const vfs::MutationPolicy& policy = {});

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_ARCHIVE_WRITER_H_
