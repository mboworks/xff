// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/diagnostics.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/fuzzy/fuzzy.h"
#include "xff/parser/diagnostics.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

// Recognize a single edit, including adjacent transposition. This boosts the
// shared Levenshtein score, whose transpositions otherwise cost two edits.
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

struct Suggestion {
  std::string_view name;
  int score;
};

std::optional<int> SuggestionScore(std::string_view token, std::string_view candidate) {
  if (token.size() < 4 || token.size() > 80 || candidate.size() < 4 || candidate.size() > 80 || token == candidate
      || token.starts_with("--") != candidate.starts_with("--")) {
    return std::nullopt;
  }
  const auto length = std::max(token.size(), candidate.size());
  const int score = IsNearby(token, candidate) ? 100 - static_cast<int>((100 + (length / 2)) / length)
                                               : fuzzy::LevenshteinPercent(token, candidate, false);
  // Require most of the spelling to match, while retaining one-edit short names.
  return score >= 75 ? std::optional<int>(score) : std::nullopt;
}

std::string RenderSuggestions(std::vector<Suggestion> candidates, std::string_view prefix = "") {
  std::ranges::sort(candidates, [](const Suggestion& left, const Suggestion& right) {
    return left.score != right.score ? left.score > right.score : left.name < right.name;
  });
  const auto duplicates = std::ranges::unique(candidates, {}, &Suggestion::name);
  candidates.erase(duplicates.begin(), duplicates.end());
  if (candidates.empty()) {
    return {};
  }
  if (candidates.size() > 3) {
    candidates.resize(3);
  }
  std::vector<std::string> names;
  names.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    names.push_back(absl::StrCat(prefix, candidate.name));
  }
  return absl::StrCat("Did you mean '", absl::StrJoin(names, "' or '"), "'?\n");
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
  std::vector<Suggestion> candidates;
  for (const auto& descriptor : registry::All()) {
    if (const auto score = SuggestionScore(token, descriptor.name)) {
      candidates.push_back({.name = descriptor.name, .score = *score});
    }
  }
  return RenderSuggestions(std::move(candidates));
}

}  // namespace

std::string UnknownGlobalHint(std::string_view token) {
  token = token.substr(0, token.find('='));
  if (IsKnownGlobal(token)
      || std::ranges::any_of(registry::All(), [token](const auto& entry) { return entry.name == token; })) {
    return {};
  }
  std::vector<Suggestion> candidates;
  for (const GlobalFlag& flag : Globals()) {
    if (flag.config_only) {
      continue;
    }
    if (const auto score = SuggestionScore(token, flag.name)) {
      candidates.push_back({.name = flag.name, .score = *score});
    }
    if (const auto score = SuggestionScore(token, flag.alias)) {
      candidates.push_back({.name = flag.alias, .score = *score});
    }
  }
  for (const auto& descriptor : registry::All()) {
    if (const auto score = SuggestionScore(token, descriptor.name)) {
      candidates.push_back({.name = descriptor.name, .score = *score});
    }
  }
  return RenderSuggestions(std::move(candidates));
}

std::string UnknownHelpHint(std::string_view selector) {
  if (selector.size() < 4 || selector.size() > 80 || selector.contains('=')) {
    return {};
  }
  const std::string token = absl::AsciiStrToLower(selector);
  std::vector<Suggestion> candidates;
  bool exact = false;
  const auto add = [&](std::string_view name) {
    if (!token.starts_with('-')) {
      name.remove_prefix(std::min(name.find_first_not_of('-'), name.size()));
    }
    exact = exact || token == name;
    if (const auto score = SuggestionScore(token, name)) {
      candidates.push_back({.name = name, .score = *score});
    }
  };
  for (const HelpTopic& topic : HelpTopics()) {
    add(topic.name);
    for (const std::string_view alias : topic.aliases) {
      add(alias);
    }
  }
  for (const GlobalFlag& flag : Globals()) {
    add(flag.name);
    add(flag.alias);
  }
  for (const auto& descriptor : registry::All()) {
    add(descriptor.name);
  }
  return exact ? std::string() : RenderSuggestions(std::move(candidates), "--help=");
}

std::string ParseErrorHint(const absl::Status& status) {
  if (const auto token = status.GetPayload(parser::kUnknownPredicatePayload); token.has_value()) {
    return PredicateHint(std::string(*token));
  }
  return {};
}

}  // namespace xff::cli
