// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_program.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "mbo/status/status_macros.h"
#include "mbo/types/strong_id.h"
#include "xff/engine/expression_contract.h"

namespace xff::engine {
namespace {

struct InstructionTag {};

using InstructionId = mbo::types::ConstStrongId<InstructionTag, std::size_t>;

enum class Operation { kEvaluate, kJumpIfFalse, kJumpIfTrue, kNot, kSave, kXor };

struct Execution {
  const PreparedExpression::Worker& predicates;
  EvalContext& context;
  std::vector<std::uint8_t>& saved;
  std::size_t end;
  std::size_t next = 0;
  EvaluationResult result;
};

struct Instruction {
  Operation operation;
  ExpressionSourceId source;
  InstructionId target;
  void (*execute)(const Instruction&, Execution&);
};

void Evaluate(const Instruction& instruction, Execution& execution) {
  execution.result = execution.predicates.EvaluatePredicate(instruction.source, execution.context);
  execution.next = execution.result.unknown || execution.result.deferred ? execution.end : execution.next + 1;
}

void JumpIfFalse(const Instruction& instruction, Execution& execution) {
  execution.next = execution.result.matched ? execution.next + 1 : instruction.target.value();
}

void JumpIfTrue(const Instruction& instruction, Execution& execution) {
  execution.next = execution.result.matched ? instruction.target.value() : execution.next + 1;
}

void Negate(const Instruction&, Execution& execution) {
  execution.result.matched = !execution.result.matched;
  ++execution.next;
}

void Save(const Instruction&, Execution& execution) {
  execution.saved.push_back(static_cast<std::uint8_t>(execution.result.matched));
  ++execution.next;
}

void Xor(const Instruction&, Execution& execution) {
  execution.result.matched = execution.result.matched != (execution.saved.back() != 0);
  execution.saved.pop_back();
  ++execution.next;
}

Instruction MakeInstruction(Operation operation, ExpressionSourceId source) {
  switch (operation) {
    case Operation::kEvaluate: return {.operation = operation, .source = source, .execute = Evaluate};
    case Operation::kJumpIfFalse: return {.operation = operation, .source = source, .execute = JumpIfFalse};
    case Operation::kJumpIfTrue: return {.operation = operation, .source = source, .execute = JumpIfTrue};
    case Operation::kNot: return {.operation = operation, .source = source, .execute = Negate};
    case Operation::kSave: return {.operation = operation, .source = source, .execute = Save};
    case Operation::kXor: return {.operation = operation, .source = source, .execute = Xor};
  }
  std::unreachable();
}

void SwitchStep(const Instruction& instruction, Execution& execution) {
  switch (instruction.operation) {
    case Operation::kEvaluate: Evaluate(instruction, execution); break;
    case Operation::kJumpIfFalse: JumpIfFalse(instruction, execution); break;
    case Operation::kJumpIfTrue: JumpIfTrue(instruction, execution); break;
    case Operation::kNot: Negate(instruction, execution); break;
    case Operation::kSave: Save(instruction, execution); break;
    case Operation::kXor: Xor(instruction, execution); break;
  }
}

struct Lowering {
  struct Frame {
    std::reference_wrapper<const parser::Expr> expression;
    ExpressionSourceId source;
    std::optional<InstructionId> branch;
    int phase = 0;
  };

  std::vector<Instruction> instructions;
  std::size_t scratch = 0;
  std::size_t maximum_scratch = 0;

  void AfterLeft(Frame& frame) {
    using Kind = parser::Expr::Kind;
    const Kind kind = frame.expression.get().kind;
    if (kind == Kind::kAnd || kind == Kind::kNand || kind == Kind::kOr || kind == Kind::kNor) {
      frame.branch = InstructionId{instructions.size()};
      const auto operation =
          kind == Kind::kAnd || kind == Kind::kNand ? Operation::kJumpIfFalse : Operation::kJumpIfTrue;
      instructions.push_back(MakeInstruction(operation, frame.source));
    } else if (kind == Kind::kXor || kind == Kind::kXnor) {
      instructions.push_back(MakeInstruction(Operation::kSave, frame.source));
      maximum_scratch = std::max(maximum_scratch, ++scratch);
    }
  }

  void Finish(const Frame& frame) {
    using Kind = parser::Expr::Kind;
    const Kind kind = frame.expression.get().kind;
    if (frame.branch.has_value()) {
      instructions.at(frame.branch->value()).target = InstructionId{instructions.size()};
    }
    if (kind == Kind::kXor || kind == Kind::kXnor) {
      instructions.push_back(MakeInstruction(Operation::kXor, frame.source));
      --scratch;
    }
    if (kind == Kind::kNot || kind == Kind::kNand || kind == Kind::kNor || kind == Kind::kXnor) {
      instructions.push_back(MakeInstruction(Operation::kNot, frame.source));
    }
  }

