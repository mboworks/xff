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

#include "xff/content/line_match.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::content {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::Optional;

Matcher<LineMatch> LineIs(std::size_t number, std::string_view text) {
  return AllOf(Field("number", &LineMatch::number, number), Field("text", &LineMatch::text, text));
}

// Matches every line (so the result is the full line inventory: numbers + text).
bool Any(std::string_view /*line*/) {
  return true;
}

struct LineMatchTest : ::testing::Test {};

TEST_F(LineMatchTest, EmptyContentHasNoLines) {
  EXPECT_THAT(CollectLineMatches("", Any), IsEmpty());
}

TEST_F(LineMatchTest, SingleLineWithoutTrailingNewline) {
  EXPECT_THAT(CollectLineMatches("hello", Any), ElementsAre(LineIs(1, "hello")));
}

TEST_F(LineMatchTest, TrailingNewlineDoesNotAddPhantomLine) {
  // "a\nb\n" is two lines, not three -- the byte after the final newline is not a line.
  EXPECT_THAT(CollectLineMatches("a\nb\n", Any), ElementsAre(LineIs(1, "a"), LineIs(2, "b")));
}

TEST_F(LineMatchTest, MissingFinalNewlineStillYieldsLastLine) {
  EXPECT_THAT(CollectLineMatches("a\nb", Any), ElementsAre(LineIs(1, "a"), LineIs(2, "b")));
}

TEST_F(LineMatchTest, BlankInteriorLineIsCounted) {
  EXPECT_THAT(CollectLineMatches("a\n\nb", Any), ElementsAre(LineIs(1, "a"), LineIs(2, ""), LineIs(3, "b")));
}

TEST_F(LineMatchTest, CarriageReturnIsStrippedFromCrlf) {
  // The '\r' of a CRLF is not part of the reported text (so patterns match either).
  EXPECT_THAT(CollectLineMatches("a\r\nb\r\n", Any), ElementsAre(LineIs(1, "a"), LineIs(2, "b")));
}

TEST_F(LineMatchTest, LoneNewlineIsOneEmptyLine) {
  EXPECT_THAT(CollectLineMatches("\n", Any), ElementsAre(LineIs(1, "")));
}

TEST_F(LineMatchTest, ReturnsOnlyMatchingLinesWithTheirNumbers) {
  const std::string_view content = "alpha\nbravo\nalpha again\ncharlie\n";
  const auto has_alpha = [](std::string_view line) { return absl::StrContains(line, "alpha"); };
  EXPECT_THAT(CollectLineMatches(content, has_alpha), ElementsAre(LineIs(1, "alpha"), LineIs(3, "alpha again")));
}

TEST_F(LineMatchTest, NoMatchingLinesIsEmpty) {
  const auto never = [](std::string_view) { return false; };
  EXPECT_THAT(CollectLineMatches("a\nb\nc\n", never), IsEmpty());
}

Matcher<ContextLine> CtxIs(std::size_t number, std::string_view text, bool is_match, std::size_t group) {
  return AllOf(
      Field("number", &ContextLine::number, number), Field("text", &ContextLine::text, text),
      Field("is_match", &ContextLine::is_match, is_match), Field("group", &ContextLine::group, group));
}

// The single match is "HIT"; the other lines are context.
bool IsHit(std::string_view line) {
  return line == "HIT";
}

struct LineContextTest : ::testing::Test {};

TEST_F(LineContextTest, ZeroContextReturnsOnlyMatches) {
  // before == after == 0: exactly the matches; adjacent matches share a group, a gap splits them.
  EXPECT_THAT(
      CollectLineMatchesWithContext("HIT\na\nHIT\n", IsHit, 0, 0),
      ElementsAre(CtxIs(1, "HIT", true, 0), CtxIs(3, "HIT", true, 1)));
}

TEST_F(LineContextTest, AfterContext) {
  EXPECT_THAT(
      CollectLineMatchesWithContext("a\nHIT\nb\nc\n", IsHit, 0, 1),
      ElementsAre(CtxIs(2, "HIT", true, 0), CtxIs(3, "b", false, 0)));
}

TEST_F(LineContextTest, BeforeContext) {
  EXPECT_THAT(
      CollectLineMatchesWithContext("a\nHIT\nb\n", IsHit, 1, 0),
      ElementsAre(CtxIs(1, "a", false, 0), CtxIs(2, "HIT", true, 0)));
}

