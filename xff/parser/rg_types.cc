// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/parser/rg_types.h"

#include <algorithm>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "mbo/status/status_macros.h"

namespace xff::parser {
namespace {
struct Definition {
  std::set<std::string> languages;
  std::vector<std::string> globs;

  void Append(const Definition& other) {
    languages.insert(other.languages.begin(), other.languages.end());
    globs.insert(globs.end(), other.globs.begin(), other.globs.end());
  }
};

struct TypeCatalog {
  absl::btree_map<std::string, Definition, std::less<>> definitions;
  absl::btree_map<std::string, std::string, std::less<>> aliases;

  void AddAlias(std::string_view alias, std::string_view canonical) {
    const std::string name = absl::AsciiStrToLower(alias);
    if (name.empty() || name == "all" || definitions.contains(name)) {
      return;
    }
    auto [found, inserted] = aliases.try_emplace(name, canonical);
    if (!inserted && found->second != canonical) {
      found->second.clear();  // An ambiguous alias must never silently pick a language.
    }
  }

  absl::StatusOr<std::string> Name(std::string_view name) const {
    std::string key = absl::AsciiStrToLower(name);
    if (definitions.contains(key)) {
      return key;
    }
    const auto found = aliases.find(key);
    if (found == aliases.end()) {
      return key;
    }
    if (found->second.empty()) {
      return absl::InvalidArgumentError(absl::StrCat("ambiguous file type alias '", name, "'; use a language name"));
    }
    return found->second;
  }

