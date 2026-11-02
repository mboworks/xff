// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/matching/mime/mime.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
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
#include "xff/matching/mime/database.h"
#include "xff/vfs/local_fs.h"

namespace xff::mime {
namespace {

namespace stdfs = ::std::filesystem;
// Object member order gives `--mime-conflicts=first|last` an observable, useful
// meaning. JSON itself does not assign semantic significance to that order, but
// nlohmann preserves the source order here rather than silently sorting keys.
using Json = nlohmann::ordered_json;

constexpr std::string_view kFallback = "application/octet-stream";
constexpr auto kStringFields = std::to_array<std::string_view>({"description", "source", "charset"});

struct Vocabulary {
  struct Record {
    std::string_view description;
    std::string_view source;
    std::string_view charset;
    std::optional<bool> compressible;
    std::vector<std::string_view> aliases;
    std::vector<std::string_view> extensions;
  };

  std::map<std::string, Record, std::less<>> types;
  std::map<std::string, std::string, std::less<>> extensions;
  std::vector<TypeInfo> views;
  std::vector<std::unique_ptr<const Json>> layers;
};

struct State {
  absl::Mutex mutex;
  std::vector<std::unique_ptr<const Vocabulary>> snapshots ABSL_GUARDED_BY(mutex);
  mbo::types::OptionalRef<const Vocabulary> active ABSL_GUARDED_BY(mutex);
  mbo::types::OptionalRef<const Vocabulary> default_vocabulary ABSL_GUARDED_BY(mutex);
  std::size_t default_database_count ABSL_GUARDED_BY(mutex) = 0;
};

State& GlobalState() {
  static State state;
  return state;
}

std::string Lower(std::string_view value) {
  return absl::AsciiStrToLower(std::string(value));
}

void AddCore(Vocabulary& vocabulary, std::string_view extension, std::string_view type) {
  Vocabulary::Record& info = vocabulary.types[std::string(type)];
  info.extensions.emplace_back(extension);
  vocabulary.extensions[std::string(extension)] = type;
}

Vocabulary CoreVocabulary() {
  Vocabulary vocabulary;
  constexpr auto kEntries = std::to_array<std::pair<std::string_view, std::string_view>>({
      {"7z", "application/x-7z-compressed"},
      {"avi", "video/x-msvideo"},
      {"bmp", "image/bmp"},
      {"bz2", "application/x-bzip2"},
      {"css", "text/css"},
      {"csv", "text/csv"},
      {"flac", "audio/flac"},
      {"gif", "image/gif"},
      {"gz", "application/gzip"},
      {"htm", "text/html"},
      {"html", "text/html"},
      {"ico", "image/vnd.microsoft.icon"},
      {"jpeg", "image/jpeg"},
      {"jpg", "image/jpeg"},
      {"js", "text/javascript"},
      {"json", "application/json"},
      {"md", "text/markdown"},
      {"mkv", "video/x-matroska"},
      {"mov", "video/quicktime"},
      {"mp3", "audio/mpeg"},
      {"mp4", "video/mp4"},
      {"ogg", "audio/ogg"},
      {"otf", "font/otf"},
      {"pdf", "application/pdf"},
      {"png", "image/png"},
      {"rar", "application/vnd.rar"},
      {"svg", "image/svg+xml"},
      {"tar", "application/x-tar"},
      {"tif", "image/tiff"},
      {"tiff", "image/tiff"},
      {"toml", "application/toml"},
      {"ttf", "font/ttf"},
      {"txt", "text/plain"},
      {"wasm", "application/wasm"},
      {"wav", "audio/wav"},
      {"webm", "video/webm"},
      {"webp", "image/webp"},
      {"woff", "font/woff"},
      {"woff2", "font/woff2"},
      {"xml", "application/xml"},
      {"xz", "application/x-xz"},
      {"yaml", "application/yaml"},
      {"yml", "application/yaml"},
      {"zip", "application/zip"},
  });
  for (const auto& [extension, type] : kEntries) {
    AddCore(vocabulary, extension, type);
  }
  vocabulary.types[std::string(kFallback)];
  return vocabulary;
}

absl::Status JsonError(std::string_view layer, std::string_view message) {
  return absl::InvalidArgumentError(absl::StrCat("MIME vocabulary '", layer, "': ", message));
}

absl::StatusOr<std::vector<std::string_view>> StringList(
    const Json& object,
    std::string_view field,
    std::string_view layer,
    std::string_view type) {
  const auto found = object.find(field);
  if (found == object.end()) {
    return std::vector<std::string_view>{};
  }
  if (!found->is_array()) {
    return JsonError(layer, absl::StrCat(type, ".", field, " must be an array of strings"));
  }
  std::vector<std::string_view> result;
  for (const Json& value : *found) {
    if (!value.is_string()) {
      return JsonError(layer, absl::StrCat(type, ".", field, " must contain only strings"));
    }
    result.push_back(value.get_ref<const std::string&>());
  }
  return result;
}

absl::Status ApplyStringFields(
    Vocabulary::Record& info,
    const Json& value,
    std::string_view layer,
    std::string_view type) {
  for (const std::string_view field : kStringFields) {
    const auto found = value.find(field);
    if (found == value.end()) {
      continue;
    }
    if (!found->is_string()) {
      return JsonError(layer, absl::StrCat(type, ".", field, " must be a string"));
    }
    const std::string_view field_value = found->get_ref<const std::string&>();
    if (field == "description") {
      info.description = field_value;
    } else if (field == "source") {
      info.source = field_value;
    } else {
      info.charset = field_value;
    }
  }
  return absl::OkStatus();
}

absl::Status ApplyOptionalFields(
    Vocabulary::Record& info,
    const Json& value,
    std::string_view layer,
    std::string_view type) {
  if (const auto found = value.find("compressible"); found != value.end()) {
    if (!found->is_boolean()) {
      return JsonError(layer, absl::StrCat(type, ".compressible must be boolean"));
    }
    info.compressible = found->get<bool>();
  }
  if (value.contains("aliases")) {
    MBO_ASSIGN_OR_RETURN(info.aliases, StringList(value, "aliases", layer, type));
  }
  return absl::OkStatus();
}

class LayerProcessor {
 public:
  LayerProcessor(Vocabulary& vocabulary, std::string_view layer, ConflictPolicy conflicts)
      : vocabulary_(vocabulary), layer_(layer), conflicts_(conflicts) {}

