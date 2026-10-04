// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_program.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::StatusIs;
using ::testing::_;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Lt;
using ::testing::Optional;
using ::testing::PrintToString;
using ::testing::ValuesIn;

using Arguments = std::vector<std::string>;

constexpr auto kBooleanOperators = std::to_array<std::string_view>({"-a", "-o", "-nand", "-nor", "-xor", "-xnor", ","});

Arguments JoinExpression(const Arguments& left, std::string_view operation, const Arguments& right) {
  Arguments result;
  result.reserve(left.size() + right.size() + 3);
  result.emplace_back("(");
  result.append_range(left);
  result.emplace_back(operation);
  result.append_range(right);
  result.emplace_back(")");
  return result;
}

// Exhaust syntax trees with zero, one or two logical operators, including unary NOT.
std::vector<Arguments> SmallBooleanTrees() {
  std::array<std::vector<Arguments>, 3> levels;
  levels.front() = {{"-false"}, {"-true"}};
  for (std::size_t count = 1; count < levels.size(); ++count) {
    auto& output = levels.at(count);
    for (const auto& child : levels.at(count - 1)) {
      Arguments negation{"!", "("};
      negation.append_range(child);
      negation.emplace_back(")");
      output.push_back(std::move(negation));
    }
    for (std::size_t left_count = 0; left_count < count; ++left_count) {
      for (const auto& left : levels.at(left_count)) {
        for (const auto& right : levels.at(count - left_count - 1)) {
          for (const auto operation : kBooleanOperators) {
            output.push_back(JoinExpression(left, operation, right));
          }
        }
      }
    }
  }
  std::vector<Arguments> result;
  for (const auto& level : levels) {
    result.append_range(level);
  }
  return result;
}

// Fixed engine and modulo selection make the generated token sequences portable and reproducible.
// Effects are limited to the fixture's recording output sink; no generated host mutation/execution.
Arguments SeededExpression(std::mt19937_64& random, std::size_t depth) {
  static const auto kLeaves = std::to_array<Arguments>({
      {"-true"},
      {"-false"},
      {"-type", "f"},
      {"-size", "+0c"},
      {"-size", "-100c"},
      {"-name", "f*"},
      {"-regex", "f.*"},
      {"-fuzzy", "fil"},
      {"-printf", "visited"},
      {"-perm", "0644"},
  });
  if (depth == 0 || random() % 4 == 0) {
    return kLeaves.at(random() % kLeaves.size());
  }
  auto left = SeededExpression(random, depth - 1);
  if (random() % 5 == 0) {
    Arguments result{"!", "("};
    result.append_range(left);
    result.emplace_back(")");
    return result;
  }
  const auto operation = kBooleanOperators.at(random() % kBooleanOperators.size());
  return JoinExpression(left, operation, SeededExpression(random, depth - 1));
}

class ProgramFs final : public vfs::FileSystem {
 public:
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::PermissionDeniedError("isolated program test");
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return absl::PermissionDeniedError("isolated program test");
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("mutation denied"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::PermissionDeniedError("isolated program test");
  }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override {
    return absl::PermissionDeniedError("isolated program test");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("memory"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }
};

struct ProgramVariant {
  std::string_view name;
  ProgramDispatch dispatch;
  ProgramOptimizations optimizations;
};

constexpr auto kProgramVariants = std::to_array<ProgramVariant>({
    {.name = "Switch", .dispatch = ProgramDispatch::kSwitch},
    {.name = "Functions", .dispatch = ProgramDispatch::kFunctions},
    {.name = "ConstantsSwitch", .dispatch = ProgramDispatch::kSwitch, .optimizations = {.constants = true}},
    {.name = "ConstantsFunctions", .dispatch = ProgramDispatch::kFunctions, .optimizations = {.constants = true}},
    {.name = "JumpsSwitch", .dispatch = ProgramDispatch::kSwitch, .optimizations = {.jumps = true}},
    {.name = "JumpsFunctions", .dispatch = ProgramDispatch::kFunctions, .optimizations = {.jumps = true}},
    {.name = "FusionSwitch", .dispatch = ProgramDispatch::kSwitch, .optimizations = {.fusion = true}},
    {.name = "FusionFunctions", .dispatch = ProgramDispatch::kFunctions, .optimizations = {.fusion = true}},
    {.name = "AllSwitch",
     .dispatch = ProgramDispatch::kSwitch,
     .optimizations = {.constants = true, .jumps = true, .fusion = true}},
    {.name = "AllFunctions",
     .dispatch = ProgramDispatch::kFunctions,
     .optimizations = {.constants = true, .jumps = true, .fusion = true}},
});

struct ExpressionProgramTest : ::testing::TestWithParam<ProgramVariant> {
  static absl::StatusOr<parser::Command> Parse(const std::vector<std::string>& arguments) {
    MBO_ASSIGN_OR_RETURN(auto command, parser::Parse(arguments));
    parser::BindMatchers(command, regex::Grammar::kRe2, parser::CaseMode::kSensitive);
    return command;
  }

