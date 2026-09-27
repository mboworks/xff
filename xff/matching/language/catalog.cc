// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/matching/language/catalog.h"

#include <algorithm>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "mbo/status/status_macros.h"
#include "xff/matching/regex/regex.h"

namespace xff::language {
namespace {
struct Definition {
  absl::btree_set<std::string> languages;
  std::vector<std::string> globs;
  std::vector<regex::Matcher> matchers;
  std::vector<std::string_view> aliases;

  void Append(const Definition& other) {
    languages.insert(other.languages.begin(), other.languages.end());
    globs.insert(globs.end(), other.globs.begin(), other.globs.end());
  }
};

struct Definitions {
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
    return Definition{.languages = found->second.languages, .globs = found->second.globs};
  }
};

Definitions CatalogTypes(const LanguageSnapshot& vocabulary) {
  Definitions catalog;
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

absl::Status AddDefinition(Definitions& catalog, std::string_view spec) {
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

std::string ListDefinitions(const Definitions& catalog) {
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

struct Catalog::Data {
  explicit Data(LanguageSnapshot snapshot) : vocabulary(snapshot), types(CatalogTypes(snapshot)) {}

  LanguageSnapshot vocabulary;
  Definitions types;
  absl::btree_map<std::string, std::vector<std::string_view>, std::less<>> memberships;
  std::vector<std::string_view> glob_types;

  absl::Status Finalize() {
    for (auto& [name, definition] : types.definitions) {
      for (const auto& language : definition.languages) {
        memberships[language].push_back(name);
      }
      if (!definition.globs.empty()) {
        glob_types.push_back(name);
      }
      for (const auto& glob : definition.globs) {
        MBO_ASSIGN_OR_RETURN(auto matcher, regex::Matcher::Compile(glob, false, regex::Grammar::kShglob));
        definition.matchers.push_back(std::move(matcher));
      }
    }
    for (const auto& [alias, name] : types.aliases) {
      if (auto found = types.definitions.find(name); found != types.definitions.end() && alias != name) {
        found->second.aliases.push_back(alias);
      }
    }
    return absl::OkStatus();
  }
};

absl::StatusOr<Catalog> Catalog::Compile(LanguageSnapshot vocabulary, absl::Span<const CatalogEdit> edits) {
  auto data = std::make_shared<Data>(vocabulary);
  for (const auto& edit : edits) {
    if (edit.kind == CatalogEdit::Kind::kClear) {
      MBO_ASSIGN_OR_RETURN(const auto key, data->types.Name(edit.value));
      data->types.definitions.erase(key);
    } else {
      MBO_RETURN_IF_ERROR(AddDefinition(data->types, edit.value));
    }
  }
  MBO_RETURN_IF_ERROR(data->Finalize());
  return Catalog(std::move(data));
}

absl::StatusOr<std::string> Catalog::Resolve(std::string_view name) const {
  MBO_ASSIGN_OR_RETURN(auto key, data_->types.Name(name));
  if (key != "all" && !data_->types.definitions.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat("unknown file type '", name, "'; use --type-list"));
  }
  return key;
}

absl::InlinedVector<FilterType, 4> Catalog::CandidatesForName(std::string_view basename) const {
  absl::InlinedVector<std::string_view, 4> names;
  for (const auto& candidate : data_->vocabulary.CandidatesForName(basename)) {
    const auto found = data_->memberships.find(candidate.name);
    if (found != data_->memberships.end()) {
      names.insert(names.end(), found->second.begin(), found->second.end());
    }
  }
  for (const auto& name : data_->glob_types) {
    const auto& definition = data_->types.definitions.at(name);
    if (std::ranges::any_of(definition.matchers, [&](const auto& matcher) { return matcher.FullMatch(basename); })) {
      names.push_back(name);
    }
  }
  std::ranges::sort(names);
  const auto duplicates = std::ranges::unique(names);
  names.erase(duplicates.begin(), duplicates.end());
  absl::InlinedVector<FilterType, 4> result;
  result.reserve(names.size());
  for (const auto& name : names) {
    result.push_back({.name = name, .aliases = data_->types.definitions.at(name).aliases});
  }
  return result;
}

std::string Catalog::Listing() const {
  return ListDefinitions(data_->types);
}

}  // namespace xff::language
