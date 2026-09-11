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

#include "xff/matching/regex/regex.h"

#include <array>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::regex {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::SizeIs;

struct RegexTest : ::testing::Test {};

TEST_F(RegexTest, FullMatchAnchorsBothEnds) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile(".*\\.txt", /*case_insensitive=*/false));
  EXPECT_THAT(matcher.FullMatch("a/b/c.txt"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("c.txt.bak"), IsFalse());  // trailing text -> not a whole-string match
  EXPECT_THAT(matcher.FullMatch("c.md"), IsFalse());
}

TEST_F(RegexTest, PartialMatchIsUnanchored) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("c\\.txt", /*case_insensitive=*/false));
  EXPECT_THAT(matcher.PartialMatch("a/b/c.txt"), IsTrue());  // matches a substring (unlike FullMatch)
  EXPECT_THAT(matcher.PartialMatch("c.txt.bak"), IsTrue());  // trailing text is fine for a partial match
  EXPECT_THAT(matcher.PartialMatch("c.md"), IsFalse());      // still must occur somewhere
  EXPECT_THAT(matcher.FullMatch("a/b/c.txt"), IsFalse());    // the same pattern does not match the whole string
}

TEST_F(RegexTest, FindFirstReturnsLeftmostMatchSpan) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("E[0-9]+", /*case_insensitive=*/false));
  EXPECT_THAT(matcher.FindFirst("code E42 and E7"), Optional(Pair(5, 3)));  // leftmost: E42 at offset 5
  EXPECT_THAT(matcher.FindFirst("no match here"), Eq(std::nullopt));
  EXPECT_THAT(matcher.FindFirst("E9"), Optional(Pair(0, 2)));  // at the very start
}

TEST_F(RegexTest, CaseInsensitiveFoldsCase) {
  ASSERT_OK_AND_ASSIGN(const Matcher folded, Matcher::Compile("readme", /*case_insensitive=*/true));
  EXPECT_THAT(folded.FullMatch("README"), IsTrue());
  ASSERT_OK_AND_ASSIGN(const Matcher exact, Matcher::Compile("readme", /*case_insensitive=*/false));
  EXPECT_THAT(exact.FullMatch("README"), IsFalse());
}

TEST_F(RegexTest, InvalidPatternReturnsError) {
  EXPECT_THAT(Matcher::Compile("a(b", /*case_insensitive=*/false), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(RegexTest, Re2IsTheExplicitDefaultGrammar) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("a.c", /*case_insensitive=*/false, Grammar::kRe2));
  EXPECT_THAT(matcher.FullMatch("abc"), IsTrue());
}

TEST_F(RegexTest, EreSupportsAlternationCapturesAndCaseFolding) {
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("(cat|dog)-([0-9]+)", /*case_insensitive=*/true, Grammar::kEre));
  EXPECT_THAT(matcher.FullMatchCaptures("DOG-42"), Optional(ElementsAre("DOG-42", "DOG", "42")));
  EXPECT_THAT(matcher.FindFirst("a cat-7 then dog-2"), Optional(Pair(Eq(2U), Eq(5U))));
  EXPECT_THAT(matcher.PartialMatch("nothing"), IsFalse());
}

TEST_F(RegexTest, EreFullMatchRequiresBothBoundaries) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("cat|dog", /*case_insensitive=*/false, Grammar::kEre));
  EXPECT_THAT(matcher.FullMatch("cat"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("catapult"), IsFalse());
  EXPECT_THAT(matcher.FullMatch("a dog"), IsFalse());
  EXPECT_THAT(matcher.FullMatch("bird"), IsFalse());
}

TEST_F(RegexTest, EreCapturesRequireAFullMatchAndRepresentUnmatchedGroupsAsEmpty) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("(cat)?dog", /*case_insensitive=*/false, Grammar::kEre));
  EXPECT_THAT(matcher.FullMatchCaptures("dog"), Optional(ElementsAre("dog", "")));
  EXPECT_THAT(matcher.FullMatchCaptures("doghouse"), Eq(std::nullopt));
  EXPECT_THAT(matcher.FullMatchCaptures("a dog"), Eq(std::nullopt));
  EXPECT_THAT(matcher.FullMatchCaptures("bird"), Eq(std::nullopt));
}