  void Check(const std::vector<std::string>& arguments, bool scored = false) {
    SCOPED_TRACE(PrintToString(arguments));
    SCOPED_TRACE(scored);
    ASSERT_OK_AND_ASSIGN(const auto command, Parse(arguments));
    ASSERT_OK_AND_ASSIGN(const auto program, ExpressionProgram::Prepare(*command.expression, GetParam().optimizations));
    const auto worker = program.MakeWorker(GetParam().dispatch);
    auto bytes = worker.StorageBytes();
    EXPECT_THAT(program.NodeCount(), Ge(1));
    EXPECT_THAT(program.InstructionCount(), Ge(1));
    EXPECT_THAT(program.StorageBytes(), Ge(sizeof(ExpressionProgram)));
    EXPECT_THAT(bytes, Ge(sizeof(ExpressionProgram::Worker)));
    const auto emit = [this](std::string_view text) { output.append(text); };
    EvalContext context{
        .visit = visit,
        .emit = emit,
        .dry_run = dry_run,
        .fs = fs,
        .now = absl::UnixEpoch(),
        .tz = absl::UTCTimeZone(),
        .fuzzy_score = scored ? mbo::types::OptionalRef{score} : mbo::types::OptionalRef<std::optional<int>>{},
        .control = control,
    };
    // Warm the reusable state stack before checking stable storage. This call uses only the
    // isolated test sinks; clear every effect before the actual differential observations.
    if (scored) {
      worker.Evaluate(context);
      bytes = worker.StorageBytes();
    }
    for (const std::uint64_t size : {0, 7, 1'024}) {
      metadata.size = size;
      output.clear();
      control = {};
      context.content.Invalidate();
      score.reset();
      const auto expected = EvaluateDeferred(*command.expression, context);
      const auto expected_output = std::exchange(output, {});
      const auto expected_control = std::exchange(control, {});
      context.content.Invalidate();
      score.reset();
      const auto actual = worker.Evaluate(context);
      EXPECT_THAT(actual.used_fallback, IsFalse());
      EXPECT_THAT(actual.used_stateful, Eq(scored));
      EXPECT_THAT(actual.result.matched, Eq(expected.matched));
      EXPECT_THAT(actual.result.unknown, Eq(expected.unknown));
      EXPECT_THAT(actual.result.deferred, Eq(expected.deferred));
      EXPECT_THAT(actual.result.fuzzy, Eq(expected.fuzzy));
      EXPECT_THAT(output, EqualsText(expected_output));
      EXPECT_THAT(control.prune, Eq(expected_control.prune));
      EXPECT_THAT(control.quit, Eq(expected_control.quit));
      EXPECT_THAT(
          control.metadata_error,
          StatusIs(expected_control.metadata_error.code(), Eq(expected_control.metadata_error.message())));
      EXPECT_THAT(
          control.mutation_error,
          StatusIs(expected_control.mutation_error.code(), Eq(expected_control.mutation_error.message())));
      EXPECT_THAT(worker.StorageBytes(), Eq(bytes));
    }
  }

  ProgramFs fs;
  vfs::Metadata metadata{.type = vfs::FileType::kRegular, .size = 7};
  Visit visit{.path = "file", .name = "file", .metadata = metadata, .fs = fs};
  Control control;
  std::string output;
  bool dry_run = false;
  std::optional<int> score;
};

TEST_P(ExpressionProgramTest, EveryOperatorPreservesTruthAndOutputOrderWithoutFallback) {
  constexpr auto kOperators = std::to_array<std::string_view>({"-a", "-o", "-nand", "-nor", "-xor", "-xnor", ","});
  for (const auto operation : kOperators) {
    for (const bool left : {false, true}) {
      for (const bool right : {false, true}) {
        SCOPED_TRACE(operation);
        for (const bool scored : {false, true}) {
          Check(
              {
                  ".",
                  "(",
                  "-printf",
                  "left",
                  ",",
                  left ? "-true" : "-false",
                  ")",
                  std::string(operation),
                  "(",
                  "-printf",
                  "right",
                  ",",
                  right ? "-true" : "-false",
                  ")",
              },
              scored);
        }
      }
    }
  }
}

TEST_P(ExpressionProgramTest, NestedControlFlowAndScratchAreReusedAcrossEntries) {
  Check({
      ".", "(", "-size", "0", "-xor", "(",     "-type", "f",  "-xnor",  "-false",
      ")", ")", "-nand", "!", "(",    "-size", "+100c", "-o", "-false", ")",
  });
  Check({".", "!", "!", "-type", "f", ",", "-prune", ",", "-quit", ",", "-false"});
  Check({".", "-regex", "f.*", "-a", "!", "-regex", "d.*"});
}

TEST_P(ExpressionProgramTest, LongChainsAndNestedSavedValuesKeepStableStorage) {
  for (const std::size_t count : {1, 4, 16, 64, 256}) {
    std::vector<std::string> arguments{"."};
    arguments.reserve(1 + (2 * count));
    for (std::size_t index = 0; index < count; ++index) {
      arguments.emplace_back("-size");
      arguments.emplace_back("-1024c");
    }
    Check(arguments);
  }
  constexpr std::size_t kDepth = 64;
  std::vector<std::string> nested{"."};
  nested.reserve(2 + (4 * kDepth));
  for (std::size_t index = 0; index < kDepth; ++index) {
    nested.insert(nested.end(), {"-true", "-xor", "("});
  }
  nested.emplace_back("-false");
  nested.insert(nested.end(), kDepth, ")");
  Check(nested);
  Check(nested, true);
}

TEST_P(ExpressionProgramTest, UnknownStopsBeforeNegationAndLaterActions) {
  const auto fail = [this] { return fs.Stat(visit.path, false).status(); };
  visit.load_metadata.emplace(fail);
  Check({".", "-false", "-a", "-size", "1c"});
  Check({".", "-true", "-xor", "(", "!", "-size", "1c", ",", "-printf", "not reached", ")"});
  EXPECT_THAT(control.metadata_error, StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("isolated")));
  EXPECT_THAT(output, IsEmpty());
}

TEST_P(ExpressionProgramTest, DryRunAndMutationFailureRetainTheReferenceBehavior) {
  dry_run = true;
  Check({".", "!", "-exec", "never-run", "{}", ";", ",", "-printf", "not reached"});
  dry_run = false;
  Check({".", "-delete", "-a", "-printf", "not reached"});
  EXPECT_THAT(control.mutation_error, StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("mutation denied")));
}

