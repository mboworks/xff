// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_COMPARISON_ENTRIES_H_
#define XFF_ENGINE_COMPARISON_ENTRIES_H_

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/memory/arena.h"
#include "xff/engine/parallel_compare.h"

namespace xff::engine {

// One coordinator appends each side. Text and entry addresses stay stable until
// destruction, including across later insertions. Duplicate keys keep the first
// entry. Sorting touches only compact key/index records, never filesystem owners.
class ComparisonEntries final {
 public:
  using OrderedEntry = std::pair<std::string_view, std::size_t>;

  ComparisonEntries() = default;
  ComparisonEntries(const ComparisonEntries&) = delete;
  ComparisonEntries& operator=(const ComparisonEntries&) = delete;
  ComparisonEntries(ComparisonEntries&&) = delete;
  ComparisonEntries& operator=(ComparisonEntries&&) = delete;

  void Add(std::string_view relative, const TreeCompareEntry& entry);
  [[nodiscard]] const std::vector<OrderedEntry>& Ordered();

  [[nodiscard]] const TreeCompareEntry& Get(std::size_t index) const { return entries_.at(index); }

 private:
  using TextArena = mbo::memory::Arena<
      mbo::memory::NewDeleteBlockSource,
      {
          .initial_block_size = 1'048'576,
          .maximum_block_size = 1'048'576,
          .growth_numerator = 2,
          .growth_denominator = 1,
      }>;
  std::string_view CopyText(std::string_view text);

  TextArena text_;
  mbo::container::SegmentedVector<TreeCompareEntry, {.segment_size = 64, .segment_reservation = 0}> entries_;
  absl::flat_hash_map<std::string_view, std::size_t> index_;
  std::vector<OrderedEntry> ordered_;
  bool sorted_ = true;
};

}  // namespace xff::engine
#endif  // XFF_ENGINE_COMPARISON_ENTRIES_H_
