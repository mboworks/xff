// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_program.h"

#include <algorithm>
#include <chrono>
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
#include "xff/registry/descriptor.h"

namespace xff::engine {
namespace {

struct InstructionTag {};

using InstructionId = mbo::types::ConstStrongId<InstructionTag, std::size_t>;

enum class Operation {
  kEvaluate,
  kJumpIfFalse,
  kJumpIfTrue,
  kNot,
  kSave,
  kXor,
  kTrue,
  kFalse,
  kEvaluateJumpIfFalse,
  kEvaluateJumpIfTrue,
};

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

void Constant(const Instruction& instruction, Execution& execution) {
  execution.result = {.matched = instruction.operation == Operation::kTrue};
  ++execution.next;
}

void EvaluateAndBranch(const Instruction& instruction, Execution& execution) {
  execution.result = execution.predicates.EvaluatePredicate(instruction.source, execution.context);
  if (execution.result.unknown || execution.result.deferred) {
    execution.next = execution.end;
  } else {
    const bool branch_on_true = instruction.operation == Operation::kEvaluateJumpIfTrue;
    execution.next = execution.result.matched == branch_on_true ? instruction.target.value() : execution.next + 1;
  }
}

bool IsBranch(Operation operation) {
  return operation == Operation::kJumpIfFalse || operation == Operation::kJumpIfTrue;
}

bool HasTarget(Operation operation) {
  return IsBranch(operation) || operation == Operation::kEvaluateJumpIfFalse
         || operation == Operation::kEvaluateJumpIfTrue;
}

Instruction MakeInstruction(Operation operation, ExpressionSourceId source) {
  switch (operation) {
    case Operation::kEvaluate: return {.operation = operation, .source = source, .execute = Evaluate};
    case Operation::kJumpIfFalse: return {.operation = operation, .source = source, .execute = JumpIfFalse};
    case Operation::kJumpIfTrue: return {.operation = operation, .source = source, .execute = JumpIfTrue};
    case Operation::kNot: return {.operation = operation, .source = source, .execute = Negate};
    case Operation::kSave: return {.operation = operation, .source = source, .execute = Save};
    case Operation::kXor: return {.operation = operation, .source = source, .execute = Xor};
    case Operation::kTrue:
    case Operation::kFalse: return {.operation = operation, .source = source, .execute = Constant};
    case Operation::kEvaluateJumpIfFalse:
    case Operation::kEvaluateJumpIfTrue:
      return {.operation = operation, .source = source, .execute = EvaluateAndBranch};
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
    case Operation::kTrue:
    case Operation::kFalse: Constant(instruction, execution); break;
    case Operation::kEvaluateJumpIfFalse:
    case Operation::kEvaluateJumpIfTrue: EvaluateAndBranch(instruction, execution); break;
  }
}

// Facts are computed bottom-up over validated preorder IDs. A known truth also proves that
// evaluating the subtree has no effects. In particular X AND false cannot discard an unknown X.
struct SubtreeFacts {
  std::size_t nodes = 1;
  std::size_t instructions = 1;
  std::optional<bool> truth;
};

std::optional<bool> ConstantTruth(parser::Expr::Kind kind, std::optional<bool> left, std::optional<bool> right) {
  using Kind = parser::Expr::Kind;
  if (!left.has_value()) {
    return std::nullopt;
  }
  switch (kind) {
    case Kind::kNot: return !*left;
    case Kind::kAnd: return !*left ? std::optional(false) : right;
    case Kind::kOr: return *left ? std::optional(true) : right;
    case Kind::kNand: return !*left ? std::optional(true) : right.transform([](bool value) { return !value; });
    case Kind::kNor: return *left ? std::optional(false) : right.transform([](bool value) { return !value; });
    case Kind::kXor: return right.transform([left_value = *left](bool value) { return left_value != value; });
    case Kind::kXnor: return right.transform([left_value = *left](bool value) { return left_value == value; });
    case Kind::kComma: return right;
    case Kind::kPredicate: break;  // Predicates use the registry's audited contract.
  }
  return std::nullopt;
}

std::size_t OperatorInstructions(parser::Expr::Kind kind) {
  using Kind = parser::Expr::Kind;
  switch (kind) {
    case Kind::kComma: return 0;
    case Kind::kNand:
    case Kind::kNor:
    case Kind::kXor: return 2;
    case Kind::kXnor: return 3;
    default: return 1;
  }
}

std::vector<SubtreeFacts> AnalyzeConstants(const std::vector<ExpressionSource>& sources) {
  std::vector<SubtreeFacts> facts(sources.size());
  for (std::size_t end = sources.size(); end != 0; --end) {
    const std::size_t index = end - 1;
    const auto& source = sources.at(index).expression.get();
    auto& fact = facts.at(index);
    if (source.kind == parser::Expr::Kind::kPredicate) {
      if (source.descriptor) {
        fact.truth = source.descriptor->constant_truth;
      }
      continue;
    }
    const auto& left = facts.at(index + 1);
    const SubtreeFacts right =
        source.rhs ? facts.at(index + 1 + left.nodes) : SubtreeFacts{.nodes = 0, .instructions = 0};
    fact = {
        .nodes = 1 + left.nodes + right.nodes,
        .instructions = OperatorInstructions(source.kind) + left.instructions + right.instructions,
        .truth = ConstantTruth(source.kind, left.truth, right.truth),
    };
  }
  return facts;
}

using PreparationClock = std::chrono::steady_clock;

PreparationClock::time_point StartTiming(bool measure) {
  return measure ? PreparationClock::now() : PreparationClock::time_point{};
}

std::int64_t Elapsed(PreparationClock::time_point start, bool measure) {
  return measure ? std::chrono::duration_cast<std::chrono::nanoseconds>(PreparationClock::now() - start).count() : 0;
}

// Each rewrite preserves source identity and remaps every destination, including the one-past-end
// exit. All compiler-generated branches go forward, so jump threading is a single reverse pass.
struct InstructionOptimizer {
  std::vector<Instruction> instructions;

