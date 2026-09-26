// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <array>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/parser/parser.h"

namespace xff::parser {
namespace {
using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::_;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsTrue;
using ::testing::NotNull;
using ::testing::Optional;

struct RgTest : ::testing::Test {};

TEST_F(RgTest, SearchAndNativeFilterAreIndependent) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", "-n", "TODO", "src", "--xff", "-rxc", "license"}));
  ASSERT_THAT(command.rg, Optional(_));
  const auto search = command.rg.value_or(RgSearch{});
  EXPECT_THAT(search.patterns, ElementsAre(Field(&RgPattern::value, "TODO")));
  EXPECT_THAT(command.roots, ElementsAre("src"));
  ASSERT_THAT(command.expression, NotNull());
  EXPECT_THAT(command.expression->args, ElementsAre("license"));
  EXPECT_THAT(command.globals, Contains("--config=rg"));
  EXPECT_THAT(command.globals, Contains("--line-number"));
}

TEST_F(RgTest, NativeSwitchIsNoOpAndConfigDoesNotSelectGrammar) {
  ASSERT_OK_AND_ASSIGN(const auto native, Parse({"--xff", "src", "--xff", "-name", "*.cc"}));
  EXPECT_THAT(native.rg, Eq(std::nullopt));
  EXPECT_THAT(native.roots, ElementsAre("src"));
  ASSERT_OK_AND_ASSIGN(const auto preset, Parse({"--config=rg", "src", "-rxc", "TODO"}));
  EXPECT_THAT(preset.rg, Eq(std::nullopt));
}

TEST_F(RgTest, OptionsOwnTheirArguments) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", "-e", "--xff", "-f", "--rg", "root"}));
  ASSERT_THAT(command.rg, Optional(_));
  const auto search = command.rg.value_or(RgSearch{});
  EXPECT_THAT(search.patterns, ElementsAre(Field(&RgPattern::value, "--xff"), Field(&RgPattern::value, "--rg")));
  EXPECT_THAT(search.patterns.back().file, IsTrue());
  EXPECT_THAT(command.roots, ElementsAre("root"));
  ASSERT_OK_AND_ASSIGN(const auto native, Parse({".", "-exec", "echo", "--rg", "--xff", ";"}));
  EXPECT_THAT(native.expression->args, ElementsAre("echo", "--rg", "--xff"));
}

TEST_F(RgTest, DelimiterMakesModeTokensAndOperatorsLiteralPaths) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", "--", "--xff", "+", "-name", "--rg"}));
  ASSERT_THAT(command.rg, Optional(_));
  const auto search = command.rg.value_or(RgSearch{});
  EXPECT_THAT(search.patterns, ElementsAre(Field(&RgPattern::value, "--xff")));
  EXPECT_THAT(command.roots, ElementsAre("+", "-name", "--rg"));
}

TEST_F(RgTest, ExplicitPatternsMakeEveryPositionalARootRegardlessOfOrder) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", "root", "-eone", "other", "--regexp=two"}));
  ASSERT_THAT(command.rg, Optional(_));
  const auto search = command.rg.value_or(RgSearch{});
  EXPECT_THAT(search.patterns, ElementsAre(Field(&RgPattern::value, "one"), Field(&RgPattern::value, "two")));
  EXPECT_THAT(command.roots, ElementsAre("root", "other"));
}

TEST_F(RgTest, AttachedValuesBundlesAndConflictingShortOptions) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", "-nio", "-M120", "-PL", "-H", "-g*.cc", "-j4", "-C2", "x"}));
  ASSERT_THAT(command.rg, Optional(_));
  EXPECT_THAT(command.globals, Contains("--only-matching"));
  EXPECT_THAT(command.globals, Contains("--case=insensitive"));
  EXPECT_THAT(command.globals, Contains("--regextype=PCRE2"));
  EXPECT_THAT(command.globals, Contains("-L"));
  EXPECT_THAT(command.globals, Contains("--with-filename"));
  EXPECT_THAT(command.globals, Contains("--include=*.cc"));
  EXPECT_THAT(command.globals, Contains("--jobs=4"));
  EXPECT_THAT(command.globals, Contains("--context=2"));
  const auto search = command.rg.value_or(RgSearch{});
  EXPECT_THAT(search.max_columns, Eq(120));
}

TEST_F(RgTest, PlusIsNativeOrButAnOrdinaryRgPattern) {
  ASSERT_OK_AND_ASSIGN(const auto rg, Parse({"--rg", "-F", "+", "root", "--xff", "-name", "a", "+", "-name", "b"}));
  ASSERT_THAT(rg.rg, Optional(_));
  const auto search = rg.rg.value_or(RgSearch{});
  EXPECT_THAT(search.patterns, ElementsAre(Field(&RgPattern::value, "+")));
  EXPECT_THAT(rg.expression->kind, Eq(Expr::Kind::kOr));
  ASSERT_OK_AND_ASSIGN(const auto native, Parse({".", "-name", "+", "-o", "-true"}));
  EXPECT_THAT(native.expression->lhs->args, ElementsAre("+"));
  ASSERT_OK_AND_ASSIGN(const auto exec, Parse({".", "-exec", "echo", "{}", "+"}));
  EXPECT_THAT(exec.expression->exec_batch, IsTrue());
}

TEST_F(RgTest, RejectsMissingValuesAndUnsupportedShorts) {
  constexpr auto options = std::to_array({"-e", "-f", "-g", "-M", "-A", "-B", "-C", "-j", "--regexp", "--file"});
  for (const auto* const option : options) {
    EXPECT_THAT(Parse({"--rg", option}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("requires"))) << option;
  }
  EXPECT_THAT(Parse({"--rg"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("requires PATTERN")));
  EXPECT_THAT(Parse({"--rg", "-Mno", "x"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("integer")));
  EXPECT_THAT(
      Parse({"--rg", "--count=3", "x"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("does not take")));
  EXPECT_THAT(Parse({"--rg", "-type", "x"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("unsupported rg")));
}

TEST_F(RgTest, RejectsModeValuesAndRootsAfterTheNativeBoundary) {
  EXPECT_THAT(Parse({"--rg=no", "root"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("do not take")));
  EXPECT_THAT(Parse({"--xff=yes", "root"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("do not take")));
  EXPECT_THAT(
      Parse({"--rg", "x", "--xff", "extra-root"}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("filter expression")));
  EXPECT_THAT(Parse({"--rg", "--help=rg"}), IsOk());
}

TEST_F(RgTest, RejectsLateSelectionAndReentry) {
  EXPECT_THAT(Parse({"root", "--rg", "x"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("must precede")));
  EXPECT_THAT(Parse({"--rg", "x", "--rg"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("once")));
  EXPECT_THAT(
      Parse({"--rg", "x", "--xff", "-true", "--rg"}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("re-entry")));
  EXPECT_THAT(Parse({"--rg", "--help"}), IsOk());
}
}  // namespace
}  // namespace xff::parser
