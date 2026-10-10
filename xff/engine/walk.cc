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

#include "xff/engine/walk.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "mbo/status/status_macros.h"
#include "mbo/types/optional_ref.h"
#include "xff/engine/run_executor.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

// Final path component, tolerating a single trailing '/' (but not a lone "/").
std::string_view Basename(std::string_view path) {
  if (path.size() > 1 && path.back() == '/') {
    path.remove_suffix(1);
  }
  const std::string_view::size_type slash = path.rfind('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// One directory child, with either complete or lazily loaded metadata.
struct Stated {
  std::string path;
  // The entry's own final component, as the LISTING reported it. Not derivable from `path` in
  // general: an archive member's path is `a.tar!one.txt`, whose name is `one.txt`, not the whole
  // string a slash-based basename would yield. Empty only for a root operand, which has no listing.
  std::string name;
  mutable vfs::Metadata metadata;
  bool ok = false;
  mutable absl::Status status;
  mutable bool metadata_loaded = true;
};

// The result of reading one directory: its children, or a ReadDir error.
using Listing = absl::StatusOr<std::vector<Stated>>;

// Creating and synchronizing the directory-read pool has a fixed cost. A small sibling fan-out
// cannot repay it on native memory-backed fixtures; wait for enough independent directory reads
// to keep the workers useful. This is a work-unit threshold rather than a file-count threshold,
// so layout shape remains part of the decision and narrow/deep walks stay serial.
inline constexpr std::size_t kMinParallelDirectoryReads = 64;

class Walker {
 public:
  Walker(
      const vfs::FileSystem& fs,
      const WalkOptions& options,
      Visitor visit,
      WalkErrorFn on_error,
      mbo::types::OptionalRef<const ContainerMounter> mount_container,
      RunExecutor& executor)
      : fs_(fs),
        options_(options),
        visit_(visit),
        on_error_(on_error),
        mount_container_(mount_container),
        follow_children_(options.symlinks == SymlinkMode::kAll),
        executor_(executor) {}

  // Leaf jobs capture this walker, so its identity must remain stable until drained.
  Walker(const Walker&) = delete;
  Walker& operator=(const Walker&) = delete;
  Walker(Walker&&) = delete;
  Walker& operator=(Walker&&) = delete;

  ~Walker() {
    // A stop action may leave prefetched siblings unconsumed. Their drain jobs
    // still reference this walker, so join those jobs before its state is torn
    // down. Normal walks reach already-completed jobs here.
    for (const RunTask<void>& drain : drain_tasks_) {
      drain.Wait();
    }
  }

  // Whether `stated` is a FILE this walk should try to open as a container. `kRoots` offers only the
  // paths named on the command line (depth 0), `kAll` offers every file met; `kNone` never asks.
  bool ShouldTryMount(const Stated& stated, int depth) const {
    if (!mount_container_.has_value() || options_.archive == ArchiveDive::kNone) {
      return false;
    }
    if (container_depth_ >= options_.archive_depth) {
      return false;  // --archive-depth: this walk is already as many containers deep as allowed
    }
    if (!stated.ok || stated.metadata.type != vfs::FileType::kRegular) {
      return false;
    }
    return options_.archive == ArchiveDive::kAll || depth == 0;
  }

  // Opens `container`, or answers null. A failed open is not automatically an error: InvalidArgument
  // means "not an archive", the answer for every ordinary file the walk offers, and the file is simply
  // walked as itself; any other failure IS reported, because an archive that cannot be read is a real
  // problem. Reported at most once per container: a probe that fails is never retried.
  std::shared_ptr<const vfs::FileSystem> MountContainer(const Stated& container, int depth) {
    absl::StatusOr<std::unique_ptr<const vfs::FileSystem>> mounted = (*mount_container_)(container.path, fs_, depth);
    if (mounted.ok()) {
      return *std::move(mounted);
    }
    if (!absl::IsInvalidArgument(mounted.status())) {
      on_error_(container.path, mounted.status());
    }
    return nullptr;
  }

  // Answers whether `container` can be dived, keeping no open archive afterwards. Used by the
  // mid-walk sites under `mount_before_visit`, which need the answer for a whole listing block before
  // the first dive: retaining each child's mount until then would hold a directory's worth of open
  // archives at once (a compressed single file is decompressed into memory, so that is unbounded),
  // so this trades a second open at dive time for a peak of one.
  bool ProbeMountable(const Stated& container, int depth) { return MountContainer(container, depth) != nullptr; }

  // Whether the mid-walk sites will dive `child`, answered before its own entry is emitted. Without
  // `mount_before_visit` that is just "may we try" - the open, and therefore the answer, comes later.
  bool WillDive(const Stated& child, int depth) {
    if (!ShouldTryMount(child, depth)) {
      return false;
    }
    return !options_.mount_before_visit || ProbeMountable(child, depth);
  }

  // Walks an ALREADY-OPEN container's members as children of it, at `depth` + 1.
  void DescendMounted(std::shared_ptr<const vfs::FileSystem> mounted, const Stated& container, int depth) {
    // A nested walker over the mounted filesystem: members are ordinary entries to it, so every rule
    // the outer walk enforces (max_depth, min_depth, prune, quit, post_order, sort) applies unchanged.
    // The mounter is passed on, so a container inside a container dives too - bounded by
    // --archive-depth through `container_depth_`. Under `roots` that bound never binds: a member is
    // never at depth 0, so the mode itself already says "only the archive I was pointed at".
    Walker inner(*mounted, options_, visit_, on_error_, mount_container_, executor_);
    // The inner walk's entries belong to the CONTAINER's filesystem, so they carry shared
    // ownership of it: a consumer that outlives the dive (a mount) keeps the reader alive.
    inner.fs_owner_ = std::move(mounted);
    inner.container_depth_ = container_depth_ + 1;
    inner.current_root_ = current_root_;
    inner.current_root_index_ = current_root_index_;
    inner.root_dev_ = container.metadata.dev;
    inner.DescendMembers(container.path, depth);
    if (inner.stopped_) {
      stopped_ = true;  // a -quit inside the archive stops the whole walk, as it would in a directory
    }
  }

  // Opens a container and walks its members, for the callers that visit it before descending.
  void DescendContainer(const Stated& container, int depth) {
    const std::shared_ptr<const vfs::FileSystem> mounted = MountContainer(container, depth);
    if (mounted != nullptr) {
      DescendMounted(mounted, container, depth);
    }
  }

  // Lists `container` through the mounted filesystem and walks each member at `depth` + 1. Separate
  // from Descend() because a container is NOT a directory to stat: it keeps its real-file identity, so
  // there is nothing to loop-guard here and the listing is the entry point.
  void DescendMembers(const std::string& container, int depth) {
    if (options_.max_depth >= 0 && depth >= options_.max_depth) {
      return;
    }
    Listing listing = ReadNow(container);
    if (!listing.ok()) {
      on_error_(container, listing.status());
      return;
    }
    if (options_.sort != SortOrder::kNone && options_.sort != SortOrder::kRoots) {
      absl::c_sort(*listing, [](const Stated& lhs, const Stated& rhs) { return lhs.path < rhs.path; });
    }
    HandleChildren(*listing, depth + 1);
  }

  void WalkRoots(absl::Span<const std::string> roots) {
    std::vector<std::size_t> ordered_roots(roots.size());
    std::iota(ordered_roots.begin(), ordered_roots.end(), 0);
    if (options_.sort == SortOrder::kRoots || options_.sort == SortOrder::kGlobal) {
      absl::c_stable_sort(
          ordered_roots, [&](std::size_t lhs, std::size_t rhs) { return roots.at(lhs) < roots.at(rhs); });
    }
    for (const std::size_t root_index : ordered_roots) {
      const std::string& root = roots.at(root_index);
      if (stopped_) {
        return;
      }
      // -P never follows, -H follows command-line operands (depth 0), -L all.
      const bool follow = options_.symlinks != SymlinkMode::kNever;
      const Stated stated = StatNode(root, follow);
      root_dev_ = stated.ok ? stated.metadata.dev : 0;
      current_root_ = root;
      current_root_index_ = root_index;
      VisitSubtree(stated, /*depth=*/0, /*prefetched=*/{});
    }
  }

 private:
  // lstat (or stat, when following) a single path into a Stated, with the
  // dangling-symlink fallback to the link itself.
  Stated StatNode(std::string path, bool follow, std::string name = {}) const {
    // A path with no listing behind it (a root operand) falls back to the slash-based basename,
    // which is right for every real filesystem path.
    std::string entry_name = name.empty() ? std::string(Basename(path)) : std::move(name);
    absl::StatusOr<vfs::Metadata> metadata = fs_.StatFields(path, follow, options_.metadata_fields);
    if (!metadata.ok() && follow) {
      metadata = fs_.StatFields(path, /*follow_symlinks=*/false, options_.metadata_fields);
    }
    if (!metadata.ok()) {
      return Stated{.path = std::move(path), .name = std::move(entry_name), .ok = false, .status = metadata.status()};
    }
    return Stated{.path = std::move(path), .name = std::move(entry_name), .metadata = *metadata, .ok = true};
  }

  Stated StatEntry(vfs::Entry entry) const {
    if (options_.metadata != MetadataDemand::kAlways && entry.type != vfs::FileType::kUnknown
        && entry.type != vfs::FileType::kDirectory && (!follow_children_ || entry.type != vfs::FileType::kSymlink)) {
      return {
          .path = std::move(entry.path),
          .name = std::move(entry.name),
          .metadata = {.type = entry.type, .source = entry.source},
          .ok = true,
          .metadata_loaded = false,
      };
    }
    return StatNode(std::move(entry.path), follow_children_, std::move(entry.name));
  }

  // Resolve exactly the eager metadata required for this owned listing slice. Safe on a worker.
  std::vector<Stated> StatEntries(absl::Span<vfs::Entry> entries) const {
    std::vector<Stated> children;
    children.reserve(entries.size());
    for (vfs::Entry& entry : entries) {
      children.push_back(StatEntry(std::move(entry)));
    }
    return children;
  }

  Listing ReadDir(const std::string& dir) const {
    MBO_ASSIGN_OR_RETURN(auto entries, fs_.ReadDir(dir));
    return StatEntries(absl::MakeSpan(entries));
  }

  // Only the coordinator dispatches and joins stat chunks. Directory read-ahead jobs
  // stay leaves: no worker ever waits for jobs submitted to its own pool.
  Listing ReadNow(const std::string& dir) {
    MBO_ASSIGN_OR_RETURN(auto entries, fs_.ReadDir(dir));
    constexpr std::size_t kChunk = 128;
    if (options_.workers <= 1 || options_.metadata != MetadataDemand::kAlways || entries.size() < 4 * kChunk
        || fs_owner_ || absl::c_any_of(entries, [](const vfs::Entry& entry) {
             return entry.source == vfs::Source::kArchiveMember;
           })) {
      return StatEntries(absl::MakeSpan(entries));
    }
    const auto chunks = (entries.size() + kChunk - 1) / kChunk;
    const auto pending = std::min(chunks, options_.workers);
    executor_.Start(pending);
    std::atomic<std::size_t> next = 0;
    std::vector<Stated> children(entries.size());
    std::vector<RunTask<void>> reads;
    reads.reserve(pending);
    for (std::size_t worker = 0; worker < pending; ++worker) {
      reads.push_back(executor_.Submit([&] {
        for (;;) {
          const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
          if (index >= chunks) {
            return;
          }
          const std::size_t first = index * kChunk;
          const std::size_t end = std::min(first + kChunk, entries.size());
          for (std::size_t entry_index = first; entry_index < end; ++entry_index) {
            children[entry_index] = StatEntry(std::move(entries[entry_index]));
          }
        }
      }));
    }
    for (RunTask<void>& read : reads) {
      read.Get();
    }
    return children;
  }

  bool Descendable(const Stated& stated, int depth) const {
    const bool is_dir = stated.ok && stated.metadata.type == vfs::FileType::kDirectory;
    const bool within_depth = options_.max_depth < 0 || depth < options_.max_depth;
    const bool on_root_fs = !options_.single_filesystem || stated.metadata.dev == root_dev_;
    return is_dir && within_depth && on_root_fs;
  }

  // Only the coordinator accesses this record after its listing completion arrives. Exclusive
  // ownership makes the lazy cache lock-free; it is never shared with matcher workers.
  absl::Status LoadMetadata(const Stated& stated) const {
    if (!stated.metadata_loaded) {
      auto metadata = fs_.StatFields(stated.path, follow_children_, options_.metadata_fields);
      stated.metadata_loaded = true;
      stated.status = metadata.status();
      if (metadata.ok()) {
        stated.metadata = *metadata;
      }
    }
    return stated.status;
  }

  // Reports `stated` to the visitor (pre/post order handled by the caller).
  // Returns the visitor's action, or kContinue when the entry is below
  // `min_depth` (traversed but not visited) or failed to stat.
  WalkAction VisitOne(const Stated& stated, int depth, bool dived = false) {
    if (!stated.ok) {
      // -ignore_readdir_race: an entry that vanished after readdir (ENOENT) is a
      // race, not an error worth reporting; other stat failures still surface.
      if (!(options_.ignore_readdir_race && absl::IsNotFound(stated.status))) {
        on_error_(stated.path, stated.status);
      }
      return WalkAction::kContinue;
    }
    if (depth < options_.min_depth) {
      return WalkAction::kContinue;
    }
    const auto load_metadata = [&] { return LoadMetadata(stated); };
    const Visit visit{
        .path = stated.path,
        .name = stated.name,
        .root = current_root_,
        .depth = depth,
        .metadata = stated.metadata,
        .dived = dived,
        .fs = fs_,
        .fs_owner = fs_owner_,
        .root_index = current_root_index_,
        .load_metadata = load_metadata,
    };
    const WalkAction action = visit_(visit);
    if (action == WalkAction::kStop) {
      stopped_ = true;
    }
    return action;
  }

  static bool IsDir(const Stated& stated) { return stated.ok && stated.metadata.type == vfs::FileType::kDirectory; }

  // Visits `stated` and, if it is a descendable directory, descends into it.
  // Pre-order by default; post-order (`-depth`) descends first, then visits, and
  // `-prune` has no effect (matching find). `prefetched` is the directory's
  // already-submitted listing read (from the parent's batch), or empty to read now.
  void VisitSubtree(const Stated& stated, int depth, mbo::types::OptionalRef<RunTask<Listing>> prefetched) {
    if (stopped_) {
      return;
    }
    const bool descend = Descendable(stated, depth);
    const bool dive = ShouldTryMount(stated, depth);
    // Under `mount_before_visit` the container is opened here, so the visit can be told whether its
    // members follow; the open is kept and reused for the dive below. Otherwise the open happens at
    // the dive, which is what lets `-prune` skip it altogether.
    const std::shared_ptr<const vfs::FileSystem> mounted =
        dive && options_.mount_before_visit ? MountContainer(stated, depth) : nullptr;
    const bool dived = dive && (!options_.mount_before_visit || mounted != nullptr);
    if (options_.post_order) {
      if (descend) {
        Descend(stated, depth, prefetched);
      } else if (mounted != nullptr) {
        DescendMounted(mounted, stated, depth);
      } else if (dive && !options_.mount_before_visit) {
        DescendContainer(stated, depth);
      }
      if (!stopped_) {
        VisitOne(stated, depth, options_.mount_before_visit && dived);
      }
      return;
    }
    const WalkAction action = VisitOne(stated, depth, options_.mount_before_visit && dived);
    if (stopped_ || action == WalkAction::kPrune) {
      return;
    }
    if (descend) {
      Descend(stated, depth, prefetched);
    } else if (mounted != nullptr) {
      // A container is a FILE, so it never reaches Descend; -prune above still applies to it, which is
      // what makes `-name '*.tar' -prune` skip diving without skipping the file itself.
      DescendMounted(mounted, stated, depth);
    } else if (dive && !options_.mount_before_visit) {
      DescendContainer(stated, depth);
    }
  }

  // Reads `dir` (from its prefetched result, or now) and recurses its children,
  // guarding against filesystem loops (only possible when following symlinks).
  void Descend(const Stated& dir, int depth, mbo::types::OptionalRef<RunTask<Listing>> prefetched) {
    const std::pair<std::uint64_t, std::uint64_t> id{dir.metadata.dev, dir.metadata.ino};
    if (!ancestors_.insert(id).second) {
      on_error_(dir.path, absl::FailedPreconditionError("filesystem loop detected"));
      return;
    }
    Listing listing = prefetched.has_value() ? prefetched->Get() : ReadNow(dir.path);
    if (!listing.ok()) {
      // A directory that vanished before we could read it is the same readdir race.
      if (!(options_.ignore_readdir_race && absl::IsNotFound(listing.status()))) {
        on_error_(dir.path, listing.status());
      }
      ancestors_.erase(id);
      return;
    }
    if (options_.sort != SortOrder::kNone && options_.sort != SortOrder::kRoots) {
      absl::c_sort(*listing, [](const Stated& lhs, const Stated& rhs) { return lhs.path < rhs.path; });
    }
    HandleChildren(*listing, depth + 1);
    ancestors_.erase(id);
  }

  // Recurses a directory's (already sorted) children. The sort modes differ only
  // in how a subdirectory's entry is grouped relative to its subtree. Reads for
  // the descendable subdirectories are submitted as a batch up front so the pool
  // overlaps their IO while the coordinator visits in order.
  // NOLINTNEXTLINE(readability-function-cognitive-complexity): dispatching the ordering modes is cohesive.
  void HandleChildren(const std::vector<Stated>& children, int depth) {
    // Inline DFS at each entry's position. kTree emits a subtree in its sorted
    // place; post-order (`-depth`) always uses this shape (descend then visit).
    if (options_.sort == SortOrder::kTree || options_.sort == SortOrder::kGlobal || options_.post_order) {
      std::vector<RunTask<Listing>> reads = SubmitSubdirReads(children, depth);
      for (std::size_t i = 0; i < children.size(); ++i) {
        if (stopped_) {
          return;
        }
        VisitSubtree(
            children[i], depth,
            reads[i].Valid() ? mbo::types::OptionalRef{reads[i]} : mbo::types::OptionalRef<RunTask<Listing>>{});
      }
      return;
    }
    if (options_.sort == SortOrder::kSubtree) {
      // Non-directory entries first (sorted block), then each subtree contiguous.
      std::vector<bool> dived(children.size(), false);
      for (std::size_t i = 0; i < children.size(); ++i) {
        if (stopped_) {
          return;
        }
        if (!IsDir(children[i])) {
          dived[i] = WillDive(children[i], depth);
          VisitOne(children[i], depth, options_.mount_before_visit && dived[i]);
        }
      }
      std::vector<RunTask<Listing>> reads = SubmitSubdirReads(children, depth);
      for (std::size_t i = 0; i < children.size(); ++i) {
        if (stopped_) {
          return;
        }
        if (IsDir(children[i])) {
          VisitSubtree(
              children[i], depth,
              reads[i].Valid() ? mbo::types::OptionalRef{reads[i]} : mbo::types::OptionalRef<RunTask<Listing>>{});
        } else if (dived[i]) {
          // A container groups its members like a directory, so under kSubtree it belongs in the
          // subtree block rather than the flat block its own entry was emitted in.
          DescendContainer(children[i], depth);
        }
      }
      return;
    }
    // kDir / kNone: emit the whole listing block, then recurse the subdirectories.
    // The per-child action is captured so a pruned directory is not descended into.
    std::vector<bool> pruned(children.size(), false);
    std::vector<bool> dived(children.size(), false);
    for (std::size_t i = 0; i < children.size(); ++i) {
      if (stopped_) {
        return;
      }
      dived[i] = WillDive(children[i], depth);
      pruned[i] = VisitOne(children[i], depth, options_.mount_before_visit && dived[i]) == WalkAction::kPrune;
    }
    std::vector<RunTask<Listing>> reads = SubmitSubdirReads(children, depth, pruned);
    for (std::size_t i = 0; i < children.size(); ++i) {
      if (stopped_) {
        return;
      }
      if (pruned[i]) {
        continue;
      }
      if (Descendable(children[i], depth)) {
        Descend(
            children[i], depth,
            reads[i].Valid() ? mbo::types::OptionalRef{reads[i]}
                             : mbo::types::OptionalRef<RunTask<Listing>>{});  // entry already visited above
      } else if (dived[i]) {
        // `--archive=all`: a container met mid-walk descends exactly where a directory would, and
        // after its own visit, so a prune on the container still skips its members.
        DescendContainer(children[i], depth);
      }
    }
  }

  // Makes every descendable subdirectory in `children` available to a bounded set
  // of drain jobs, returning a vector aligned with `children` (an invalid task
  // where there is no read). Each drain performs many reads when the batch is
  // large, so executor synchronization is paid per worker rather than per
  // directory. The sequential walk reads lazily at descend time instead, so it
  // never pre-reads siblings it might not reach.
  std::vector<RunTask<Listing>> SubmitSubdirReads(
      const std::vector<Stated>& children,
      int depth,
      const std::vector<bool>& pruned = {}) {
    std::vector<RunTask<Listing>> reads(children.size());
    if (options_.workers <= 1) {
      return reads;
    }
    std::vector<std::size_t> directories;
    for (std::size_t index = 0; index < children.size(); ++index) {
      if ((pruned.empty() || !pruned[index]) && Descendable(children[index], depth)) {
        directories.push_back(index);
      }
    }
    if (directories.size() < kMinParallelDirectoryReads) {
      return reads;
    }
    std::erase_if(drain_tasks_, [](RunTask<void>& drain) { return drain.Ready(); });
    executor_.Start(directories.size());

    struct ReadBatch final {
      std::vector<std::string> paths;
      std::vector<RunPromise<Listing>> promises;
      std::atomic<std::size_t> next = 0;
    };

    const auto batch = std::make_shared<ReadBatch>();
    batch->paths.reserve(directories.size());
    batch->promises.resize(directories.size());
    for (std::size_t batch_index = 0; batch_index < directories.size(); ++batch_index) {
      const std::size_t child_index = directories[batch_index];
      batch->paths.push_back(children[child_index].path);
      reads[child_index] = batch->promises[batch_index].Task();
    }
    const std::size_t drains = std::min(executor_.worker_count(), directories.size());
    for (std::size_t worker = 0; worker < drains; ++worker) {
      drain_tasks_.push_back(executor_.Submit([this, batch] {
        for (;;) {
          const std::size_t index = batch->next.fetch_add(1, std::memory_order_relaxed);
          if (index >= batch->paths.size()) {
            return;
          }
          batch->promises[index].SetValue(ReadDir(batch->paths[index]));
        }
      }));
    }
    return reads;
  }

  const vfs::FileSystem& fs_;
  const WalkOptions& options_;
  Visitor visit_;
  WalkErrorFn on_error_;
  // Empty when archive diving is off, and always empty inside a mounted container: nesting is
  // --archive-depth's business, not something to fall into by recursion.
  mbo::types::OptionalRef<const ContainerMounter> mount_container_;
  const bool follow_children_;
  bool stopped_ = false;
  std::uint64_t root_dev_ = 0;
  // How many containers this walker is already inside; 0 for the walk over the real filesystem.
  int container_depth_ = 0;
  // Shared ownership of `fs_` when this walker walks a CONTAINER; empty for the real filesystem.
  std::shared_ptr<const vfs::FileSystem> fs_owner_;
  std::string_view current_root_;
  std::size_t current_root_index_ = 0;
  std::set<std::pair<std::uint64_t, std::uint64_t>> ancestors_;
  RunExecutor& executor_;
  std::vector<RunTask<void>> drain_tasks_;
};

}  // namespace

absl::Status Walk(
    const vfs::FileSystem& fs,
    absl::Span<const std::string> roots,
    const WalkOptions& options,
    Visitor visit,
    WalkErrorFn on_error) {
  RunExecutor executor(options.workers > 1 ? options.workers : std::size_t{0});
  return Walk(fs, roots, options, executor, visit, on_error);
}

absl::Status Walk(
    const vfs::FileSystem& fs,
    absl::Span<const std::string> roots,
    const WalkOptions& options,
    RunExecutor& executor,
    Visitor visit,
    WalkErrorFn on_error) {
  Walker walker(fs, options, visit, on_error, /*mount_container=*/{}, executor);
  walker.WalkRoots(roots);
  return absl::OkStatus();
}

absl::Status Walk(
    const vfs::FileSystem& fs,
    absl::Span<const std::string> roots,
    const WalkOptions& options,
    Visitor visit,
    WalkErrorFn on_error,
    ContainerMounter mount_container) {
  RunExecutor executor(options.workers > 1 ? options.workers : std::size_t{0});
  return Walk(fs, roots, options, executor, visit, on_error, mount_container);
}

absl::Status Walk(
    const vfs::FileSystem& fs,
    absl::Span<const std::string> roots,
    const WalkOptions& options,
    RunExecutor& executor,
    Visitor visit,
    WalkErrorFn on_error,
    ContainerMounter mount_container) {
  Walker walker(fs, options, visit, on_error, mount_container, executor);
  walker.WalkRoots(roots);
  return absl::OkStatus();
}

}  // namespace xff::engine