TEST_F(RegexTest, EreRejectsInvalidPatterns) {
  EXPECT_THAT(
      Matcher::Compile("a(b", /*case_insensitive=*/false, Grammar::kEre), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      Matcher::Compile(std::string_view("a\0b", 3), /*case_insensitive=*/false, Grammar::kEre),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("NUL")));
}

TEST_F(RegexTest, EreRewriteSupportsCapturesAndZeroLengthMatches) {
  ASSERT_OK_AND_ASSIGN(
      const Matcher captures, Matcher::Compile("([a-z]+)-([0-9]+)", /*case_insensitive=*/false, Grammar::kEre));
  EXPECT_THAT(captures.Rewrite("a-1 b-22", "\\2:\\1", /*global=*/true), "1:a 22:b");

  ASSERT_OK_AND_ASSIGN(const Matcher empty, Matcher::Compile("x*", /*case_insensitive=*/false, Grammar::kEre));
  EXPECT_THAT(empty.Rewrite("ab", "_", /*global=*/true), "_a_b_");

  ASSERT_OK_AND_ASSIGN(const Matcher anchored, Matcher::Compile("^", /*case_insensitive=*/false, Grammar::kEre));
  EXPECT_THAT(anchored.Rewrite("ab", "_", /*global=*/true), "_ab");
}

TEST_F(RegexTest, EreRewriteSupportsSingleMatchesAndReplacementEscapes) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("(a)(b)?", /*case_insensitive=*/false, Grammar::kEre));
  EXPECT_THAT(matcher.Rewrite("ab ab", "\\0-\\1-\\2", /*global=*/false), "ab-a-b ab");
  EXPECT_THAT(matcher.Rewrite("a", "<\\2>", /*global=*/false), "<>");
  EXPECT_THAT(matcher.Rewrite("a", "\\9", /*global=*/false), "");
  EXPECT_THAT(matcher.Rewrite("a", "\\q\\", /*global=*/false), "q\\");
  EXPECT_THAT(matcher.Rewrite("bird", "x", /*global=*/true), "bird");
}

TEST_F(RegexTest, EreDoesNotSilentlyTruncateSubjectsAtNul) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("a", /*case_insensitive=*/false, Grammar::kEre));
  const std::string subject("a\0b", 3);
  EXPECT_THAT(matcher.FullMatch(subject), IsFalse());
  EXPECT_THAT(matcher.PartialMatch(subject), IsFalse());
  EXPECT_THAT(matcher.FindFirst(subject), Eq(std::nullopt));
  EXPECT_THAT(matcher.FullMatchCaptures(subject), Eq(std::nullopt));
  EXPECT_THAT(matcher.Rewrite(subject, "x", /*global=*/true), subject);
}

TEST_F(RegexTest, Pcre2GrammarIsNotBuiltInAndReportsUnimplemented) {
  // No PCRE2 backend is registered in this (lean) test binary, so Compile reports the grammar as
  // unavailable -- a distinct Unimplemented state from an InvalidArgument for a bad pattern, and
  // never a silent fallback to RE2. A full build links the real backend and this succeeds.
  EXPECT_THAT(Pcre2Available(), IsFalse());
  EXPECT_THAT(
      Matcher::Compile("a.c", /*case_insensitive=*/false, Grammar::kPcre2), StatusIs(absl::StatusCode::kUnimplemented));
}

TEST_F(RegexTest, FullMatchCapturesReturnsGroups) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("(.*)/([^/]+)\\.(.*)", /*case_insensitive=*/false));
  const auto captures = matcher.FullMatchCaptures("a/b/c.txt");
  EXPECT_THAT(captures, Optional(ElementsAre("a/b/c.txt", "a/b", "c", "txt")));  // [0]=whole match, then the 3 groups
  EXPECT_THAT(matcher.FullMatchCaptures("nomatch"), Eq(std::nullopt));           // no full match -> nullopt
}

