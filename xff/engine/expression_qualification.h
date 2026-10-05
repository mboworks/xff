// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_EXPRESSION_QUALIFICATION_H_
#define XFF_ENGINE_EXPRESSION_QUALIFICATION_H_

#include "absl/status/statusor.h"
#include "xff/engine/expression_execution.h"
#include "xff/parser/ast.h"

namespace xff::engine {

// Test/benchmark vocabulary only. No production target may depend on this header.
enum class ExpressionExecutor {
  kTree,
  kProduction,
  kBound,
  kPrepared,
  kPreparedEager,
  kProgramSwitch,
  kProgramFunctions,
  kProgramOptimized,
};

// kTree returns a null factory so RunFind uses its original evaluation path without an adapter.
// Invalid enum values return a failing factory, preserving the run's preparation error boundary.
ExpressionFactory QualifiedExpressionFactory(ExpressionExecutor executor);

// Direct preparation also supports a tree adapter for worker/move contract tests.
absl::StatusOr<ExpressionExecution> PrepareQualifiedExpression(
    const parser::Expr& expression,
    ExpressionExecutor executor);

}  // namespace xff::engine

#endif  // XFF_ENGINE_EXPRESSION_QUALIFICATION_H_
