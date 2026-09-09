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

#ifndef XFF_ENGINE_WALK_H_
#define XFF_ENGINE_WALK_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "mbo/types/optional_ref.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

// Which symlinks the walk resolves (stats the target) before testing/descending:
// none (find `-P`, the default), only command-line operands (`-H`), or all
// (`-L`). Following enables filesystem-loop detection.
enum class SymlinkMode { kNever, kRoots, kAll };

// Root and sibling ordering (xff's --sort). kNone keeps the
// filesystem's readdir order (find's default, fastest, non-reproducible). The
// other modes sort each directory's entries by path; they differ in how subtree
// contents are emitted relative to the listing (see docs/design-parallel.md):
//   kDir     - emit each directory's sorted listing as a block; subtree contents
//              follow in sorted child order (ordering within a directory).
//   kSubtree - sorted non-dir entries, then each sorted subtree inlined contiguously.
//   kTree    - each root is path-ordered depth-first; roots retain argv order.
//   kRoots   - roots are sorted; directory listings retain filesystem order.
//   kGlobal  - roots are sorted, then each is walked as kTree.
enum class SortOrder { kNone, kDir, kSubtree, kTree, kRoots, kGlobal };

// Traversal limits and parallelism (see docs/design-parallel.md).
// How far archive diving descends, mirroring `--archive=none|roots|all`. The three are NESTED, not a
// boolean: diving into an archive NAMED as a search root and diving into archives met mid-walk are
// separately wanted (design.md, ratified 2026-08-05).
enum class ArchiveDive : std::uint8_t { kNone, kRoots, kAll };

struct WalkOptions {
  // Entries shallower than `min_depth` are traversed but not visited (find
  // `-mindepth`). A root operand is depth 0.
  int min_depth = 0;
  // Directories at depth `max_depth` are visited but not descended into; `-1`
  // means unlimited (find `-maxdepth`).
  int max_depth = -1;
  // When true, a directory is visited after its contents instead of before
  // (find `-depth`); `-prune` then has no effect, matching find.
  bool post_order = false;
  // When true, do not descend into a directory on a different device than the
  // walk root it was reached from (find `-xdev`): the mount point is visited but
  // its contents are not.
  bool single_filesystem = false;
  // When true, an entry that vanishes between readdir and stat (an ENOENT race)
  // is silently skipped instead of reported (find `-ignore_readdir_race`).
  bool ignore_readdir_race = false;
  // Which symlinks to resolve before stat/descend (find `-P`/`-H`/`-L`). When a
  // symlink is followed, its target's metadata is reported and a directory target
  // is descended into, with loop detection.
  SymlinkMode symlinks = SymlinkMode::kNever;
  // Sibling ordering within each directory (xff `--sort`); kNone is readdir order.
  SortOrder sort = SortOrder::kNone;
  // Worker threads for the parallel directory read-ahead; `1` is the sequential
  // walk. The visitor always runs on a single coordinator thread, so evaluation
  // and emission stay single-threaded; only `readdir`+`lstat` run in parallel.
  std::size_t workers = 1;
  // How far to descend INTO containers (xff `--archive` / `-z`), given a mounter to open one with.
  // `kNone` is find's behaviour, where an archive is one plain file. See `ContainerMounter`.
  ArchiveDive archive = ArchiveDive::kNone;
  // How many containers deep diving may go (xff `--archive-depth`), counted in CONTAINERS, not
  // directory levels: 1 (the default) opens an archive but not an archive inside it. Its own knob
  // because nesting is a decompression-bomb risk that has nothing to do with -maxdepth, which keeps
  // counting member levels as ordinary depth.
  int archive_depth = 1;
  // Open a container BEFORE its own entry is visited, so `Visit::dived` is exact (see there).
  //
  // Off by default because it costs: a container is normally opened only after the visit, so
  // `-name '*.tar' -prune` skips the open entirely, and mid-walk the answer is needed for a whole
  // listing block at once (so the walk opens each container once to ask and again to walk it, rather
  // than holding a directory's worth of open archives). Only a caller that needs the answer at visit
  // time - a reduction that must not count both a container and its members - turns this on.
  bool mount_before_visit = false;
};