TEST_F(RegexTest, FullMatchCapturesWithNoGroupsReturnsWholeMatchOnly) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("a.c", /*case_insensitive=*/false));
  const auto captures = matcher.FullMatchCaptures("abc");
  EXPECT_THAT(captures, Optional(ElementsAre("abc")));  // no groups -> index 0 (whole match) only
}

TEST_F(RegexTest, RewriteReplacesFirstOrAllMatches) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("[0-9]+", /*case_insensitive=*/false));
  EXPECT_THAT(matcher.Rewrite("a12b34", "#", /*global=*/false), "a#b34");  // first match only
  EXPECT_THAT(matcher.Rewrite("a12b34", "#", /*global=*/true), "a#b#");    // every match
  EXPECT_THAT(matcher.Rewrite("abc", "#", /*global=*/true), "abc");        // no match -> unchanged
}

TEST_F(RegexTest, RewriteSupportsBackreferences) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("(\\w+)@(\\w+)", /*case_insensitive=*/false));
  EXPECT_THAT(matcher.Rewrite("user@host", "\\2.\\1", /*global=*/false), "host.user");  // \1/\2 backrefs
}

TEST_F(RegexTest, ExactGrammarMatchesLiterally) {
  // kExact is a literal engine: metacharacters are plain text, FullMatch is equality, PartialMatch a
  // substring test. It is a core grammar, always available, and never fails to compile.
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("3.50", /*case_insensitive=*/false, Grammar::kExact));
  EXPECT_THAT(matcher.FullMatch("3.50"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("3X50"), IsFalse());   // '.' is literal, not a wildcard
  EXPECT_THAT(matcher.FullMatch("x3.50"), IsFalse());  // FullMatch is whole-string equality
  EXPECT_THAT(matcher.PartialMatch("price 3.50 now"), IsTrue());
  EXPECT_THAT(matcher.PartialMatch("price 3X50"), IsFalse());
}

TEST_F(RegexTest, ExactGrammarCompilesAnyPatternAndFindsTheSpan) {
  // A pattern that is not a valid regex is a fine literal (never an InvalidArgument), and FindFirst
  // reports the literal span (offset + pattern length), 1:1 with the old -grep EXACT substring path.
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("foo(bar", /*case_insensitive=*/false, Grammar::kExact));
  EXPECT_THAT(matcher.PartialMatch("call foo(bar) now"), IsTrue());
  EXPECT_THAT(matcher.FindFirst("aXfoo(barX"), Optional(Pair(Eq(2U), Eq(7U))));  // "foo(bar" at offset 2, len 7
  EXPECT_THAT(matcher.FindFirst("no match"), Eq(std::nullopt));
  EXPECT_THAT(matcher.Rewrite("x foo(bar y foo(bar", "Z", /*global=*/true), "x Z y Z");  // literal replace
}

TEST_F(RegexTest, ExactGrammarHandlesEmptyAndSingleReplacementBoundaries) {
  ASSERT_OK_AND_ASSIGN(const Matcher empty, Matcher::Compile("", /*case_insensitive=*/false, Grammar::kExact));
  EXPECT_THAT(empty.FindFirst("anything"), Optional(Pair(Eq(0U), Eq(0U))));
  EXPECT_THAT(empty.Rewrite("anything", "replacement", /*global=*/true), "anything");

  ASSERT_OK_AND_ASSIGN(const Matcher folded, Matcher::Compile("Ab", /*case_insensitive=*/true, Grammar::kExact));
  EXPECT_THAT(folded.Rewrite("AB ab AB", "x", /*global=*/false), "x ab AB");
}

TEST_F(RegexTest, UnknownGrammarReportsInternalError) {
  EXPECT_THAT(
      Matcher::Compile("anything", /*case_insensitive=*/false, static_cast<Grammar>(255)),
      StatusIs(absl::StatusCode::kInternal));
}

