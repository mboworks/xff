// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_contract.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
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
#include "mbo/testing/status.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/registry/registry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::Optional;
using ::testing::SizeIs;

class RecordingExpressionFs final : public vfs::FileSystem {
 public:
  mutable std::vector<std::string> events;
  std::optional<vfs::Metadata> target_metadata;

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    events.push_back("readdir " + std::string(path));
    return std::vector<vfs::Entry>{};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    events.push_back("stat " + std::string(path));
    if (target_metadata.has_value()) {
      return *target_metadata;
    }
    return absl::PermissionDeniedError("recorded stat failure");
  }

  absl::Status Remove(std::string_view path) const override {
    events.push_back("remove " + std::string(path));
    return absl::PermissionDeniedError("isolated filesystem denies mutation");
  }

  bool Access(std::string_view path, vfs::AccessMode) const override {
    events.push_back("access " + std::string(path));
    return true;
  }

  absl::StatusOr<std::string> ReadLink(std::string_view path) const override {
    events.push_back("readlink " + std::string(path));
    return std::string("target");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("memory"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    events.push_back("read " + std::string(path));
    return std::string("needle\n");
  }
};

enum class Executor { kTree, kBound, kPrepared, kPreparedWorker };

struct ExpressionContractTest : ::testing::TestWithParam<Executor> {
  static absl::StatusOr<parser::Command> Parse(const std::vector<std::string>& arguments) {
    MBO_ASSIGN_OR_RETURN(auto command, parser::Parse(arguments));
    parser::BindMatchers(command, regex::Grammar::kRe2, parser::CaseMode::kSensitive);
    return command;
  }

  EvaluationResult Observe(const parser::Expr& expression, bool dry_run = false) {
    control = {};
    const auto emit = [this](std::string_view text) { fs.events.push_back("output " + std::string(text)); };
    DeferredEvaluation deferred{.decisions = decisions, .memo = memo};
    EvalContext context{
        .visit = visit,
        .emit = emit,
        .dry_run = dry_run,
        .fs = fs,
        .now = absl::UnixEpoch(),
        .tz = absl::UTCTimeZone(),
        .fuzzy_score = score,
        .deferred = deferred,
        .control = control,
        .first_counts = counts,
    };
    if (GetParam() == Executor::kTree) {
      return EvaluateDeferred(expression, context);
    }
    if (GetParam() == Executor::kPrepared || GetParam() == Executor::kPreparedWorker) {
      auto prepared = PreparedExpression::Prepare(expression);
      EXPECT_THAT(prepared, IsOk());
      if (!prepared.ok()) {
        return {.unknown = true};
      }
      if (GetParam() == Executor::kPreparedWorker) {
        auto worker = prepared->MakeWorker();
        return worker.Evaluate(context);
      }
      return prepared->Evaluate(context);
    }
    auto bound = BoundExpression::Prepare(expression);
    EXPECT_THAT(bound, IsOk());
    return bound.ok() ? bound->Evaluate(context) : EvaluationResult{.unknown = true};
  }

  void Decide(const std::optional<ExprIdentity>& source) {
    ASSERT_THAT(source, Optional(_));
    // Keep presence visible to static analysis as well as the assertion matcher.
    if (source.has_value()) {
      decisions.emplace(*source, true);
    }
  }

  RecordingExpressionFs fs;
  vfs::Metadata metadata{.type = vfs::FileType::kRegular, .size = 7, .mode = 0644};
  Visit visit{.path = "tree/file.txt", .name = "file.txt", .metadata = metadata, .fs = fs};
  Control control;
  std::optional<int> score;
  FirstCounts counts;
  DeferredDecisions decisions;
  EvaluationMemo memo;
};

TEST_P(ExpressionContractTest, SourceIdsRetainPreorderAndSurviveOwnerMove) {
  ASSERT_OK_AND_ASSIGN(auto command, Parse({".", "-true", ",", "!", "-false"}));
  ASSERT_OK_AND_ASSIGN(const auto sources, DescribeExpression(*command.expression));
  EXPECT_THAT(sources, SizeIs(4));
  const auto owner = std::move(command.expression);
  EXPECT_THAT(ExprIdentity{sources.at(0).expression.get()}, Eq(ExprIdentity{*owner}));
  for (std::size_t index = 0; index < sources.size(); ++index) {
    EXPECT_THAT(sources.at(index).id.value(), Eq(index));
    EXPECT_THAT(sources.at(index).requirements.optimization_barrier, IsTrue());
  }
  EXPECT_THAT(sources.at(1).expression.get().descriptor->name, Eq("-true"));
  EXPECT_THAT(sources.at(2).expression.get().kind, Eq(parser::Expr::Kind::kNot));
  EXPECT_THAT(sources.at(3).expression.get().descriptor->name, Eq("-false"));
}

