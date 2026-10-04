// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_EXPRESSION_EXECUTION_H_
#define XFF_ENGINE_EXPRESSION_EXECUTION_H_

#include <memory>

#include "absl/status/statusor.h"
#include "xff/engine/evaluate.h"
#include "xff/parser/ast.h"

namespace xff::engine {

// Internal qualification selector, deliberately absent from CLI/configuration vocabulary.
// The default remains the production tree until whole-run measurements justify changing it.
enum class ExpressionExecutor { kTree, kBound, kPrepared, kProgramSwitch, kProgramFunctions };

// Immutable preparation shared across the coordinator and matcher workers. The original AST
// must outlive this object; workers must die before it. Moving owners preserves their storage.
class ExpressionExecution final {
 private:
  struct Data;

 public:
  class Worker final {
   public:
    Worker(Worker&&) noexcept;
    Worker& operator=(Worker&&) noexcept;
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker();

    // A worker is reused serially; different workers may run concurrently.
    EvaluationResult Evaluate(EvalContext& context) const;

   private:
    friend class ExpressionExecution;
    struct State;
    explicit Worker(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
  };

  static absl::StatusOr<ExpressionExecution> Prepare(const parser::Expr& expression, ExpressionExecutor executor);
  ExpressionExecution(ExpressionExecution&&) noexcept;
  ExpressionExecution& operator=(ExpressionExecution&&) noexcept;
  ExpressionExecution(const ExpressionExecution&) = delete;
  ExpressionExecution& operator=(const ExpressionExecution&) = delete;
  ~ExpressionExecution();

  [[nodiscard]] Worker MakeWorker() const;
  [[nodiscard]] bool UsesIndexedMatchers() const;

 private:
  explicit ExpressionExecution(std::unique_ptr<Data> data);
  std::unique_ptr<Data> data_;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_EXPRESSION_EXECUTION_H_
