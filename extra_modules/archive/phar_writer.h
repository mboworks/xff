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

#ifndef XFF_ARCHIVE_PHAR_WRITER_H_
#define XFF_ARCHIVE_PHAR_WRITER_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "xff/archive/archive_reader.h"
#include "xff/vfs/mutations.h"

namespace xff::archive {

// Rewrites the native phar at `path` without `members`, the write half of `phar_reader.h`.
//
// libarchive cannot write a phar any more than it can read one, so this is hand-written too - but the
// format makes the job small. A phar is a stub, a manifest of fixed-shape entries, then each member's
// stored bytes IN MANIFEST ORDER with no absolute offsets anywhere. Removing a member is therefore:
// drop its manifest entry, drop its stored bytes, fix the member count and the manifest length, and
// recompute the trailing signature. Every surviving member's entry and bytes are copied VERBATIM, so
// nothing is re-encoded, nothing is re-compressed, and a per-member compression flag (gz / bz2) keeps
// working without this code knowing anything about codecs.
//
// The signature is the one part that must be recomputed rather than copied: it is a digest over the
// whole file before it, so any change invalidates it. `md5`, `sha1`, `sha256` and `sha512` are
// recomputed; an OpenSSL-signed phar is refused, because re-signing needs the private key.
//
// All or nothing, as for the libarchive path: the rewrite goes to a temporary file beside the original
// and is renamed over it only once complete, and the original's permission bits carry over.
//
// Errors: InvalidArgument when `path` is not a native phar; DataLoss when its manifest or data section
// does not check out; NotFound when a name in `members` is not in the archive (nothing is written
// then); Unimplemented for a signature this build cannot recompute; Unavailable when the temporary
// file or the rename fails.
[[nodiscard]] absl::Status RemovePharMembersOfFile(
    std::string_view path,
    const std::vector<std::string>& members,
    const vfs::MutationPolicy& policy = {});

// The member a TAR-based or ZIP-based phar keeps its signature in (php-src phar_tar.c / phar_zip.c
// write `.phar/signature.bin`). Those variants are ordinary tars and zips, so the libarchive writer
// can rewrite them - but the signature is a MEMBER there rather than a trailer, computed over the rest
// of the container, so a rewrite that copied it through would produce a container PHP rejects as
// tampered with. A caller must therefore refuse such a container rather than quietly break it, which
// is what this answers.
//
// `members` are the container's member names as listed. A `.phar` variant WITHOUT a signature member
// is not flagged: there is nothing stale to leave behind.
[[nodiscard]] bool IsSignedTarOrZipPhar(const std::vector<Member>& members);

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_PHAR_WRITER_H_