  void Compact(const std::vector<bool>& removed) {
    std::vector<InstructionId> positions;
    positions.reserve(instructions.size() + 1);
    std::size_t next = 0;
    for (const bool remove : removed) {
      positions.emplace_back(next);
      next += static_cast<std::size_t>(!remove);
    }
    positions.emplace_back(next);
    next = 0;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
      if (removed.at(index)) {
        continue;
      }
      auto instruction = instructions.at(index);
      if (HasTarget(instruction.operation)) {
        instruction.target = InstructionId{positions.at(instruction.target.value())};
      }
      instructions.at(next++) = Instruction{instruction};
    }
    instructions.resize(next);
  }

  ProgramPassStats SimplifyJumps(bool measure) {
    const auto start = StartTiming(measure);
    ProgramPassStats stats{.enabled = true, .input_instructions = instructions.size()};
    for (std::size_t end = instructions.size(); end != 0; --end) {
      const std::size_t index = end - 1;
      auto& instruction = instructions.at(index);
      if (!IsBranch(instruction.operation)) {
        continue;
      }
      const auto target = instruction.target.value();
      if (target < instructions.size() && instructions.at(target).operation == instruction.operation) {
        instruction.target = InstructionId{instructions.at(target).target};
        ++stats.rewrites;
      }
    }
    stats.output_instructions = instructions.size();
    stats.elapsed_ns = Elapsed(start, measure);
    return stats;
  }