TEST_P(ExpressionProgramTest, FuzzyAndDeferredContextsUseIterativeStateWithoutFallback) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({".", "-fuzzy", "file", "-top", "1"}));
  ASSERT_OK_AND_ASSIGN(const auto program, ExpressionProgram::Prepare(*command.expression, GetParam().optimizations));
  const auto worker = program.MakeWorker(GetParam().dispatch);
  const auto emit = [](std::string_view) {};
  DeferredDecisions decisions;
  EvaluationMemo memo;
  DeferredEvaluation deferred{.decisions = decisions, .memo = memo};
  EvalContext context{
      .visit = visit,
      .emit = emit,
      .fs = fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .fuzzy_score = score,
      .control = control,
  };
  const auto scored = worker.Evaluate(context);
  EXPECT_THAT(scored.used_fallback, IsFalse());
  EXPECT_THAT(scored.used_stateful, IsTrue());
  context.fuzzy_score.reset();
  context.deferred.set_ref(deferred);
  const auto waiting = worker.Evaluate(context);
  EXPECT_THAT(waiting.used_fallback, IsFalse());
  EXPECT_THAT(waiting.used_stateful, IsTrue());
  ASSERT_THAT(waiting.result.waiting_at, Optional(_));
  if (waiting.result.waiting_at.has_value()) {
    decisions.emplace(*waiting.result.waiting_at, true);
  }
  EXPECT_THAT(worker.Evaluate(context).result.matched, IsTrue());
}