TEST_F(RegexTest, ExactGrammarCaseInsensitiveFoldsAsciiCase) {
  ASSERT_OK_AND_ASSIGN(const Matcher folded, Matcher::Compile("Readme", /*case_insensitive=*/true, Grammar::kExact));
  EXPECT_THAT(folded.FullMatch("README"), IsTrue());
  EXPECT_THAT(folded.PartialMatch("the readme file"), IsTrue());
  EXPECT_THAT(folded.FindFirst("see README now"), Optional(Pair(Eq(4U), Eq(6U))));  // span in the original text
  ASSERT_OK_AND_ASSIGN(const Matcher exact, Matcher::Compile("Readme", /*case_insensitive=*/false, Grammar::kExact));
  EXPECT_THAT(exact.FullMatch("README"), IsFalse());
}

TEST_F(RegexTest, ExactGrammarCapturesTheWholeLiteralMatch) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("Readme", /*case_insensitive=*/true, Grammar::kExact));
  EXPECT_THAT(matcher.FullMatchCaptures("README"), Optional(ElementsAre("README")));
  EXPECT_THAT(matcher.FullMatchCaptures("README.md"), Eq(std::nullopt));
}

TEST_F(RegexTest, FnmatchGrammarIsAWholeStringWildcard) {
  // kFnmatch is a shell glob: FullMatch is a whole-string fnmatch (`*` matches any char, incl '/');
  // '.' is literal. A core grammar; Compile never fails.
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("a*.txt", /*case_insensitive=*/false, Grammar::kFnmatch));
  EXPECT_THAT(matcher.FullMatch("a.txt"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("abc.txt"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("a/b/c.txt"), IsTrue());  // '*' spans '/' (no FNM_PATHNAME - flat, like -path)
  EXPECT_THAT(matcher.FullMatch("a.md"), IsFalse());
  EXPECT_THAT(matcher.FullMatch("xa.txt"), IsFalse());  // FullMatch is anchored (whole string)
}

TEST_F(RegexTest, FnmatchPartialMatchWrapsInStars) {
  // PartialMatch wraps the pattern in `*…*` so it matches anywhere; FindFirst reports the whole text
  // as the span (fnmatch is a whole-string test, not a sub-span search).
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("f?o", /*case_insensitive=*/false, Grammar::kFnmatch));
  EXPECT_THAT(matcher.PartialMatch("a foo b"), IsTrue());  // contains an f-any-o triple
  EXPECT_THAT(matcher.PartialMatch("a fizz b"), IsFalse());
  EXPECT_THAT(matcher.FindFirst("a foo b"), Optional(Pair(Eq(0U), Eq(7U))));  // whole text is the match
  EXPECT_THAT(matcher.FindFirst("nope"), Eq(std::nullopt));
}

TEST_F(RegexTest, FnmatchCaseInsensitiveUsesCasefold) {
  ASSERT_OK_AND_ASSIGN(const Matcher folded, Matcher::Compile("R*E", /*case_insensitive=*/true, Grammar::kFnmatch));
  EXPECT_THAT(folded.FullMatch("readme"), IsTrue());
  ASSERT_OK_AND_ASSIGN(const Matcher exact, Matcher::Compile("R*E", /*case_insensitive=*/false, Grammar::kFnmatch));
  EXPECT_THAT(exact.FullMatch("readme"), IsFalse());
}

TEST_F(RegexTest, FnmatchCapturesItsWholeMatchAndDoesNotRewrite) {
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("report-??.txt", /*case_insensitive=*/false, Grammar::kFnmatch));
  EXPECT_THAT(matcher.FullMatchCaptures("report-42.txt"), Optional(ElementsAre("report-42.txt")));
  EXPECT_THAT(matcher.FullMatchCaptures("old-report-42.txt"), Eq(std::nullopt));
  EXPECT_THAT(matcher.Rewrite("report-42.txt", "renamed", /*global=*/true), "report-42.txt");
  EXPECT_THAT(matcher.Rewrite("not a match", "renamed", /*global=*/false), "not a match");
}

