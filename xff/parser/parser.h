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

#ifndef XFF_PARSER_PARSER_H_
#define XFF_PARSER_PARSER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"

namespace xff::parser {

// Borrows the mutable pointee without transferring ownership.
inline mbo::types::OptionalRef<Expr> AsOptionalExpr(const std::unique_ptr<Expr>& expr) {
  return expr ? mbo::types::OptionalRef<Expr>{*expr} : std::nullopt;
}

// Borrows a mutable pointee through a read-only view.
inline mbo::types::OptionalRef<const Expr> AsConstOptionalExpr(const std::unique_ptr<Expr>& expr) {
  return expr ? mbo::types::OptionalRef<const Expr>{*expr} : std::nullopt;
}

// Borrows an already read-only pointee.
inline mbo::types::OptionalRef<const Expr> AsConstOptionalExpr(const std::unique_ptr<const Expr>& expr) {
  return expr ? mbo::types::OptionalRef<const Expr>{*expr} : std::nullopt;
}

// Parses argv into [globals] [roots] [expression] per the xff grammar:
// leading '-'/'+' global flags up to the first directory or an explicit '--',
// one or more roots, then the position-dependent find expression
// (precedence ! > -a > -o, implicit -a between adjacent predicates, ( )
// grouping). Returns an error for an unknown predicate, a predicate missing
// arguments, an unexpected operator, or unbalanced parentheses.
absl::StatusOr<Command> Parse(const std::vector<std::string>& args);

// Enforces the active find/xff style on a parsed command. Under the strict find
// style (registry::Style::kFind, selected by --config=find), any expression
// primary tagged registry::Style::kXff (an xff extension such as -println or
// -capture) is outside find's vocabulary, so this returns InvalidArgument naming
// the first such primary; the xff style (the default) accepts the full vocabulary
// and always returns Ok. design-config.md "CLI selectors".
absl::Status EnforceStyle(const Command& command, registry::Style style);

// How the name/content matchers treat letter case, independent of the per-primary
// -i variants (which always fold). kSmart folds only when the pattern has no uppercase.
enum class CaseMode { kSensitive, kInsensitive, kSmart };

// Resolves the case mode from the globals and active style. Default is style-scoped:
// find/xff -> kSensitive (find-compatible), the opinionated style (rg) -> kSmart.
// Overrides (last wins): `--case=sensitive|insensitive|smart`; the shorts `-i`
// (insensitive), `-s`/`-s+` (smart), `-s-` (sensitive).
CaseMode ResolveCaseMode(const std::vector<std::string>& globals, registry::Style style);

// Resolves the final matcher grammar from the fully expanded global stream.
regex::Grammar GrammarFromGlobals(const std::vector<std::string>& globals);

// Applies final case semantics and compiles every expression matcher exactly
// once. Call only after configuration and CLI globals have fully resolved.
void BindMatchers(Command& command, regex::Grammar grammar, CaseMode mode);

// Whether `command` contains a primary that may take the TERMINAL for itself: -ok / -okdir prompt and
// read a reply, and -exec / -execdir hand our stdin / stdout to a child that might (an editor).
// Read from the registry (Descriptor::terminal), never from a name list here, so a new primary of
// that shape is covered by declaring it. A command without an expression is false.
//
// The caller is the CLI's listing pager, which must not sit between such a primary and the user.
[[nodiscard]] bool TakesTerminal(const Command& command);

}  // namespace xff::parser

#endif  // XFF_PARSER_PARSER_H_