TEST_P(ExpressionContractTest, RejectsIncompleteTreesBeforeObservation) {
  for (const auto kind : {parser::Expr::Kind::kPredicate, parser::Expr::Kind::kNot, parser::Expr::Kind::kAnd}) {
    const parser::Expr invalid{.kind = kind};
    EXPECT_THAT(DescribeExpression(invalid), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("shape")));
  }
  const parser::Expr invalid_kind{.kind = static_cast<parser::Expr::Kind>(-1)};
  EXPECT_THAT(DescribeExpression(invalid_kind), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("shape")));
  EXPECT_THAT(fs.events, IsEmpty());
}

TEST_P(ExpressionContractTest, EveryRegisteredPrimaryRetainsAnOptimizationBarrier) {
  for (const auto& descriptor : registry::All()) {
    if (descriptor.kind == registry::Kind::kOperator) {
      continue;
    }
    SCOPED_TRACE(descriptor.name);
    const parser::Expr expression{.kind = parser::Expr::Kind::kPredicate, .descriptor = descriptor};
    ASSERT_OK_AND_ASSIGN(const auto sources, DescribeExpression(expression));
    const auto& requirements = sources.front().requirements;
    EXPECT_THAT(requirements.metadata, Eq(descriptor.needs_metadata));
    EXPECT_THAT(requirements.output, Eq(descriptor.stdout_output || descriptor.writes_file));
    EXPECT_THAT(requirements.execution, Eq(descriptor.safety == registry::Safety::kSecurity));
    EXPECT_THAT(requirements.optimization_barrier, IsTrue());
  }
}

TEST_P(ExpressionContractTest, RejectsExtraAndMissingChildren) {
  struct Shape {
    parser::Expr::Kind kind;
    bool left;
    bool right;
  };

  constexpr auto kMalformed = std::to_array<Shape>({
      {
          .kind = parser::Expr::Kind::kPredicate,
          .left = true,
      },
      {
          .kind = parser::Expr::Kind::kPredicate,
          .right = true,
      },
      {
          .kind = parser::Expr::Kind::kNot,
          .left = true,
          .right = true,
      },
      {
          .kind = parser::Expr::Kind::kAnd,
          .left = true,
      },
  });
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({".", "-true"}));
  for (const auto& shape : kMalformed) {
    const parser::Expr expression{
        .kind = shape.kind,
        .descriptor = command.expression->descriptor,
        .lhs = shape.left ? std::make_unique<parser::Expr>() : nullptr,
        .rhs = shape.right ? std::make_unique<parser::Expr>() : nullptr,
    };
    EXPECT_THAT(DescribeExpression(expression), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("shape")));
  }
  EXPECT_THAT(fs.events, IsEmpty());
}

