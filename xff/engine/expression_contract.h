// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_EXPRESSION_CONTRACT_H_
#define XFF_ENGINE_EXPRESSION_CONTRACT_H_

#include <cstddef>
#include <functional>
#include <vector>

#include "absl/status/statusor.h"
#include "mbo/types/strong_id.h"
#include "xff/parser/ast.h"

namespace xff::engine {

struct ExpressionSourceTag {};

using ExpressionSourceId = mbo::types::ConstStrongId<ExpressionSourceTag, std::size_t>;

// Known requirements, not permission to reorder or omit evaluation. Unclassified reads,
// failures, state and whole-command effects remain barriers until individually audited.
struct ExpressionRequirements {
  bool metadata = false;
  bool output = false;
  bool action = false;
  bool capture = false;
  bool execution = false;
  bool mutation = false;
  bool control = false;
  bool fuzzy = false;
  bool optimization_barrier = true;
};

struct ExpressionSource {
  ExpressionSourceId id;
  std::reference_wrapper<const parser::Expr> expression;
  ExpressionRequirements requirements;
};

// Validate shape and assign dense preorder IDs before rewriting. The original tree must outlive
// the result. Moving its owning unique_ptr preserves identity; replacing its nodes does not.
// This preparation-only table is not constructed or searched on the per-entry production path.
absl::StatusOr<std::vector<ExpressionSource>> DescribeExpression(const parser::Expr& expression);

}  // namespace xff::engine

#endif  // XFF_ENGINE_EXPRESSION_CONTRACT_H_
