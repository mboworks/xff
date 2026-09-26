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

#ifndef XFF_PARSER_RG_H_
#define XFF_PARSER_RG_H_

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/parser/ast.h"

namespace xff::parser {
// Shared grammar and help metadata. A nonempty argument names the required value.
struct RgOption {
  enum class Effect { kGlobal, kPattern, kFile, kWord, kLine, kText, kColumns, kGlob, kRoot, kThreads };
  std::string_view name;
  char short_name = '\0';
  std::string_view replacement;
  std::string_view argument;
  Effect effect = Effect::kGlobal;
  std::string_view summary;
};

std::span<const RgOption> RgOptions();

// Parse the segment after --rg. --xff starts a native filter expression.
absl::StatusOr<Command> ParseRg(const std::vector<std::string>& args, std::size_t start);
}  // namespace xff::parser
#endif  // XFF_PARSER_RG_H_
