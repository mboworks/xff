// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_execution.h"

#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "mbo/status/status_macros.h"
#include "xff/engine/expression_program.h"

namespace xff::engine {

struct ExpressionExecution::Data {
  const parser::Expr& source;
  ExpressionExecutor executor;
  std::optional<BoundExpression> bound;
  std::optional<PreparedExpression> prepared;
  std::optional<ExpressionProgram> program;
};

struct ExpressionExecution::Worker::State {
  const Data& data;
  std::optional<PreparedExpression::Worker> prepared;
  std::optional<ExpressionProgram::Worker> program;
};

ExpressionExecution::ExpressionExecution(std::unique_ptr<Data> data) : data_(std::move(data)) {}

ExpressionExecution::ExpressionExecution(ExpressionExecution&&) noexcept = default;
ExpressionExecution& ExpressionExecution::operator=(ExpressionExecution&&) noexcept = default;
ExpressionExecution::~ExpressionExecution() = default;

ExpressionExecution::Worker::Worker(std::unique_ptr<State> state) : state_(std::move(state)) {}

ExpressionExecution::Worker::Worker(Worker&&) noexcept = default;
ExpressionExecution::Worker& ExpressionExecution::Worker::operator=(Worker&&) noexcept = default;
ExpressionExecution::Worker::~Worker() = default;

absl::StatusOr<ExpressionExecution> ExpressionExecution::Prepare(
    const parser::Expr& expression,
    ExpressionExecutor executor) {
  auto data = std::make_unique<Data>(Data{.source = expression, .executor = executor});
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

ExpressionExecution::Worker ExpressionExecution::MakeWorker() const {
  auto state = std::make_unique<Worker::State>(Worker::State{.data = *data_});
  if (data_->prepared) {
    state->prepared.emplace(data_->prepared->MakeWorker(
        data_->executor == ExpressionExecutor::kPreparedEager ? PreparedExpression::MatcherInitialization::kEager
                                                              : PreparedExpression::MatcherInitialization::kOnDemand));
  }
  if (data_->program) {
    state->program.emplace(data_->program->MakeWorker(
        data_->executor == ExpressionExecutor::kProgramSwitch ? ProgramDispatch::kSwitch
                                                              : ProgramDispatch::kFunctions));
  }
  return Worker(std::move(state));
}

bool ExpressionExecution::UsesIndexedMatchers() const {
  return data_->prepared.has_value() || data_->program.has_value();
}

EvaluationResult ExpressionExecution::Worker::Evaluate(EvalContext& context) const {
  if (state_->data.bound.has_value()) {
    return state_->data.bound->Evaluate(context);
  }
  if (state_->prepared.has_value()) {
    return state_->prepared->Evaluate(context);
  }
  if (state_->program.has_value()) {
    return state_->program->Evaluate(context).result;
  }
  return EvaluateDeferred(state_->data.source, context);
}

}  // namespace xff::engine
