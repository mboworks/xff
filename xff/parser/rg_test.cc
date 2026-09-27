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
#include "xff/parser/rg_types.h"

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
using ::testing::SizeIs;

struct RgTest : ::testing::Test {};

TEST_F(RgTest, OptionMetadataIsCompleteAndUnambiguous) {
  std::set<std::string_view> names;
  std::set<char> shorts;
  for (const RgOption& option : RgOptions()) {
    SCOPED_TRACE(option.name);
    EXPECT_THAT(option.name, Not(IsEmpty()));
    EXPECT_THAT(option.summary, Not(IsEmpty()));
    EXPECT_THAT(names.insert(option.name).second, IsTrue());
    if (option.short_name != '\0') {
      EXPECT_THAT(shorts.insert(option.short_name).second, IsTrue());
    }
    if (option.effect == RgOption::Effect::kGlobal) {
      EXPECT_THAT(option.replacement, Not(IsEmpty()));
    }
    if (!option.argument.empty()) {
      EXPECT_THAT(
          ParseRg({"--" + std::string(option.name)}, 0),
          StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("requires")));
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

TEST_F(RgTest, RejectsLateSelectionAndReentry) {
  EXPECT_THAT(Parse({"root", "--rg", "x"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("must precede")));
  EXPECT_THAT(Parse({"--rg", "x", "--rg"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("once")));
  EXPECT_THAT(
      Parse({"--rg", "x", "--xff", "-true", "--rg"}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("re-entry")));
  EXPECT_THAT(Parse({"--rg", "--help"}), IsOk());
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
  EXPECT_THAT(search.types, SizeIs(4));
}

TEST_F(RgTest, DefaultTypesUseCanonicalLanguagesAndSuffixAliases) {
  for (const std::string_view name : {"cpp", "C++", "CPP"}) {
    SCOPED_TRACE(name);
    ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", "-t", std::string(name), "hit"}));
    ASSERT_OK_AND_ASSIGN(const auto types, RgTypes::Compile(command.rg.value_or(RgSearch{})));
    EXPECT_THAT(types.Includes("main.cc"), Eq(true));
    EXPECT_THAT(types.Includes("main.CPP"), Eq(true));
    EXPECT_THAT(types.Includes("main.h"), Eq(true));
    EXPECT_THAT(types.Includes("main.py"), Eq(false));
  }
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", "--type-list"}));
  ASSERT_OK_AND_ASSIGN(const auto types, RgTypes::Compile(command.rg.value_or(RgSearch{})));
  EXPECT_THAT(types.Listing(), HasSubstr("cpp: language C++\n"));
  EXPECT_THAT(types.Listing(), HasSubstr("py: language Python\n"));
}

TEST_F(RgTest, ClearAndAddApplyToEveryAliasOfTheSameType) {
  ASSERT_OK_AND_ASSIGN(
      const auto command, Parse({"--rg", "--type-clear=cpp", "--type-add=C++:*.local", "-tcc", "hit"}));
  ASSERT_OK_AND_ASSIGN(const auto types, RgTypes::Compile(command.rg.value_or(RgSearch{})));
  EXPECT_THAT(types.Includes("main.local"), Eq(true));
  EXPECT_THAT(types.Includes("main.cc"), Eq(false));
  ASSERT_OK_AND_ASSIGN(const auto cleared, Parse({"--rg", "--type-clear=C++", "-tcpp", "hit"}));
  EXPECT_THAT(RgTypes::Compile(cleared.rg.value_or(RgSearch{})), StatusIs(absl::StatusCode::kInvalidArgument, _));
}

TEST_F(RgTest, TypeSelectionUsesGlobsAndOrderedOverrides) {
  ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", "-tcpp", "-Tc", "hit"}));
  ASSERT_OK_AND_ASSIGN(const auto types, RgTypes::Compile(command.rg.value_or(RgSearch{})));
  EXPECT_THAT(types.Includes("one.cc"), Eq(true));
  EXPECT_THAT(types.Includes("one.h"), Eq(false));
  EXPECT_THAT(types.Includes("one.py"), Eq(false));
  ASSERT_OK_AND_ASSIGN(const auto reverse, Parse({"--rg", "-Tc", "-tcpp", "hit"}));
  ASSERT_OK_AND_ASSIGN(const auto reversed, RgTypes::Compile(reverse.rg.value_or(RgSearch{})));
  EXPECT_THAT(reversed.Includes("one.h"), Eq(true));
}

TEST_F(RgTest, TypeDefinitionsCanBeImportedClearedAndListedWithoutPattern) {
  ASSERT_OK_AND_ASSIGN(
      const auto command, Parse(
                              {"--rg", "--type-clear=cpp", "--type-add=cpp:*.one", "--type-add=local:include:cpp,py",
                               "--type-add=local:*.{two,three}", "--type-list", "-tlocal"}));
  ASSERT_OK_AND_ASSIGN(const auto types, RgTypes::Compile(command.rg.value_or(RgSearch{})));
  EXPECT_THAT(types.Listing(), HasSubstr("cpp: *.one\n"));
  EXPECT_THAT(types.Includes("one.cc"), Eq(false));
  EXPECT_THAT(types.Includes("one.one"), Eq(true));
  EXPECT_THAT(types.Includes("one.py"), Eq(true));
  EXPECT_THAT(types.Includes("one.two"), Eq(true));
  EXPECT_THAT(types.Includes("one.three"), Eq(true));
  ASSERT_OK_AND_ASSIGN(const auto all, Parse({"--rg", "-tall", "hit"}));
  ASSERT_OK_AND_ASSIGN(const auto all_types, RgTypes::Compile(all.rg.value_or(RgSearch{})));
  EXPECT_THAT(all_types.Includes("main.cc"), Eq(true));
  EXPECT_THAT(all_types.Includes("unrecognized.zznotatype"), Eq(false));
  ASSERT_OK_AND_ASSIGN(const auto none, Parse({"--rg", "-Tall", "hit"}));
  ASSERT_OK_AND_ASSIGN(const auto other_types, RgTypes::Compile(none.rg.value_or(RgSearch{})));
  EXPECT_THAT(other_types.Includes("main.cc"), Eq(false));
  EXPECT_THAT(other_types.Includes("unrecognized.zznotatype"), Eq(true));
}

TEST_F(RgTest, InvalidTypeSpecificationsFailBeforeTraversal) {
  const std::vector<std::string> invalid{
      "-tunknown",
      "-th",
      "--type-add=bad-name:*.bad",
      "--type-add=:x",
      "--type-add=name",
      "--type-add=name:",
      "--type-add=all:*.all",
      "--type-add=name:include:missing"};
  for (const auto& arg : invalid) {
    SCOPED_TRACE(arg);
    ASSERT_OK_AND_ASSIGN(const auto command, Parse({"--rg", arg, "hit"}));
    EXPECT_THAT(RgTypes::Compile(command.rg.value_or(RgSearch{})), StatusIs(absl::StatusCode::kInvalidArgument, _));
  }
}

}  // namespace
}  // namespace xff::parser
