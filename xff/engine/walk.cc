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
#include <deque>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/status/status_macros.h"
#include "mbo/types/optional_ref.h"
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

// Complete observations live beside a listing instead of inside every sortable child.
using CompleteMetadata = vfs::MetadataObservation;

using LazyMetadataPool =
    mbo::container::SegmentedVector<CompleteMetadata, {.segment_size = 64, .segment_reservation = 0}>;

struct Stated {
  ~Stated() = default;

  Stated(std::string path, std::string name, vfs::FileType type, vfs::Source source)
      : path(std::move(path)), name(std::move(name)), type(type), source(source), ok(true) {}

  Stated(std::string path, std::string name, CompleteMetadata& metadata)
      : path(std::move(path)), name(std::move(name)), ok(metadata.status.ok()), complete(metadata) {}

  Stated(const Stated&) = default;
  Stated(Stated&&) noexcept = default;
  Stated& operator=(const Stated&) = delete;

  Stated& operator=(Stated&& other) noexcept {
    if (this != &other) {
      path = std::move(other.path);
      name = std::move(other.name);
      type = other.type;
      source = other.source;
      ok = other.ok;
      complete.reset();
      if (other.complete) {
        complete.set_ref(*other.complete);
      }
    }
    return *this;
  }

  std::string path;
  // Preserve the listing's own component, including archive separator semantics.
  std::string name;
  vfs::FileType type = vfs::FileType::kUnknown;
  vfs::Source source = vfs::Source::kLocalFs;
  bool ok = false;
  mutable mbo::types::OptionalRef<CompleteMetadata> complete;

  vfs::FileType Type() const { return complete ? complete->value.type : type; }

  vfs::Source Origin() const { return complete ? complete->value.source : source; }

  std::uint64_t Device() const { return complete ? complete->value.dev : 0; }

  std::uint64_t Inode() const { return complete ? complete->value.ino : 0; }
};

struct ChildBatch {
  ChildBatch() = default;
  ~ChildBatch() = default;
  ChildBatch(const ChildBatch&) = delete;
  ChildBatch& operator=(const ChildBatch&) = delete;
  ChildBatch(ChildBatch&&) noexcept = default;
  ChildBatch& operator=(ChildBatch&&) noexcept = default;

  std::vector<Stated> entries;
  std::vector<CompleteMetadata> eager;
  std::vector<std::vector<CompleteMetadata>> chunks;
  mutable LazyMetadataPool lazy;

  // Moving vector owners preserves the complete-record addresses stored by children.
  void Append(ChildBatch other) {
    entries.append_range(other.entries | std::views::as_rvalue);
    if (eager.empty()) {
      eager = std::move(other.eager);
    } else if (!other.eager.empty()) {
      chunks.push_back(std::move(other.eager));
    }
    chunks.append_range(other.chunks | std::views::as_rvalue);
  }
};

using Listing = absl::StatusOr<ChildBatch>;

// One coordinator owns each completed listing. Its callback observation stays stable
// when a lazy read fills the listing-owned cache, including cached errors and type changes.
struct VisitMetadata {
  const Stated& stated;
  LazyMetadataPool& pool;
  const vfs::FileSystem& fs;
  std::string_view path;
  bool follow;
  vfs::MetadataFields fields;
  std::optional<vfs::Metadata> hint;

  static std::optional<vfs::Metadata> InitialHint(const Stated& stated) {
    if (stated.complete) {
      return std::nullopt;
    }
    return vfs::Metadata{.type = stated.Type(), .source = stated.Origin()};
  }

  const vfs::Metadata& Value() const { return hint ? *hint : stated.complete->value; }

  absl::Status Load() {
    if (!stated.complete && hint) {
      auto metadata = fs.StatFields(path, follow, fields);
      auto& cached = pool.emplace_back(
          CompleteMetadata{
              .value = metadata.ok() ? *metadata : *hint,
              .status = metadata.status(),
          });
      stated.complete.set_ref(cached);
      if (metadata.ok()) {
        *hint = *metadata;
      }
    }
    return stated.complete->status;
  }
};

// Reuses only directory-local scratch. The caller's directory and entry survive each lookup.
class StatPath final {
 public:
  StatPath(std::string_view directory, bool names_only) : directory_(directory), names_only_(names_only) {}

