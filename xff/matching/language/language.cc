// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/matching/language/language.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "mbo/status/status_macros.h"
#include "mbo/types/optional_ref.h"
#include "nlohmann/json.hpp"
#include "xff/matching/language/language_database_api.h"
#include "xff/vfs/local_fs.h"

namespace xff::language {
struct LanguageVocabulary {
  struct TerminalColor {
    std::array<char, 18> bytes{};  // `38;2;255;255;255`
    std::uint8_t size = 0;

    [[nodiscard]] std::string_view View() const { return {bytes.data(), size}; }
  };

  struct Record {
    std::string_view type;
    std::string_view color;
    std::string_view group;
    std::string_view source;
    std::vector<std::string_view> aliases;
    std::vector<std::string_view> extensions;
    std::vector<std::string_view> filenames;
    std::vector<std::string_view> shared_extensions;
    std::vector<std::string_view> shared_filenames;
    TerminalColor terminal_color;
  };

  using SharedClaims = std::map<std::string, std::set<std::string, std::less<>>, std::less<>>;

  struct Match {
    std::string_view preferred;
    std::vector<LanguageInfo> candidates;
  };

  using Matches = std::map<std::string, Match, std::less<>>;

  std::map<std::string, Record, std::less<>> languages;
  std::map<std::string, std::string, std::less<>> extensions;
  std::map<std::string, std::string, std::less<>> filenames;
  SharedClaims shared_extensions;
  SharedClaims shared_filenames;
  Matches extension_matches;
  Matches filename_matches;
  std::vector<LanguageInfo> views;
  std::vector<std::unique_ptr<const nlohmann::ordered_json>> layers;
  std::vector<std::unique_ptr<const std::string>> normalized;
};

namespace {

using Json = nlohmann::ordered_json;

constexpr auto kStringFields = std::to_array<std::string_view>({"type", "color", "group", "source"});

struct State {
  absl::Mutex mutex;
  std::vector<std::unique_ptr<const LanguageVocabulary>> snapshots ABSL_GUARDED_BY(mutex);
  mbo::types::OptionalRef<const LanguageVocabulary> active ABSL_GUARDED_BY(mutex);
};

State& GlobalState() {
  static State state;
  return state;
}

std::string Lower(std::string_view value) {
  return absl::AsciiStrToLower(std::string(value));
}

std::optional<std::uint8_t> HexDigit(char digit) {
  if (digit >= '0' && digit <= '9') {
    return static_cast<std::uint8_t>(digit - '0');
  }
  const char folded = absl::ascii_tolower(static_cast<unsigned char>(digit));
  if (folded >= 'a' && folded <= 'f') {
    return static_cast<std::uint8_t>(folded - 'a' + 10);
  }
  return std::nullopt;
}

std::optional<std::uint8_t> HexByte(std::string_view value) {
  if (value.size() != 2) {
    return std::nullopt;
  }
  const std::optional<std::uint8_t> high = HexDigit(value.front());
  const std::optional<std::uint8_t> low = HexDigit(value.back());
  if (!high.has_value() || !low.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>((static_cast<unsigned>(*high) * 16U) + static_cast<unsigned>(*low));
}

void AppendDecimal(std::uint8_t value, LanguageVocabulary::TerminalColor& result) {
  const auto append = [&](char digit) { result.bytes.at(result.size++) = digit; };
  if (value >= 100) {
    append(static_cast<char>('0' + (value / 100)));
    value %= 100;
    append(static_cast<char>('0' + (value / 10)));
  } else if (value >= 10) {
    append(static_cast<char>('0' + (value / 10)));
  }
  append(static_cast<char>('0' + (value % 10)));
}

LanguageVocabulary::TerminalColor MakeTerminalColor(std::string_view color) {
  LanguageVocabulary::TerminalColor result;
  if (color.size() != 7 || color.front() != '#') {
    return result;
  }
  const std::optional<std::uint8_t> red = HexByte(color.substr(1, 2));
  const std::optional<std::uint8_t> green = HexByte(color.substr(3, 2));
  const std::optional<std::uint8_t> blue = HexByte(color.substr(5, 2));
  if (!red.has_value() || !green.has_value() || !blue.has_value()) {
    return result;
  }
  for (const char prefix : std::string_view("38;2;")) {
    result.bytes.at(result.size++) = prefix;
  }
  AppendDecimal(*red, result);
  result.bytes.at(result.size++) = ';';
  AppendDecimal(*green, result);
  result.bytes.at(result.size++) = ';';
  AppendDecimal(*blue, result);
  return result;
}

void AddCore(LanguageVocabulary& vocabulary, std::string_view extension, std::string_view language) {
  LanguageVocabulary::Record& info = vocabulary.languages[std::string(language)];
  info.extensions.emplace_back(extension);
  vocabulary.extensions[std::string(extension)] = language;
}

void AddCoreFilename(LanguageVocabulary& vocabulary, std::string_view filename, std::string_view language) {
  LanguageVocabulary::Record& info = vocabulary.languages[std::string(language)];
  info.filenames.emplace_back(filename);
  vocabulary.filenames[std::string(filename)] = language;
}

LanguageVocabulary CoreVocabulary() {
  LanguageVocabulary vocabulary;
  constexpr auto kExtensions = std::to_array<std::pair<std::string_view, std::string_view>>({
      {"asm", "Assembly"},
      {"awk", "Awk"},
      {"bash", "Shell"},
      {"bat", "Batchfile"},
      {"bzl", "Starlark"},
      {"c", "C"},
      {"cc", "C++"},
      {"clj", "Clojure"},
      {"cljc", "Clojure"},
      {"cljs", "Clojure"},
      {"cmake", "CMake"},
      {"cpp", "C++"},
      {"cs", "C#"},
      {"css", "CSS"},
      {"cxx", "C++"},
      {"d", "D"},
      {"dart", "Dart"},
      {"el", "Emacs Lisp"},
      {"erl", "Erlang"},
      {"ex", "Elixir"},
      {"exs", "Elixir"},
      {"f90", "Fortran"},
      {"go", "Go"},
      {"groovy", "Groovy"},
      {"h", "C"},
      {"hh", "C++"},
      {"hpp", "C++"},
      {"hrl", "Erlang"},
      {"hs", "Haskell"},
      {"htm", "HTML"},
      {"html", "HTML"},
      {"hxx", "C++"},
      {"ini", "INI"},
      {"java", "Java"},
      {"jl", "Julia"},
      {"js", "JavaScript"},
      {"json", "JSON"},
      {"jsx", "JavaScript"},
      {"kt", "Kotlin"},
      {"kts", "Kotlin"},
      {"less", "Less"},
      {"lua", "Lua"},
      {"m", "Objective-C"},
      {"markdown", "Markdown"},
      {"md", "Markdown"},
      {"mjs", "JavaScript"},
      {"ml", "OCaml"},
      {"mli", "OCaml"},
      {"mm", "Objective-C++"},
      {"nim", "Nim"},
      {"php", "PHP"},
      {"pl", "Perl"},
      {"pm", "Perl"},
      {"proto", "Protocol Buffer"},
      {"ps1", "PowerShell"},
      {"py", "Python"},
      {"pyi", "Python"},
      {"r", "R"},
      {"rb", "Ruby"},
      {"rs", "Rust"},
      {"rst", "reStructuredText"},
      {"sass", "Sass"},
      {"scala", "Scala"},
      {"scm", "Scheme"},
      {"scss", "SCSS"},
      {"sh", "Shell"},
      {"sql", "SQL"},
      {"swift", "Swift"},
      {"tex", "TeX"},
      {"tf", "HCL"},
      {"toml", "TOML"},
      {"ts", "TypeScript"},
      {"tsx", "TypeScript"},
      {"vim", "Vim Script"},
      {"xml", "XML"},
      {"yaml", "YAML"},
      {"yml", "YAML"},
      {"zig", "Zig"},
  });
  for (const auto& [extension, language] : kExtensions) {
    AddCore(vocabulary, extension, language);
  }
  // Header filenames alone cannot distinguish these languages. Preserve the
  // preferred C label while exposing all three candidates to language filters.
  for (const std::string_view language : {"C++", "Objective-C"}) {
    vocabulary.languages[std::string(language)].shared_extensions.emplace_back("h");
    vocabulary.shared_extensions["h"].emplace(language);
  }
  constexpr auto kFilenames = std::to_array<std::pair<std::string_view, std::string_view>>({
      {".bash_profile", "Shell"},
      {".bashrc", "Shell"},
      {".profile", "Shell"},
      {".zshrc", "Shell"},
      {"BUILD", "Starlark"},
      {"BUILD.bazel", "Starlark"},
      {"CMakeLists.txt", "CMake"},
      {"Dockerfile", "Dockerfile"},
      {"GNUmakefile", "Makefile"},
      {"Gemfile", "Ruby"},
      {"MODULE.bazel", "Starlark"},
      {"Makefile", "Makefile"},
      {"Rakefile", "Ruby"},
      {"WORKSPACE", "Starlark"},
      {"WORKSPACE.bazel", "Starlark"},
      {"makefile", "Makefile"},
  });
  for (const auto& [filename, language] : kFilenames) {
    AddCoreFilename(vocabulary, filename, language);
  }
  return vocabulary;
}

absl::Status JsonError(std::string_view layer, std::string_view message) {
  return absl::InvalidArgumentError(absl::StrCat("language vocabulary '", layer, "': ", message));
}

absl::StatusOr<std::vector<std::string_view>> StringList(
    const Json& object,
    std::string_view field,
    std::string_view layer,
    std::string_view language) {
  const auto found = object.find(field);
  if (found == object.end()) {
    return std::vector<std::string_view>{};
  }
  if (!found->is_array()) {
    return JsonError(layer, absl::StrCat(language, ".", field, " must be an array of strings"));
  }
  std::vector<std::string_view> result;
  for (const Json& value : *found) {
    if (!value.is_string()) {
      return JsonError(layer, absl::StrCat(language, ".", field, " must contain only strings"));
    }
    result.push_back(value.get_ref<const std::string&>());
  }
  return result;
}

absl::Status ApplyStringFields(
    LanguageVocabulary::Record& info,
    const Json& value,
    std::string_view layer,
    std::string_view name) {
  for (const std::string_view field : kStringFields) {
    const auto found = value.find(field);
    if (found == value.end()) {
      continue;
    }
    if (!found->is_string()) {
      return JsonError(layer, absl::StrCat(name, ".", field, " must be a string"));
    }
    const std::string_view field_value = found->get_ref<const std::string&>();
    if (field == "type") {
      info.type = field_value;
    } else if (field == "color") {
      info.color = field_value;
    } else if (field == "group") {
      info.group = field_value;
    } else {
      info.source = field_value;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<std::string_view>> Filenames(
    const Json& value,
    std::string_view layer,
    std::string_view name,
    std::string_view field = "filenames") {
  MBO_ASSIGN_OR_RETURN(auto filenames, StringList(value, field, layer, name));
  for (const std::string_view filename : filenames) {
    if (filename.empty() || absl::StrContains(filename, '/')) {
      return JsonError(layer, absl::StrCat(name, " has invalid filename '", filename, "'"));
    }
  }
  return filenames;
}

absl::Status ApplyAliases(
    LanguageVocabulary::Record& info,
    const Json& value,
    std::string_view layer,
    std::string_view name) {
  if (!value.contains("aliases")) {
    return absl::OkStatus();
  }
  MBO_ASSIGN_OR_RETURN(info.aliases, StringList(value, "aliases", layer, name));
  return absl::OkStatus();
}

class LayerProcessor {
 public:
  LayerProcessor(LanguageVocabulary& vocabulary, std::string_view layer, ConflictPolicy conflicts)
      : vocabulary_(vocabulary), layer_(layer), conflicts_(conflicts) {}

  absl::Status Apply(std::string_view text) {
    root_ = std::make_unique<Json>(Json::parse(text, nullptr, false));
    if (root_->is_discarded()) {
      return JsonError(layer_, "invalid JSON");
    }
    if (!root_->is_object()) {
      return JsonError(layer_, "top level must be an object keyed by canonical language name");
    }
    for (const auto& [name, value] : root_->items()) {
      MBO_RETURN_IF_ERROR(ApplyEntry(name, value));
    }
    Commit();
    return absl::OkStatus();
  }

 private:
  using Claims = std::map<std::string, std::string, std::less<>>;
  using Replacements = std::set<std::string, std::less<>>;

  absl::Status AddClaims(
      Claims& claims,
      absl::Span<const std::string_view> values,
      std::string_view language,
      std::string_view kind) {
    for (const std::string_view value : values) {
      const auto [claim, inserted] = claims.emplace(value, language);
      if (inserted || claim->second == language) {
        continue;
      }
      if (conflicts_ == ConflictPolicy::kError) {
        return JsonError(layer_, absl::StrCat(kind, " '", value, "' is claimed by ", claim->second, " and ", language));
      }
      if (conflicts_ == ConflictPolicy::kLast) {
        claim->second = language;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<std::vector<std::string_view>> Extensions(
      const Json& value,
      std::string_view name,
      std::string_view field = "extensions") {
    MBO_ASSIGN_OR_RETURN(auto extensions, StringList(value, field, layer_, name));
    for (std::string_view& extension : extensions) {
      std::string normalized = Lower(extension);
      if (normalized.starts_with('.')) {
        normalized.erase(0, 1);
      }
      if (normalized.empty() || absl::StrContains(normalized, '/')) {
        return JsonError(layer_, absl::StrCat(name, " has invalid extension '", normalized, "'"));
      }
      if (normalized != extension) {
        normalized_.push_back(std::make_unique<const std::string>(std::move(normalized)));
        extension = *normalized_.back();
      }
    }
    return extensions;
  }

  absl::Status ApplyExtensionClaims(LanguageVocabulary::Record& info, const Json& value, std::string_view name) {
    if (!value.contains("extensions")) {
      return absl::OkStatus();
    }
    MBO_ASSIGN_OR_RETURN(info.extensions, Extensions(value, name));
    replaced_extensions_.insert(std::string(name));
    return AddClaims(extension_claims_, info.extensions, name, "extension");
  }

  absl::Status ApplyFilenameClaims(LanguageVocabulary::Record& info, const Json& value, std::string_view name) {
    if (!value.contains("filenames")) {
      return absl::OkStatus();
    }
    MBO_ASSIGN_OR_RETURN(info.filenames, Filenames(value, layer_, name));
    replaced_filenames_.insert(std::string(name));
    return AddClaims(filename_claims_, info.filenames, name, "filename");
  }

  absl::Status ApplySharedClaims(LanguageVocabulary::Record& info, const Json& value, std::string_view name) {
    if (value.contains("shared_extensions")) {
      MBO_ASSIGN_OR_RETURN(info.shared_extensions, Extensions(value, name, "shared_extensions"));
      replaced_shared_extensions_.emplace(name);
      for (const auto extension : info.shared_extensions) {
        shared_extensions_[std::string(extension)].emplace(name);
      }
    }
    if (value.contains("shared_filenames")) {
      MBO_ASSIGN_OR_RETURN(info.shared_filenames, Filenames(value, layer_, name, "shared_filenames"));
      replaced_shared_filenames_.emplace(name);
      for (const auto filename : info.shared_filenames) {
        shared_filenames_[std::string(filename)].emplace(name);
      }
    }
    return absl::OkStatus();
  }

  absl::Status ApplyEntry(std::string_view name, const Json& value) {
    if (name.empty() || !value.is_object()) {
      return JsonError(layer_, absl::StrCat("invalid language entry: ", name));
    }
    LanguageVocabulary::Record info;
    if (const auto found = vocabulary_.languages.find(name); found != vocabulary_.languages.end()) {
      info = found->second;
    }
    MBO_RETURN_IF_ERROR(ApplyStringFields(info, value, layer_, name));
    MBO_RETURN_IF_ERROR(ApplyAliases(info, value, layer_, name));
    MBO_RETURN_IF_ERROR(ApplyExtensionClaims(info, value, name));
    MBO_RETURN_IF_ERROR(ApplyFilenameClaims(info, value, name));
    MBO_RETURN_IF_ERROR(ApplySharedClaims(info, value, name));
    languages_[std::string(name)] = std::move(info);
    return absl::OkStatus();
  }

  static void RemoveShared(LanguageVocabulary::SharedClaims& claims, const Replacements& replacements) {
    for (auto& [key, languages] : claims) {
      for (const auto& language : replacements) {
        languages.erase(language);
      }
    }
    std::erase_if(claims, [](const auto& claim) { return claim.second.empty(); });
  }

  void Commit() {
    RemoveShared(vocabulary_.shared_extensions, replaced_shared_extensions_);
    RemoveShared(vocabulary_.shared_filenames, replaced_shared_filenames_);
    for (const std::string& name : replaced_extensions_) {
      std::erase_if(vocabulary_.extensions, [&](const auto& claim) { return claim.second == name; });
    }
    for (const std::string& name : replaced_filenames_) {
      std::erase_if(vocabulary_.filenames, [&](const auto& claim) { return claim.second == name; });
    }
    for (auto& [name, info] : languages_) {
      vocabulary_.languages[name] = std::move(info);
    }
    for (const auto& [extension, name] : extension_claims_) {
      vocabulary_.extensions[extension] = name;
      vocabulary_.shared_extensions.erase(extension);
    }
    for (const auto& [filename, name] : filename_claims_) {
      vocabulary_.filenames[filename] = name;
      vocabulary_.shared_filenames.erase(filename);
    }
    for (const auto& [extension, names] : shared_extensions_) {
      vocabulary_.shared_extensions[extension].insert(names.begin(), names.end());
    }
    for (const auto& [filename, names] : shared_filenames_) {
      vocabulary_.shared_filenames[filename].insert(names.begin(), names.end());
    }
    for (auto& value : normalized_) {
      vocabulary_.normalized.push_back(std::move(value));
    }
    vocabulary_.layers.push_back(std::move(root_));
  }

  LanguageVocabulary& vocabulary_;
  std::string_view layer_;
  ConflictPolicy conflicts_;
  Claims extension_claims_;
  Claims filename_claims_;
  std::map<std::string, LanguageVocabulary::Record, std::less<>> languages_;
  Replacements replaced_extensions_;
  Replacements replaced_filenames_;
  Replacements replaced_shared_extensions_;
  Replacements replaced_shared_filenames_;
  LanguageVocabulary::SharedClaims shared_extensions_;
  LanguageVocabulary::SharedClaims shared_filenames_;
  std::unique_ptr<const Json> root_;
  std::vector<std::unique_ptr<const std::string>> normalized_;
};

absl::StatusOr<std::string> ReadFile(const std::string& path) {
  const vfs::LocalFs fs;
  const absl::StatusOr<std::string> content = fs.ReadContent(path);
  if (!content.ok()) {
    return absl::NotFoundError(absl::StrCat("cannot read language vocabulary: ", path));
  }
  return *content;
}

mbo::types::OptionalRef<const LanguageVocabulary::Match> Lookup(
    const LanguageVocabulary& vocabulary,
    std::string_view name) {
  if (const auto found = vocabulary.filename_matches.find(name); found != vocabulary.filename_matches.end()) {
    return found->second;
  }
  const std::string folded = Lower(name);
  for (std::size_t dot = folded.find('.'); dot != std::string::npos; dot = folded.find('.', dot + 1)) {
    const auto found = vocabulary.extension_matches.find(std::string_view(folded).substr(dot + 1));
    if (found != vocabulary.extension_matches.end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

LanguageInfo View(std::string_view name, const LanguageVocabulary::Record& record) {
  return {
      .name = name,
      .type = record.type,
      .color = record.color,
      .group = record.group,
      .source = record.source,
      .aliases = record.aliases,
      .extensions = record.extensions,
      .filenames = record.filenames,
      .shared_extensions = record.shared_extensions,
      .shared_filenames = record.shared_filenames,
  };
}

LanguageVocabulary::Matches BuildMatches(
    const LanguageVocabulary& vocabulary,
    const std::map<std::string, std::string, std::less<>>& preferred,
    const LanguageVocabulary::SharedClaims& shared) {
  LanguageVocabulary::Matches matches;
  for (const auto& [key, name] : preferred) {
    const auto language = vocabulary.languages.find(name);
    auto& match = matches[key];
    match.preferred = language->first;
    match.candidates.push_back(View(language->first, language->second));
  }
  for (const auto& [key, names] : shared) {
    auto& match = matches[key];
    match.candidates.reserve(match.candidates.size() + names.size());
    for (const auto& name : names) {
      if (name != match.preferred) {
        const auto language = vocabulary.languages.find(name);
        match.candidates.push_back(View(language->first, language->second));
      }
    }
    std::ranges::sort(match.candidates, {}, &LanguageInfo::name);
  }
  return matches;
}

void Finalize(LanguageVocabulary& vocabulary) {
  vocabulary.views.reserve(vocabulary.languages.size());
  for (auto& [name, record] : vocabulary.languages) {
    record.terminal_color = MakeTerminalColor(record.color);
    vocabulary.views.push_back(View(name, record));
  }
  vocabulary.extension_matches = BuildMatches(vocabulary, vocabulary.extensions, vocabulary.shared_extensions);
  vocabulary.filename_matches = BuildMatches(vocabulary, vocabulary.filenames, vocabulary.shared_filenames);
}

void EnsureConfigured(State& state) ABSL_EXCLUSIVE_LOCKS_REQUIRED(state.mutex) {
  if (state.active.has_value()) {
    return;
  }
  auto vocabulary = std::make_unique<LanguageVocabulary>(CoreVocabulary());
  for (const Database& database : Databases()) {
    CHECK_OK(LayerProcessor(*vocabulary, database.name, ConflictPolicy::kLast).Apply(database.json()));
  }
  Finalize(*vocabulary);
  state.snapshots.push_back(std::move(vocabulary));
  state.active.set_ref(*state.snapshots.back());
}

}  // namespace

absl::Status Configure(absl::Span<const std::string> files, ConflictPolicy conflicts) {
  if (files.empty()) {
    State& state = GlobalState();
    const absl::MutexLock lock(state.mutex);
    state.active.reset();
    return absl::OkStatus();
  }
  LanguageVocabulary vocabulary = CoreVocabulary();
  for (const Database& database : Databases()) {
    MBO_RETURN_IF_ERROR(LayerProcessor(vocabulary, database.name, ConflictPolicy::kLast).Apply(database.json()));
  }
  for (const std::string& file : files) {
    MBO_ASSIGN_OR_RETURN(const std::string text, ReadFile(file));
    MBO_RETURN_IF_ERROR(LayerProcessor(vocabulary, file, conflicts).Apply(text));
  }
  Finalize(vocabulary);
  State& state = GlobalState();
  const absl::MutexLock lock(state.mutex);
  auto snapshot = std::make_unique<const LanguageVocabulary>(std::move(vocabulary));
  state.snapshots.push_back(std::move(snapshot));
  state.active.set_ref(*state.snapshots.back());
  return absl::OkStatus();
}

LanguageSnapshot ActiveSnapshot() {
  State& state = GlobalState();
  const absl::MutexLock lock(state.mutex);
  EnsureConfigured(state);
  return LanguageSnapshot(*state.active);
}

std::optional<LanguageInfo> LanguageSnapshot::InfoForName(std::string_view name) const {
  const auto found = Lookup(vocabulary_.get(), name);
  if (!found.has_value() || found->preferred.empty()) {
    return std::nullopt;
  }
  return View(found->preferred, vocabulary_.get().languages.find(found->preferred)->second);
}

std::string_view LanguageSnapshot::LanguageForName(std::string_view name) const {
  const auto found = Lookup(vocabulary_.get(), name);
  return found.has_value() ? found->preferred : std::string_view{};
}

std::string_view LanguageSnapshot::TerminalColorForName(std::string_view name) const {
  const auto found = Lookup(vocabulary_.get(), name);
  if (!found.has_value() || found->preferred.empty()) {
    return {};
  }
  return vocabulary_.get().languages.find(found->preferred)->second.terminal_color.View();
}

absl::Span<const LanguageInfo> LanguageSnapshot::CandidatesForName(std::string_view name) const {
  const auto found = Lookup(vocabulary_.get(), name);
  return found.has_value() ? absl::Span<const LanguageInfo>(found->candidates) : absl::Span<const LanguageInfo>{};
}

absl::Span<const LanguageInfo> CandidatesForName(std::string_view name) {
  return ActiveSnapshot().CandidatesForName(name);
}

absl::Span<const LanguageInfo> LanguageSnapshot::Languages() const {
  return vocabulary_.get().views;
}

std::optional<LanguageInfo> InfoForName(std::string_view name) {
  return ActiveSnapshot().InfoForName(name);
}

std::string_view LanguageForName(std::string_view name) {
  return ActiveSnapshot().LanguageForName(name);
}

std::string_view TerminalColorForName(std::string_view name) {
  return ActiveSnapshot().TerminalColorForName(name);
}

absl::Span<const LanguageInfo> Languages() {
  return ActiveSnapshot().Languages();
}

}  // namespace xff::language
