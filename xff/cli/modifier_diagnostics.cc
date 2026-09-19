// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/modifier_diagnostics.h"

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/cli/globals.h"
#include "xff/config/config.h"
#include "xff/engine/consumers.h"
#include "xff/registry/consumers.h"

namespace xff::cli {
namespace {

std::string_view ConsumerRequirement(registry::ModifierConsumer consumer) {
  using registry::ModifierConsumer;
  switch (consumer) {
    case ModifierConsumer::kArchiveReduction:
      return "requires archive traversal and an ordinary summary, histogram, shard group, or packing operation";
    case ModifierConsumer::kArchiveTraversal: return "requires available archive traversal (roots, all, or any)";
    case ModifierConsumer::kArchiveNestedTraversal:
      return "requires available all/any archive traversal; roots does not nest";
    case ModifierConsumer::kHashAlgorithm: return "requires a hash consumer without an explicit algorithm";
    case ModifierConsumer::kHashEncoding: return "requires a hash consumer without an explicit encoding";
    case ModifierConsumer::kNone: return "";
    case ModifierConsumer::kGrep: return "requires a -grep action";
    case ModifierConsumer::kGrepLines: return "requires -grep line output; --count suppresses line output";
    case ModifierConsumer::kSharedContext:
      return "requires -grep line output or symmetric default -diff context; --count suppresses grep lines";
    case ModifierConsumer::kFileDiffContext: return "requires a -diff action using default context";
    case ModifierConsumer::kDiffContext:
      return "requires --compare=diff or -diff context output without an explicit per-action context count";
    case ModifierConsumer::kFileDiff: return "requires a -diff action";
    case ModifierConsumer::kDefaultDiffFormat:
      return "requires -diff without an attached style; tree diffs use unified output";
    case ModifierConsumer::kDiffComputation: return "requires -diff or --compare=diff";
    case ModifierConsumer::kShardStatus: return "requires -shard-status";
    case ModifierConsumer::kShardGrouping: return "requires ordinary --shards grouping or -shard-status";
    case ModifierConsumer::kShardListing:
      return "requires an ordinary --shards listing without active summaries or histograms";
    case ModifierConsumer::kHistogramBars: return "requires a plain or aligned histogram with bars";
    case ModifierConsumer::kRankedReduction:
      return "requires an ordinary summary or categorical histogram; comparison-result tables and numeric ranges are "
             "not top-limited";
    case ModifierConsumer::kPrecisionReduction: return "requires an active summary or histogram mean";
  }
  return "";
}

struct ModifierRequest {
  std::string_view flag;
  registry::ModifierConsumer consumer;
  config::Source source;
};

}  // namespace

absl::StatusOr<std::string> InactiveModifierNotes(
    const parser::Command& effective,
    const std::vector<config::ResolvedFlag>& application,
    registry::Style style) {
  std::map<std::string_view, ModifierRequest> latest;
  for (const config::ResolvedFlag& applied : application) {
    if (applied.is_argument) {
      continue;
    }
    const auto flag = LookupGlobalArgument(applied.flag);
    if (flag && flag->required_consumer != registry::ModifierConsumer::kNone) {
      latest.insert_or_assign(
          flag->name,
          ModifierRequest{.flag = applied.flag, .consumer = flag->required_consumer, .source = applied.source});
    }
  }
  if (!std::ranges::any_of(latest, [](const auto& entry) { return entry.second.source == config::Source::kCli; })) {
    return std::string{};
  }
  MBO_ASSIGN_OR_RETURN(const auto consumers, engine::ActiveModifierConsumers(effective, style));
  std::string notes;
  for (const auto& [name, request] : latest) {
    if (request.source == config::Source::kCli && !consumers.contains(request.consumer)) {
      absl::StrAppend(&notes, "inactive-modifier\t", request.flag, "\t", ConsumerRequirement(request.consumer), "\n");
    }
  }
  return notes;
}

}  // namespace xff::cli