TEST_P(ExpressionProgramTest, StatefulOperatorsComposeScoresAndReuseScratch) {
  constexpr auto kOperators = std::to_array<std::string_view>({"-a", "-o", "-nand", "-nor", "-xor", "-xnor", ","});
  for (const auto operation : kOperators) {
    Check({".", "-fuzzy", "fil", std::string(operation), "-fuzzy", "file"}, true);
    Check({".", "(", "-fuzzy", "fil", std::string(operation), "-false", ")", "-a", "-fuzzy", "file"}, true);
    Check({".", "!", "(", "-fuzzy", "fil", std::string(operation), "-fuzzy", "file", ")"}, true);
  }
  const auto fail = [this] { return fs.Stat(visit.path, false).status(); };
  visit.load_metadata.emplace(fail);
  Check({".", "-fuzzy", "file", "-a", "(", "-true", "-xor", "-size", "1c", ")"}, true);
}

TEST_P(ExpressionProgramTest, PersistentStateRestoresContextAcrossTwoReplayFrontiers) {
  ASSERT_OK_AND_ASSIGN(
      const auto command, Parse(
                              {".", "-fuzzy", "file", "-printf", "first", "-top", "1", "-printf", "second", "-top", "1",
                               "-printf", "third"}));
  ASSERT_OK_AND_ASSIGN(const auto program, ExpressionProgram::Prepare(*command.expression, GetParam().optimizations));
  const auto worker = program.MakeWorker(GetParam().dispatch);
  DeferredDecisions decisions;
  EvaluationMemo memo;
  DeferredEvaluation deferred{.decisions = decisions, .memo = memo};
  const auto emit = [this](std::string_view text) { output.append(text); };
  EvalContext context{
      .visit = visit,
      .emit = emit,
      .fs = fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .fuzzy_score = score,
      .deferred = deferred,
      .incoming_fuzzy_score = 61,
      .control = control,
  };
  const auto first = worker.Evaluate(context);
  EXPECT_THAT(first.used_stateful, IsTrue());
  EXPECT_THAT(first.result.deferred, IsTrue());
  EXPECT_THAT(first.result.fuzzy, Optional(61));
  EXPECT_THAT(output, EqualsText("first"));
  EXPECT_THAT(context.incoming_fuzzy_score, Optional(61));
  ASSERT_THAT(first.result.waiting_at, Optional(_));
  if (first.result.waiting_at.has_value()) {
    decisions.emplace(*first.result.waiting_at, true);
  }
  const auto bytes = worker.StorageBytes();
  const auto second = worker.Evaluate(context);
  EXPECT_THAT(second.result.deferred, IsTrue());
  EXPECT_THAT(output, EqualsText("firstsecond"));
  ASSERT_THAT(second.result.waiting_at, Optional(_));
  if (second.result.waiting_at.has_value()) {
    decisions.emplace(*second.result.waiting_at, true);
  }
  EXPECT_THAT(worker.Evaluate(context).result.matched, IsTrue());
  EXPECT_THAT(output, EqualsText("firstsecondthird"));
  EXPECT_THAT(context.incoming_fuzzy_score, Optional(61));
  EXPECT_THAT(worker.StorageBytes(), Eq(bytes));
  // Exercise the restored binding: absence or binding to worker scratch cannot update score.
  score.reset();
  if (context.fuzzy_score.has_value()) {
    context.fuzzy_score->emplace(17);
  }
  EXPECT_THAT(score, Optional(17));
  context.deferred.reset();
  context.fuzzy_score.reset();
  EXPECT_THAT(worker.Evaluate(context).used_stateful, IsFalse());
  EXPECT_THAT(worker.StorageBytes(), Eq(bytes));
}

