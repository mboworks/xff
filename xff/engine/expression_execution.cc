// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_execution.h"

#include <utility>

namespace xff::engine {

ExpressionExecution::ExpressionExecution(std::unique_ptr<Plan> plan) : plan_(std::move(plan)) {}

ExpressionExecution::ExpressionExecution(ExpressionExecution&&) noexcept = default;
ExpressionExecution& ExpressionExecution::operator=(ExpressionExecution&&) noexcept = default;
ExpressionExecution::~ExpressionExecution() = default;

ExpressionExecution::Worker::Worker(std::unique_ptr<State> state) : state_(std::move(state)) {}

ExpressionExecution::Worker::Worker(Worker&&) noexcept = default;
ExpressionExecution::Worker& ExpressionExecution::Worker::operator=(Worker&&) noexcept = default;
ExpressionExecution::Worker::~Worker() = default;

ExpressionExecution::Worker ExpressionExecution::MakeWorker() const {
  return plan_->MakeWorker();
}

bool ExpressionExecution::UsesIndexedMatchers() const {
  return plan_->UsesIndexedMatchers();
}

EvaluationResult ExpressionExecution::Worker::Evaluate(EvalContext& context) const {
  return state_->Evaluate(context);
}

}  // namespace xff::engine