TEST_P(ExpressionContractTest, AllBooleanOperatorsPreserveTruthAndObservableOrder) {
  struct Operation {
    std::string_view token;
    bool short_false;
    bool short_true;
    std::array<bool, 4> truth;
  };

  constexpr auto kOperations = std::to_array<Operation>({
      {
          .token = "-a",
          .short_false = true,
          .truth = {false, false, false, true},
      },
      {
          .token = "-o",
          .short_true = true,
          .truth = {false, true, true, true},
      },
      {
          .token = "-nand",
          .short_false = true,
          .truth = {true, true, true, false},
      },
      {
          .token = "-nor",
          .short_true = true,
          .truth = {true, false, false, false},
      },
      {
          .token = "-xor",
          .truth = {false, true, true, false},
      },
      {
          .token = "-xnor",
          .truth = {true, false, false, true},
      },
      {
          .token = ",",
          .truth = {false, true, false, true},
      },
  });
  for (const auto& operation : kOperations) {
    for (const bool left : {false, true}) {
      for (const bool right : {false, true}) {
        SCOPED_TRACE(operation.token);
        ASSERT_OK_AND_ASSIGN(
            const auto command,
            Parse(
                {".", "(", "-printf", "left", ",", left ? "-true" : "-false", ")", std::string(operation.token), "(",
                 "-printf", "right", ",", right ? "-true" : "-false", ")"}));
        ASSERT_THAT(DescribeExpression(*command.expression), IsOk());
        fs.events.clear();
        memo.clear();
        const auto result = Observe(*command.expression);
        EXPECT_THAT(result.matched, Eq(operation.truth.at((2UZ * left) + right)));
        EXPECT_THAT(result.unknown, IsFalse());
        EXPECT_THAT(result.deferred, IsFalse());
        if ((left && operation.short_true) || (!left && operation.short_false)) {
          EXPECT_THAT(fs.events, ElementsAre("output left"));
        } else {
          EXPECT_THAT(fs.events, ElementsAre("output left", "output right"));
        }
      }
    }
  }
}

TEST_P(ExpressionContractTest, ShortCircuitSkipsReadsAndNotDiscardsFuzzyScore) {
  ASSERT_OK_AND_ASSIGN(const auto skipped, Parse({".", "-false", "-content", "needle"}));
  EXPECT_THAT(Observe(*skipped.expression).matched, IsFalse());
  EXPECT_THAT(fs.events, IsEmpty());
  ASSERT_OK_AND_ASSIGN(const auto negated, Parse({".", "!", "-fuzzy", "file"}));
  const auto result = Observe(*negated.expression);
  EXPECT_THAT(result.matched, IsFalse());
  EXPECT_THAT(result.fuzzy, Eq(std::nullopt));
}

TEST_P(ExpressionContractTest, DeferredReplayDoesNotRepeatPrefixOutput) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({".", "-printf", "prefix", "-top", "1", "-printf", "suffix"}));
  const auto pending = Observe(*command.expression);
  ASSERT_THAT(pending.waiting_at, Optional(_));
  EXPECT_THAT(pending.deferred, IsTrue());
  EXPECT_THAT(fs.events, ElementsAre("output prefix"));
  Decide(pending.waiting_at);
  const auto resumed = Observe(*command.expression);
  EXPECT_THAT(resumed.matched, IsTrue());
  EXPECT_THAT(resumed.deferred, IsFalse());
  EXPECT_THAT(fs.events, ElementsAre("output prefix", "output suffix"));
}

TEST_P(ExpressionContractTest, DryRunExecutionRemainsUnknownAndStopsLaterEffects) {
  ASSERT_OK_AND_ASSIGN(
      const auto command, Parse({".", "-exec", "ignored-command", "{}", ";", ",", "-printf", "later"}));
  const auto result = Observe(*command.expression, true);
  EXPECT_THAT(result.unknown, IsTrue());
  ASSERT_THAT(fs.events, SizeIs(1));
  EXPECT_THAT(fs.events.front(), HasSubstr("ignored-command"));
  EXPECT_THAT(control.mutation_error, IsOk());
}

TEST_P(ExpressionContractTest, MetadataFailureIsUnknownAndCannotBeNegatedOrHidden) {
  const auto fail = [this] { return fs.Stat(visit.path, false).status(); };
  visit.load_metadata.emplace(fail);
  ASSERT_OK_AND_ASSIGN(const auto skipped, Parse({".", "-false", "-size", "+1c"}));
  EXPECT_THAT(Observe(*skipped.expression).matched, IsFalse());
  EXPECT_THAT(fs.events, IsEmpty());
  ASSERT_OK_AND_ASSIGN(const auto reached, Parse({".", "!", "-size", "+1c", ",", "-printf", "later"}));
  const auto result = Observe(*reached.expression);
  EXPECT_THAT(result.unknown, IsTrue());
  EXPECT_THAT(control.metadata_error, StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("stat failure")));
  EXPECT_THAT(fs.events, ElementsAre("stat tree/file.txt"));
}

TEST_P(ExpressionContractTest, UnreachableSizeOperandStillParticipatesInWholeCommandValidation) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({".", "-false", "-size", "garbage"}));
  EXPECT_THAT(ValidateSizeArgs(*command.expression), StatusIs(absl::StatusCode::kInvalidArgument, _));
  EXPECT_THAT(fs.events, IsEmpty());
}