  void Compile(const parser::Expr& expression, std::size_t node_count) {
    instructions.reserve(2 * node_count);
    std::vector<Frame> pending;
    pending.reserve(node_count);
    pending.push_back({.expression = std::cref(expression), .source = ExpressionSourceId{0}});
    std::size_t next_source = 1;
    while (!pending.empty()) {
      auto& frame = pending.back();
      const auto& node = frame.expression.get();
      if (node.kind == parser::Expr::Kind::kPredicate) {
        instructions.push_back(MakeInstruction(Operation::kEvaluate, frame.source));
        pending.pop_back();
      } else if (frame.phase == 0) {
        frame.phase = 1;
        pending.push_back({.expression = std::cref(*node.lhs), .source = ExpressionSourceId{next_source++}});
      } else if (frame.phase == 1 && node.rhs) {
        AfterLeft(frame);
        frame.phase = 2;
        pending.push_back({.expression = std::cref(*node.rhs), .source = ExpressionSourceId{next_source++}});
      } else {
        Finish(frame);
        pending.pop_back();
      }
    }
  }
};

}  // namespace

struct ExpressionProgram::Data {
  PreparedExpression predicates;
  std::vector<Instruction> instructions;
  std::size_t maximum_scratch;
};

struct ExpressionProgram::Worker::State {
  std::reference_wrapper<const Data> program;
  PreparedExpression::Worker predicates;
  ProgramDispatch dispatch;
  std::vector<std::uint8_t> saved;
};

ExpressionProgram::ExpressionProgram(std::unique_ptr<Data> data) : data_(std::move(data)) {}

ExpressionProgram::ExpressionProgram(ExpressionProgram&&) noexcept = default;
ExpressionProgram& ExpressionProgram::operator=(ExpressionProgram&&) noexcept = default;
ExpressionProgram::~ExpressionProgram() = default;

absl::StatusOr<ExpressionProgram> ExpressionProgram::Prepare(const parser::Expr& expression) {
  MBO_ASSIGN_OR_RETURN(auto predicates, PreparedExpression::Prepare(expression));
  Lowering lowering;
  lowering.Compile(expression, predicates.NodeCount());
  return ExpressionProgram(
      std::make_unique<Data>(Data{
          .predicates = std::move(predicates),
          .instructions = std::move(lowering.instructions),
          .maximum_scratch = lowering.maximum_scratch,
      }));
}

ExpressionProgram::Worker::Worker(std::unique_ptr<State> state) : state_(std::move(state)) {}

ExpressionProgram::Worker::Worker(Worker&&) noexcept = default;
ExpressionProgram::Worker& ExpressionProgram::Worker::operator=(Worker&&) noexcept = default;
ExpressionProgram::Worker::~Worker() = default;

ExpressionProgram::Worker ExpressionProgram::MakeWorker(ProgramDispatch dispatch) const {
  auto state = std::make_unique<Worker::State>(Worker::State{
      .program = std::cref(*data_),
      .predicates = data_->predicates.MakeWorker(),
      .dispatch = dispatch,
  });
  state->saved.reserve(data_->maximum_scratch);
  return Worker(std::move(state));
}

ProgramEvaluation ExpressionProgram::Worker::Evaluate(EvalContext& context) const {
  if (context.fuzzy_score.has_value() || context.deferred.has_value()) {
    return {.result = state_->predicates.Evaluate(context), .used_fallback = true};
  }
  state_->saved.clear();
  const auto& instructions = state_->program.get().instructions;
  Execution execution{
      .predicates = state_->predicates,
      .context = context,
      .saved = state_->saved,
      .end = instructions.size(),
  };
  if (state_->dispatch == ProgramDispatch::kSwitch) {
    while (execution.next < instructions.size()) {
      SwitchStep(instructions.at(execution.next), execution);
    }
  } else {
    while (execution.next < instructions.size()) {
      const auto& instruction = instructions.at(execution.next);
      instruction.execute(instruction, execution);
    }
  }
  return {.result = execution.result};
}

std::size_t ExpressionProgram::Worker::StorageBytes() const {
  return sizeof(*this) + sizeof(State) + state_->predicates.StorageBytes() - sizeof(PreparedExpression::Worker)
         + (state_->saved.capacity() * sizeof(std::uint8_t));
}

std::size_t ExpressionProgram::InstructionCount() const {
  return data_->instructions.size();
}

std::size_t ExpressionProgram::NodeCount() const {
  return data_->predicates.NodeCount();
}

std::size_t ExpressionProgram::OperandCount() const {
  return data_->predicates.OperandCount();
}

std::size_t ExpressionProgram::StorageBytes() const {
  return sizeof(*this) + sizeof(Data) + data_->predicates.StorageBytes() - sizeof(PreparedExpression)
         + (data_->instructions.capacity() * sizeof(Instruction));
}

}  // namespace xff::engine
