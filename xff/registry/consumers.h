// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_REGISTRY_CONSUMERS_H_
#define XFF_REGISTRY_CONSUMERS_H_

namespace xff::registry {

// Typed dependencies of optional modifiers. A consumer's presence means it can
// use a setting; it does not prove that a conditional expression will reach it.
enum class ModifierConsumer {
  kNone,
  kHashAlgorithm,
  kHashEncoding,
  kGrep,
  kGrepLines,
  kSharedContext,
  kFileDiffContext,
  kDiffContext,
  kFileDiff,
  kDefaultDiffFormat,
  kDiffComputation,
  kShardStatus,
  kShardGrouping,
  kShardListing,
  kHistogramBars,
  kRankedReduction,
  kPrecisionReduction,
  kArchiveTraversal,
  kArchiveNestedTraversal,
  kArchiveReduction,
};

}  // namespace xff::registry

#endif  // XFF_REGISTRY_CONSUMERS_H_