TEST_P(ExpressionContractTest, IndependentFirstBudgetsPersistAcrossEntries) {
  ASSERT_OK_AND_ASSIGN(
      const auto command,
      Parse({".", "(", "-first", "1", "-printf", "first", ")", "-o", "(", "-first", "1", "-printf", "second", ")"}));
  EXPECT_THAT(Observe(*command.expression).matched, IsTrue());
  memo.clear();  // New entry; per-entry replay memo expires, per-run counters remain.
  EXPECT_THAT(Observe(*command.expression).matched, IsTrue());
  memo.clear();
  EXPECT_THAT(Observe(*command.expression).matched, IsFalse());
  EXPECT_THAT(fs.events, ElementsAre("output first", "output second"));
  EXPECT_THAT(counts, SizeIs(2));
}

TEST_P(ExpressionContractTest, TwoDeferredFrontiersPreserveExactlyOnceEffects) {
  ASSERT_OK_AND_ASSIGN(
      const auto command,
      Parse({".", "-printf", "prefix", "-top", "1", "-printf", "middle", "-top", "1", "-printf", "suffix"}));
  const auto first = Observe(*command.expression);
  ASSERT_THAT(first.waiting_at, Optional(_));
  Decide(first.waiting_at);
  const auto second = Observe(*command.expression);
  ASSERT_THAT(second.waiting_at, Optional(_));
  EXPECT_THAT(second.waiting_at, Not(Eq(first.waiting_at)));
  EXPECT_THAT(fs.events, ElementsAre("output prefix", "output middle"));
  Decide(second.waiting_at);
  EXPECT_THAT(Observe(*command.expression).matched, IsTrue());
  EXPECT_THAT(fs.events, ElementsAre("output prefix", "output middle", "output suffix"));
}

TEST_P(ExpressionContractTest, FailedDeletionUsesTheIsolatedSinkAndPreservesMutationError) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({".", "-delete", "-printf", "later"}));
  const auto result = Observe(*command.expression);
  EXPECT_THAT(result.matched, IsFalse());
  EXPECT_THAT(
      control.mutation_error,
      StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("isolated filesystem denies mutation")));
  EXPECT_THAT(fs.events, ElementsAre("remove tree/file.txt"));
}

TEST_P(ExpressionContractTest, TraversalEffectsStayObservableEvenWithFalseResult) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({".", "-prune", ",", "-quit", ",", "-false"}));
  EXPECT_THAT(Observe(*command.expression).matched, IsFalse());
  EXPECT_THAT(control.prune, IsTrue());
  EXPECT_THAT(control.quit, IsTrue());
}

TEST_P(ExpressionContractTest, EveryRegisteredPrimaryHasAnExplicitBindingDisposition) {
  for (const auto& descriptor : registry::All()) {
    if (descriptor.kind == registry::Kind::kOperator) {
      continue;
    }
    SCOPED_TRACE(descriptor.name);
    const parser::Expr expression{.kind = parser::Expr::Kind::kPredicate, .descriptor = descriptor};
    EXPECT_THAT(BoundExpression::Prepare(expression), IsOk());
    EXPECT_THAT(PreparedExpression::Prepare(expression), IsOk());
  }
}

TEST_P(ExpressionContractTest, AnUnregisteredHandlerIsRejectedRatherThanSilentlyAcceptingIt) {
  const registry::Descriptor descriptor{.name = "unregistered", .needs_metadata = false};
  const parser::Expr expression{.kind = parser::Expr::Kind::kPredicate, .descriptor = descriptor};
  EXPECT_THAT(
      BoundExpression::Prepare(expression),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("missing engine evaluation binding")));
  EXPECT_THAT(
      PreparedExpression::Prepare(expression),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("missing engine evaluation binding")));
}

TEST_P(ExpressionContractTest, BoundPreparationRejectsMalformedInputBeforeObservations) {
  const parser::Expr incomplete{.kind = parser::Expr::Kind::kAnd};
  EXPECT_THAT(BoundExpression::Prepare(incomplete), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("shape")));
  EXPECT_THAT(
      PreparedExpression::Prepare(incomplete), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("shape")));
  EXPECT_THAT(fs.events, IsEmpty());
}