  std::string_view For(const vfs::Entry& entry) {
    if (!names_only_) {
      return entry.path;
    }
    if (prefix_size_ == std::string::npos) {
      scratch_.assign(directory_);
      if (!scratch_.empty() && scratch_.back() != '/') {
        scratch_.push_back('/');
      }
      prefix_size_ = scratch_.size();
    }
    scratch_.resize(prefix_size_);
    scratch_.append(entry.name);
    return scratch_;
  }

 private:
  const std::string_view directory_;
  const bool names_only_;
  std::string scratch_;
  std::size_t prefix_size_ = std::string::npos;
};

class Walker {
  // Typed leaf jobs share the existing persistent workers. A request owns a slot until its
  // result is consumed, or marks it abandoned so the worker can release it after completion.
  class ReadPool {
   public:
    enum class Kind { kDirectory, kStat };

   private:
    enum class TaskState { kPending, kReady, kAbandoned };

    struct Storage;

    struct Task {
      Task(Storage& storage, std::size_t index) : storage(storage), index(index) {}

      Storage& storage;
      std::size_t index = 0;
      Kind kind = Kind::kDirectory;
      std::string directory;
      absl::Span<vfs::Entry> entries;
      absl::Span<const CompleteMetadata> observations;
      Listing listing;
      ChildBatch stated;
      std::atomic<TaskState> state{TaskState::kPending};
    };

    struct Storage {
      explicit Storage(ReadPool& pool) : pool(pool) {}

      ReadPool& pool;
      std::deque<Task> tasks;
      std::queue<std::size_t> pending;
      std::vector<std::size_t> free;
    };

   public:
    template<Kind kKind>
    class Request {
     public:
      using Result = std::conditional_t<kKind == Kind::kDirectory, Listing, ChildBatch>;
      Request() = default;

      explicit Request(Task& task) : task_(task) {}

      ~Request() { Reset(); }

      Request(const Request&) = delete;
      Request& operator=(const Request&) = delete;

      Request(Request&& other) noexcept : task_(other.task_) { other.task_.reset(); }

      Request& operator=(Request&& other) noexcept {
        if (this != &other) {
          Reset();
          if (other.task_) {
            task_.set_ref(*other.task_);
          }
          other.task_.reset();
        }
        return *this;
      }

      bool Valid() const { return task_.has_value(); }

      Result Take() {
        auto result = task_->storage.pool.template Take<kKind>(*task_);
        task_.reset();
        return result;
      }

     private:
      void Reset() {
        if (task_) {
          task_->storage.pool.Abandon(*task_);
          task_.reset();
        }
      }

      mbo::types::OptionalRef<Task> task_;
    };

    ReadPool(Walker& walker, std::size_t workers) : walker_(walker), max_workers_(workers) {}

    ReadPool(const ReadPool&) = delete;
    ReadPool& operator=(const ReadPool&) = delete;
    ReadPool(ReadPool&&) = delete;
    ReadPool& operator=(ReadPool&&) = delete;

    void Start(std::size_t directories) {
      const std::size_t count = std::min(max_workers_, directories);
      threads_.reserve(count);
      while (threads_.size() < count) {
        threads_.emplace_back([this] { Run(); });
      }
    }

    ~ReadPool() {
      {
        const absl::MutexLock lock(mutex_);
        stop_ = true;
      }
      for (auto& thread : threads_) {
        thread.join();
      }
    }

    Request<Kind::kDirectory> SubmitDirectory(std::string_view directory) {
      return Request<Kind::kDirectory>(Submit(Kind::kDirectory, directory, {}));
    }

    Request<Kind::kStat> SubmitStats(
        absl::Span<vfs::Entry> entries,
        std::string_view directory,
        absl::Span<const CompleteMetadata> observations = {}) {
      return Request<Kind::kStat>(Submit(Kind::kStat, directory, entries, observations));
    }

   private:
    Task& Submit(
        Kind kind,
        std::string_view directory,
        absl::Span<vfs::Entry> entries,
        absl::Span<const CompleteMetadata> observations = {}) {
      Task& task = [&] -> Task& {
        const absl::MutexLock lock(mutex_);
        if (!storage_) {
          storage_.emplace(*this);
        }
        std::size_t index = 0;
        if (storage_->free.empty()) {
          index = storage_->tasks.size();
          storage_->tasks.emplace_back(*storage_, index);
        } else {
          index = storage_->free.back();
          storage_->free.pop_back();
        }
        auto& selected = storage_->tasks.at(index);
        selected.kind = kind;
        selected.directory = directory;
        selected.entries = entries;
        selected.observations = observations;
        selected.state.store(TaskState::kPending, std::memory_order_relaxed);
        if (!threads_.empty()) {
          storage_->pending.push(index);
        }
        return selected;
      }();
      if (threads_.empty()) {
        Execute(task);
      }
      return task;
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): lock contract uses this pool's mutex.
    void Recycle(Task& task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
      std::string{}.swap(task.directory);
      task.entries = {};
      task.observations = {};
      task.listing = absl::UnknownError("");
      task.stated = ChildBatch{};
      task.storage.free.push_back(task.index);
    }

