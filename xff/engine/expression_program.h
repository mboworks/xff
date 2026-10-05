// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_EXPRESSION_PROGRAM_H_
#define XFF_ENGINE_EXPRESSION_PROGRAM_H_

#include <cstddef>
#include <memory>

#include "absl/status/statusor.h"
#include "xff/engine/evaluate.h"
#include "xff/parser/ast.h"

namespace xff::engine {

enum class ProgramDispatch { kSwitch, kFunctions };

struct ProgramEvaluation {
  EvaluationResult result;
  bool used_fallback = false;
};

// Experimental contiguous boolean program. The source AST must outlive the program and workers;
// a worker must also be destroyed before its program's storage. Moving owners preserves storage.
// Scoring and deferred replay currently use a reported whole-expression fallback. No predicate
// instruction performs name lookup. The production driver does not select this experiment.
class ExpressionProgram final {
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

    // Reuses worker-owned scratch; one worker cannot be evaluated concurrently.
    ProgramEvaluation Evaluate(EvalContext& context) const;
    [[nodiscard]] std::size_t StorageBytes() const;

   private:
    friend class ExpressionProgram;
    struct State;
    explicit Worker(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
  };

  static absl::StatusOr<ExpressionProgram> Prepare(const parser::Expr& expression);
  ExpressionProgram(ExpressionProgram&&) noexcept;
  ExpressionProgram& operator=(ExpressionProgram&&) noexcept;
  ExpressionProgram(const ExpressionProgram&) = delete;
  ExpressionProgram& operator=(const ExpressionProgram&) = delete;
  ~ExpressionProgram();

  [[nodiscard]] Worker MakeWorker(ProgramDispatch dispatch) const;
  [[nodiscard]] std::size_t InstructionCount() const;
  [[nodiscard]] std::size_t NodeCount() const;
  [[nodiscard]] std::size_t OperandCount() const;
  // Owned capacities, excluding the source AST, allocator headers and regex-backend allocations.
  [[nodiscard]] std::size_t StorageBytes() const;

 private:
  explicit ExpressionProgram(std::unique_ptr<Data> data);
  std::unique_ptr<Data> data_;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_EXPRESSION_PROGRAM_H_