TEST_P(ExpressionContractTest, ConfigurationOnlyPredicatesRemainTrue) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({".", "-regextype", "re2", "-daystart", "-maxdepth", "2"}));
  EXPECT_THAT(Observe(*command.expression).matched, IsTrue());
  EXPECT_THAT(fs.events, IsEmpty());
}

TEST_P(ExpressionContractTest, FuzzyOnlyOrVisitsNestedRightBranchForTheBestScore) {
  ASSERT_OK_AND_ASSIGN(const auto left, Parse({".", "-fuzzy", "fe"}));
  const auto left_result = Observe(*left.expression);
  ASSERT_THAT(left_result.fuzzy, Optional(_));
  memo.clear();
  ASSERT_OK_AND_ASSIGN(const auto right, Parse({".", "-fuzzy", "file"}));
  const auto right_result = Observe(*right.expression);
  ASSERT_THAT(right_result.fuzzy, Optional(_));
  memo.clear();
  ASSERT_OK_AND_ASSIGN(
      const auto combined, Parse({".", "-fuzzy", "fe", "-o", "(", "-fuzzy", "file", "-a", "-fuzzy", "file", ")"}));
  if (left_result.fuzzy.has_value() && right_result.fuzzy.has_value()) {
    EXPECT_THAT(Observe(*combined.expression).fuzzy, Optional(std::max(*left_result.fuzzy, *right_result.fuzzy)));
  }
  EXPECT_THAT(fs.events, IsEmpty());
}

TEST_P(ExpressionContractTest, FuzzyOrDoesNotVisitAnEffectfulRightBranch) {
  ASSERT_OK_AND_ASSIGN(
      const auto command, Parse({".", "-fuzzy", "file", "-o", "(", "-printf", "must-not-run", "-fuzzy", "file", ")"}));
  const auto result = Observe(*command.expression);
  EXPECT_THAT(result.matched, IsTrue());
  EXPECT_THAT(result.fuzzy, Optional(_));
  EXPECT_THAT(fs.events, IsEmpty());
}

struct PreparedOperandTest : ::testing::Test {
  static void IgnoreOutput(std::string_view) {}

