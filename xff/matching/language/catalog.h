// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_MATCHING_LANGUAGE_CATALOG_H_
#define XFF_MATCHING_LANGUAGE_CATALOG_H_

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "xff/matching/language/language.h"

namespace xff::language {

struct CatalogEdit {
  enum class Kind { kAdd, kClear };
  Kind kind;
  std::string value;
};

struct FilterType {
  std::string_view name;
  StringViewSpan aliases;
};

// Immutable filtering definitions over the language database. A cheap copy retains
// the definitions and their source snapshot; it never changes preferred labels.
class Catalog {
 public:
  static absl::StatusOr<Catalog> Compile(LanguageSnapshot vocabulary, absl::Span<const CatalogEdit> edits);

  // Resolve canonical names, aliases, and unambiguous suffix aliases. `all` is reserved.
  absl::StatusOr<std::string> Resolve(std::string_view name) const;
  // Returned names/aliases remain valid while a copy of this Catalog handle is retained.
  absl::InlinedVector<FilterType, 4> CandidatesForName(std::string_view basename) const;
  std::string Listing() const;

 private:
  struct Data;

  explicit Catalog(std::shared_ptr<const Data> data) : data_(std::move(data)) {}

  std::shared_ptr<const Data> data_;
};

// The catalog configured for this invocation; returned handles retain their state
// across later Configure calls, just like LanguageSnapshot.
Catalog ActiveCatalog();

}  // namespace xff::language
#endif  // XFF_MATCHING_LANGUAGE_CATALOG_H_
