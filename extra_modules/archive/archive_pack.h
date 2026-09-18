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

#ifndef XFF_ARCHIVE_ARCHIVE_PACK_H_
#define XFF_ARCHIVE_ARCHIVE_PACK_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "xff/archive/archive_backend.h"
#include "xff/vfs/mutations.h"

namespace xff::archive {

// One file to write into a new archive: where to read it from, and the name it gets INSIDE.
// Separating the two is the whole point - the walk knows the path on disk, the caller decides what
// the member should be called, and neither has to guess the other.
struct PackEntry {
  std::string source;  // the path on disk to read
  std::string name;    // relative member destination; normalized and checked before writing
};

// One writer setting, in XFF's vocabulary rather than libarchive's. The names here are xff's own and
// are TRANSLATED per format (see PackOptionDocs): that is the whole point of the indirection, because
// libarchive's own option names differ between its versions and between its writers, and a name xff
// passed through unchecked could be neither documented from a table nor validated.
struct PackSetting {
  std::string name;
  std::string value;
};

// How the archive is written, beyond what the output name already decides. An empty list means
// libarchive's defaults, which is what a caller with no opinion should pass. The last value given for
// a name wins, so a caller can append without first removing.
struct PackSettings {
  std::vector<PackSetting> options;
  PackDuplicatePolicy duplicates = PackDuplicatePolicy::kError;
  vfs::MutationPolicy mutations;
};

// One entry of the option vocabulary, for the usage error and for the generated help. `formats` is
// the comma-joined list of output names the option applies to, so a reader can see at a glance that
// `zip64` is a zip thing and `threads` is not a gzip one.
struct PackOptionDoc {
  std::string name;
  std::string value_syntax;  // "N", "yes|no", "store|deflate"
  absl::Span<const std::string_view> formats;
  std::string detail;
};

// The whole vocabulary, in the order the help lists it. The SOT for the check, the error message and
// the documentation together, so they cannot disagree.
[[nodiscard]] std::vector<PackOptionDoc> PackOptionDocs();

// Writes `entries` into a NEW archive at `path`, format chosen from the output NAME (see
// FormatFromName). This is the counterpart of the reader: xff walks, matches, and hands the result
// here, so an archive is built from an expression rather than from a file list piped through tar.
//
// Member destinations are normalized and checked before output creation. Duplicate destinations
// fail by default; first-wins keeps the earliest entry in input order. No root prefix is invented.
// Absolute names, parent traversal, and embedded NULs are rejected.
//
// All or nothing, as the rewrite path is: the archive is built beside `path` and renamed over it
// only after every entry is written, so a failure part way leaves no half archive behind - and an
// existing file at `path` survives an attempt that fails.
//
// Errors: InvalidArgument when the output name carries no format this build can write, when an
// option name is not in the vocabulary, when it does not apply to the chosen format, or when its
// value is the wrong shape; NotFound when a source path cannot be opened (nothing is written then, so
// a mistyped root cannot silently produce a partial archive); Unavailable when the temporary file or
// the rename fails. Every option is checked BEFORE the first entry is written.
[[nodiscard]] absl::Status PackFiles(
    std::string_view path,
    const std::vector<PackEntry>& entries,
    const PackSettings& options = {});

// The archive format an output NAME asks for, as a libarchive format+filter pair name ("tar.gz",
// "zip", ...), or empty when the name carries none. Public because the CLI validates the flag
// BEFORE the walk: spending a whole traversal to then report "I cannot write .rar" is the wrong
// order, and the accepted set belongs in one place rather than in a message.
[[nodiscard]] std::string FormatFromName(std::string_view path);

// Every output name FormatFromName accepts, in the order the help lists them. The SOT for both the
// check and the documentation, so they cannot disagree.
[[nodiscard]] std::vector<std::string> PackFormats();

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_ARCHIVE_PACK_H_