  void Check(std::string_view flag, std::string_view argument, bool supply_argument = true) {
    SCOPED_TRACE(flag);
    SCOPED_TRACE(argument);
    const auto descriptor = registry::Lookup(flag);
    ASSERT_THAT(descriptor, Optional(_));
    parser::Expr expression{.kind = parser::Expr::Kind::kPredicate, .descriptor = descriptor};
    if (supply_argument) {
      expression.args.emplace_back(argument);
    }
    ASSERT_OK_AND_ASSIGN(const auto prepared, PreparedExpression::Prepare(expression));
    EXPECT_THAT(prepared.NodeCount(), Eq(1));
    EXPECT_THAT(prepared.OperandCount(), Eq(supply_argument ? 1 : 0));
    EXPECT_THAT(prepared.StorageBytes(), Ge(sizeof(PreparedExpression)));
    // Reuse the same prepared object with multiple entries and resolved block sizes.
    for (const std::uint64_t block_size : {512, 1'024}) {
      for (const std::uint64_t value : kValues) {
        metadata.size = value;
        metadata.blocks = value / 512;
        metadata.mode = static_cast<std::uint32_t>(value);
        metadata.nlink = value;
        metadata.ino = value;
        metadata.uid = static_cast<std::uint32_t>(value);
        metadata.gid = static_cast<std::uint32_t>(value);
        EvalContext context{
            .visit = visit,
            .emit = IgnoreOutput,
            .fs = fs,
            .now = absl::UnixEpoch(),
            .tz = absl::UTCTimeZone(),
            .block_size = block_size,
            .control = control,
        };
        fs.events.clear();
        const auto expected = EvaluateDeferred(expression, context);
        const auto events = std::exchange(fs.events, {});
        const auto actual = prepared.Evaluate(context);
        EXPECT_THAT(actual.matched, Eq(expected.matched));
        EXPECT_THAT(actual.unknown, Eq(expected.unknown));
        EXPECT_THAT(actual.deferred, Eq(expected.deferred));
        EXPECT_THAT(fs.events, Eq(events));
      }
    }
  }

  static constexpr auto kValues = std::to_array<std::uint64_t>({
      0,
      1,
      7,
      8,
      0644,
      07777,
      512,
      513,
      1'000,
      1'024,
      1'025,
      1ULL << 32U,
      std::numeric_limits<std::uint64_t>::max(),
  });
  RecordingExpressionFs fs;
  vfs::Metadata metadata{.type = vfs::FileType::kRegular};
  Visit visit{.path = "file", .name = "file", .metadata = metadata, .fs = fs};
  Control control;
};

TEST_F(PreparedOperandTest, MovingPreparedStorageKeepsSourceIdentityAndTypedOperands) {
  ASSERT_OK_AND_ASSIGN(auto command, parser::Parse({".", "-type", "f", "-size", "+1c"}));
  ASSERT_OK_AND_ASSIGN(auto prepared, PreparedExpression::Prepare(*command.expression));
  ASSERT_OK_AND_ASSIGN(const auto bound, BoundExpression::Prepare(*command.expression));
  const auto bytes = prepared.StorageBytes();
  auto moved = std::move(prepared);
  ASSERT_OK_AND_ASSIGN(auto replacement, PreparedExpression::Prepare(*command.expression));
  replacement = std::move(moved);
  EXPECT_THAT(replacement.NodeCount(), Eq(3));
  EXPECT_THAT(replacement.OperandCount(), Eq(2));
  EXPECT_THAT(replacement.StorageBytes(), Eq(bytes));
  EXPECT_THAT(bound.StorageBytes(), Ge(sizeof(BoundExpression)));
  const auto source_owner = std::move(command.expression);
  metadata.size = 7;
  EvalContext context{
      .visit = visit,
      .emit = IgnoreOutput,
      .fs = fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .control = control,
  };
  EXPECT_THAT(replacement.Evaluate(context).matched, IsTrue());
  metadata.size = 0;
  EXPECT_THAT(replacement.Evaluate(context).matched, IsFalse());
  EXPECT_THAT(EvaluateDeferred(*source_owner, context).matched, IsFalse());
  EXPECT_THAT(fs.events, IsEmpty());
}

TEST_F(PreparedOperandTest, IndexedWorkersReuseIndependentRegexSlotsAndCaptures) {
  ASSERT_OK_AND_ASSIGN(auto command, parser::Parse({".", "-regex", "(fi)(le)", "-rxc", "need(le)"}));
  parser::BindMatchers(command, regex::Grammar::kRe2, parser::CaseMode::kSensitive);
  ASSERT_OK_AND_ASSIGN(auto prepared, PreparedExpression::Prepare(*command.expression));
  auto first = prepared.MakeWorker();
  auto second = prepared.MakeWorker();
  auto moved = std::move(first);
  second = std::move(moved);
  EXPECT_THAT(second.MatcherCount(), Eq(2));
  EXPECT_THAT(second.StorageBytes(), Ge(sizeof(PreparedExpression::Worker)));
  std::vector<std::string> captures;
  EvalContext context{
      .visit = visit,
      .emit = IgnoreOutput,
      .fs = fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .control = control,
      .captures = captures,
  };
  const auto third = prepared.MakeWorker();
  const auto moved_program = std::move(prepared);
  EXPECT_THAT(second.Evaluate(context).matched, IsTrue());
  EXPECT_THAT(captures, ElementsAre("file", "fi", "le"));
  captures.clear();
  context.content.Invalidate();
  EXPECT_THAT(third.Evaluate(context).matched, IsTrue());
  EXPECT_THAT(captures, ElementsAre("file", "fi", "le"));
  context.content.Invalidate();
  EXPECT_THAT(moved_program.Evaluate(context).matched, IsTrue());
}

TEST_F(PreparedOperandTest, WorkerWithoutACompiledMatcherKeepsNoMatchSemantics) {
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({".", "-rxc", "needle"}));
  ASSERT_OK_AND_ASSIGN(const auto prepared, PreparedExpression::Prepare(*command.expression));
  const auto worker = prepared.MakeWorker();
  EXPECT_THAT(worker.MatcherCount(), Eq(1));
  EvalContext context{
      .visit = visit,
      .emit = IgnoreOutput,
      .fs = fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .control = control,
  };
  EXPECT_THAT(worker.Evaluate(context).matched, IsFalse());
  EXPECT_THAT(prepared.Evaluate(context).matched, IsFalse());
  EXPECT_THAT(fs.events, IsEmpty());
}