  absl::Status Apply(std::string_view text) {
    root_ = std::make_unique<Json>(Json::parse(text, nullptr, false));
    if (root_->is_discarded()) {
      return JsonError(layer_, "invalid JSON");
    }
    if (!root_->is_object()) {
      return JsonError(layer_, "top level must be an object keyed by media type");
    }
    for (const auto& [raw_type, value] : root_->items()) {
      MBO_RETURN_IF_ERROR(ApplyEntry(raw_type, value));
    }
    Commit();
    return absl::OkStatus();
  }

 private:
  absl::StatusOr<std::vector<std::string_view>> Extensions(const Json& value, std::string_view type) {
    MBO_ASSIGN_OR_RETURN(auto extensions, StringList(value, "extensions", layer_, type));
    for (std::string_view& extension : extensions) {
      std::string normalized = Lower(extension);
      if (normalized.starts_with('.')) {
        normalized.erase(0, 1);
      }
      if (normalized.empty() || absl::StrContains(normalized, '/')) {
        return JsonError(layer_, absl::StrCat(type, " has invalid extension '", normalized, "'"));
      }
      if (normalized != extension) {
        normalized_.push_back(std::make_unique<const std::string>(std::move(normalized)));
        extension = *normalized_.back();
      }
    }
    return extensions;
  }

  absl::Status AddClaims(absl::Span<const std::string_view> extensions, std::string_view type) {
    for (const std::string_view extension : extensions) {
      const auto [claim, inserted] = claims_.emplace(extension, type);
      if (inserted || claim->second == type) {
        continue;
      }
      if (conflicts_ == ConflictPolicy::kError) {
        return JsonError(
            layer_, absl::StrCat("extension '", extension, "' is claimed by ", claim->second, " and ", type));
      }
      if (conflicts_ == ConflictPolicy::kLast) {
        claim->second = type;
      }
    }
    return absl::OkStatus();
  }

  absl::Status ApplyEntry(std::string_view raw_type, const Json& value) {
    const std::string type = Lower(raw_type);
    if (!absl::StrContains(type, '/') || !value.is_object()) {
      return JsonError(layer_, absl::StrCat("invalid media-type entry: ", raw_type));
    }
    Vocabulary::Record info;
    if (const auto found = vocabulary_.types.find(type); found != vocabulary_.types.end()) {
      info = found->second;
    }
    MBO_RETURN_IF_ERROR(ApplyStringFields(info, value, layer_, type));
    MBO_RETURN_IF_ERROR(ApplyOptionalFields(info, value, layer_, type));
    if (value.contains("extensions")) {
      MBO_ASSIGN_OR_RETURN(const auto extensions, Extensions(value, type));
      info.extensions.clear();
      replaced_types_.insert(type);
      MBO_RETURN_IF_ERROR(AddClaims(extensions, type));
    }
    types_[type] = std::move(info);
    return absl::OkStatus();
  }

  void Commit() {
    for (const std::string& type : replaced_types_) {
      std::erase_if(vocabulary_.extensions, [&](const auto& claim) { return claim.second == type; });
    }
    for (auto& [type, info] : types_) {
      vocabulary_.types[type] = std::move(info);
    }
    for (const auto& [extension, type] : claims_) {
      const auto claim = vocabulary_.extensions.insert_or_assign(extension, type).first;
      vocabulary_.types[type].extensions.push_back(claim->first);
    }
    vocabulary_.layers.push_back(std::move(root_));
  }

