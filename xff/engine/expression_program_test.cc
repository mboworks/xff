// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_program.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

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

struct ExpressionProgramTest : ::testing::TestWithParam<ProgramDispatch> {
  static absl::StatusOr<parser::Command> Parse(const std::vector<std::string>& arguments) {
    MBO_ASSIGN_OR_RETURN(auto command, parser::Parse(arguments));
    parser::BindMatchers(command, regex::Grammar::kRe2, parser::CaseMode::kSensitive);
    return command;
  }

  void Check(const std::vector<std::string>& arguments, bool scored = false) {
    ASSERT_OK_AND_ASSIGN(const auto command, Parse(arguments));
    ASSERT_OK_AND_ASSIGN(const auto program, ExpressionProgram::Prepare(*command.expression));
    const auto worker = program.MakeWorker(GetParam());
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
  ASSERT_OK_AND_ASSIGN(const auto program, ExpressionProgram::Prepare(*command.expression));
  const auto worker = program.MakeWorker(GetParam());
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
  ASSERT_OK_AND_ASSIGN(const auto program, ExpressionProgram::Prepare(*command.expression));
  const auto worker = program.MakeWorker(GetParam());
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
  ASSERT_OK_AND_ASSIGN(auto program, ExpressionProgram::Prepare(*command.expression));
  auto worker = program.MakeWorker(GetParam());
  auto moved_worker = std::move(worker);
  auto replacement_worker = program.MakeWorker(GetParam());
  replacement_worker = std::move(moved_worker);
  auto moved_program = std::move(program);
  ASSERT_OK_AND_ASSIGN(auto replacement_program, ExpressionProgram::Prepare(*command.expression));
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

TEST_P(ExpressionProgramTest, MalformedInputIsRejectedBeforeLowering) {
  const parser::Expr invalid{.kind = parser::Expr::Kind::kAnd};
  EXPECT_THAT(ExpressionProgram::Prepare(invalid), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("shape")));
}

INSTANTIATE_TEST_SUITE_P(
    Dispatch,
    ExpressionProgramTest,
    ::testing::Values(ProgramDispatch::kSwitch, ProgramDispatch::kFunctions),
    [](const ::testing::TestParamInfo<ProgramDispatch>& info) {
      return info.param == ProgramDispatch::kSwitch ? "Switch" : "Functions";
    });

}  // namespace
}  // namespace xff::engine