    template<Kind kKind>
    Request<kKind>::Result Take(Task& task) {
      // Exactly one consumer owns this lease. Ready publishes the completed payload;
      // only its consumer can take it, so an abandoned state cannot reach this wait.
      task.state.wait(TaskState::kPending, std::memory_order_acquire);
      auto result = [&] {
        if constexpr (kKind == Kind::kDirectory) {
          return std::move(task.listing);
        } else {
          return std::move(task.stated);
        }
      }();
      {
        const absl::MutexLock lock(mutex_);
        Recycle(task);
      }
      return result;
    }

    void ReleaseCompleted(Task& task) {
      struct Payload {
        std::string directory;
        Listing listing;
        ChildBatch stated;
      };

      // Declare the moved payload before the lock so its allocations die after unlocking.
      const Payload released{
          .directory = std::move(task.directory),
          .listing = std::move(task.listing),
          .stated = std::move(task.stated),
      };
      const absl::MutexLock lock(mutex_);
      Recycle(task);
    }

    void Abandon(Task& task) {
      // The second participant releases an unused payload: either this consumer observes
      // Ready, or the producer observes Abandoned when publishing its result.
      if (task.state.exchange(TaskState::kAbandoned, std::memory_order_acq_rel) == TaskState::kReady) {
        ReleaseCompleted(task);
      }
    }

    template<typename Result>
    void Complete(Task& task, Result result) {
      if constexpr (std::is_same_v<Result, Listing>) {
        task.listing = std::move(result);
      } else {
        task.stated = std::move(result);
      }
      if (task.state.exchange(TaskState::kReady, std::memory_order_acq_rel) == TaskState::kAbandoned) {
        ReleaseCompleted(task);
      } else {
        // Storage remains stable until every worker joins. A late notification after
        // recycling is harmless: wait rechecks the new generation's atomic value.
        task.state.notify_one();
      }
    }

    void Execute(Task& task) {
      if (task.kind == Kind::kDirectory) {
        Complete(task, walker_.ReadDir(task.directory));
      } else {
        Complete(task, walker_.StatEntries(task.entries, task.directory, task.observations));
      }
    }

    bool Pending() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
      return stop_ || (storage_ && !storage_->pending.empty());
    }

    mbo::types::OptionalRef<Task> Next() {
      const absl::MutexLock lock(mutex_, absl::Condition(this, &ReadPool::Pending));
      if (!storage_ || storage_->pending.empty()) {
        return std::nullopt;
      }
      const auto index = storage_->pending.front();
      storage_->pending.pop();
      return storage_->tasks.at(index);
    }

    void Run() {
      while (auto task = Next()) {
        Execute(*task);
      }
    }