TEST_F(LineContextTest, SymmetricContextClampsAtEdges) {
  // before == after == 1 around a match on the first line: no line 0, one after.
  EXPECT_THAT(
      CollectLineMatchesWithContext("HIT\nb\n", IsHit, 1, 1),
      ElementsAre(CtxIs(1, "HIT", true, 0), CtxIs(2, "b", false, 0)));
}

TEST_F(LineContextTest, OverlappingWindowsMergeIntoOneGroup) {
  // Two matches whose context windows touch merge into a single group (no separator between).
  EXPECT_THAT(
      CollectLineMatchesWithContext("a\nHIT\nb\nHIT\nc\n", IsHit, 1, 1),
      ElementsAre(
          CtxIs(1, "a", false, 0), CtxIs(2, "HIT", true, 0), CtxIs(3, "b", false, 0), CtxIs(4, "HIT", true, 0),
          CtxIs(5, "c", false, 0)));
}

TEST_F(LineContextTest, GapStartsANewGroup) {
  // Matches far apart -> two groups (caller prints "--" between them).
  EXPECT_THAT(
      CollectLineMatchesWithContext("HIT\na\nb\nc\nd\nHIT\n", IsHit, 0, 1),
      ElementsAre(CtxIs(1, "HIT", true, 0), CtxIs(2, "a", false, 0), CtxIs(6, "HIT", true, 1)));
}

struct LineCountTest : ::testing::Test {};

TEST_F(LineCountTest, CountLinesMatchesGrepSemantics) {
  EXPECT_THAT(CountLines(""), 0U);
  EXPECT_THAT(CountLines("a"), 1U);  // an unterminated single line
  EXPECT_THAT(CountLines("a\n"), 1U);
  EXPECT_THAT(CountLines("a\nb\n"), 2U);
  EXPECT_THAT(CountLines("a\nb"), 2U);  // a final line with no trailing newline still counts
  EXPECT_THAT(CountLines("\n"), 1U);    // a lone newline is one (empty) line
  EXPECT_THAT(CountLines("\n\n"), 2U);
  EXPECT_THAT(CountLines("a\r\nb\r\n"), 2U);  // CRLF counts like LF
}

TEST_F(LineCountTest, BinarySniffStopsAtEightThousandBytes) {
  std::string content(8'192, 'x');
  content.at(7'999) = '\0';
  EXPECT_THAT(ContentLineCount(content), Eq(std::nullopt));
  content.at(7'999) = 'x';
  const auto offsets = std::to_array<std::size_t>({8'000U, 8'050U, 8'191U});
  for (const std::size_t offset : offsets) {
    content.at(offset) = '\0';
    EXPECT_THAT(ContentLineCount(content), Optional(Eq(std::size_t{1}))) << offset;
    content.at(offset) = 'x';
  }
}

TEST_F(LineCountTest, FileLineCountReadsAndCounts) {
  const std::string path = std::string(::testing::TempDir()) + "/xff_line_count_txt";
  { std::ofstream(path) << "one\ntwo\nthree\n"; }
  EXPECT_THAT(FileLineCount(path), Optional(Eq(std::size_t{3})));
  { std::ofstream(path) << "no trailing newline"; }
  EXPECT_THAT(FileLineCount(path), Optional(Eq(std::size_t{1})));
  { std::ofstream(path) << ""; }  // truncate to empty
  EXPECT_THAT(FileLineCount(path), Optional(Eq(std::size_t{0})));
  (void)std::remove(path.c_str());
}

TEST_F(LineCountTest, FileLineCountSkipsBinaryAndUnreadable) {
  const std::string path = std::string(::testing::TempDir()) + "/xff_line_count_bin";
  { std::ofstream(path, std::ios::binary).write("a\0b\n", 4); }  // a NUL byte in the content
  EXPECT_THAT(FileLineCount(path), Eq(std::nullopt));            // a NUL byte marks the file binary
  (void)std::remove(path.c_str());
  EXPECT_THAT(FileLineCount(std::string(::testing::TempDir()) + "/xff_line_count_absent"), Eq(std::nullopt));
}

}  // namespace
}  // namespace xff::content
