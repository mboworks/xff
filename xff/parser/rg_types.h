// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_PARSER_RG_TYPES_H_
#define XFF_PARSER_RG_TYPES_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "xff/matching/language/language.h"
#include "xff/matching/regex/regex.h"
#include "xff/parser/ast.h"

namespace xff::parser {
class RgTypes {
 public:
  static absl::StatusOr<RgTypes> Compile(const RgSearch& search);
  bool Includes(std::string_view basename) const;

  std::string_view Listing() const { return listing_; }

 private:
  struct Rule {
    bool include;
    absl::flat_hash_set<std::string> languages;
    std::vector<regex::Matcher> matchers;
  };

  std::optional<language::LanguageSnapshot> vocabulary_;
  std::vector<Rule> rules_;
  bool default_include_ = true;
  std::string listing_;
};
}  // namespace xff::parser
#endif  // XFF_PARSER_RG_TYPES_H_
