// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_contract.h"

#include <cstddef>
#include <functional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"

namespace xff::engine {
namespace {

bool ValidShape(const parser::Expr& node) {
  switch (node.kind) {
    case parser::Expr::Kind::kPredicate: return node.descriptor.has_value() && !node.lhs && !node.rhs;
    case parser::Expr::Kind::kNot: return node.lhs != nullptr && !node.rhs;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma: return node.lhs != nullptr && node.rhs != nullptr;
  }
  return false;
}

ExpressionRequirements Requirements(const parser::Expr& node) {
  if (node.kind != parser::Expr::Kind::kPredicate) {
    return {};
  }
  const auto& descriptor = *node.descriptor;
  return {
      .metadata = descriptor.needs_metadata,
      .output = descriptor.stdout_output || descriptor.writes_file,
      .action = descriptor.kind == registry::Kind::kAction,
      .capture = descriptor.binds_capture || descriptor.binding == registry::Binding::kLabelRegex,
      .execution = descriptor.safety == registry::Safety::kSecurity,
      .mutation = descriptor.safety == registry::Safety::kSafety || descriptor.writes_file,
      .control = descriptor.control != registry::Control::kNone
                 || descriptor.traversal_effect != registry::TraversalEffect::kNone || !descriptor.pure,
      .fuzzy = descriptor.binding == registry::Binding::kFuzzy,
  };
}

}  // namespace

absl::StatusOr<std::vector<ExpressionSource>> DescribeExpression(const parser::Expr& expression) {
  // Collect stable references once; no recursive call stack or per-node owned copy.
  std::vector<std::reference_wrapper<const parser::Expr>> pending{std::cref(expression)};
  std::vector<std::reference_wrapper<const parser::Expr>> ordered;
  while (!pending.empty()) {
    const auto& node = pending.back().get();
    pending.pop_back();
    if (!ValidShape(node)) {
      return absl::InvalidArgumentError("invalid expression tree shape");
    }
    ordered.push_back(std::cref(node));
    if (node.rhs) {
      pending.push_back(std::cref(*node.rhs));
    }
    if (node.lhs) {
      pending.push_back(std::cref(*node.lhs));
    }
  }
  std::vector<ExpressionSource> result;
  result.reserve(ordered.size());
  for (const auto& reference : ordered) {
    // A vector of these nonzero-sized records cannot reach the size_t invalid sentinel.
    result.push_back({
        .id = ExpressionSourceId{result.size()},
        .expression = reference,
        .requirements = Requirements(reference.get()),
    });
  }
  return result;
}

}  // namespace xff::engine