TEST_P(ExpressionProgramTest, MovesRetainSourceAndProgramStorage) {
  std::unique_ptr<parser::Expr> source;
  ASSERT_OK_AND_ASSIGN(auto command, Parse({".", "-type", "f"}));
  ASSERT_OK_AND_ASSIGN(auto program, ExpressionProgram::Prepare(*command.expression, GetParam().optimizations));
  auto worker = program.MakeWorker(GetParam().dispatch);
  auto moved_worker = std::move(worker);
  auto replacement_worker = program.MakeWorker(GetParam().dispatch);
  replacement_worker = std::move(moved_worker);
  auto moved_program = std::move(program);
  ASSERT_OK_AND_ASSIGN(
      auto replacement_program, ExpressionProgram::Prepare(*command.expression, GetParam().optimizations));
  replacement_program = std::move(moved_program);
  source = std::move(command.expression);
  EXPECT_THAT(replacement_program.OperandCount(), Eq(1));
  const auto emit = [](std::string_view) {};
  EvalContext context{
      .visit = visit,
      .emit = emit,
      .fs = fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .control = control,
  };
  EXPECT_THAT(replacement_worker.Evaluate(context).result.matched, IsTrue());
  EXPECT_THAT(EvaluateDeferred(*source, context).matched, IsTrue());
}

TEST_P(ExpressionProgramTest, EveryConstantOperatorAndKnownShortCircuitKeepsTruth) {
  constexpr auto kOperators = std::to_array<std::string_view>({"-a", "-o", "-nand", "-nor", "-xor", "-xnor", ","});
  for (const auto operation : kOperators) {
    for (const std::string_view left : {"-true", "-false"}) {
      for (const std::string_view right : {"-true", "-false"}) {
        Check({".", std::string(left), std::string(operation), std::string(right)});
        Check({".", "!", "(", std::string(left), std::string(operation), std::string(right), ")"});
      }
      Check({".", std::string(left), std::string(operation), "-size", "+0c"});
    }
  }
}

TEST_P(ExpressionProgramTest, ExhaustiveSmallBooleanTreesPreserveTheReferenceResult) {
  const auto trees = SmallBooleanTrees();
  ASSERT_THAT(trees.size(), Eq(902));
  for (const auto& expression : trees) {
    Arguments arguments{"."};
    arguments.append_range(expression);
    Check(arguments);
    Check(arguments, true);
  }
}

TEST_P(ExpressionProgramTest, SeededLargerTreesPreserveValuesScoresAndOutputOrder) {
  constexpr std::uint64_t kSeed = 0x58464645585052;
  std::mt19937_64 random(kSeed);
  for (std::size_t index = 0; index < 32; ++index) {
    SCOPED_TRACE(index);
    Arguments arguments{"."};
    arguments.append_range(SeededExpression(random, 6));
    Check(arguments);
    Check(arguments, true);
  }
}

TEST_P(ExpressionProgramTest, RewritesKeepIncomingEdgesNegationAndSourceIdentity) {
  constexpr auto kOperators = std::to_array<std::string_view>({"-a", "-o", "-nand", "-nor", "-xor", "-xnor", ","});
  for (const auto outer : kOperators) {
    for (const auto inner : kOperators) {
      for (const std::string_view truth : {"-true", "-false"}) {
        Check({
            ".",
            "(",
            "-size",
            "+0c",
            std::string(inner),
            "-type",
            "f",
            ")",
            std::string(outer),
            "(",
            std::string(truth),
            ",",
            "-printf",
            "reached",
            ")",
        });
        Check({
            ".",
            std::string(truth),
            std::string(outer),
            "(",
            "-size",
            "+0c",
            std::string(inner),
            "!",
            "-type",
            "d",
            ")",
            ",",
            "-printf",
            "tail",
        });
      }
    }
  }
}

