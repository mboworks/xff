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

#include "xff/parser/rg.h"

#include <array>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/parser/parser.h"
#include "xff/registry/compatibility.h"

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
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Optional;

struct RgTest : ::testing::Test {};

TEST_F(RgTest, OptionMetadataIsCompleteAndUnambiguous) {
  std::set<std::string_view> names;
  std::set<std::string_view> shorts;
  for (const registry::CompatibilityOption& option : registry::CompatibilityOptions()) {
    SCOPED_TRACE(option.name);
    EXPECT_THAT(option.name, Not(IsEmpty()));
    EXPECT_THAT(option.summary, Not(IsEmpty()));
    EXPECT_THAT(names.insert(option.name).second, IsTrue());
    if (!option.alias.empty()) {
      EXPECT_THAT(shorts.insert(option.alias).second, IsTrue());
    }
    if (option.effect == registry::CompatibilityOption::Effect::kGlobal) {
      EXPECT_THAT(option.replacement, Not(IsEmpty()));
    }
    if (!option.argument.empty()) {
      EXPECT_THAT(
          ParseRg({std::string(option.name)}, 0), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("requires")));
    }
  }
}

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

TEST_F(RgTest, NamedAndPositionalRootsKeepTheirOrderAndNativeNames) {
  ASSERT_OK_AND_ASSIGN(
      const auto command,
      Parse({"--rg", "--root=first=one", "hit", "two", "--root=last=three", "four", "--xff", "-type", "f"}));
  EXPECT_THAT(command.roots, ElementsAre("one", "two", "three", "four"));
  EXPECT_THAT(command.root_names, ElementsAre("first", "", "last", ""));
  EXPECT_THAT(command.globals, Contains("--root=first=one"));
  ASSERT_THAT(command.rg, Optional(_));
  EXPECT_THAT(command.rg.value_or(RgSearch{}).patterns, ElementsAre(Field(&RgPattern::value, "hit")));
  ASSERT_OK_AND_ASSIGN(const auto explicit_pattern, Parse({"--rg", "-ehit", "--root", "only=-name"}));
  EXPECT_THAT(explicit_pattern.roots, ElementsAre("-name"));
  EXPECT_THAT(explicit_pattern.root_names, ElementsAre("only"));
}

