// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/expression_execution.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::StatusIs;
using ::testing::_;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsTrue;
using ::testing::PrintToString;

// All input and failures are synthetic. Even deletion tests cannot reach host mutation APIs.
struct ExecutionFs final : vfs::FileSystem {
  mutable std::atomic<std::size_t> stats = 0;
  mutable std::atomic<std::size_t> reads = 0;
  mutable std::atomic<std::size_t> removes = 0;
  bool fail_metadata = false;
  bool fail_content = false;

  static bool Directory(std::string_view path) {
    return path == "root" || path == "left" || path == "right" || path.ends_with("/sub");
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    if (!Directory(path)) {
      return absl::NotFoundError("missing fixture directory");
    }
    std::vector<vfs::Entry> entries;
    const std::size_t count = path.ends_with("/sub") ? 4 : 128;
    entries.reserve(count + 1);
    for (std::size_t index = 0; index < count; ++index) {
      const std::string name = absl::StrCat("file", index, index % 2 == 0 ? ".txt" : ".cc");
      entries.push_back({.path = absl::StrCat(path, "/", name), .name = name, .type = vfs::FileType::kRegular});
    }
    if (!path.ends_with("/sub")) {
      entries.push_back({.path = absl::StrCat(path, "/sub"), .name = "sub", .type = vfs::FileType::kDirectory});
    }
    return entries;
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    ++stats;
    if (!Directory(path) && fail_metadata) {
      return absl::PermissionDeniedError("fixture stat failure");
    }
    return vfs::Metadata{
        .type = Directory(path) ? vfs::FileType::kDirectory : vfs::FileType::kRegular,
        .size = 7,
        .mode = 0644,
        .ino = path.ends_with("/sub") ? 2U : 1U,
    };
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    ++reads;
    if (fail_content) {
      return absl::PermissionDeniedError("fixture content failure");
    }
    return path.ends_with(".txt") ? "needle\n" : "absent\n";
  }

  absl::Status Remove(std::string_view) const override {
    ++removes;
    return absl::PermissionDeniedError("fixture mutation denied");
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::NotFoundError("not a link"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }
};

struct Observation {
  RunResult result;
  std::string output;
  std::vector<std::string> errors;
  std::size_t stats = 0;
  std::size_t reads = 0;
  std::size_t removes = 0;
};

struct ExpressionExecutionTest : ::testing::TestWithParam<ExpressionExecutor> {
  static Observation Observe(const parser::Command& command, ExpressionExecutor executor, bool failure = false) {
    ExecutionFs fs;
    fs.fail_metadata = failure;
    fs.fail_content = failure;
    Observation observed;
    observed.result = RunFind(
        command, fs, [&](std::string_view text) { observed.output.append(text); },
        [&](std::string_view path, absl::Status status) {
          observed.errors.push_back(absl::StrCat(path, ": ", status.ToString()));
        },
        std::nullopt, 0, executor);
    std::ranges::sort(observed.errors);
    observed.stats = fs.stats.load();
    observed.reads = fs.reads.load();
    observed.removes = fs.removes.load();
    return observed;
  }

