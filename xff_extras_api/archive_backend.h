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

#ifndef XFF_ARCHIVE_ARCHIVE_BACKEND_H_
#define XFF_ARCHIVE_ARCHIVE_BACKEND_H_

// The seam through which the core opens an archive container, without depending on the extra that
// can do it.
//
// Same shape as the PCRE2 backend slot next door (`RegisterPcre2Backend` / `Pcre2Available`), and for
// the same reason: the archive reader lives in a REMOVABLE module (`extra_modules/archive`), so the
// core cannot name it. The extra self-registers an opener at static init; a build without the extra
// registers nothing, `ContainerSupportAvailable()` answers false, and `--archive` can then say "this
// binary was not built with it" instead of failing obscurely somewhere in the walk.
//
// The opener yields a `vfs::FileSystem` over ONE container, which is what lets the walk treat members
// as ordinary entries: the whole predicate and action vocabulary works on them unchanged, and nothing
// in the engine needs to know a container is involved beyond deciding to mount one.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/mutations.h"

namespace xff::archive {

// Opens `container` (a real filesystem path) as a read-only filesystem over its members. `options`
// carries the member-path spelling (`--archive-separator` / `--archive-prefix`) so rendered paths
// round-trip through the same flags the user set.
//
// Must return InvalidArgumentError when the file is not an archive it can open, and DataLossError when
// it opens but is corrupt: the walk treats the first as an ordinary file and only reports the second,
// so collapsing them would turn every non-archive into an error.
// `bytes`, when present, IS the container's content: a container nested in another one has no path
// of its own, so its parent hands over what it read and `container` is only the label member paths
// are rendered with. Absent means "open the path".
using ContainerOpener = absl::AnyInvocable<absl::StatusOr<
    std::unique_ptr<vfs::FileSystem>>(std::string_view, std::optional<std::string_view>, MemberPathOptions) const>;

// Streaming counterpart; the source owns its parent chain and supports independent cursors.
using ContainerSourceOpener = absl::AnyInvocable<
    absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(std::string_view, vfs::SharedReadSource, MemberPathOptions) const>;

// One format a reader understands, for the `--help=archive` formats table and the cheap name gate.
// The reader still validates content; suffixes only decide which ordinary names are worth offering.
struct ReadFormatInfo {
  std::string name;
  std::vector<std::string> suffixes;
  std::string detail;
};

// Adds or replaces one independently linked container reader. Readers are kept by stable `name` and
// tried in name order, so static-initialization order cannot decide which backend sees a container
// first. InvalidArgument means "not my format" and falls through; every other status is the reader's
// definitive answer. Passing an empty opener removes that name, which keeps tests able to restore the
// process-wide registry.
void RegisterContainerReader(
    std::string name,
    ContainerOpener opener,
    std::vector<ReadFormatInfo> formats,
    ContainerSourceOpener source_opener = {});

[[nodiscard]] absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenContainerSource(
    std::string_view container,
    const vfs::SharedReadSource& source,
    MemberPathOptions options = {});

// Installs a process-wide override opener. This is the narrow test seam retained for callers that
// need to replace the composed registry temporarily; production extras use RegisterContainerReader.
void RegisterContainerOpener(ContainerOpener opener);

// Self-registers `opener` on construction. Declare one at namespace scope in the backend's TU, in a
// target marked `alwayslink` so the linker cannot drop it:
//
//   const xff::archive::ContainerRegistrar kRegisterArchive{
//       "archive", &OpenArchiveContainer, ReadFormats()};
struct ContainerRegistrar {
  ContainerRegistrar(std::string name, ContainerOpener opener, std::vector<ReadFormatInfo> formats) {
    RegisterContainerReader(std::move(name), std::move(opener), std::move(formats));
  }
};

// Whether `name` (a basename, not a whole path) looks like a container by NAME alone: a cheap gate
// in front of the reader, which otherwise reads and format-bids on every file it is offered.
//
// `--archive=all` offers every regular file in the tree, so without a gate a plain `xff -z+ .` opens
// each one - the cost of diving would fall on runs that dive nothing. Names are only a heuristic in
// both directions: an archive with no extension is missed (that is what `--archive-any` is for), and
// a `.zip` that is really text is still rejected by the reader. The heuristic decides who gets ASKED,
// never who is an archive.
//
// Lives here rather than in the extra because the CORE decides whom to offer, and the answer must be
// the same in a lean build (where it simply never matters).
[[nodiscard]] bool LooksLikeContainerName(std::string_view name);

using ContainerNameProbe = absl::AnyInvocable<bool(std::string_view) const>;
void RegisterContainerNameProbe(std::string name, ContainerNameProbe probe);
bool ContextualContainerName(std::string_view path);

// Whether this binary has an archive backend linked at all. False in the lean build, where the
// `--archive` surface still exists (it is always documented) but cannot do anything.
[[nodiscard]] bool ContainerSupportAvailable();

// Opens `container` through the registered backend. Returns UnimplementedError when no backend is
// linked, so a caller that skipped ContainerSupportAvailable() still gets a clear answer rather than
// a crash.
[[nodiscard]] absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenContainer(
    std::string_view container,
    MemberPathOptions options = {});

// OpenContainer for a container whose bytes are already in hand (one nested inside another), with
// the same errors. `container` is the label, not a path that has to exist.
[[nodiscard]] absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenContainerBytes(
    std::string_view container,
    std::string_view bytes,
    MemberPathOptions options = {});

// Removes `members` (member NAMES, as the container stores them) from `container`, which is a real
// filesystem path. The only WRITE the archive surface has, and the reason it is a separate seam from
// the opener: a container that can be read is not necessarily one that can be rewritten.
//
// Must return UnimplementedError for a format it cannot write back - the answer for a container xff
// parses itself (a phar's manifest, offsets and signature all move) and for one that is a single
// compressed file rather than an archive. A member it cannot find is NotFound; anything else is the
// error that stopped it. The rewrite must be all-or-nothing: on failure `container` is unchanged.
using ContainerMemberRemover =
    absl::AnyInvocable<absl::Status(std::string_view, const std::vector<std::string>&, const vfs::MutationPolicy&)
                           const>;

// Registers the process-wide member remover, like RegisterContainerOpener. Separate registrations so
// a backend that can only read simply does not register this one.
void RegisterContainerMemberRemover(ContainerMemberRemover remover);

// Self-registers `remover` on construction; see ContainerRegistrar.
struct ContainerRemoverRegistrar {
  explicit ContainerRemoverRegistrar(ContainerMemberRemover remover) {
    RegisterContainerMemberRemover(std::move(remover));
  }
};

// Whether this binary can rewrite a container at all (a backend registered a remover).
[[nodiscard]] bool ContainerRemovalAvailable();

// Removes `members` from `container` through the registered remover, or UnimplementedError when none
// is linked.
[[nodiscard]] absl::Status RemoveContainerMembers(
    std::string_view container,
    const std::vector<std::string>& members,
    const vfs::MutationPolicy& policy = {});

// One file to write into a NEW container: the path to read, and the name it gets inside. The two are
// separate because only the caller knows what a member should be called - the walk knows roots and
// the user's ordering, the backend knows neither.
struct PackFile {
  std::string source;
  std::string name;
  // Directories may contain other destinations; files and symlinks may not.
  bool is_directory = false;
};

// Duplicate archive destinations are rejected unless the caller explicitly selects first-wins.
enum class PackDuplicatePolicy { kError, kFirst };

// Canonicalizes relative member destinations and selects entries in input order. Duplicate errors
// identify both source paths, including when a file or symlink blocks a child destination.
// Set is_directory for directory entries. Absolute paths, parent traversal, and embedded NULs are rejected.
// This pure planning step runs before creating any output; it also serves dry-run callers.
[[nodiscard]] absl::StatusOr<std::vector<PackFile>> PlanPackFiles(
    const std::vector<PackFile>& files,
    PackDuplicatePolicy duplicates = PackDuplicatePolicy::kError);

// One writer setting in XFF's vocabulary, which the backend TRANSLATES to whatever its library calls
// the same thing. The indirection is the point: libarchive's own option names differ between writers
// and between its versions, so a name passed through unchecked could be neither validated nor
// documented from a table.
struct PackOption {
  std::string name;
  std::string value;
};

// How the container is written, beyond what the output name decides. An empty list keeps the format's
// defaults; the last value given for a name wins.
struct PackOptions {
  std::vector<PackOption> options;
  PackDuplicatePolicy duplicates = PackDuplicatePolicy::kError;
  vfs::MutationPolicy mutations;
};

// One entry of the vocabulary the linked backend accepts, for the pre-walk check and for the help
// that is generated from it. Empty when no packer is linked.
struct PackOptionInfo {
  std::string name;
  std::string value_syntax;                    // "N", "yes|no", "store|deflate"
  absl::Span<const std::string_view> formats;  // immutable output-name vocabulary it applies to
  std::string detail;
};

// Writes `files` into a NEW container at `path`, format chosen from the output NAME. The third seam
// rather than a mode on the second: rewriting a container that exists and creating one that does not
// are different capabilities, and a backend may have either.
//
// Contract for a backend: names and order are stored exactly as given; the write is all-or-nothing,
// so a failure leaves any existing file at `path` untouched; an output name carrying no writable
// format is InvalidArgument, and an unreadable source is NotFound with nothing written.
using ContainerPacker =
    absl::AnyInvocable<absl::Status(std::string_view, const std::vector<PackFile>&, const PackOptions&) const>;

// Registers the process-wide packer, like the other two. Separate again: a backend that can read and
// rewrite need not be able to create. `formats` are the output names it accepts ("tar", "tar.gz",
// "zip", ...) and travel WITH the packer so a backend cannot register one without the other - the CLI
// needs them to reject an impossible output name before the walk rather than after it. `vocabulary`
// travels for the same reason: an unknown option name must be a usage error before the traversal, and
// `--help=archive` lists exactly what this binary accepts rather than a hand-kept copy.
void RegisterContainerPacker(
    ContainerPacker packer,
    std::vector<std::string> formats,
    std::vector<PackOptionInfo> vocabulary);

// Self-registers `packer` on construction; see ContainerRegistrar.
struct ContainerPackerRegistrar {
  ContainerPackerRegistrar(
      ContainerPacker packer,
      std::vector<std::string> formats,
      std::vector<PackOptionInfo> vocabulary) {
    RegisterContainerPacker(std::move(packer), std::move(formats), std::move(vocabulary));
  }
};

// Installs the override opener's read-format vocabulary. Production extras register their formats
// atomically with their opener through RegisterContainerReader.
void RegisterContainerReadFormats(std::vector<ReadFormatInfo> formats);

// The read formats the linked backend declared, in its own (documentation) order. Empty when no
// backend is linked, which is how a lean build's help simply has no table.
[[nodiscard]] std::vector<ReadFormatInfo> ContainerReadFormats();

// Whether this binary can create a container at all (a backend registered a packer).
[[nodiscard]] bool ContainerPackingAvailable();

// The output names the registered packer accepts, for the usage error and the help. Empty when no
// packer is linked.
[[nodiscard]] std::vector<std::string> ContainerPackFormats();

// The format an output NAME asks for (the longest registered name it ends with, after a dot, folding
// case), or empty when it names none. Lets the CLI reject an impossible output before spending a walk
// on it; the backend applies the same rule to the same list, which its own test pins.
[[nodiscard]] std::string ContainerPackFormatFor(std::string_view path);

// The writer-option vocabulary the linked backend accepts. Empty when no packer is linked, which is
// what makes a lean build's `--help=archive` simply not carry the table.
[[nodiscard]] std::vector<PackOptionInfo> ContainerPackVocabulary();

// Writes `files` into `path` through the registered packer, or UnimplementedError when none is
// linked.
[[nodiscard]] absl::Status PackContainer(
    std::string_view path,
    const std::vector<PackFile>& files,
    const PackOptions& options = {});

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_ARCHIVE_BACKEND_H_
