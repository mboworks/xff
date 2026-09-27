// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/matching/language/type_filter.h"

#include <algorithm>
#include <ranges>
#include <utility>

#include "mbo/status/status_macros.h"

namespace xff::language {
absl::StatusOr<TypeFilter> TypeFilter::Compile(absl::Span<const TypeRule> rules) {
  TypeFilter result;
  if (rules.empty()) {
    return result;
  }
  result.catalog_.emplace(ActiveCatalog());
  for (const auto& option : rules) {
    MBO_ASSIGN_OR_RETURN(auto name, result.catalog_->Resolve(option.value));
    const bool include = option.include;
    result.default_include_ = result.default_include_ && !include;
    result.rules_.push_back({.include = include, .name = std::move(name)});
  }
  return result;
}

bool TypeFilter::Includes(std::string_view basename) const {
  if (!catalog_) {
    return default_include_;
  }
  const auto candidates = catalog_->CandidatesForName(basename);
  for (const auto& rule : rules_ | std::views::reverse) {
    if ((rule.name == "all" && !candidates.empty())
        || std::ranges::any_of(candidates, [&](const auto& candidate) { return candidate.name == rule.name; })) {
      return rule.include;
    }
  }
  return default_include_;
}
}  // namespace xff::language
