// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_EXPRESSION_EXECUTION_H_
#define XFF_ENGINE_EXPRESSION_EXECUTION_H_

#include <cstddef>
#include <memory>

#include "absl/status/statusor.h"
#include "xff/engine/evaluate.h"
#include "xff/parser/ast.h"

namespace xff::engine {

// Coordinators reuse original const regex matchers. Pool workers own lazy private matcher state.
// Neither kind of worker may itself be entered concurrently.
enum class ExpressionWorkerRole { kCoordinator, kConcurrent };

// Counts from the prepared expression itself, not estimates from flag names. Owned bytes
// exclude the source AST, regex backends, adapters, allocator headers and worker storage.
struct ExpressionPreparation {
  std::size_t nodes = 0;
  std::size_t operands = 0;
  std::size_t matcher_slots = 0;
  std::size_t owned_bytes = 0;
};

// Immutable preparation shared across the coordinator and matcher workers. The original AST
// must outlive this object; workers must die before it. Moving owners preserves their storage.
// Production uses prepared recursion. Alternative qualification factories live in a test-only
// target, so selecting a benchmark alternative cannot link every candidate into the shipping CLI.
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
    [[nodiscard]] virtual Worker MakeWorker(ExpressionWorkerRole role) const = 0;
    [[nodiscard]] virtual bool UsesIndexedMatchers() const = 0;
  };

  explicit ExpressionExecution(std::unique_ptr<Plan> plan, ExpressionPreparation preparation = {});
  ExpressionExecution(ExpressionExecution&&) noexcept;
  ExpressionExecution& operator=(ExpressionExecution&&) noexcept;
  ExpressionExecution(const ExpressionExecution&) = delete;
  ExpressionExecution& operator=(const ExpressionExecution&) = delete;
  ~ExpressionExecution();

  [[nodiscard]] Worker MakeWorker(ExpressionWorkerRole role = ExpressionWorkerRole::kConcurrent) const;
  [[nodiscard]] bool UsesIndexedMatchers() const;

  [[nodiscard]] const ExpressionPreparation& Preparation() const { return preparation_; }

 private:
  std::unique_ptr<Plan> plan_;
  ExpressionPreparation preparation_;
};

// Called once per distinct run expression, after command validation. A null factory selects
// the original tree directly. This is a function pointer, not a borrowed object pointer.
using ExpressionFactory = absl::StatusOr<ExpressionExecution> (*)(const parser::Expr&);

// Production preparation. Reuses the original boolean/effect semantics with bound callbacks
// and typed operands. Coordinator matching borrows compiled state; pool workers fork on demand.
absl::StatusOr<ExpressionExecution> PrepareExpressionExecution(const parser::Expr& expression);

}  // namespace xff::engine

#endif  // XFF_ENGINE_EXPRESSION_EXECUTION_H_