TEST_F(RegexTest, MoveAssignmentReplacesTheBackend) {
  ASSERT_OK_AND_ASSIGN(Matcher matcher, Matcher::Compile("old", /*case_insensitive=*/false, Grammar::kExact));
  ASSERT_OK_AND_ASSIGN(Matcher replacement, Matcher::Compile("new", /*case_insensitive=*/false, Grammar::kExact));
  matcher = std::move(replacement);
  EXPECT_THAT(matcher.FullMatch("new"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("old"), IsFalse());
}

TEST_F(RegexTest, GlobGrammarIsPathSegmentAware) {
  // kGlob is a path-aware shell glob (translated to RE2): unlike fnmatch, `*` stops at `/`.
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("src/*.txt", /*case_insensitive=*/false, Grammar::kGlob));
  EXPECT_THAT(matcher.FullMatch("src/a.txt"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("src/sub/a.txt"), IsFalse());  // '*' does not cross '/' (unlike FNMATCH)
  EXPECT_THAT(matcher.FullMatch("a.txt"), IsFalse());
}

TEST_F(RegexTest, GlobDoubleStarCrossesDirectories) {
  // `**` is the cross-directory wildcard (the gitignore/shell globstar).
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("src/**/*.txt", /*case_insensitive=*/false, Grammar::kGlob));
  EXPECT_THAT(matcher.FullMatch("src/a.txt"), IsTrue());      // zero directories
  EXPECT_THAT(matcher.FullMatch("src/x/y/a.txt"), IsTrue());  // several directories
  EXPECT_THAT(matcher.FullMatch("other/a.txt"), IsFalse());
}