  absl::StatusOr<Definition> Select(std::string_view name) const {
    MBO_ASSIGN_OR_RETURN(const auto key, Name(name));
    if (key == "all") {
      Definition result;
      for (const auto& [type, definition] : definitions) {
        result.Append(definition);
      }
      return result;
    }
    const auto found = definitions.find(key);
    if (found == definitions.end()) {
      return absl::InvalidArgumentError(absl::StrCat("unknown file type '", name, "'; use --type-list"));
    }
    return found->second;
  }
};

TypeCatalog CatalogTypes(const language::LanguageSnapshot& vocabulary) {
  TypeCatalog catalog;
  for (const auto& info : vocabulary.Languages()) {
    const std::string name = absl::AsciiStrToLower(info.name);
    catalog.definitions[name].languages.emplace(info.name);
    catalog.aliases.emplace(name, name);
  }
  for (const auto& info : vocabulary.Languages()) {
    const std::string canonical = absl::AsciiStrToLower(info.name);
    for (const std::string_view alias : info.aliases) {
      catalog.AddAlias(alias, canonical);
    }
    // Suffix aliases come from effective candidates, including overlaps. An
    // ambiguous suffix alias requires a canonical language name instead.
    for (const auto suffixes : {info.extensions, info.shared_extensions}) {
      for (const std::string_view suffix : suffixes) {
        if (std::ranges::any_of(
                vocabulary.CandidatesForName(absl::StrCat("file.", suffix)),
                [&](const auto& candidate) { return candidate.name == info.name; })) {
          catalog.AddAlias(suffix, canonical);
        }
      }
    }
  }
  return catalog;
}

absl::Status AddDefinition(TypeCatalog& catalog, std::string_view spec) {
  const auto colon = spec.find(':');
  const auto name = spec.substr(0, colon);
  MBO_ASSIGN_OR_RETURN(const auto key, catalog.Name(name));
  // Existing canonical names/aliases can contain punctuation; new custom names
  // retain rg's letters-and-numbers rule, independent of the current locale.
  MBO_ASSIGN_OR_RETURN(const auto valid, regex::Matcher::Compile("[\\p{L}\\p{N}]+", false));
  const bool known = catalog.definitions.contains(key) || catalog.aliases.contains(absl::AsciiStrToLower(name));
  if (colon == std::string_view::npos || key == "all" || (!known && !valid.FullMatch(name))
      || colon + 1 == spec.size()) {
    return absl::InvalidArgumentError(
        "--type-add requires NAME:GLOB; new NAME contains only letters and numbers and cannot be all");
  }
  const auto value = spec.substr(colon + 1);
  if (!value.starts_with("include:")) {
    MBO_RETURN_IF_ERROR(regex::Matcher::Compile(value, false, regex::Grammar::kShglob).status());
    catalog.definitions[key].globs.emplace_back(value);
    return absl::OkStatus();
  }
  Definition imported;
  for (const std::string_view source : absl::StrSplit(value.substr(8), ',')) {
    MBO_ASSIGN_OR_RETURN(const auto definition, catalog.Select(source));
    imported.Append(definition);
  }
  catalog.definitions[key].Append(imported);
  return absl::OkStatus();
}

absl::StatusOr<TypeCatalog> BuildDefinitions(const RgSearch& search, const language::LanguageSnapshot& vocabulary) {
  TypeCatalog catalog = CatalogTypes(vocabulary);
  for (const auto& rule : search.types) {
    if (rule.kind == RgTypeRule::Kind::kClear) {
      MBO_ASSIGN_OR_RETURN(const auto key, catalog.Name(rule.value));
      catalog.definitions.erase(key);
    } else if (rule.kind == RgTypeRule::Kind::kAdd) {
      MBO_RETURN_IF_ERROR(AddDefinition(catalog, rule.value));
    }
  }
  return catalog;
}

std::string Describe(const Definition& definition) {
  std::vector<std::string> parts;
  parts.reserve(definition.languages.size() + definition.globs.size());
  for (const auto& name : definition.languages) {
    parts.push_back(absl::StrCat("language ", name));
  }
  parts.insert(parts.end(), definition.globs.begin(), definition.globs.end());
  std::ranges::sort(parts);
  const auto duplicates = std::ranges::unique(parts);
  parts.erase(duplicates.begin(), duplicates.end());
  return absl::StrJoin(parts, ", ");
}

std::string ListDefinitions(const TypeCatalog& catalog) {
  absl::btree_map<std::string, std::string, std::less<>> rows;
  for (const auto& [name, definition] : catalog.definitions) {
    rows.emplace(name, Describe(definition));
  }
  for (const auto& [alias, name] : catalog.aliases) {
    if (const auto found = rows.find(name); found != rows.end()) {
      const std::string description = found->second;
      rows.try_emplace(alias, description);
    }
  }
  std::string listing;
  for (const auto& [name, description] : rows) {
    absl::StrAppend(&listing, name, ": ", description, "\n");
  }
  return listing;
}
}  // namespace

absl::StatusOr<RgTypes> RgTypes::Compile(const RgSearch& search) {
  RgTypes result;
  if (search.types.empty() && !search.type_list) {
    return result;
  }
  const auto vocabulary = language::ActiveSnapshot();
  result.vocabulary_ = vocabulary;
  MBO_ASSIGN_OR_RETURN(const auto catalog, BuildDefinitions(search, vocabulary));
  for (const auto& option : search.types) {
    if (option.kind != RgTypeRule::Kind::kInclude && option.kind != RgTypeRule::Kind::kExclude) {
      continue;
    }
    MBO_ASSIGN_OR_RETURN(const auto definition, catalog.Select(option.value));
    Rule rule{.include = option.kind == RgTypeRule::Kind::kInclude};
    rule.languages.insert(definition.languages.begin(), definition.languages.end());
    result.default_include_ = result.default_include_ && !rule.include;
    for (const auto& glob : definition.globs) {
      MBO_ASSIGN_OR_RETURN(auto matcher, regex::Matcher::Compile(glob, false, regex::Grammar::kShglob));
      rule.matchers.push_back(std::move(matcher));
    }
    result.rules_.push_back(std::move(rule));
  }
  if (search.type_list) {
    result.listing_ = ListDefinitions(catalog);
  }
  return result;
}

bool RgTypes::Includes(std::string_view basename) const {
  if (!vocabulary_) {
    return default_include_;
  }
  const auto candidates = vocabulary_->CandidatesForName(basename);
  for (const auto& rule : rules_ | std::views::reverse) {
    if (std::ranges::any_of(candidates, [&](const auto& candidate) { return rule.languages.contains(candidate.name); })
        || std::ranges::any_of(rule.matchers, [&](const auto& matcher) { return matcher.FullMatch(basename); })) {
      return rule.include;
    }
  }
  return default_include_;
}
}  // namespace xff::parser
