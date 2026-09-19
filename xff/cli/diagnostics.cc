// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/diagnostics.h"

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "xff/cli/globals.h"
#include "xff/parser/diagnostics.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

// One insertion, deletion, substitution, or adjacent transposition. Linear time,
// bounded input, and no fuzzy acceptance of commands.
bool IsNearby(std::string_view token, std::string_view candidate) {
  if (token.size() < 4 || token.size() > 80 || candidate.size() < 4
      || token.starts_with("--") != candidate.starts_with("--")) {
    return false;
  }
  if (token.size() > candidate.size()) {
    std::swap(candidate, token);
  }
  if (candidate.size() - token.size() > 1) {
    return false;
  }
  const auto mismatch = std::ranges::mismatch(token, candidate);
  const auto offset = static_cast<std::size_t>(mismatch.in1 - token.begin());
  if (offset == token.size()) {
    return candidate.size() != token.size();
  }
  if (token.size() != candidate.size()) {
    return token.substr(offset) == candidate.substr(offset + 1);
  }
  if (token.substr(offset + 1) == candidate.substr(offset + 1)) {
    return true;
  }
  return offset + 1 < token.size() && token.at(offset) == candidate.at(offset + 1)
         && token.at(offset + 1) == candidate.at(offset) && token.substr(offset + 2) == candidate.substr(offset + 2);
}

std::string RenderSuggestions(std::vector<std::string_view> candidates) {
  std::ranges::sort(candidates);
  const auto duplicates = std::ranges::unique(candidates);
  candidates.erase(duplicates.begin(), duplicates.end());
  if (candidates.empty() || candidates.size() > 3) {
    return {};
  }
  return absl::StrCat("Did you mean '", absl::StrJoin(candidates, "' or '"), "'?\n");
}

std::string PredicateHint(std::string_view token) {
  if (const auto flag = LookupGlobalArgument(token); flag && !flag->config_only) {
    std::string hint = absl::StrCat("'", token, "' is a leading-only global; place it before the roots.\n");
    if (flag->name.starts_with("--")) {
      absl::StrAppend(
          &hint, "The '", flag->name, "' long form is position-independent; see 'xff --help=", flag->name,
          "' for its values.\n");
    }
    return hint;
  }
  std::vector<std::string_view> candidates;
  for (const auto& descriptor : registry::All()) {
    if (IsNearby(token, descriptor.name)) {
      candidates.push_back(descriptor.name);
    }
  }
  return RenderSuggestions(std::move(candidates));
}

}  // namespace

std::string UnknownGlobalHint(std::string_view token) {
  token = token.substr(0, token.find('='));
  std::vector<std::string_view> candidates;
  for (const GlobalFlag& flag : Globals()) {
    if (!flag.config_only && IsNearby(token, flag.name)) {
      candidates.push_back(flag.name);
    }
    if (!flag.config_only && !flag.alias.empty() && IsNearby(token, flag.alias)) {
      candidates.push_back(flag.alias);
    }
  }
  for (const auto& descriptor : registry::All()) {
    if (IsNearby(token, descriptor.name)) {
      candidates.push_back(descriptor.name);
    }
  }
  return RenderSuggestions(std::move(candidates));
}

std::string ParseErrorHint(const absl::Status& status) {
  if (const auto token = status.GetPayload(parser::kUnknownPredicatePayload); token.has_value()) {
    return PredicateHint(std::string(*token));
  }
  return {};
}

}  // namespace xff::cli