TEST_P(ExpressionProgramTest, ConstantFoldingDoesNotDiscardTheEffectsOfAnUnknownLeftSide) {
  Check({".", "-printf", "keep", ",", "-false", "-a", "-printf", "skip"});
  Check({".", "-printf", "keep", "-a", "-false"});
  Check({".", "-printf", "keep", "-o", "-true"});
  Check({".", "-false", "-a", "(", "-delete", ",", "-prune", ",", "-quit", ")"});
  Check({".", "-true", "-o", "(", "-size", "garbage", ",", "-printf", "skip", ")"});
}

TEST_P(ExpressionProgramTest, ReportsEachPassAndOptionalPreparationTiming) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({".", "-type", "f", "-size", "+0c", "-perm", "0644"}));
  const auto options = GetParam().optimizations;
  ASSERT_OK_AND_ASSIGN(const auto program, ExpressionProgram::Prepare(*command.expression, options));
  const auto& stats = program.OptimizationStats();
  EXPECT_THAT(stats.constants.enabled, Eq(options.constants));
  EXPECT_THAT(stats.jumps.enabled, Eq(options.jumps));
  EXPECT_THAT(stats.fusion.enabled, Eq(options.fusion));
  EXPECT_THAT(stats.constants.input_instructions, Eq(5));
  EXPECT_THAT(stats.constants.elapsed_ns, Eq(0));
  EXPECT_THAT(stats.jumps.elapsed_ns, Eq(0));
  EXPECT_THAT(stats.fusion.elapsed_ns, Eq(0));
  if (options.jumps) {
    EXPECT_THAT(stats.jumps.rewrites, Gt(0));
  }
  if (options.fusion) {
    EXPECT_THAT(stats.fusion.rewrites, Gt(0));
    EXPECT_THAT(stats.fusion.output_instructions, Lt(stats.fusion.input_instructions));
  }
  ASSERT_OK_AND_ASSIGN(
      const auto literal, Parse({".", "-false", "-a", "(", "-printf", "unreachable", "-size", "1c", ")"}));
  ASSERT_OK_AND_ASSIGN(const auto folded, ExpressionProgram::Prepare(*literal.expression, options));
  if (options.constants) {
    EXPECT_THAT(folded.InstructionCount(), Eq(1));
    EXPECT_THAT(folded.OptimizationStats().constants.rewrites, Eq(1));
    EXPECT_THAT(
        folded.OptimizationStats().constants.output_instructions,
        Lt(folded.OptimizationStats().constants.input_instructions));
  }
  ASSERT_OK_AND_ASSIGN(
      const auto measured,
      ExpressionProgram::Prepare(
          *command.expression, {.constants = true, .jumps = true, .fusion = true, .measure_time = true}));
  EXPECT_THAT(measured.OptimizationStats().constants.elapsed_ns, Ge(0));
  EXPECT_THAT(measured.OptimizationStats().jumps.elapsed_ns, Ge(0));
  EXPECT_THAT(measured.OptimizationStats().fusion.elapsed_ns, Ge(0));
}

TEST_P(ExpressionProgramTest, MalformedInputIsRejectedBeforeLowering) {
  const parser::Expr invalid{.kind = parser::Expr::Kind::kAnd};
  EXPECT_THAT(ExpressionProgram::Prepare(invalid), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("shape")));
}

INSTANTIATE_TEST_SUITE_P(
    Dispatch,
    ExpressionProgramTest,
    ValuesIn(kProgramVariants),
    [](const ::testing::TestParamInfo<ProgramVariant>& info) { return std::string(info.param.name); });

}  // namespace
}  // namespace xff::engine
