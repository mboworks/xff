// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_MATCHING_LANGUAGE_TYPE_FILTER_H_
#define XFF_MATCHING_LANGUAGE_TYPE_FILTER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/matching/language/catalog.h"

namespace xff::language {
struct TypeRule {
  bool include;
  std::string value;
};

class TypeFilter {
 public:
  static absl::StatusOr<TypeFilter> Compile(absl::Span<const TypeRule> rules);
  bool Includes(std::string_view basename) const;

 private:
  struct Rule {
    bool include;
    std::string name;
  };

  std::optional<Catalog> catalog_;
  std::vector<Rule> rules_;
  bool default_include_ = true;
};
}  // namespace xff::language
#endif  // XFF_MATCHING_LANGUAGE_TYPE_FILTER_H_