    Walker& walker_;
    const std::size_t max_workers_;
    mutable absl::Mutex mutex_;
    std::optional<Storage> storage_ ABSL_GUARDED_BY(mutex_);
    bool stop_ ABSL_GUARDED_BY(mutex_) = false;
    std::vector<std::thread> threads_;
  };

  using ReadRequest = ReadPool::Request<ReadPool::Kind::kDirectory>;
  using StatRequest = ReadPool::Request<ReadPool::Kind::kStat>;

 public:
  Walker(
      const vfs::FileSystem& fs,
      const WalkOptions& options,
      Visitor visit,
      WalkErrorFn on_error,
      mbo::types::OptionalRef<const ContainerMounter> mount_container)
      : fs_(fs),
        options_(options),
        visit_(visit),
        on_error_(on_error),
        mount_container_(mount_container),
        follow_children_(options.symlinks == SymlinkMode::kAll),
        name_only_listings_(
            (options.archive != ArchiveDive::kAll || !mount_container) && fs.SupportsNameOnlyListings()),
        // Full-path backends can expose observations too. Keep native name-only observations
        // disabled when mounting requires their ordinary complete-path listing instead.
        bulk_metadata_listings_(
            options.metadata == MetadataDemand::kAlways && (name_only_listings_ || !fs.SupportsNameOnlyListings())
            && fs.SupportsDirectoryMetadata(1)),
        coordinator_bulk_metadata_listings_(
            bulk_metadata_listings_ && (options.workers <= 1 || fs.SupportsDirectoryMetadata(options.workers))),
        pool_(*this, options.workers > 1 ? options.workers : std::size_t{0}) {}

  // Whether `stated` is a FILE this walk should try to open as a container. `kRoots` offers only the
  // paths named on the command line (depth 0), `kAll` offers every file met; `kNone` never asks.
  bool ShouldTryMount(const Stated& stated, int depth) const {
    if (!mount_container_.has_value() || options_.archive == ArchiveDive::kNone) {
      return false;
    }
    if (container_depth_ >= options_.archive_depth) {
      return false;  // --archive-depth: this walk is already as many containers deep as allowed
    }
    if (!stated.ok || stated.Type() != vfs::FileType::kRegular) {
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
    Walker inner(*mounted, options_, visit_, on_error_, mount_container_);
    // The inner walk's entries belong to the CONTAINER's filesystem, so they carry shared
    // ownership of it: a consumer that outlives the dive (a mount) keeps the reader alive.
    inner.fs_owner_ = std::move(mounted);
    inner.container_depth_ = container_depth_ + 1;
    inner.current_root_ = current_root_;
    inner.current_root_index_ = current_root_index_;
    inner.root_dev_ = container.Device();
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
      absl::c_sort(listing->entries, [this](const Stated& lhs, const Stated& rhs) {
        return name_only_listings_ ? lhs.name < rhs.name : lhs.path < rhs.path;
      });
    }
    HandleChildren(*listing, depth + 1, container);
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
      CompleteMetadata metadata = StatNode(root, follow);
      const Stated stated{root, std::string(Basename(root)), metadata};
      LazyMetadataPool root_metadata;
      root_dev_ = stated.ok ? stated.Device() : 0;
      current_root_ = root;
      current_root_index_ = root_index;
      VisitSubtree(stated, root_metadata, /*depth=*/0, /*prefetched=*/{});
    }
  }

 private:
  // Preserve eager lstat/stat order and the dangling-link fallback.
  CompleteMetadata StatNode(std::string_view path, bool follow) const {
    auto metadata = fs_.StatFields(path, follow, options_.metadata_fields);
    if (!metadata.ok() && follow) {
      metadata = fs_.StatFields(path, false, options_.metadata_fields);
    }
    if (!metadata.ok()) {
      return {.status = metadata.status()};
    }
    return {.value = *metadata};
  }

  bool NeedsEagerMetadata(const vfs::Entry& entry) const {
    return options_.metadata == MetadataDemand::kAlways || entry.type == vfs::FileType::kUnknown
           || entry.type == vfs::FileType::kDirectory || (follow_children_ && entry.type == vfs::FileType::kSymlink);
  }

  ChildBatch StatOrdinaryEntries(absl::Span<vfs::Entry> entries, std::string_view directory = {}) const {
    ChildBatch children;
    children.entries.reserve(entries.size());
    const std::size_t eager_count =
        options_.metadata == MetadataDemand::kAlways
            ? entries.size()
            : static_cast<std::size_t>(std::ranges::count_if(
                  entries, [this](const vfs::Entry& entry) { return NeedsEagerMetadata(entry); }));
    children.eager.reserve(eager_count);
    std::string stat_path;
    std::size_t prefix_size = std::string::npos;
    for (vfs::Entry& entry : entries) {
      if (!NeedsEagerMetadata(entry)) {
        children.entries.emplace_back(std::move(entry.path), std::move(entry.name), entry.type, entry.source);
      } else {
        const std::string_view path = [&] {
          if (!name_only_listings_) {
            return std::string_view(entry.path);
          }
          if (prefix_size == std::string::npos) {
            stat_path.assign(directory);
            if (!stat_path.empty() && stat_path.back() != '/') {
              stat_path.push_back('/');
            }
            prefix_size = stat_path.size();
          }
          stat_path.resize(prefix_size);
          stat_path.append(entry.name);
          return std::string_view(stat_path);
        }();
        if (entry.name.empty()) {
          entry.name = Basename(path);
        }
        auto& metadata = children.eager.emplace_back(StatNode(path, follow_children_));
        if (name_only_listings_ && metadata.status.ok() && metadata.value.type == vfs::FileType::kDirectory) {
          entry.path = path;
        }
        children.entries.emplace_back(std::move(entry.path), std::move(entry.name), metadata);
      }
    }
    return children;
  }

  // Transfer the observation owner before constructing references into it. It never grows while
  // children borrow records, and vector moves preserve those addresses through sorting/frontiers.
  ChildBatch StatObservedEntries(
      absl::Span<vfs::Entry> entries,
      std::string_view directory,
      std::vector<CompleteMetadata> observations) const {
    ChildBatch children;
    children.entries.reserve(entries.size());
    children.eager = std::move(observations);
    StatPath stat_path(directory, name_only_listings_);
    for (std::size_t index = 0; index < entries.size(); ++index) {
      auto& entry = entries.at(index);
      auto& metadata = children.eager.at(index);
      if (!metadata.status.ok() || (follow_children_ && metadata.value.type == vfs::FileType::kSymlink)) {
        metadata = StatNode(stat_path.For(entry), follow_children_);
      }
      if (metadata.status.ok() && metadata.value.type == vfs::FileType::kDirectory) {
        entry.path = stat_path.For(entry);
      }
      children.entries.emplace_back(std::move(entry.path), std::move(entry.name), metadata);
    }
    return children;
  }

  ChildBatch StatEntries(
      absl::Span<vfs::Entry> entries,
      std::string_view directory,
      absl::Span<const CompleteMetadata> observations) const {
    if (observations.empty()) {
      return StatOrdinaryEntries(entries, directory);
    }
    return StatObservedEntries(entries, directory, std::vector<CompleteMetadata>(std::from_range, observations));
  }

  ChildBatch FinishEntries(vfs::DirectoryMetadata listing, std::string_view directory) const {
    if (listing.observations.empty()) {
      return StatOrdinaryEntries(absl::MakeSpan(listing.entries), directory);
    }
    return StatObservedEntries(absl::MakeSpan(listing.entries), directory, std::move(listing.observations));
  }

  absl::StatusOr<vfs::DirectoryMetadata> ReadObservedEntries(const std::string& dir) const {
    MBO_ASSIGN_OR_RETURN(auto result, fs_.ReadDirMetadata(dir, options_.metadata_fields));
    if (!result.observations.empty() && result.observations.size() != result.entries.size()) {
      return absl::InternalError("directory metadata observations do not match entries");
    }
    return result;
  }

  Listing ReadDir(const std::string& dir) const {
    if (bulk_metadata_listings_) {
      MBO_ASSIGN_OR_RETURN(auto listing, ReadObservedEntries(dir));
      return FinishEntries(std::move(listing), dir);
    }
    MBO_ASSIGN_OR_RETURN(auto entries, name_only_listings_ ? fs_.ReadDirNames(dir) : fs_.ReadDir(dir));
    return StatOrdinaryEntries(absl::MakeSpan(entries), dir);
  }

  // Only the coordinator dispatches and joins stat chunks. Directory read-ahead jobs
  // stay leaves: no worker ever waits for jobs submitted to its own pool.
  Listing ReadNow(const std::string& dir) {
    return coordinator_bulk_metadata_listings_ ? ReadObservedNow(dir) : ReadOrdinaryNow(dir);
  }

  Listing ReadOrdinaryNow(const std::string& dir) {
    MBO_ASSIGN_OR_RETURN(auto entries, name_only_listings_ ? fs_.ReadDirNames(dir) : fs_.ReadDir(dir));
    constexpr std::size_t kChunk = 128;
    if (options_.workers <= 1 || options_.metadata != MetadataDemand::kAlways || entries.size() < 4 * kChunk
        || fs_owner_ || absl::c_any_of(entries, [](const vfs::Entry& entry) {
             return entry.source == vfs::Source::kArchiveMember;
           })) {
      return StatOrdinaryEntries(absl::MakeSpan(entries), dir);
    }
    const auto chunks = (entries.size() + kChunk - 1) / kChunk;
    const auto pending = std::min(chunks, options_.workers);
    return StatChunks(entries, dir, {}, chunks, pending);
  }

  Listing ReadObservedNow(const std::string& dir) {
    MBO_ASSIGN_OR_RETURN(auto listing, ReadObservedEntries(dir));
    auto& entries = listing.entries;
    const auto observations = absl::MakeConstSpan(listing.observations);
    constexpr std::size_t kChunk = 128;
    const auto chunks = (entries.size() / kChunk) + static_cast<std::size_t>(entries.size() % kChunk != 0);
    const auto pending = std::min(chunks, options_.workers);
    const auto missing = observations.empty()
                             ? entries.size()
                             : static_cast<std::size_t>(std::ranges::count_if(
                                   observations, [](const auto& value) { return !value.status.ok(); }));
    if (pending <= 1 || options_.metadata != MetadataDemand::kAlways || entries.size() < 4 * kChunk
        || missing < 4 * kChunk || fs_owner_
        || absl::c_any_of(
            entries, [](const vfs::Entry& entry) { return entry.source == vfs::Source::kArchiveMember; })) {
      return FinishEntries(std::move(listing), dir);
    }
    return StatChunks(entries, dir, observations, chunks, pending);
  }

  ChildBatch StatChunks(
      std::vector<vfs::Entry>& entries,
      std::string_view dir,
      absl::Span<const CompleteMetadata> observations,
      std::size_t chunks,
      std::size_t pending) {
    constexpr std::size_t kChunk = 128;
    pool_.Start(pending);
    const auto all_entries = absl::MakeSpan(entries);
    const auto submit = [&](std::size_t index) {
      const auto offset = index * kChunk;
      const auto chunk = all_entries.subspan(offset, std::min(kChunk, entries.size() - offset));
      return pool_.SubmitStats(
          chunk, name_only_listings_ ? std::string_view(dir) : std::string_view{},
          observations.empty() ? observations : observations.subspan(offset, chunk.size()));
    };
    std::vector<StatRequest> reads;
    reads.reserve(pending);
    for (std::size_t index = 0; index < pending; ++index) {
      reads.push_back(submit(index));
    }
    ChildBatch children;
    children.entries.reserve(entries.size());
    children.chunks.reserve(chunks - 1);
    for (std::size_t index = 0; index < chunks; ++index) {
      auto& read = reads.at(index % pending);
      children.Append(read.Take());
      if (index + pending < chunks) {
        read = submit(index + pending);
      }
    }
    return children;
  }

  ReadRequest SubmitRead(const std::string& dir) { return pool_.SubmitDirectory(dir); }

  // Bound speculative listings without changing the existing leaf-worker pool.
  // Each active recursion level owns at most four requests per available worker;
  // this is a request bound, not a global byte budget for arbitrary tree shapes.
  class ReadFrontier final {
   public:
    ReadFrontier(
        Walker& walker,
        const std::vector<Stated>& children,
        int depth,
        mbo::types::OptionalRef<const std::vector<bool>> pruned = std::nullopt)
        : walker_(walker), children_(children), depth_(depth), pruned_(pruned) {
      if (walker_.options_.workers <= 1) {
        return;
      }
      const auto indices = std::views::iota(std::size_t{0}, children_.size());
      const auto directories = static_cast<std::size_t>(
          std::ranges::count_if(indices, [this](std::size_t index) { return Eligible(index); }));
      if (directories < 2) {
        return;
      }
      walker_.pool_.Start(directories);
      window_ = std::min(directories, 4 * std::min(directories, walker_.options_.workers));
      requests_.emplace();
    }

    ~ReadFrontier() = default;

    ReadFrontier(const ReadFrontier&) = delete;
    ReadFrontier& operator=(const ReadFrontier&) = delete;
    ReadFrontier(ReadFrontier&&) = delete;
    ReadFrontier& operator=(ReadFrontier&&) = delete;

    ReadRequest Take(std::size_t index) {
      if (!requests_) {
        return {};
      }
      auto& requests = *requests_;
      while (!requests.empty() && requests.front().index < index) {
        requests.pop_front();
      }
      next_ = std::max(next_, index);
      while (requests.size() < window_ && next_ < children_.size()) {
        if (Eligible(next_)) {
          requests.push_back({
              .index = next_,
              .read = walker_.SubmitRead(children_.at(next_).path),
          });
        }
        ++next_;
      }
      if (requests.empty() || requests.front().index != index) {
        return {};
      }
      auto read = std::move(requests.front().read);
      requests.pop_front();
      // Leave the released slot available for a deeper frontier until the next sibling.
      return read;
    }

   private:
    struct Request {
      std::size_t index;
      ReadRequest read;
    };

    bool Eligible(std::size_t index) const {
      return (!pruned_ || !pruned_->at(index)) && walker_.Descendable(children_.at(index), depth_);
    }

    Walker& walker_;
    const std::vector<Stated>& children_;
    const int depth_;
    const mbo::types::OptionalRef<const std::vector<bool>> pruned_;
    std::size_t next_ = 0;
    std::size_t window_ = 0;
    // Avoid even the deque's initial allocation in serial and file-only directories.
    std::optional<std::deque<Request>> requests_;
  };

  bool Descendable(const Stated& stated, int depth) const {
    const bool is_dir = stated.ok && stated.Type() == vfs::FileType::kDirectory;
    const bool within_depth = options_.max_depth < 0 || depth < options_.max_depth;
    const bool on_root_fs = !options_.single_filesystem || stated.Device() == root_dev_;
    return is_dir && within_depth && on_root_fs;
  }

  // Reports `stated` to the visitor (pre/post order handled by the caller).
  // Returns the visitor's action, or kContinue when the entry is below
  // `min_depth` (traversed but not visited) or failed to stat.
  WalkAction VisitOne(
      const Stated& stated,
      LazyMetadataPool& metadata_pool,
      int depth,
      bool dived = false,
      std::string_view path = {}) {
    if (path.empty()) {
      path = stated.path;
    }
    if (!stated.ok) {
      // -ignore_readdir_race: an entry that vanished after readdir (ENOENT) is a
      // race, not an error worth reporting; other stat failures still surface.
      if (!(options_.ignore_readdir_race && absl::IsNotFound(stated.complete->status))) {
        on_error_(path, stated.complete->status);
      }
      return WalkAction::kContinue;
    }
    if (depth < options_.min_depth) {
      return WalkAction::kContinue;
    }
    VisitMetadata metadata{
        .stated = stated,
        .pool = metadata_pool,
        .fs = fs_,
        .path = path,
        .follow = follow_children_,
        .fields = options_.metadata_fields,
        .hint = VisitMetadata::InitialHint(stated),
    };
    const auto load_metadata = [&] { return metadata.Load(); };
    const Visit visit{
        .path = path,
        .name = stated.name,
        .root = current_root_,
        .depth = depth,
        .metadata = metadata.Value(),
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

  static bool IsDir(const Stated& stated) { return stated.ok && stated.Type() == vfs::FileType::kDirectory; }

  // Visits `stated` and, if it is a descendable directory, descends into it.
  // Pre-order by default; post-order (`-depth`) descends first, then visits, and
  // `-prune` has no effect (matching find). `prefetched` is the directory's
  // already-submitted listing read (from the parent's batch), or empty to read now.
  void VisitSubtree(
      const Stated& stated,
      LazyMetadataPool& metadata_pool,
      int depth,
      mbo::types::OptionalRef<ReadRequest> prefetched) {
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
        VisitOne(stated, metadata_pool, depth, options_.mount_before_visit && dived);
      }
      return;
    }
    const WalkAction action = VisitOne(stated, metadata_pool, depth, options_.mount_before_visit && dived);
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

  // Reads `dir` (from its prefetched request, or now) and recurses its children,
  // guarding against filesystem loops (only possible when following symlinks).
  void Descend(const Stated& dir, int depth, mbo::types::OptionalRef<ReadRequest> prefetched) {
    const std::pair<std::uint64_t, std::uint64_t> id{dir.Device(), dir.Inode()};
    if (!ancestors_.insert(id).second) {
      on_error_(dir.path, absl::FailedPreconditionError("filesystem loop detected"));
      return;
    }
    Listing listing = prefetched.has_value() ? prefetched->Take() : ReadNow(dir.path);
    if (!listing.ok()) {
      // A directory that vanished before we could read it is the same readdir race.
      if (!(options_.ignore_readdir_race && absl::IsNotFound(listing.status()))) {
        on_error_(dir.path, listing.status());
      }
      ancestors_.erase(id);
      return;
    }
    if (options_.sort != SortOrder::kNone && options_.sort != SortOrder::kRoots) {
      absl::c_sort(listing->entries, [this](const Stated& lhs, const Stated& rhs) {
        return name_only_listings_ ? lhs.name < rhs.name : lhs.path < rhs.path;
      });
    }
    HandleChildren(*listing, depth + 1, dir.path);
    ancestors_.erase(id);
  }

  // Recurses a directory's (already sorted) children. The sort modes differ only
  // in how a subdirectory's entry is grouped relative to its subtree. A bounded
  // window overlaps sibling reads while the coordinator visits in order.
  // NOLINTNEXTLINE(readability-function-cognitive-complexity): dispatching the ordering modes is cohesive.
  void HandleChildren(const ChildBatch& batch, int depth, std::string_view directory) {
    const auto& children = batch.entries;
    // This buffer is local to a listing. Nested directory visits cannot invalidate it;
    // queued consumers already snapshot Visit's callback-bounded path observation.
    std::string path_scratch;
    std::size_t prefix_size = std::string::npos;
    const auto visit_child = [&](const Stated& child, bool dived = false) {
      if (!child.path.empty()) {
        return VisitOne(child, batch.lazy, depth, dived);
      }
      if (prefix_size == std::string::npos) {
        path_scratch.assign(directory);
        if (!path_scratch.empty() && path_scratch.back() != '/') {
          path_scratch.push_back('/');
        }
        prefix_size = path_scratch.size();
      }
      path_scratch.resize(prefix_size);
      path_scratch.append(child.name);
      return VisitOne(child, batch.lazy, depth, false, path_scratch);
    };
    // Inline DFS at each entry's position. kTree emits a subtree in its sorted
    // place; post-order (`-depth`) always uses this shape (descend then visit).
    if (options_.sort == SortOrder::kTree || options_.sort == SortOrder::kGlobal || options_.post_order) {
      ReadFrontier reads(*this, children, depth);
      for (std::size_t i = 0; i < children.size(); ++i) {
        if (stopped_) {
          return;
        }
        if (children.at(i).path.empty()) {
          visit_child(children.at(i));
          continue;
        }
        auto read = reads.Take(i);
        VisitSubtree(
            children.at(i), batch.lazy, depth,
            read.Valid() ? mbo::types::OptionalRef{read} : mbo::types::OptionalRef<ReadRequest>{});
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
          visit_child(children[i], options_.mount_before_visit && dived[i]);
        }
      }
      ReadFrontier reads(*this, children, depth);
      for (std::size_t i = 0; i < children.size(); ++i) {
        if (stopped_) {
          return;
        }
        if (IsDir(children[i])) {
          auto read = reads.Take(i);
          VisitSubtree(
              children.at(i), batch.lazy, depth,
              read.Valid() ? mbo::types::OptionalRef{read} : mbo::types::OptionalRef<ReadRequest>{});
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
      pruned[i] = visit_child(children[i], options_.mount_before_visit && dived[i]) == WalkAction::kPrune;
    }
    ReadFrontier reads(*this, children, depth, pruned);
    for (std::size_t i = 0; i < children.size(); ++i) {
      if (stopped_) {
        return;
      }
      if (pruned[i]) {
        continue;
      }
      if (Descendable(children[i], depth)) {
        auto read = reads.Take(i);
        Descend(
            children.at(i), depth,
            read.Valid() ? mbo::types::OptionalRef{read}
                         : mbo::types::OptionalRef<ReadRequest>{});  // entry already visited above
      } else if (dived[i]) {
        // `--archive=all`: a container met mid-walk descends exactly where a directory would, and
        // after its own visit, so a prune on the container still skips its members.
        DescendContainer(children[i], depth);
      }
    }
  }

  const vfs::FileSystem& fs_;
  const WalkOptions& options_;
  Visitor visit_;
  WalkErrorFn on_error_;
  // Empty when archive diving is off, and always empty inside a mounted container: nesting is
  // --archive-depth's business, not something to fall into by recursion.
  mbo::types::OptionalRef<const ContainerMounter> mount_container_;
  const bool follow_children_;
  const bool name_only_listings_;
  const bool bulk_metadata_listings_;
  const bool coordinator_bulk_metadata_listings_;
  bool stopped_ = false;
  std::uint64_t root_dev_ = 0;
  // How many containers this walker is already inside; 0 for the walk over the real filesystem.
  int container_depth_ = 0;
  // Shared ownership of `fs_` when this walker walks a CONTAINER; empty for the real filesystem.
  std::shared_ptr<const vfs::FileSystem> fs_owner_;
  std::string_view current_root_;
  std::size_t current_root_index_ = 0;
  std::set<std::pair<std::uint64_t, std::uint64_t>> ancestors_;
  // Join pending reads before destroying the mounted filesystem or other walker state.
  ReadPool pool_;
};

}  // namespace

absl::Status Walk(
    const vfs::FileSystem& fs,
    absl::Span<const std::string> roots,
    const WalkOptions& options,
    Visitor visit,
    WalkErrorFn on_error) {
  Walker walker(fs, options, visit, on_error, /*mount_container=*/{});
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
  Walker walker(fs, options, visit, on_error, mount_container);
  walker.WalkRoots(roots);
  return absl::OkStatus();
}

}  // namespace xff::engine
