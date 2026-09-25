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

#ifndef XFF_ENGINE_COLLECT_H_
#define XFF_ENGINE_COLLECT_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/btree_map.h"
#include "mbo/container/segmented_sequence.h"
#include "mbo/memory/arena.h"
#include "mbo/types/optional_ref.h"
#include "xff/engine/walk.h"
#include "xff/parser/ast.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

// The collection a bare `-collect` (no `:NAME`) adds to. Named rather than special-cased: the
// unnamed form is the default name, so nothing downstream needs an "anonymous bucket" branch.
inline constexpr std::string_view kDefaultCollection = "default";

// Common record layout for individually owned entries and collection-owned text views.
//
// This is not defensive copying: a `Visit` is borrowed by construction (its path / name / root are
// views into the walk's own buffers and its metadata is a reference), so storing one and reading it
// after the walk is a use-after-free. `fs_owner` is the same lesson one level up - inside a mounted
// container the filesystem dies with the dive, which is exactly how --archive-mount raced
// ~ArchiveFileSystem (see Visit::fs_owner in walk.h).
template<typename Text>
struct BasicCollectedEntry {
  Text path;
  Text name;
  Text root;
  int depth = 0;
  vfs::Metadata metadata;
  mbo::types::OptionalRef<const vfs::FileSystem> fs;
  std::shared_ptr<const vfs::FileSystem> fs_owner;
  std::size_t root_index = 0;

  // A Visit borrowing THIS entry's storage, for handing to the same sink functions the walk feeds.
  // Valid while this record stays put and its text owner lives.
  [[nodiscard]] Visit AsVisit() const {
    return Visit{
        .path = path,
        .name = name,
        .root = root,
        .depth = depth,
        .metadata = metadata,
        .fs = fs,
        .fs_owner = fs_owner,
        .root_index = root_index,
    };
  }
};

using CollectedEntry = BasicCollectedEntry<std::string>;

// The named collections one run gathered. Keyed by name and ordered by it, so a run that reduces
// several collections reports them in a stable order regardless of walk order or thread timing.
class Collections {
 public:
  // Text belongs to this collection's arena, not the traversal or the individual record.
  using Entry = BasicCollectedEntry<std::string_view>;
  using EntriesType = mbo::container::SegmentedSequence<Entry, {.segment_size = 64, .segment_reservation = 0}>;

  // The row / byte ceiling a collection may occupy, from `--buffer` (0 = no limit, the default).
  // Bytes count the stored path, name and root text, which is what a collection actually holds on to.
  struct Budget {
    std::size_t rows = 0;
    std::size_t bytes = 0;
  };

  void SetBudget(Budget budget) { budget_ = budget; }

  // Whether the expression has any `-collect` at all, which is what switches the reduction sinks to
  // read this collection. Carried here so the driver needs no separate flag beside the store.
  void SetActive(bool active) { active_ = active; }

  [[nodiscard]] bool Active() const { return active_; }

  // Appends `visit` to `name`, copying what the entry needs to outlive the walk. Returns false once
  // the budget is exceeded, and then keeps returning false without storing anything more.
  //
  // Overflow is a hard stop for the CALLER to report, never a silent truncation: a collection feeds
  // --summary, and a summary computed over "some of the matches" is indistinguishable from a correct
  // one. That is the same reason --max-results caps output without stopping the walk.
  bool Add(std::string_view name, const Visit& visit);

  // True once an Add was refused because the budget was reached.
  [[nodiscard]] bool Overflowed() const { return overflowed_; }

  // The budget in force, for the caller's diagnostic.
  [[nodiscard]] Budget CurrentBudget() const { return budget_; }

  // The collected entries for `name`, or an empty sequence when nothing collected under it (which is
  // the honest answer for a `-collect` in a branch that never ran).
  [[nodiscard]] const EntriesType& Entries(std::string_view name) const;

  // The collection names that actually received an entry, in name order.
  [[nodiscard]] std::vector<std::string_view> Names() const;

  [[nodiscard]] bool Empty() const { return by_name_.empty(); }

  // Entries across every collection. An entry collected under two names counts twice, because it
  // was collected twice.
  [[nodiscard]] std::size_t Size() const;

 private:
  // Only the coordinator appends. The arena outlives all entries and adds no locks.
  using TextArena = mbo::memory::Arena<
      mbo::memory::NewDeleteBlockSource,
      {.initial_block_size = 4'096, .maximum_block_size = 65'536, .growth_numerator = 2, .growth_denominator = 1}>;
  std::string_view CopyText(std::string_view text);

  TextArena text_;
  absl::btree_map<std::string, EntriesType> by_name_;
  Budget budget_;
  bool active_ = false;
  std::size_t rows_ = 0;
  std::size_t bytes_ = 0;
  bool overflowed_ = false;
};

// One `-collect` node: the collection it writes to, and whether it carried the `!` modifier saying a
// name an earlier node already used is deliberate.
struct CollectSite {
  std::string_view name;
  bool override_name = false;
};

// Every `-collect` node in `expr`, in AST order, duplicates intact. Two readings come off this one
// walk: PRESENCE (any node at all switches what the reduction sinks read) and the DUPLICATE-name rule
// (a name reused by a node without `!` is a usage error). A bare `-collect` reports
// kDefaultCollection.
[[nodiscard]] std::vector<CollectSite> CollectSites(const parser::Expr& expr);

}  // namespace xff::engine

#endif  // XFF_ENGINE_COLLECT_H_