  Vocabulary& vocabulary_;
  std::string_view layer_;
  ConflictPolicy conflicts_;
  std::map<std::string, std::string, std::less<>> claims_;
  std::map<std::string, Vocabulary::Record, std::less<>> types_;
  std::set<std::string, std::less<>> replaced_types_;
  std::unique_ptr<const Json> root_;
  std::vector<std::unique_ptr<const std::string>> normalized_;
};

absl::StatusOr<std::string> ReadFile(const std::string& path) {
  const vfs::LocalFs fs;
  const absl::StatusOr<std::string> content = fs.ReadContent(path);
  if (!content.ok()) {
    return absl::NotFoundError(absl::StrCat("cannot read MIME vocabulary: ", path));
  }
  return *content;
}

TypeInfo View(std::string_view type, const Vocabulary::Record& record) {
  return {
      .type = type,
      .description = record.description,
      .source = record.source,
      .charset = record.charset,
      .compressible = record.compressible,
      .aliases = record.aliases,
      .extensions = record.extensions,
  };
}

TypeInfo Lookup(const Vocabulary& vocabulary, std::string_view name) {
  const std::string extension = stdfs::path(std::string(name)).extension().string();
  if (extension.size() > 1) {
    const auto found = vocabulary.extensions.find(Lower(std::string_view(extension).substr(1)));
    if (found != vocabulary.extensions.end()) {
      return View(found->second, vocabulary.types.at(found->second));
    }
  }
  return View(kFallback, vocabulary.types.at(std::string(kFallback)));
}

void Finalize(Vocabulary& vocabulary) {
  vocabulary.views.reserve(vocabulary.types.size());
  for (const auto& [type, record] : vocabulary.types) {
    vocabulary.views.push_back(View(type, record));
  }
}

void EnsureConfigured(State& state) ABSL_EXCLUSIVE_LOCKS_REQUIRED(state.mutex) {
  if (state.active.has_value()) {
    return;
  }
  const auto databases = Databases();
  if (state.default_vocabulary && state.default_database_count == databases.size()) {
    state.active.set_ref(*state.default_vocabulary);
    return;
  }
  auto vocabulary = std::make_unique<Vocabulary>(CoreVocabulary());
  for (const Database& database : databases) {
    CHECK_OK(LayerProcessor(*vocabulary, database.name, ConflictPolicy::kLast).Apply(database.json));
  }
  Finalize(*vocabulary);
  state.snapshots.push_back(std::move(vocabulary));
  state.default_vocabulary.set_ref(*state.snapshots.back());
  state.default_database_count = databases.size();
  state.active.set_ref(*state.default_vocabulary);
}

}  // namespace

std::string_view TypeInfo::Category() const {
  return type.substr(0, type.find('/'));
}

absl::Status Configure(absl::Span<const std::string> files, ConflictPolicy conflicts) {
  if (files.empty()) {
    State& state = GlobalState();
    const absl::MutexLock lock(state.mutex);
    // Registered databases are trusted, generated build inputs. Defer their
    // comparatively expensive JSON parse until a MIME predicate or field is
    // actually evaluated; user-provided layers below remain eagerly validated.
    state.active.reset();
    return absl::OkStatus();
  }
  Vocabulary vocabulary = CoreVocabulary();
  for (const Database& database : Databases()) {
    MBO_RETURN_IF_ERROR(LayerProcessor(vocabulary, database.name, ConflictPolicy::kLast).Apply(database.json));
  }
  for (const std::string& file : files) {
    MBO_ASSIGN_OR_RETURN(const std::string text, ReadFile(file));
    MBO_RETURN_IF_ERROR(LayerProcessor(vocabulary, file, conflicts).Apply(text));
  }
  Finalize(vocabulary);
  State& state = GlobalState();
  const absl::MutexLock lock(state.mutex);
  auto snapshot = std::make_unique<const Vocabulary>(std::move(vocabulary));
  state.snapshots.push_back(std::move(snapshot));
  state.active.set_ref(*state.snapshots.back());
  return absl::OkStatus();
}

TypeInfo InfoForName(std::string_view name) {
  State& state = GlobalState();
  const absl::MutexLock lock(state.mutex);
  EnsureConfigured(state);
  return Lookup(*state.active, name);
}

std::string_view TypeForName(std::string_view name) {
  return InfoForName(name).type;
}

absl::Span<const TypeInfo> Types() {
  State& state = GlobalState();
  const absl::MutexLock lock(state.mutex);
  EnsureConfigured(state);
  return state.active->views;
}

}  // namespace xff::mime
