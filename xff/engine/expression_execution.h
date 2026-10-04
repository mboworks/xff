// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_EXPRESSION_EXECUTION_H_
#define XFF_ENGINE_EXPRESSION_EXECUTION_H_

#include <memory>

#include "absl/status/statusor.h"
#include "xff/engine/evaluate.h"
#include "xff/parser/ast.h"

namespace xff::engine {

// Immutable preparation shared across the coordinator and matcher workers. The original AST
// must outlive this object; workers must die before it. Moving owners preserves their storage.
// The engine owns this interface only. Qualification factories live in a test-only target, so
// selecting a benchmark alternative cannot pull every candidate into the shipping executable.
class ExpressionExecution final {
 public:
  class Worker final {
   public:
    struct State {
      virtual ~State() = default;
      virtual EvaluationResult Evaluate(EvalContext& context) const = 0;
    };

    explicit Worker(std::unique_ptr<State> state);
    Worker(Worker&&) noexcept;
    Worker& operator=(Worker&&) noexcept;
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker();

    // A worker is reused serially; different workers may run concurrently.
    EvaluationResult Evaluate(EvalContext& context) const;

   private:
    std::unique_ptr<State> state_;
  };

  struct Plan {
    virtual ~Plan() = default;
    [[nodiscard]] virtual Worker MakeWorker() const = 0;
    [[nodiscard]] virtual bool UsesIndexedMatchers() const = 0;
  };

  explicit ExpressionExecution(std::unique_ptr<Plan> plan);
  ExpressionExecution(ExpressionExecution&&) noexcept;
  ExpressionExecution& operator=(ExpressionExecution&&) noexcept;
  ExpressionExecution(const ExpressionExecution&) = delete;
  ExpressionExecution& operator=(const ExpressionExecution&) = delete;
  ~ExpressionExecution();

  [[nodiscard]] Worker MakeWorker() const;
  [[nodiscard]] bool UsesIndexedMatchers() const;

 private:
  std::unique_ptr<Plan> plan_;
};

// Called once per distinct run expression, after command validation. A null factory selects
// the original tree directly. This is a function pointer, not a borrowed object pointer.
using ExpressionFactory = absl::StatusOr<ExpressionExecution> (*)(const parser::Expr&);

}  // namespace xff::engine

#endif  // XFF_ENGINE_EXPRESSION_EXECUTION_H_