TEST_F(RgTest, NamedRootsRetainValidationAndDoNotReplaceTheSearchPattern) {
  EXPECT_THAT(Parse({"--rg", "--root=x=one"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("PATTERN")));
  EXPECT_THAT(
      Parse({"--rg", "hit", "--root=bad"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("NAME=PATH")));
  EXPECT_THAT(
      Parse({"--rg", "hit", "--root=../bad=one"}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("single directory component")));
  EXPECT_THAT(
      Parse({"--rg", "hit", "--root=x=one", "--root=x=two"}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("duplicate root name")));
  EXPECT_THAT(
      Parse({"--rg", "hit", "--xff", "--root=x=one"}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("put search paths before")));
}

TEST_F(RgTest, StdinCannotSupplyBothPatternsAndSearchedContent) {
  EXPECT_THAT(
      Parse({"--rg", "-f", "-", "-"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("stdin simultaneously")));
  EXPECT_THAT(
      Parse({"--rg", "-f", "-", "--root=input=-"}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("stdin simultaneously")));
  EXPECT_THAT(Parse({"--rg", "-f", "-"}), IsOk());
  EXPECT_THAT(Parse({"--rg", "-f", "patterns", "-"}), IsOk());
  EXPECT_THAT(Parse({"--rg", "-e", "-", "-"}), IsOk());
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

TEST_F(RgTest, ZeroThreadsSelectTheAutomaticAllowanceWithoutChangingNativeJobs) {
  for (const std::string_view flag : {"-j0", "-j00", "--threads=0"}) {
    ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", std::string(flag), "hit"}));
    EXPECT_THAT(command.globals, Contains("--jobs=all"));
  }
  ASSERT_OK_AND_ASSIGN(const auto separate, Parse({"--rg", "--threads", "0", "hit"}));
  EXPECT_THAT(separate.globals, Contains("--jobs=all"));
  ASSERT_OK_AND_ASSIGN(const auto native, Parse({"--rg", "--jobs=0", "hit"}));
  EXPECT_THAT(native.globals, Contains("--jobs=0"));
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

TEST_F(RgTest, PlusUsesTheResolvedStyleIncludingNestedExpressions) {
  ASSERT_OK_AND_ASSIGN(const auto native, Parse({".", "-true", "+", "-false"}));
  EXPECT_THAT(EnforceStyle(native, registry::Style::kXff), IsOk());
  EXPECT_THAT(
      EnforceStyle(native, registry::Style::kFind),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("'+' is an xff extension")));
  ASSERT_OK_AND_ASSIGN(const auto explicit_xff, Parse({"--xff", ".", "-true", "+", "-false"}));
  EXPECT_THAT(EnforceStyle(explicit_xff, registry::Style::kXff), IsOk());
  ASSERT_OK_AND_ASSIGN(const auto nested, Parse({".", "-false", "-o", "(", "-true", "+", "-false", ")"}));
  EXPECT_THAT(
      EnforceStyle(nested, registry::Style::kFind),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("'+' is an xff extension")));
  ASSERT_OK_AND_ASSIGN(const auto left_nested, Parse({".", "(", "-true", "+", "-false", ")", "-o", "-false"}));
  EXPECT_THAT(EnforceStyle(left_nested, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(const auto find_or, Parse({".", "-name", "+", "-or", "-true"}));
  EXPECT_THAT(EnforceStyle(find_or, registry::Style::kFind), IsOk());
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
  EXPECT_THAT(Parse({"--rg", "-J", "x"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("unsupported rg")));
}

TEST_F(RgTest, RejectsModeValuesAndRootsAfterTheNativeBoundary) {
  EXPECT_THAT(Parse({"--rg=no", "root"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("do not take")));
  EXPECT_THAT(Parse({"--xff=yes", "root"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("do not take")));
  EXPECT_THAT(
      Parse({"--rg", "x", "--xff", "extra-root"}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("filter expression")));
  EXPECT_THAT(Parse({"--rg", "--help=rg"}), IsOk());
}

TEST_F(RgTest, ModeReentryPreservesSearchAndNativeVocabulary) {
  EXPECT_THAT(Parse({"root", "--rg", "x"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("must precede")));
  EXPECT_THAT(Parse({"--rg", "x", "--rg"}), IsOk());
  ASSERT_OK_AND_ASSIGN(
      const auto command,
      Parse({"--rg", "x", "src", "--xff", "-type", "f", "--rg", "-tcpp", "-o", "--xff", "-name", "*.h"}));
  EXPECT_THAT(command.globals, Contains("--file-type=cpp"));
  EXPECT_THAT(command.globals, Contains("--only-matching"));
  EXPECT_THAT(command.roots, ElementsAre("src"));
  ASSERT_THAT(command.expression, NotNull());
  EXPECT_THAT(command.expression->kind, Expr::Kind::kAnd);
  EXPECT_THAT(Parse({"--rg", "--help"}), IsOk());
}

TEST_F(RgTest, ReentryPreservesLiteralPrimaryArgumentsAndSettingsOrder) {
  ASSERT_OK_AND_ASSIGN(
      const auto command, Parse({
                              "--rg",
                              "hit",
                              "tree",
                              "--xff",
                              "-name",
                              "--rg",
                              "-o",
                              "-name",
                              "--xff",
                              "--type-add=mine:*.one",
                              "--rg",
                              "--type-clear=mine",
                              "--type-add=mine:*.two",
                              "-tmine",
                              "--xff",
                              "-rxc",
                              "--rg",
                              "--rg",
                              "-N",
                          }));
  EXPECT_THAT(command.roots, ElementsAre("tree"));
  EXPECT_THAT(
      command.globals, ElementsAre(
                           "--config=rg", "--match-output", "--exit-match", "--type-add=mine:*.one",
                           "--type-clear=mine", "--type-add=mine:*.two", "--file-type=mine", "--no-line-number"));
  ASSERT_THAT(command.expression, NotNull());
  EXPECT_THAT(command.expression->kind, Expr::Kind::kOr);
}

TEST_F(RgTest, ReentryDoesNotInterpretCommandOrCaptureArguments) {
  const auto primaries = std::to_array<std::string_view>({"-exec", "-capture:result"});
  for (const auto primary : primaries) {
    ASSERT_OK_AND_ASSIGN(
        const auto command, Parse({
                                "--rg",
                                "hit",
                                "tree",
                                "--xff",
                                std::string(primary),
                                "echo",
                                "--rg",
                                "--xff",
                                ";",
                                "--rg",
                                "-n",
                            }));
    EXPECT_THAT(command.globals, Contains("--line-number"));
    ASSERT_THAT(command.expression, NotNull());
    EXPECT_THAT(command.expression->args, Contains("--rg"));
    EXPECT_THAT(command.expression->args, Contains("--xff"));
  }
}

TEST_F(RgTest, CompatibilityControlsAreTypedAndLastOverrideWins) {
  ASSERT_OK_AND_ASSIGN(
      const auto command, Parse(
                              {"--rg", "-U", "--no-multiline", "--multiline-dotall", "--no-multiline-dotall",
                               "--no-unicode", "--unicode", "--heading", "--no-heading", "--column", "--no-column",
                               "-xw", "-tcpp", "-Tpy", "--type-add=local:*.loc", "--type-clear=local", "hit"}));
  ASSERT_THAT(command.rg, Optional(_));
  const auto search = command.rg.value_or(RgSearch{});
  EXPECT_THAT(search.multiline, Eq(false));
  EXPECT_THAT(search.dotall, Eq(false));
  EXPECT_THAT(search.unicode, Eq(true));
  EXPECT_THAT(search.unicode_explicit, Eq(true));
  EXPECT_THAT(search.heading, Optional(false));
  EXPECT_THAT(search.column, Eq(false));
  EXPECT_THAT(search.word, Eq(true));
  EXPECT_THAT(search.line, Eq(false));
  EXPECT_THAT(command.globals, Contains("--file-type=cpp"));
  EXPECT_THAT(command.globals, Contains("--file-type-not=py"));
  EXPECT_THAT(command.globals, Contains("--type-add=local:*.loc"));
  EXPECT_THAT(command.globals, Contains("--type-clear=local"));
}

TEST_F(RgTest, TypeListingWithoutPatternUsesTheSharedGlobal) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", "--type-list"}));
  EXPECT_THAT(command.globals, Contains("--type-list"));
  EXPECT_THAT(command.rg, Optional(Field(&RgSearch::type_list, true)));
}

}  // namespace
}  // namespace xff::parser