  static void Check(const std::vector<std::string>& arguments, bool failure = false, bool expect_error = false) {
    SCOPED_TRACE(PrintToString(arguments));
    std::vector<std::string> args{"--exact", "--color=never", "--sort=dir", "--jobs=1"};
    args.insert(args.end(), arguments.begin(), arguments.end());
    ASSERT_OK_AND_ASSIGN(auto command, parser::Parse(args));
    parser::BindMatchers(
        command, parser::GrammarFromGlobals(command.globals),
        parser::ResolveCaseMode(command.globals, registry::Style::kXff));
    const auto expected = Observe(command, ExpressionExecutor::kTree, failure);
    const auto actual = Observe(command, GetParam(), failure);
    if (expect_error) {
      EXPECT_THAT(expected.result.errors, Gt(0));
    } else {
      EXPECT_THAT(expected.result.errors, Eq(0)) << PrintToString(expected.errors);
    }
    EXPECT_THAT(actual.result.errors, Eq(expected.result.errors));
    EXPECT_THAT(actual.result.any_match, Eq(expected.result.any_match));
    EXPECT_THAT(actual.output, EqualsText(expected.output));
    EXPECT_THAT(actual.errors, Eq(expected.errors));
    EXPECT_THAT(actual.stats, Eq(expected.stats));
    EXPECT_THAT(actual.reads, Eq(expected.reads));
    EXPECT_THAT(actual.removes, Eq(expected.removes));
  }
};

TEST_P(ExpressionExecutionTest, SelectionOutputSummariesAndComparisonPreserveTheWholeRun) {
  const std::vector<std::vector<std::string>> cases{
      {"root"},
      {"root", "-type", "f", "-name", "*.txt"},
      {"root", "-type", "f", "-size", "+2c", "-perm", "0644", "-printf", "%p\\n"},
      {"--summary=ext", "root", "-type", "f"},
      {"--compare=summary", "--summary=ext", "left", "right", "-type", "f"},
      {"root", "-first", "3", "-print"},
      {"root", "-fuzzy", "file", "-top", "3", "-print"},
      {
          "root",
          "-type",
          "f",
          "-fuzzy",
          "file",
          "-printf",
          "prefix\\n",
          "-top",
          "3",
          "-printf",
          "middle\\n",
          "-top",
          "1",
          "-print",
      },
      {"root", "-name", "sub", "-prune", "-o", "-type", "f", "-print"},
      {"root", "-type", "f", "-print", "-quit"},
      {"root", "-false", "-a", "-printf", "unreachable"},
      {"root", "-false", "-a", "-prune"},
      {"root", "-true", "-o", "-delete"},
  };
  for (const auto& arguments : cases) {
    SCOPED_TRACE(PrintToString(arguments));
    Check(arguments);
  }
}

TEST_P(ExpressionExecutionTest, SerialAndPooledRegexFilteringUseTheSameOrderedOutput) {
  for (const std::string_view jobs : {"--jobs=1", "--jobs=4"}) {
    for (const std::string_view primary : {"-content", "-rxc"}) {
      Check({std::string(jobs), "root", "-type", "f", std::string(primary), "needle", "-print"});
      Check({std::string(jobs), "-M", "root", "-type", "f", std::string(primary), "needle"});
    }
    Check({std::string(jobs), "root", "-name", "*.txt", "--rg", "needle", "--xff"});
    Check({std::string(jobs), "root", "-type", "f", "-rxc", "needle", "--summary=ext"});
  }
}

TEST_P(ExpressionExecutionTest, ConditionalFailuresAndBlockedEffectsRemainObservable) {
  Check({"root", "-false", "-size", "+1c"}, true);
  Check({"root", "-type", "f", "!", "-size", "+1c", ",", "-printf", "later"}, true, true);
  Check({"root", "-type", "f", "!", "-content", "needle", ",", "-printf", "later"}, true, true);
  Check({"root", "-false", "-size", "garbage"}, false, true);
  Check({"--block-file-deletion", "root", "-delete"}, false, true);
  // File-only blocks remain entry-dependent: a skipped delete observes no blocked file.
  Check({"--block-file-deletion", "root", "-false", "-a", "-delete"});
  Check({"--safe", "root", "-false", "-a", "-delete"}, false, true);
  Check({"--dry-run", "root", "-name", "file0.txt", "-delete"});
  Check({"root", "-name", "file0.txt", "-delete"}, false, true);
}

TEST_P(ExpressionExecutionTest, MovingPlanAndWorkerPreservesBorrowedStorage) {
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-type", "f"}));
  ASSERT_OK_AND_ASSIGN(auto execution, ExpressionExecution::Prepare(*command.expression, GetParam()));
  auto worker = execution.MakeWorker();
  const auto moved_execution = std::move(execution);
  const auto moved_worker = std::move(worker);
  EXPECT_THAT(
      moved_execution.UsesIndexedMatchers(),
      Eq(GetParam() != ExpressionExecutor::kTree && GetParam() != ExpressionExecutor::kBound));
  const ExecutionFs fs;
  const Visit visit{.path = "root/file.txt", .metadata = {.type = vfs::FileType::kRegular}, .fs = fs};
  Control control;
  const auto discard = [](std::string_view) {};
  EvalContext context{
      .visit = visit,
      .emit = discard,
      .fs = fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .control = control,
  };
  EXPECT_THAT(moved_worker.Evaluate(context).matched, IsTrue());
}

TEST_P(ExpressionExecutionTest, InvalidPreparationFailsBeforeAnyTraversal) {
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-true"}));
  EXPECT_THAT(
      ExpressionExecution::Prepare(*command.expression, static_cast<ExpressionExecutor>(-1)),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("executor")));
  const auto result = Observe(command, static_cast<ExpressionExecutor>(-1));
  EXPECT_THAT(result.result.errors, Eq(2));
  EXPECT_THAT(result.stats, Eq(0));
  if (GetParam() != ExpressionExecutor::kTree) {
    const parser::Expr invalid{.kind = parser::Expr::Kind::kAnd};
    EXPECT_THAT(ExpressionExecution::Prepare(invalid, GetParam()), StatusIs(absl::StatusCode::kInvalidArgument, _));
  }
}

INSTANTIATE_TEST_SUITE_P(
    Executor,
    ExpressionExecutionTest,
    ::testing::Values(
        ExpressionExecutor::kTree,
        ExpressionExecutor::kBound,
        ExpressionExecutor::kPrepared,
        ExpressionExecutor::kProgramSwitch,
        ExpressionExecutor::kProgramFunctions,
        ExpressionExecutor::kProgramOptimized),
    [](const ::testing::TestParamInfo<ExpressionExecutor>& info) {
      switch (info.param) {
        case ExpressionExecutor::kTree: return "Tree";
        case ExpressionExecutor::kBound: return "Bound";
        case ExpressionExecutor::kPrepared: return "Prepared";
        case ExpressionExecutor::kProgramSwitch: return "ProgramSwitch";
        case ExpressionExecutor::kProgramFunctions: return "ProgramFunctions";
        case ExpressionExecutor::kProgramOptimized: return "ProgramOptimized";
      }
      return "Invalid";
    });

}  // namespace
}  // namespace xff::engine
