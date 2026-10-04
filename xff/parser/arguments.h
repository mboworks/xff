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

#ifndef XFF_PARSER_ARGUMENTS_H_
#define XFF_PARSER_ARGUMENTS_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "mbo/types/optional_ref.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/mode.h"

namespace xff::parser {
// The first pass extracts globals under the current mode and leaves primary
// operands intact. Parse builds the expression from this sequence exactly once.
// Views borrow argv storage until the expression pass copies each consumed value.
struct ExpressionToken {
  std::string_view text;
  mbo::types::OptionalRef<const registry::Descriptor> descriptor;
};

struct ParsedArguments {
  Command command;
  std::vector<ExpressionToken> expression;
  bool options_ended = false;
};

absl::StatusOr<ParsedArguments> ParseArguments(
    const std::vector<std::string>& args,
    std::size_t start,
    registry::Mode mode);

}  // namespace xff::parser
#endif  // XFF_PARSER_ARGUMENTS_H_
