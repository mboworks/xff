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

#include "xff/config/ini.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/env/env.h"

namespace xff::config {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::SizeIs;

struct IniTest : ::testing::Test {
  void SetUp() override {
    env::ClearForTesting();
    env::SetForTesting("XFF_INI_VALUE", "set");
    env::SetForTesting("XFF_INI_EMPTY", "");
    env::SetForTesting("XFF_INI_MISSING", std::nullopt);
  }

  void TearDown() override { env::ClearForTesting(); }
};

TEST_F(IniTest, EnvironmentValuesAndDefaultsPreserveArgumentBoundaries) {
  const auto cfg = ParseIni(R"ini(--temp-root=${XFF_INI_VALUE}/scratch
-name "${XFF_INI_VALUE}" -name ${XFF_INI_EMPTY}
-name ${XFF_INI_MISSING:-fallback with spaces}
-name ${XFF_INI_EMPTY:-fallback} -name ${XFF_INI_VALUE:-unused}
-name ${XFF_INI_VALUE:?required} -name ${XFF_INI_MISSING:-}
)ini");
  EXPECT_THAT(
      cfg.globals, ElementsAre(
                       "--temp-root=set/scratch", "-name", "set", "-name", "", "-name", "fallback with spaces", "-name",
                       "fallback", "-name", "set", "-name", "set", "-name", ""));
  for (const auto& line : cfg.global_lines) {
    EXPECT_THAT(line.syntax_error, IsEmpty());
  }
}

TEST_F(IniTest, EnvironmentTextIsNeverParsedAsSyntaxOrExpandedAgain) {
  env::SetForTesting("XFF_INI_VALUE", "a b ; # '\" \\ ${XFF_INI_MISSING} --no-safe");
  const auto cfg = ParseIni(R"ini(-name ${XFF_INI_VALUE} --hidden # comment
-name ${XFF_INI_MISSING:-$HOME;#"literal"} ; comment
-name ${XFF_INI_EMPTY}#suffix
)ini");
  EXPECT_THAT(
      cfg.globals, ElementsAre(
                       "-name", "a b ; # '\" \\ ${XFF_INI_MISSING} --no-safe", "--hidden", "-name",
                       "$HOME;#\"literal\"", "-name", "#suffix"));
}

TEST_F(IniTest, QuotingAndEscapingKeepLiteralDollarsAndFieldFormats) {
  const auto cfg = ParseIni(R"ini(-name '${XFF_INI_MISSING}' -name \${XFF_INI_MISSING}
-name "\${XFF_INI_MISSING}" -name $XFF_INI_MISSING -name $
-printf '%{env.XFF_INI_VALUE}' -printf "%{env.XFF_INI_VALUE}"
[${XFF_INI_MISSING}]
-name ${XFF_INI_VALUE}
)ini");
  EXPECT_THAT(
      cfg.globals,
      ElementsAre(
          "-name", "${XFF_INI_MISSING}", "-name", "${XFF_INI_MISSING}", "-name", "${XFF_INI_MISSING}", "-name",
          "$XFF_INI_MISSING", "-name", "$", "-printf", "%{env.XFF_INI_VALUE}", "-printf", "%{env.XFF_INI_VALUE}"));
  ASSERT_THAT(cfg.named, SizeIs(1));
  EXPECT_THAT(cfg.named[0].name, "${XFF_INI_MISSING}");
  ASSERT_THAT(cfg.named[0].lines, SizeIs(1));
  EXPECT_THAT(cfg.named[0].lines[0].tokens, ElementsAre("-name", "set"));
  EXPECT_THAT(ParseIni("-name $").globals, ElementsAre("-name", "$"));
}

TEST_F(IniTest, MissingOrEmptyRequiredVariablesInvalidateWholeLine) {
  const auto cases = std::to_array<std::string_view>(
      {"${XFF_INI_MISSING}", "${XFF_INI_MISSING:?}", "${XFF_INI_EMPTY:?}", "${XFF_INI_EMPTY:?set a nonempty value}"});
  for (const auto value : cases) {
    SCOPED_TRACE(value);
    const auto cfg = ParseIni("# comment\n--hidden -name " + std::string(value) + "\n--color=never");
    EXPECT_THAT(cfg.globals, ElementsAre("--color=never"));
    ASSERT_THAT(cfg.global_lines, SizeIs(2));
    EXPECT_THAT(cfg.global_lines[0].tokens, IsEmpty());
    EXPECT_THAT(cfg.global_lines[0].number, 2);
    EXPECT_THAT(cfg.global_lines[0].syntax_error, HasSubstr("environment variable is unset"));
  }
  EXPECT_THAT(
      ParseIni("-name ${XFF_INI_EMPTY:?set a nonempty value}").global_lines[0].syntax_error,
      HasSubstr("XFF_INI_EMPTY: set a nonempty value"));
}

TEST_F(IniTest, InvalidSubstitutionsAreDiagnosedWithoutPartialTokens) {
  struct Case {
    std::string_view input;
    std::string_view diagnostic;
  };

  const auto cases = std::to_array<Case>({
      {.input = "${", .diagnostic = "unterminated environment"},
      {.input = "${XFF_INI_VALUE\n", .diagnostic = "unterminated environment"},
      {.input = "${}", .diagnostic = "invalid environment variable name"},
      {.input = "${1BAD}", .diagnostic = "invalid environment variable name"},
      {.input = "${BAD-NAME}", .diagnostic = "invalid environment variable name"},
      {.input = "${XFF_INI_VALUE:+yes}", .diagnostic = "unsupported environment substitution"},
      {.input = "${XFF_INI_VALUE:=yes}", .diagnostic = "unsupported environment substitution"},
      {.input = "${XFF_INI_VALUE:}", .diagnostic = "unsupported environment substitution"},
      {.input = "${XFF_INI_VALUE:-${OTHER}}", .diagnostic = "nested environment substitutions"},
  });
  for (const auto& test : cases) {
    SCOPED_TRACE(test.input);
    const auto cfg = ParseIni("--hidden -name " + std::string(test.input));
    EXPECT_THAT(cfg.globals, IsEmpty());
    ASSERT_THAT(cfg.global_lines, SizeIs(1));
    EXPECT_THAT(cfg.global_lines[0].syntax_error, HasSubstr(test.diagnostic));
  }
  env::SetForTesting("_XFF123", "valid");
  EXPECT_THAT(ParseIni("-name ${_XFF123}").globals, ElementsAre("-name", "valid"));
}

TEST_F(IniTest, HashCommentsRespectWordBoundariesQuotingAndEscaping) {
  const ConfigFile cfg = ParseIni(R"ini(
# comment
  # indented comment
-name foo#bar # trailing comment
-name '#' -name "#" -name \# -name ''#suffix
[dev] # section comment
-exec echo '#literal' {} \; # command terminator survives
)ini");
  EXPECT_THAT(
      cfg.globals, ElementsAre("-name", "foo#bar", "-name", "#", "-name", "#", "-name", "#", "-name", "#suffix"));
  ASSERT_THAT(cfg.named, SizeIs(1));
  EXPECT_THAT(cfg.named[0].name, "dev");
  ASSERT_THAT(cfg.named[0].lines, SizeIs(1));
  EXPECT_THAT(cfg.named[0].lines[0].tokens, ElementsAre("-exec", "echo", "#literal", "{}", ";"));
}

TEST_F(IniTest, SemicolonCommentsPreserveQuotedAndEscapedLiterals) {
  const ConfigFile cfg = ParseIni(R"ini(; comment
  ; indented comment
-exec echo x \; ; comment
-name 'a;b' -name "c;d" -name e\;f
-name foo; comment without whitespace
[dev] ; section comment
--hidden ; ignored "unterminated quote
)ini");
  EXPECT_THAT(
      cfg.globals,
      ElementsAre("-exec", "echo", "x", ";", "-name", "a;b", "-name", "c;d", "-name", "e;f", "-name", "foo"));
  ASSERT_THAT(cfg.named, SizeIs(1));
  EXPECT_THAT(cfg.named[0].lines[0].tokens, ElementsAre("--hidden"));
}

TEST_F(IniTest, QuotesGroupArgumentsAndPreserveEmptyValuesWithoutExpansion) {
  const ConfigFile cfg = ParseIni(R"ini(-name 'a b' -name "c d" -name a\ b
--template="" --hidden
-exec echo '$HOME' "$(touch marker)" '*.cc' ~ '' \;
)ini");
  EXPECT_THAT(
      cfg.globals, ElementsAre(
                       "-name", "a b", "-name", "c d", "-name", "a b", "--template=", "--hidden", "-exec", "echo",
                       "$HOME", "$(touch marker)", "*.cc", "~", "", ";"));
}

TEST_F(IniTest, DoubleQuotesOnlyConsumeShellDefinedBackslashEscapes) {
  const ConfigFile cfg = ParseIni(R"ini(-name "\#" -name "\q" -name "\$" -name "\`" -name "\"" -name "\\"
-name 'a\b' -name a" b"' c'
)ini");
  EXPECT_THAT(
      cfg.globals, ElementsAre(
                       "-name", "\\#", "-name", "\\q", "-name", "$", "-name", "`", "-name", "\"", "-name", "\\",
                       "-name", "a\\b", "-name", "a b c"));
}

TEST_F(IniTest, ContinuationsAndQuotedNewlinesPreserveStartingLineNumbers) {
  const ConfigFile cfg = ParseIni("-name a\\\nb\n-name \"c\nd\"\n[dev]\n--hidden\n");
  EXPECT_THAT(cfg.globals, ElementsAre("-name", "ab", "-name", "c\nd"));
  ASSERT_THAT(cfg.global_lines, SizeIs(2));
  EXPECT_THAT(cfg.global_lines[0].number, 1);
  EXPECT_THAT(cfg.global_lines[1].number, 3);
  ASSERT_THAT(cfg.named, SizeIs(1));
  EXPECT_THAT(cfg.named[0].number, 5);
}

TEST_F(IniTest, ContinuationsRespectQuotesAndCommentBoundaries) {
  const ConfigFile cfg = ParseIni(R"ini(-name "a\
b" -name 'c\
d'
\
# ignored
-name foo # comment ending with \
--hidden
)ini");
  EXPECT_THAT(cfg.globals, ElementsAre("-name", "ab", "-name", "c\\\nd", "-name", "foo", "--hidden"));
}

TEST_F(IniTest, LexicalErrorsNeverExposePartialTokens) {
  const ConfigFile single = ParseIni("--hidden -name 'unfinished");
  ASSERT_THAT(single.global_lines, SizeIs(1));
  EXPECT_THAT(single.globals, IsEmpty());
  EXPECT_THAT(single.global_lines[0].syntax_error, "unterminated single quote");
  const ConfigFile quoted = ParseIni("[dev]\n-name \"unfinished");
  EXPECT_THAT(quoted.named[0].lines[0].syntax_error, "unterminated double quote");
  const ConfigFile escaped = ParseIni("-name unfinished\\");
  EXPECT_THAT(escaped.global_lines[0].syntax_error, "trailing backslash");
}

TEST_F(IniTest, WhitespaceSeparatesWordsWithoutAssignmentSugar) {
  const ConfigFile cfg = ParseIni(R"ini(--color = auto --jobs = 2
-name = -exec echo --color = auto \; --color = never
--template = 'a # b'
-- -name =
)ini");
  EXPECT_THAT(
      cfg.globals, ElementsAre(
                       "--color", "=", "auto", "--jobs", "=", "2", "-name", "=", "-exec", "echo", "--color", "=",
                       "auto", ";", "--color", "=", "never", "--template", "=", "a # b", "--", "-name", "="));
}

TEST_F(IniTest, GlobalLinesRenderToCliTokens) {
  const ConfigFile cfg = ParseIni("--no-require-system-globals\n--color=auto\n-E\n");
  EXPECT_THAT(cfg.globals, ElementsAre("--no-require-system-globals", "--color=auto", "-E"));
  EXPECT_THAT(cfg.global_lines, SizeIs(3));
}

TEST_F(IniTest, GlobalLinesMayContainMultipleDirectivesAndArguments) {
  const ConfigFile cfg = ParseIni("--hidden --color=never\n-name foo -name bar");
  EXPECT_THAT(cfg.globals, ElementsAre("--hidden", "--color=never", "-name", "foo", "-name", "bar"));
}

TEST_F(IniTest, PolicyAndDefaultsHaveNoReservedMeaning) {
  const ConfigFile cfg = ParseIni("[defaults]\n--color=auto\n[policy]\n--hidden\n");
  ASSERT_THAT(cfg.named, SizeIs(2));
  EXPECT_THAT(cfg.named[0].name, Eq("defaults"));
  EXPECT_THAT(cfg.named[1].name, Eq("policy"));
}

TEST_F(IniTest, PreservesRepeatedAndEmptyDeclarationsForValidation) {
  const ConfigFile cfg = ParseIni("[ dev ]\n[other]\n--hidden\n[dev]");
  ASSERT_THAT(cfg.named, SizeIs(3));
  EXPECT_THAT(cfg.named[0].name, "dev");
  EXPECT_THAT(cfg.named[0].number, 1);
  EXPECT_THAT(cfg.named[0].lines, IsEmpty());
  EXPECT_THAT(cfg.named[2].name, "dev");
  EXPECT_THAT(cfg.named[2].number, 4);
  EXPECT_THAT(cfg.named[2].lines, IsEmpty());
}

TEST_F(IniTest, EverySectionNameDefinesAConfig) {
  const ConfigFile cfg = ParseIni("[unknown]\n--foo=bar\n");
  EXPECT_THAT(cfg.globals, IsEmpty());
  ASSERT_THAT(cfg.named, SizeIs(1));
  EXPECT_THAT(cfg.named[0].name, Eq("unknown"));
  EXPECT_THAT(cfg.named[0].lines[0].tokens, ElementsAre("--foo=bar"));
}

TEST_F(IniTest, ParsesGlobalOptionsAndPlainNamedSectionsWithSourceLines) {
  const ConfigFile cfg = ParseIni(
      "--no-require-system-globals\n"
      "--color=auto\n"
      "[dev]\n"
      "--color=always\n"
      "-E\n"
      "[prod]\n"
      "--color=never\n");

  ASSERT_THAT(cfg.global_lines, SizeIs(2));
  EXPECT_THAT(cfg.global_lines[0].tokens, ElementsAre("--no-require-system-globals"));
  ASSERT_THAT(cfg.named, SizeIs(2));
  EXPECT_THAT(cfg.named[0].name, Eq("dev"));
  EXPECT_THAT(cfg.named[0].lines, SizeIs(2));
  EXPECT_THAT(cfg.named[0].lines[1].tokens, ElementsAre("-E"));
  EXPECT_THAT(cfg.named[1].name, Eq("prod"));
}

}  // namespace
}  // namespace xff::config