  ProgramPassStats FuseBranches(bool measure) {
    const auto start = StartTiming(measure);
    ProgramPassStats stats{.enabled = true, .input_instructions = instructions.size()};
    std::vector<bool> targeted(instructions.size() + 1, false);
    for (const auto& instruction : instructions) {
      if (HasTarget(instruction.operation)) {
        targeted.at(instruction.target.value()) = true;
      }
    }
    std::vector<bool> removed(instructions.size(), false);
    for (std::size_t index = 0; index + 1 < instructions.size(); ++index) {
      auto& instruction = instructions.at(index);
      const auto& next = instructions.at(index + 1);
      if (instruction.operation != Operation::kEvaluate || !IsBranch(next.operation) || targeted.at(index + 1)) {
        continue;
      }
      instruction = MakeInstruction(
          next.operation == Operation::kJumpIfTrue ? Operation::kEvaluateJumpIfTrue : Operation::kEvaluateJumpIfFalse,
          instruction.source);
      instruction.target = InstructionId{next.target};
      removed.at(index + 1) = true;
      ++stats.rewrites;
    }
    Compact(removed);
    stats.output_instructions = instructions.size();
    stats.elapsed_ns = Elapsed(start, measure);
    return stats;
  }
};

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
  std::size_t folded_subtrees = 0;

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

  void Compile(const parser::Expr& expression, std::size_t node_count, const std::vector<SubtreeFacts>& facts) {
    instructions.reserve(2 * node_count);
    std::vector<Frame> pending;
    pending.reserve(node_count);
    pending.push_back({.expression = std::cref(expression), .source = ExpressionSourceId{0}});
    std::size_t next_source = 1;
    while (!pending.empty()) {
      auto& frame = pending.back();
      const auto& node = frame.expression.get();
      const auto folded = frame.phase == 0 && !facts.empty() ? facts.at(frame.source.value()).truth : std::nullopt;
      if (folded.has_value()) {
        const auto& fact = facts.at(frame.source.value());
        instructions.push_back(MakeInstruction(*folded ? Operation::kTrue : Operation::kFalse, frame.source));
        next_source += fact.nodes - 1;
        ++folded_subtrees;
        pending.pop_back();
      } else if (node.kind == parser::Expr::Kind::kPredicate) {
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
  ProgramOptimizationStats optimizations;
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

absl::StatusOr<ExpressionProgram> ExpressionProgram::Prepare(
    const parser::Expr& expression,
    ProgramOptimizations optimizations) {
  MBO_ASSIGN_OR_RETURN(auto predicates, PreparedExpression::Prepare(expression));
  const auto start = StartTiming(optimizations.measure_time && optimizations.constants);
  std::vector<SubtreeFacts> facts;
  if (optimizations.constants) {
    MBO_ASSIGN_OR_RETURN(const auto sources, DescribeExpression(expression));
    facts = AnalyzeConstants(sources);
  }
  Lowering lowering;
  lowering.Compile(expression, predicates.NodeCount(), facts);
  ProgramOptimizationStats stats{
      .constants =
          {
              .enabled = optimizations.constants,
              .input_instructions = facts.empty() ? lowering.instructions.size() : facts.front().instructions,
              .output_instructions = lowering.instructions.size(),
              .rewrites = lowering.folded_subtrees,
              .elapsed_ns = Elapsed(start, optimizations.measure_time && optimizations.constants),
          },
  };
  InstructionOptimizer optimizer{.instructions = std::move(lowering.instructions)};
  if (optimizations.jumps) {
    stats.jumps = optimizer.SimplifyJumps(optimizations.measure_time);
  }
  if (optimizations.fusion) {
    stats.fusion = optimizer.FuseBranches(optimizations.measure_time);
  }
  return ExpressionProgram(
      std::make_unique<Data>(Data{
          .predicates = std::move(predicates),
          .instructions = std::move(optimizer.instructions),
          .maximum_scratch = lowering.maximum_scratch,
          .optimizations = stats,
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
    return {.result = state_->predicates.EvaluateIterative(context), .used_stateful = true};
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

const ProgramOptimizationStats& ExpressionProgram::OptimizationStats() const {
  return data_->optimizations;
}

std::size_t ExpressionProgram::StorageBytes() const {
  return sizeof(*this) + sizeof(Data) + data_->predicates.StorageBytes() - sizeof(PreparedExpression)
         + (data_->instructions.capacity() * sizeof(Instruction));
}

}  // namespace xff::engine
