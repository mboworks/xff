// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_qualification.h"

#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "mbo/status/status_macros.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/expression_program.h"

namespace xff::engine {
namespace {

struct QualificationPlan final : ExpressionExecution::Plan {
  QualificationPlan(const parser::Expr& expression, ExpressionExecutor selected)
      : source(expression), executor(selected) {}

  ExpressionExecution::Worker MakeWorker(ExpressionWorkerRole) const override;

  bool UsesIndexedMatchers() const override { return prepared.has_value() || program.has_value(); }

  const parser::Expr& source;
  ExpressionExecutor executor;
  std::optional<BoundExpression> bound;
  std::optional<PreparedExpression> prepared;
  std::optional<ExpressionProgram> program;
};

struct QualificationState final : ExpressionExecution::Worker::State {
  explicit QualificationState(const QualificationPlan& plan) : data(plan) {
    if (data.prepared) {
      prepared.emplace(data.prepared->MakeWorker(
          data.executor == ExpressionExecutor::kPreparedEager ? PreparedExpression::MatcherInitialization::kEager
                                                              : PreparedExpression::MatcherInitialization::kOnDemand));
    }
    if (data.program) {
      program.emplace(data.program->MakeWorker(
          data.executor == ExpressionExecutor::kProgramSwitch ? ProgramDispatch::kSwitch
                                                              : ProgramDispatch::kFunctions));
    }
  }

  EvaluationResult Evaluate(EvalContext& context) const override {
    if (data.bound.has_value()) {
      return data.bound->Evaluate(context);
    }
    if (prepared.has_value()) {
      return prepared->Evaluate(context);
    }
    if (program.has_value()) {
      return program->Evaluate(context).result;
    }
    return EvaluateDeferred(data.source, context);
  }

  const QualificationPlan& data;
  std::optional<PreparedExpression::Worker> prepared;
  std::optional<ExpressionProgram::Worker> program;
};

ExpressionExecution::Worker QualificationPlan::MakeWorker(ExpressionWorkerRole) const {
  return ExpressionExecution::Worker(std::make_unique<QualificationState>(*this));
}

template<ExpressionExecutor Executor>
absl::StatusOr<ExpressionExecution> Prepare(const parser::Expr& expression) {
  return PrepareQualifiedExpression(expression, Executor);
}

absl::StatusOr<ExpressionExecution> Reject(const parser::Expr&) {
  return absl::InvalidArgumentError("unknown expression executor");
}

}  // namespace

ExpressionFactory QualifiedExpressionFactory(ExpressionExecutor executor) {
  switch (executor) {
    case ExpressionExecutor::kTree: return nullptr;
    case ExpressionExecutor::kProduction: return PrepareExpressionExecution;
    case ExpressionExecutor::kBound: return Prepare<ExpressionExecutor::kBound>;
    case ExpressionExecutor::kPrepared: return Prepare<ExpressionExecutor::kPrepared>;
    case ExpressionExecutor::kPreparedEager: return Prepare<ExpressionExecutor::kPreparedEager>;
    case ExpressionExecutor::kProgramSwitch: return Prepare<ExpressionExecutor::kProgramSwitch>;
    case ExpressionExecutor::kProgramFunctions: return Prepare<ExpressionExecutor::kProgramFunctions>;
    case ExpressionExecutor::kProgramOptimized: return Prepare<ExpressionExecutor::kProgramOptimized>;
  }
  return Reject;
}

absl::StatusOr<ExpressionExecution> PrepareQualifiedExpression(
    const parser::Expr& expression,
    ExpressionExecutor executor) {
  if (executor == ExpressionExecutor::kProduction) {
    return PrepareExpressionExecution(expression);
  }
  auto data = std::make_unique<QualificationPlan>(expression, executor);
  switch (executor) {
    case ExpressionExecutor::kTree: break;
    case ExpressionExecutor::kBound: {
      MBO_ASSIGN_OR_RETURN(auto bound, BoundExpression::Prepare(expression));
      data->bound.emplace(std::move(bound));
      break;
    }
    case ExpressionExecutor::kPrepared:
    case ExpressionExecutor::kPreparedEager: {
      MBO_ASSIGN_OR_RETURN(auto prepared, PreparedExpression::Prepare(expression));
      data->prepared.emplace(std::move(prepared));
      break;
    }
    case ExpressionExecutor::kProgramSwitch:
    case ExpressionExecutor::kProgramFunctions:
    case ExpressionExecutor::kProgramOptimized: {
      const bool optimize = executor == ExpressionExecutor::kProgramOptimized;
      MBO_ASSIGN_OR_RETURN(
          auto program,
          ExpressionProgram::Prepare(expression, {.constants = optimize, .jumps = optimize, .fusion = optimize}));
      data->program.emplace(std::move(program));
      break;
    }
    default: return absl::InvalidArgumentError("unknown expression executor");
  }
  return ExpressionExecution(std::move(data));
}

}  // namespace xff::engine