TEST_F(RegexTest, GlobTrailingDoubleStarRequiresADescendant) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("src/**", /*case_insensitive=*/false, Grammar::kGlob));
  EXPECT_THAT(matcher.FullMatch("src/file"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("src/sub/file"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("src"), IsFalse());
}

TEST_F(RegexTest, GlobClassesNeverConsumeAPathSeparator) {
  ASSERT_OK_AND_ASSIGN(const Matcher negated, Matcher::Compile("[!a]", /*case_insensitive=*/false, Grammar::kGlob));
  EXPECT_THAT(negated.FullMatch("b"), IsTrue());
  EXPECT_THAT(negated.FullMatch("/"), IsFalse());

  ASSERT_OK_AND_ASSIGN(const Matcher separator, Matcher::Compile("[/]", /*case_insensitive=*/false, Grammar::kGlob));
  EXPECT_THAT(separator.FullMatch("/"), IsTrue());
}

TEST_F(RegexTest, GlobRejectsMalformedAndUnsupportedBracketExpressions) {
  EXPECT_THAT(
      Matcher::Compile("[", /*case_insensitive=*/false, Grammar::kGlob), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      Matcher::Compile("[z-a]", /*case_insensitive=*/false, Grammar::kGlob),
      StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      Matcher::Compile("[[.ch.]]", /*case_insensitive=*/false, Grammar::kGlob),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(RegexTest, GlobDelegatesToRe2ForSpanAndPartial) {
  // Because kGlob compiles to RE2, PartialMatch is unanchored and FindFirst returns a real span
  // (unlike fnmatch's whole-text span) - so -grep's {match}/{column} work under GLOB.
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("f*o", /*case_insensitive=*/false, Grammar::kGlob));
  EXPECT_THAT(matcher.PartialMatch("a foo b"), IsTrue());                     // matches within the line
  EXPECT_THAT(matcher.FindFirst("a foo b"), Optional(Pair(Eq(2U), Eq(3U))));  // "foo" at offset 2, len 3
}

TEST_F(RegexTest, ShglobGrammarExpandsBraceAlternation) {
  // kShglob is kGlob plus `{a,b}` brace alternation, so an
  // extension-of-set pattern matches any listed alternative and nothing else.
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("*.{cc,h}", /*case_insensitive=*/false, Grammar::kShglob));
  EXPECT_THAT(matcher.FullMatch("a.cc"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("a.h"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("a.hpp"), IsFalse());  // only the listed alternatives
  EXPECT_THAT(matcher.FullMatch("a.o"), IsFalse());
}

TEST_F(RegexTest, ShglobSupportsNestedAndEmptyAlternatives) {
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("{src,{test,}}/*.cc", /*case_insensitive=*/false, Grammar::kShglob));
  EXPECT_THAT(matcher.FullMatch("src/a.cc"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("test/a.cc"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("/a.cc"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("lib/a.cc"), IsFalse());
}

TEST_F(RegexTest, ShglobSupportsBoundedIntegerAndAsciiLetterSequences) {
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("part-{03..01}.{a..c}", /*case_insensitive=*/false, Grammar::kShglob));
  EXPECT_THAT(matcher.FullMatch("part-03.a"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("part-01.c"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("part-3.a"), IsFalse());
  EXPECT_THAT(matcher.FullMatch("part-00.a"), IsFalse());
  EXPECT_THAT(matcher.FullMatch("part-02.d"), IsFalse());

  EXPECT_THAT(
      Matcher::Compile("{1..10001}", /*case_insensitive=*/false, Grammar::kShglob),
      StatusIs(absl::StatusCode::kInvalidArgument, "Brace sequence exceeds the 10000-term limit."));
}

TEST_F(RegexTest, ShglobLeavesUnsupportedIncrementSequenceLiteral) {
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("{1..5..2}", /*case_insensitive=*/false, Grammar::kShglob));
  EXPECT_THAT(matcher.FullMatch("{1..5..2}"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("1"), IsFalse());
  EXPECT_THAT(matcher.FullMatch("3"), IsFalse());
}

TEST_F(RegexTest, ShglobKeepsGlobPathSemantics) {
  // The GLOB behavior carries over: `*` stops at `/`, alternatives may contain `/`.
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("{src,test}/*.cc", /*case_insensitive=*/false, Grammar::kShglob));
  EXPECT_THAT(matcher.FullMatch("src/a.cc"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("test/a.cc"), IsTrue());
  EXPECT_THAT(matcher.FullMatch("src/sub/a.cc"), IsFalse());  // '*' does not cross '/'
  EXPECT_THAT(matcher.FullMatch("lib/a.cc"), IsFalse());
}

TEST_F(RegexTest, GrammarDocsCoverEveryGrammarInValueOrder) {
  // Anti-drift for --help=grammars: exactly one doc row per Grammar, in --regextype value order.
  // kAllGrammars mirrors the enum; adding a Grammar means listing it here (proving it compiles below)
  // and adding a GrammarDocs row, or the SizeIs check fails.
  static constexpr std::array<Grammar, 7> kAllGrammars = {
      Grammar::kRe2,    Grammar::kExact, Grammar::kFnmatch, Grammar::kGlob,
      Grammar::kShglob, Grammar::kEre,   Grammar::kPcre2,
  };
  const absl::Span<const std::pair<std::string_view, std::string_view>> docs = GrammarDocs();
  EXPECT_THAT(docs, SizeIs(kAllGrammars.size()));
  std::vector<std::string_view> names;
  for (const auto& [name, description] : docs) {
    names.push_back(name);
    EXPECT_THAT(description, Not(IsEmpty()));  // every grammar carries an explanation
  }
  EXPECT_THAT(names, ElementsAre("RE2", "EXACT", "FNMATCH", "GLOB", "SHGLOB", "ERE", "PCRE2"));
}

TEST_F(RegexTest, GrammarDocsHaveStableStorage) {
  const auto docs = GrammarDocs();
  EXPECT_THAT(GrammarDocs().data(), Eq(docs.data()));
}

TEST_F(RegexTest, EveryGrammarCompilesATrivialPattern) {
  // PCRE2 (a build extra) may return Unimplemented in a lean build, so it is exercised by its own
  // dedicated test above.
  static constexpr std::array kAllGrammars = std::to_array<Grammar>({
      Grammar::kRe2,
      Grammar::kExact,
      Grammar::kFnmatch,
      Grammar::kGlob,
      Grammar::kShglob,
      Grammar::kEre,
  });
  for (const Grammar grammar : kAllGrammars) {
    EXPECT_THAT(Matcher::Compile("abc", /*case_insensitive=*/false, grammar), IsOk());
  }
}

}  // namespace
}  // namespace xff::regex
