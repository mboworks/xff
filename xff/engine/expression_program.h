// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_EXPRESSION_PROGRAM_H_
#define XFF_ENGINE_EXPRESSION_PROGRAM_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"
#include "xff/engine/evaluate.h"
#include "xff/parser/ast.h"

namespace xff::engine {

enum class ProgramDispatch { kSwitch, kFunctions };

struct ProgramOptimizations {
  bool constants = false;
  bool jumps = false;
  bool fusion = false;
  bool measure_time = false;
};

struct ProgramPassStats {
  bool enabled = false;
  std::size_t input_instructions = 0;
  std::size_t output_instructions = 0;
  std::size_t rewrites = 0;
  std::int64_t elapsed_ns = 0;  // collected only when explicitly requested
};

struct ProgramOptimizationStats {
  ProgramPassStats constants;
  ProgramPassStats jumps;
  ProgramPassStats fusion;
};

struct ProgramEvaluation {
  EvaluationResult result;
  bool used_fallback = false;
  bool used_stateful = false;
};

// Experimental contiguous boolean program. The source AST must outlive the program and workers;
// a worker must also be destroyed before its program's storage. Moving owners preserves storage.
// Scoring and deferred replay use an iterative prepared-node continuation program, with sparse
// memoization and reusable worker scratch. No predicate instruction performs name lookup or
// recursive fallback. The production driver does not select this experiment.
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

  static absl::StatusOr<ExpressionProgram> Prepare(
      const parser::Expr& expression,
      ProgramOptimizations optimizations = {});
  ExpressionProgram(ExpressionProgram&&) noexcept;
  ExpressionProgram& operator=(ExpressionProgram&&) noexcept;
  ExpressionProgram(const ExpressionProgram&) = delete;
  ExpressionProgram& operator=(const ExpressionProgram&) = delete;
  ~ExpressionProgram();

  [[nodiscard]] Worker MakeWorker(ProgramDispatch dispatch) const;
  [[nodiscard]] std::size_t InstructionCount() const;
  [[nodiscard]] std::size_t NodeCount() const;
  [[nodiscard]] std::size_t OperandCount() const;
  [[nodiscard]] const ProgramOptimizationStats& OptimizationStats() const;
  // Owned capacities, excluding the source AST, allocator headers and regex-backend allocations.
  [[nodiscard]] std::size_t StorageBytes() const;

 private:
  explicit ExpressionProgram(std::unique_ptr<Data> data);
  std::unique_ptr<Data> data_;
};

}  // namespace xff::engine

#endif  // XFF_ENGINE_EXPRESSION_PROGRAM_H_