// One visited entry handed to the `Visitor`. `path`/`name` reference storage
// owned by the walk for the duration of the call only.
struct Visit {
  std::string_view path;          // path as traversed (root prefix preserved, like find)
  std::string_view name;          // final path component
  std::string_view root;          // command-line search root this entry was reached from (find %H)
  int depth;                      // 0 for a root operand, +1 per directory level
  const vfs::Metadata& metadata;  // lstat of `path`
  // True when this entry is a container the walk OPENED, so its members follow as entries of their
  // own and its bytes are already represented by them. Always false unless
  // `WalkOptions::mount_before_visit` is set, because otherwise the container is opened after this
  // visit and the answer does not exist yet.
  bool dived = false;
  // The filesystem this entry came FROM: the walk's own, except inside a mounted container, where it
  // is the container's. A predicate that READS the entry (content, hash, diff) must go through this
  // one, or a member would read as empty - `a.tar!x` is not a path the real filesystem has. Empty
  // only for a Visit synthesized outside a walk, where the caller's own filesystem is the answer.
  mbo::types::OptionalRef<const vfs::FileSystem> fs;
  // Shared ownership of `fs` WHEN it is a container's filesystem, empty for the walk's own. The
  // walk itself needs only the reference - the dive outlives every visit inside it - but a consumer
  // that keeps the filesystem past the dive MUST hold this, or it reads freed memory. That is not
  // hypothetical: --archive-mount kept a mount for the whole run while the reader died with the
  // dive, which ThreadSanitizer caught as a race on ~ArchiveFileSystem.
  std::shared_ptr<const vfs::FileSystem> fs_owner;
};

// Visitor control flow, mirroring find: keep traversing, do not descend into
// this directory (`-prune`), or stop the entire walk (`-quit`).
enum class WalkAction { kContinue, kPrune, kStop };

using Visitor = absl::FunctionRef<WalkAction(const Visit&)>;

// Opens `container` as a read-only filesystem over its members, or fails when it is not one this
// build can open. The walk calls this and nothing else about archives: a mounted container's members
// are ordinary entries, so every predicate and action applies to them unchanged.
//
// InvalidArgument means "not an archive", and the walk then treats the path as the plain file it is -
// which is why the two statuses must stay distinct all the way from the reader (see
// xff_extras_api/archive_backend.h). Any OTHER failure is reported through `WalkErrorFn`: a container
// that IS an archive but cannot be read is a real error, not a file to walk past silently.
// `source` is the filesystem the container itself lives on: the real one for a container on disk,
// the parent container's for one nested inside another. The mounter needs it because a nested
// container has no path to open - its bytes have to be read out of its parent first.
// `depth` is the container's own depth in the walk: 0 means it was NAMED on the command line, which is
// the difference between "the user pointed at this file" and "the walk happened to meet it" - a mounter
// may gate the second (a cheap name check before the reader opens anything) and must never gate the
// first.
using ContainerMounter = absl::FunctionRef<
    absl::StatusOr<std::unique_ptr<const vfs::FileSystem>>(std::string_view, const vfs::FileSystem&, int)>;

// Reports a per-path traversal failure (unreadable directory, failed stat, ...).
// The walk continues; the engine maps these to exit code 2 later (design.md
// "Exit-code model").
using WalkErrorFn = absl::FunctionRef<void(std::string_view path, absl::Status status)>;

// Walks `roots` depth-first over `fs` -- pre-order by default, post-order when
// `options.post_order` is set: each entry at depth >= `options.min_depth` is
// passed to `visit`, and directories are descended into while `options.max_depth`
// allows and the visitor did not `kPrune`/`kStop` them (`kPrune` has no effect in
// post-order, as in find). Per-path failures are reported to `on_error` and do
// not abort. Returns `OkStatus` once the walk completes (including a `kStop`).
absl::Status Walk(
    const vfs::FileSystem& fs,
    absl::Span<const std::string> roots,
    const WalkOptions& options,
    Visitor visit,
    WalkErrorFn on_error);

// As above, plus archive diving: when `options.archive` allows it, a FILE the walk meets is offered to
// `mount_container`, and a container that opens is descended into as though it were a directory - its
// members visited at the depth below it, through the mounted filesystem.
//
// The container itself is still visited as the file it is (dual identity: a real file on disk AND the
// root of its members), so an expression matching it by name, size or type behaves exactly as without
// diving. Everything the walk enforces keeps applying inside: `-maxdepth`, `-mindepth`, `-prune`,
// `-quit`, and post-order all count member levels as ordinary depth.
absl::Status Walk(
    const vfs::FileSystem& fs,
    absl::Span<const std::string> roots,
    const WalkOptions& options,
    Visitor visit,
    WalkErrorFn on_error,
    ContainerMounter mount_container);

}  // namespace xff::engine

#endif  // XFF_ENGINE_WALK_H_