TEST_F(PreparedOperandTest, TypeListsMatchEveryEntryTypeAndRejectMalformedSuffixes) {
  constexpr auto kTypes = std::to_array({
      vfs::FileType::kUnknown,
      vfs::FileType::kRegular,
      vfs::FileType::kDirectory,
      vfs::FileType::kSymlink,
      vfs::FileType::kBlockDevice,
      vfs::FileType::kCharDevice,
      vfs::FileType::kFifo,
      vfs::FileType::kSocket,
      static_cast<vfs::FileType>(32),
  });
  constexpr auto kArguments = std::to_array<std::string_view>({
      "f",
      "d",
      "l",
      "b",
      "c",
      "p",
      "s",
      "f,d",
      "f,f",
      "",
      "f,garbage",
      "f,",
      ",f",
      "fd",
  });
  for (const auto type : kTypes) {
    metadata.type = type;
    for (const auto argument : kArguments) {
      Check("-type", argument);
      Check("-xtype", argument);
    }
  }
  metadata.type = vfs::FileType::kSymlink;
  fs.target_metadata.emplace(vfs::Metadata{.type = vfs::FileType::kDirectory});
  Check("-xtype", "d");
  Check("-xtype", "f");
  Check("-xtype", "d,garbage");
}

TEST_F(PreparedOperandTest, SizeUnitsRoundingAndDynamicBlocksMatchTheReference) {
  constexpr auto kArguments = std::to_array<std::string_view>({
      "0",
      "1",
      "+1",
      "-1",
      "1b",
      "+1c",
      "-513c",
      "1k",
      "1KiB",
      "1kB",
      "1E",
      "18446744073709551615c",
      "18446744073709551616c",
      "",
      "garbage",
      "1Z",
      "+",
  });
  for (const std::string_view flag : {"-size", "-blocks"}) {
    for (const auto argument : kArguments) {
      Check(flag, argument);
    }
  }
}

TEST_F(PreparedOperandTest, NumericComparisonsRetainMalformedAndUnsignedBoundaryBehavior) {
  constexpr auto kArguments = std::to_array<std::string_view>({
      "0",
      "1",
      "+1",
      "-1",
      "18446744073709551615",
      "18446744073709551616",
      "",
      "garbage",
      "+",
      "1x",
  });
  for (const std::string_view flag : {"-links", "-inum", "-uid", "-gid"}) {
    for (const auto argument : kArguments) {
      Check(flag, argument);
    }
  }
}

TEST_F(PreparedOperandTest, SymbolicOctalAndZeroPermissionMasksMatchTheReference) {
  constexpr auto kArguments = std::to_array<std::string_view>({
      "0644",        "-0644", "/0444",    "+0444", "0",       "-0",  "/0", "+0",   "u+rw,go=r", "+r",
      "u+s,g+s,o+t", "a=X",   "u=rw,u-w", "",      "garbage", "u+z", "u",  "u+r,", "888",
  });
  for (const auto argument : kArguments) {
    Check("-perm", argument);
  }
}

TEST_F(PreparedOperandTest, MissingOperandsStayFalseWithoutObservations) {
  constexpr auto kFlags = std::to_array<std::string_view>({
      "-type",
      "-xtype",
      "-size",
      "-blocks",
      "-links",
      "-inum",
      "-uid",
      "-gid",
      "-perm",
  });
  for (const auto flag : kFlags) {
    Check(flag, "", false);
  }
  EXPECT_THAT(fs.events, IsEmpty());
}

INSTANTIATE_TEST_SUITE_P(
    Executors,
    ExpressionContractTest,
    ::testing::Values(Executor::kTree, Executor::kBound, Executor::kPrepared, Executor::kPreparedWorker),
    [](const ::testing::TestParamInfo<Executor>& info) {
      switch (info.param) {
        case Executor::kTree: return "Tree";
        case Executor::kBound: return "Bound";
        case Executor::kPrepared: return "Prepared";
        case Executor::kPreparedWorker: return "PreparedWorker";
      }
      return "Invalid";
    });

}  // namespace
}  // namespace xff::engine
