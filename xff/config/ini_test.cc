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

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::config {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

struct IniTest : ::testing::Test {};

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
  const ConfigFile cfg = ParseIni("--no-require-system-config\n--color=auto\n-E\n");
  EXPECT_THAT(cfg.globals, ElementsAre("--no-require-system-config", "--color=auto", "-E"));
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
      "--no-require-system-config\n"
      "--color=auto\n"
      "[dev]\n"
      "--color=always\n"
      "-E\n"
      "[prod]\n"
      "--color=never\n");

  ASSERT_THAT(cfg.global_lines, SizeIs(2));
  EXPECT_THAT(cfg.global_lines[0].tokens, ElementsAre("--no-require-system-config"));
  ASSERT_THAT(cfg.named, SizeIs(2));
  EXPECT_THAT(cfg.named[0].name, Eq("dev"));
  EXPECT_THAT(cfg.named[0].lines, SizeIs(2));
  EXPECT_THAT(cfg.named[0].lines[1].tokens, ElementsAre("-E"));
  EXPECT_THAT(cfg.named[1].name, Eq("prod"));
}

}  // namespace
}  // namespace xff::config
