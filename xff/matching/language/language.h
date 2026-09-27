// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef XFF_MATCHING_LANGUAGE_LANGUAGE_H_
#define XFF_MATCHING_LANGUAGE_LANGUAGE_H_

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/types/span.h"

namespace xff::language {

using StringViewSpan = absl::Span<const std::string_view>;

struct LanguageInfo {
  std::string_view name;
  std::string_view type;
  std::string_view color;
  std::string_view group;
  std::string_view source;
  StringViewSpan aliases;
  StringViewSpan extensions;
  StringViewSpan filenames;
  StringViewSpan shared_extensions;
  StringViewSpan shared_filenames;
};

enum class ConflictPolicy { kError, kFirst, kLast };

struct LanguageVocabulary;
struct CatalogEdit;
class Catalog;

// A cheap handle to one immutable, process-retained vocabulary snapshot. Acquire it after Configure
// and reuse it for a lookup-heavy operation: unlike the process-global convenience functions below,
// its methods need no registry lock. The handle and every view it returns remain valid after a later
// Configure publishes another snapshot.
class LanguageSnapshot {
 public:
  [[nodiscard]] std::optional<LanguageInfo> InfoForName(std::string_view name) const;
  // All filename-based candidates, in canonical-name order, without reading content.
  [[nodiscard]] absl::Span<const LanguageInfo> CandidatesForName(std::string_view name) const;
  [[nodiscard]] std::string_view LanguageForName(std::string_view name) const;
  [[nodiscard]] std::string_view TerminalColorForName(std::string_view name) const;
  [[nodiscard]] absl::Span<const LanguageInfo> Languages() const;

 private:
  friend LanguageSnapshot ActiveSnapshot();
  friend Catalog ActiveCatalog();
  friend absl::Status Configure(absl::Span<const std::string>, ConflictPolicy, absl::Span<const CatalogEdit>);

  explicit LanguageSnapshot(const LanguageVocabulary& vocabulary) : vocabulary_(vocabulary) {}

  std::reference_wrapper<const LanguageVocabulary> vocabulary_;
};

// Builds and publishes an immutable process-vocabulary snapshot from the curated core, every
// linked data layer, then the JSON files in command-line order. A later layer overrides an earlier
// one. Conflict policy applies to two different languages claiming the same extension or filename
// inside one input file. Explicit shared_extensions/shared_filenames add overlapping candidates
// without changing that preferred classification. A new preferred claim clears older shared
// claims for the same key; shared claims in that layer then add candidates back. Published snapshots remain alive for
// the process lifetime, so returned views remain valid. Production configures at most once; retaining older snapshots
// primarily permits isolated repeated invocations in tests and embedders.
absl::Status Configure(
    absl::Span<const std::string> files,
    ConflictPolicy conflicts,
    absl::Span<const CatalogEdit> edits = {});

// Acquires the active vocabulary under the registry lock, initializing the default snapshot when
// needed. Subsequent lookups through the returned handle are lock-free.
LanguageSnapshot ActiveSnapshot();

// All candidates for filtering. Exact filenames win over suffixes, and the longest suffix wins.
// Shared claims may overlap; the preferred language is included when one exists. Unknown names
// return an empty span. Views are retained for the process lifetime.
absl::Span<const LanguageInfo> CandidatesForName(std::string_view name);

// The preferred metadata for the file named `name`. Its strings and spans view an immutable,
// process-retained vocabulary snapshot and therefore remain valid for the process lifetime.
std::optional<LanguageInfo> InfoForName(std::string_view name);

// The programming/markup language for the file named `name` (a basename such as "main.cc",
// "Makefile", or ".bashrc"), as a canonical github-linguist name ("C++", "Python", "Shell",
// ...). An exact filename match wins (Makefile, Dockerfile, CMakeLists.txt, BUILD, ...); else
// the extension is looked up case-insensitively (`.PY` == `.py`). Returns "" when the name has
// no recognized filename or extension.
//
// Extension/filename-based, not content-classified: the lean binary has a curated common vocabulary;
// linked data layers and JSON overlays may expand it without adding YAML parsing to the runtime.
// The heuristics linguist layers on top (shebang / modeline /
// content disambiguation of `.h`, `.m`, `.pl`, ...) are out of scope; this is a fast,
// dependency-free preferred label for the `{lang}` field. The `-lang GLOB` predicate instead
// checks every candidate from CandidatesForName, including shared suffixes. -lang
// matching is case-insensitive (a canonical name has fixed case that display keeps) and
// independent of --case / -i / -s.
//
// The returned view remains valid for the process lifetime. The immutable default snapshot is
// initialized lazily on the first language lookup, so binaries that do not use language features
// do not load linked language databases.
std::string_view LanguageForName(std::string_view name);

// The language database's `#RRGGBB` colour for `name`, converted once per immutable vocabulary
// snapshot to an ANSI true-colour SGR parameter (`38;2;R;G;B`). Returns empty when the language is
// unknown or its colour is absent/malformed. The returned view has process-lifetime backing.
std::string_view TerminalColorForName(std::string_view name);

// All canonical languages in deterministic canonical-name order. The returned span and records
// have process-lifetime backing; filename matching should use InfoForName.
absl::Span<const LanguageInfo> Languages();

}  // namespace xff::language

#endif  // XFF_MATCHING_LANGUAGE_LANGUAGE_H_
