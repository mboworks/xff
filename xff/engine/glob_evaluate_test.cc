// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <fnmatch.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;

struct GlobEvaluateTest : ::testing::Test {
  void Check(const std::vector<std::string>& patterns, const std::vector<std::string>& texts) const {
    static constexpr auto kPrimaries = std::to_array<std::string_view>({
        "-name",
        "-iname",
        "-path",
        "-ipath",
        "-wholename",
        "-iwholename",
    });
    for (const std::string_view primary : kPrimaries) {
      for (const std::string& pattern : patterns) {
        MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({".", std::string(primary), pattern}));
        const auto& expression = *command.expression;
        MBO_ASSERT_OK_AND_ASSIGN(auto prepared, PreparedExpression::Prepare(expression));
        const auto worker = prepared.MakeWorker();
        // Operands borrow the immutable AST, not the movable prepared-plan object.
        const auto moved = std::move(prepared);
        const auto second_worker = moved.MakeWorker();
        for (const std::string& text : texts) {
          for (const bool fold : {false, true}) {
            SCOPED_TRACE(primary);
            SCOPED_TRACE(pattern);
            SCOPED_TRACE(text);
            // Neither view is NUL-terminated at its end. The suffix must never be matched.
            const std::string backing = text + "unrelated suffix";
            const std::string_view slice(backing.data(), text.size());
            const vfs::Metadata metadata{.type = vfs::FileType::kRegular};
            const Visit visit{.path = slice, .name = slice, .metadata = metadata};
            // NOLINTNEXTLINE(misc-const-correctness): EvalContext mutates this control.
            Control control;
            const auto emit = [](std::string_view) {};
            EvalContext context{
                .visit = visit,
                .emit = emit,
                .fs = fs,
                .now = absl::UnixEpoch(),
                .tz = absl::UTCTimeZone(),
                .fold_name_case = fold,
                .control = control,
            };
            const int flags = fold || expression.descriptor->fold_case ? FNM_CASEFOLD : 0;
            const bool expected = ::fnmatch(pattern.c_str(), text.c_str(), flags) == 0;
            EXPECT_THAT(Evaluate(expression, context), Eq(expected));
            EXPECT_THAT(worker.Evaluate(context).matched, Eq(expected));
            EXPECT_THAT(worker.EvaluateIterative(context).matched, Eq(expected));
            EXPECT_THAT(second_worker.Evaluate(context).matched, Eq(expected));
          }
        }
      }
    }
  }

  vfs::LocalFs fs;
};

TEST_F(GlobEvaluateTest, SimpleAndComplexPatternsAgreeWithNativeFnmatch) {
  const std::vector<std::string> patterns{
      "",
      "*",
      "**",
      "***",
      "a",
      "abc",
      "a*",
      "*c",
      "*b*",
      "a*c",
      "ab*bc",
      "a**c",
      "*a*b*",
      "?",
      "a?c",
      "[ab]",
      "[!a]",
      "[a-z]*",
      "[[:alpha:]]*",
      "[",
      "[]",
      "a]",
      R"(a\*)",
      R"(a\?)",
      R"(a\[)",
      R"(a\)",
      "*/a",
      "a/*",
      "a/*/b",
      "*/.*",
      "*.txt",
      "a*b*c",
      "*abc*",
      std::string(160, 'a') + "*",
  };
  const std::vector<std::string> texts{
      "",
      "a",
      "A",
      "b",
      "abc",
      "abBc",
      "abbc",
      "ababc",
      "a*",
      "a?",
      "a[",
      "a]",
      "a\\",
      ".",
      "..",
      ".abc",
      "a/b",
      "a/.b",
      "a/x/b",
      "a/x/y/b",
      "/a",
      "file.txt",
      "FILE.TXT",
      "a b",
      "a\nb",
      std::string(160, 'a'),
      std::string(170, 'a'),
  };
  Check(patterns, texts);
}

TEST_F(GlobEvaluateTest, MissingPatternRemainsANonMatchInAnInProcessExpression) {
  const vfs::Metadata metadata{.type = vfs::FileType::kRegular};
  const Visit visit{.path = "file.txt", .name = "file.txt", .metadata = metadata};
  // NOLINTNEXTLINE(misc-const-correctness): EvalContext mutates this control.
  Control control;
  const auto emit = [](std::string_view) {};
  EvalContext context{
      .visit = visit,
      .emit = emit,
      .fs = fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .control = control,
  };
  for (const std::string_view primary : {"-name", "-path"}) {
    MBO_ASSERT_OK_AND_ASSIGN(auto command, parser::Parse({".", std::string(primary), "unused"}));
    command.expression->args.clear();
    MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PreparedExpression::Prepare(*command.expression));
    EXPECT_THAT(Evaluate(*command.expression, context), IsFalse());
    EXPECT_THAT(prepared.Evaluate(context).matched, IsFalse());
  }
}

TEST_F(GlobEvaluateTest, EmbeddedNulUsesTheExistingCStringBoundary) {
  const std::vector<std::string> patterns{
      std::string("a\0ignored", 9),
      std::string("*\0ignored", 9),
      std::string("?\0ignored", 9),
  };
  const std::vector<std::string> texts{
      "", "a", "ab", std::string("a\0tail", 6), std::string("b\0tail", 6),
  };
  Check(patterns, texts);
}

}  // namespace
}  // namespace xff::engine
