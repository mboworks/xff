// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_contract.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
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

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    events.push_back("readdir " + std::string(path));
    return std::vector<vfs::Entry>{};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    events.push_back("stat " + std::string(path));
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

enum class Executor { kTree, kBound };

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
  }
}

TEST_P(ExpressionContractTest, AnUnregisteredHandlerIsRejectedRatherThanSilentlyAcceptingIt) {
  const registry::Descriptor descriptor{.name = "unregistered", .needs_metadata = false};
  const parser::Expr expression{.kind = parser::Expr::Kind::kPredicate, .descriptor = descriptor};
  EXPECT_THAT(
      BoundExpression::Prepare(expression),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("missing engine evaluation binding")));
}

TEST_P(ExpressionContractTest, BoundPreparationRejectsMalformedInputBeforeObservations) {
  const parser::Expr incomplete{.kind = parser::Expr::Kind::kAnd};
  EXPECT_THAT(BoundExpression::Prepare(incomplete), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("shape")));
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

INSTANTIATE_TEST_SUITE_P(
    Executors,
    ExpressionContractTest,
    ::testing::Values(Executor::kTree, Executor::kBound),
    [](const ::testing::TestParamInfo<Executor>& info) { return info.param == Executor::kTree ? "Tree" : "Bound"; });

}  // namespace
}  // namespace xff::engine
