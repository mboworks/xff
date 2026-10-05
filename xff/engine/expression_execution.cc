// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_execution.h"

#include <utility>

#include "mbo/status/status_macros.h"

namespace xff::engine {
namespace {

struct CoordinatorState final : ExpressionExecution::Worker::State {
  explicit CoordinatorState(const PreparedExpression& prepared) : expression(prepared) {}

  EvaluationResult Evaluate(EvalContext& context) const override { return expression.Evaluate(context); }

  const PreparedExpression& expression;
};

struct ConcurrentState final : ExpressionExecution::Worker::State {
  explicit ConcurrentState(const PreparedExpression& prepared) : worker(prepared.MakeWorker()) {}

  EvaluationResult Evaluate(EvalContext& context) const override { return worker.Evaluate(context); }

  PreparedExpression::Worker worker;
};

struct PreparedPlan final : ExpressionExecution::Plan {
  explicit PreparedPlan(PreparedExpression prepared) : expression(std::move(prepared)) {}

  ExpressionExecution::Worker MakeWorker(ExpressionWorkerRole role) const override {
    if (role == ExpressionWorkerRole::kCoordinator) {
      return ExpressionExecution::Worker(std::make_unique<CoordinatorState>(expression));
    }
    return ExpressionExecution::Worker(std::make_unique<ConcurrentState>(expression));
  }

  bool UsesIndexedMatchers() const override { return true; }

  PreparedExpression expression;
};

}  // namespace

ExpressionExecution::ExpressionExecution(std::unique_ptr<Plan> plan, ExpressionPreparation preparation)
    : plan_(std::move(plan)), preparation_(preparation) {}

ExpressionExecution::ExpressionExecution(ExpressionExecution&&) noexcept = default;
ExpressionExecution& ExpressionExecution::operator=(ExpressionExecution&&) noexcept = default;
ExpressionExecution::~ExpressionExecution() = default;

ExpressionExecution::Worker::Worker(std::unique_ptr<State> state) : state_(std::move(state)) {}

ExpressionExecution::Worker::Worker(Worker&&) noexcept = default;
ExpressionExecution::Worker& ExpressionExecution::Worker::operator=(Worker&&) noexcept = default;
ExpressionExecution::Worker::~Worker() = default;

ExpressionExecution::Worker ExpressionExecution::MakeWorker(ExpressionWorkerRole role) const {
  return plan_->MakeWorker(role);
}

bool ExpressionExecution::UsesIndexedMatchers() const {
  return plan_->UsesIndexedMatchers();
}

EvaluationResult ExpressionExecution::Worker::Evaluate(EvalContext& context) const {
  return state_->Evaluate(context);
}

absl::StatusOr<ExpressionExecution> PrepareExpressionExecution(const parser::Expr& expression) {
  MBO_ASSIGN_OR_RETURN(auto prepared, PreparedExpression::Prepare(expression));
  const ExpressionPreparation details{
      .nodes = prepared.NodeCount(),
      .operands = prepared.OperandCount(),
      .matcher_slots = prepared.MatcherCount(),
      .owned_bytes = prepared.StorageBytes(),
  };
  return ExpressionExecution(std::make_unique<PreparedPlan>(std::move(prepared)), details);
}

}  // namespace xff::engine
