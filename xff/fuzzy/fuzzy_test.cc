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

#include "xff/fuzzy/fuzzy.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::fuzzy {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::Lt;
using ::testing::Optional;

struct FuzzyTest : ::testing::Test {};

TEST_F(FuzzyTest, TheCharactersMustAppearInOrder) {
  // The rule in one line: a subsequence, so the gaps are free but the ORDER is not.
  EXPECT_THAT(Matches("tmh", "the/main/header.h", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("hmt", "the/main/header.h", /*fold_case=*/false), IsFalse());
}

TEST_F(FuzzyTest, EveryPatternCharacterHasToBeThere) {
  EXPECT_THAT(Matches("abc", "a-b-c", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("abcd", "a-b-c", /*fold_case=*/false), IsFalse());
}

TEST_F(FuzzyTest, ARepeatedCharacterNeedsAsManyOccurrences) {
  // The greedy scan consumes each match, so `aa` needs two `a`s rather than matching one twice.
  EXPECT_THAT(Matches("aa", "banana", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("aaaa", "banana", /*fold_case=*/false), IsFalse());
}

TEST_F(FuzzyTest, AnEmptyPatternMatchesAnythingAndAnEmptyTextOnlyAnEmptyPattern) {
  // Same as an empty glob: a pattern that constrains nothing matches everything.
  EXPECT_THAT(Matches("", "anything", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("", "", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("a", "", /*fold_case=*/false), IsFalse());
}

TEST_F(FuzzyTest, CaseFoldingIsTheCallersChoice) {
  EXPECT_THAT(Matches("RM", "README.md", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("rm", "README.md", /*fold_case=*/false), IsFalse());
  EXPECT_THAT(Matches("rm", "README.md", /*fold_case=*/true), IsTrue());
  EXPECT_THAT(Matches("RM", "readme.md", /*fold_case=*/true), IsTrue());
}

TEST_F(FuzzyTest, TheWholeTextIsSearchedNotJustItsStart) {
  // An anchored match would be a prefix test; a fuzzy finder is expected to find `deep` in the tail.
  EXPECT_THAT(Matches("deep", "a/very/long/path/deep", /*fold_case=*/false), IsTrue());
}

TEST_F(FuzzyTest, SeparatorsAreOrdinaryCharacters) {
  // A pattern may spell the separator itself, which is what makes `s/f` mean "f under some s".
  EXPECT_THAT(Matches("s/f", "src/file.cc", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("f/s", "src/file.cc", /*fold_case=*/false), IsFalse());
}

struct FuzzyScoreTest : ::testing::Test {
  // The only thing a score means is "better than that other one", so the assertions are comparisons.
  static int ScoreOf(std::string_view pattern, std::string_view text, bool fold_case = false) {
    const std::optional<int> score = Score(pattern, text, fold_case);
    return score.value_or(-1);
  }
};

TEST_F(FuzzyScoreTest, NoMatchScoresNothingAndAMatchingEmptyPatternScoresZero) {
  // nullopt rather than 0 for "no match", so a matching empty pattern (which constrains nothing and
  // therefore says nothing about quality) is not confused with a failure.
  EXPECT_THAT(Score("zzz", "the_main_header.h", false), Eq(std::nullopt));
  EXPECT_THAT(Score("", "anything", false), Optional(Eq(0)));
  EXPECT_THAT(Score("toolong", "short", false), Eq(std::nullopt));
}

TEST_F(FuzzyScoreTest, PercentNormalizesScoresAcrossPatterns) {
  EXPECT_THAT(Percent("exact", "exact", false), Optional(Eq(100)));
  EXPECT_THAT(Percent("", "anything", false), Optional(Eq(100)));
  EXPECT_THAT(Percent("zzz", "anything", false), Eq(std::nullopt));
  EXPECT_THAT(Percent("foo", "far_out_of", false), Optional(AllOf(Ge(0), Lt(100))));
}

TEST_F(FuzzyScoreTest, FzfExtendedSearchCombinesSpaceSeparatedTerms) {
  // fzf's documented extended-search example: each space-separated term is required, while the
  // sigils choose prefix, suffix, and inverse-exact matching.
  // https://github.com/junegunn/fzf#search-syntax
  constexpr std::string_view query = "^music .mp3$ sbtrkt !fire";
  EXPECT_THAT(FzfPercent(query, "music-subtrakktor.mp3", true), Optional(Eq(75)));
  EXPECT_THAT(FzfPercent(query, "old-music-subtrakktor.mp3", true), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent(query, "music-fire-subtrakktor.mp3", true), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent(query, "music-subtrakktor.flac", true), Eq(std::nullopt));
}

TEST_F(FuzzyScoreTest, FzfExtendedSearchSupportsDocumentedOrGroups) {
  // This is the compound OR example from fzf's search-syntax documentation.
  constexpr std::string_view query = "^core go$ | rb$ | py$";
  EXPECT_THAT(FzfPercent(query, "core.go", false), Optional(Eq(89)));
  EXPECT_THAT(FzfPercent(query, "core.rb", false), Optional(Eq(89)));
  EXPECT_THAT(FzfPercent(query, "core.py", false), Optional(Eq(89)));
  EXPECT_THAT(FzfPercent(query, "more.py", false), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent(query, "core.cc", false), Eq(std::nullopt));
}

TEST_F(FuzzyScoreTest, FzfExtendedSearchDistinguishesAllTermOperators) {
  EXPECT_THAT(FzfPercent("'wild", "a-wild-card", false), Optional(AllOf(Ge(0), Le(100))));
  EXPECT_THAT(FzfPercent("'wild'", "a wild card", false), Optional(AllOf(Ge(0), Le(100))));
  EXPECT_THAT(FzfPercent("'wild'", "a-wilderness", false), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent("^music", "music-box", false), Optional(AllOf(Ge(0), Le(100))));
  EXPECT_THAT(FzfPercent(".mp3$", "music.mp3", false), Optional(AllOf(Ge(0), Le(100))));
  EXPECT_THAT(FzfPercent("^music.mp3$", "music.mp3", false), Optional(Eq(100)));
  EXPECT_THAT(FzfPercent("!fire", "music-water.mp3", false), Optional(Eq(100)));
  EXPECT_THAT(FzfPercent("!fire", "music-fire.mp3", false), Eq(std::nullopt));
}

TEST_F(FuzzyScoreTest, FzfExtendedSearchMatchesUpstreamsCompoundParserCase) {
  // This combines every operator exactly as fzf's TestParseTermsExtended does. In particular,
  // `!'` means inverse FUZZY (not inverse exact), and each `|` extends only the adjacent OR set.
  // https://github.com/junegunn/fzf/blob/master/src/pattern_test.go
  constexpr std::string_view query = "aaa 'bbb ^ccc ddd$ !eee !'fff !^ggg !hhh$ | ^iii$ ^xxx | 'yyy | zzz$ | !ZZZ |";
  EXPECT_THAT(FzfPercent(query, "ccc-aaabbb-ddd", false), Optional(Eq(79)));
  // Immediate variations prove that the compound parser feeds the scoring model rather than merely
  // returning an in-range sentinel. Separating the exact `bbb` term improves its boundary bonus;
  // spreading `aaa` apart weakens its fuzzy alignment.
  EXPECT_THAT(FzfPercent(query, "ccc-aaa-bbb-ddd", false), Optional(Eq(83)));
  EXPECT_THAT(FzfPercent(query, "ccc-xaaxax-bbb-ddd", false), Optional(Eq(69)));
  EXPECT_THAT(FzfPercent(query, "ccc-aaabbb-x-ddd", false), Optional(Eq(79)));
  EXPECT_THAT(FzfPercent(query, "ccc-aaabbb-fff-ddd", false), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent(query, "ccc-aaabbb-eee-ddd", false), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent(query, "xxx-ccc-aaabbb-ddd", false), Eq(std::nullopt));
}

TEST_F(FuzzyScoreTest, FzfAnchorsIgnoreCandidateEdgeWhitespaceLikeUpstream) {
  // fzf's PrefixMatch, SuffixMatch, and EqualMatch ignore candidate edge whitespace unless the
  // query explicitly begins or ends in whitespace. EqualMatch's own upstream test pins all three.
  // https://github.com/junegunn/fzf/blob/master/src/pattern_test.go#L83
  EXPECT_THAT(FzfPercent("^AbC$", "AbC", false), Optional(Eq(100)));
  EXPECT_THAT(FzfPercent("^AbC$", "  AbC  ", false), Optional(AllOf(Ge(0), Le(100))));
  EXPECT_THAT(FzfPercent("^AbC$", "ABC", false), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent("^music .mp3$", "  music-box.mp3  ", false), Optional(AllOf(Ge(0), Le(100))));
}

TEST_F(FuzzyScoreTest, FzfExtendedSearchPreservesEscapedSpaces) {
  EXPECT_THAT(FzfPercent("foo\\ bar baz", "prefix foo bar and baz", false), Optional(AllOf(Ge(0), Le(100))));
  EXPECT_THAT(FzfPercent("foo\\ bar baz", "foo_then_bar-baz", false), Eq(std::nullopt));
}

TEST_F(FuzzyScoreTest, FzfExtendedSearchHandlesEmptyAndDegenerateTerms) {
  EXPECT_THAT(FzfPercent("", "anything", false), Optional(Eq(100)));
  EXPECT_THAT(FzfPercent("   ", "anything", false), Optional(Eq(100)));
  EXPECT_THAT(FzfPercent("! ^", "anything", false), Optional(Eq(100)));
  EXPECT_THAT(FzfPercent("''", "anything", false), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent("$", "a-dollar-$", false), Optional(AllOf(Ge(0), Le(100))));
  EXPECT_THAT(FzfPercent("foo |", "foo", false), Optional(Eq(100)));
  EXPECT_THAT(FzfPercent("| foo", "foo", false), Eq(std::nullopt));
}

TEST_F(FuzzyScoreTest, FzfBoundaryTermsRequireBothWordEdges) {
  EXPECT_THAT(FzfPercent("'word'", "word", false), Optional(Eq(100)));
  EXPECT_THAT(FzfPercent("'word'", "word!", false), Optional(Eq(100)));
  EXPECT_THAT(FzfPercent("'word'", "!word", false), Optional(AllOf(Ge(0), Lt(100))));
  EXPECT_THAT(FzfPercent("'word'", "sword!", false), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent("'word'", "!words", false), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent("'longer'", "tiny", false), Eq(std::nullopt));
}

TEST_F(FuzzyScoreTest, FzfAnchorsRejectCandidatesShorterThanTheirTerms) {
  EXPECT_THAT(FzfPercent("^long", "lo", false), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent("long$", "ng", false), Eq(std::nullopt));
  EXPECT_THAT(FzfPercent("^long$", "longer", false), Eq(std::nullopt));
}

TEST_F(FuzzyScoreTest, FzfScoringMatchesPublishedRealWorldCases) {
  // Candidate/query pairs maintained by fzf itself exercise camel humps, word boundaries, and path
  // separators. They are drawn from src/algo/algo_test.go in the upstream fzf repository.
  EXPECT_THAT(FzfPercent("oBZ", "fooBarbaz1", true), Optional(Eq(72)));
  EXPECT_THAT(FzfPercent("fbb", "foo bar baz", false), Optional(Eq(91)));
  EXPECT_THAT(FzfPercent("rdoc", "/AutomatorDocument.icns", true), Optional(Eq(90)));
  EXPECT_THAT(FzfPercent("zshc", "/man1/zshcompctl.1", false), Optional(Eq(93)));
  EXPECT_THAT(FzfPercent("zshc", "/.oh-my-zsh/cache", false), Optional(Eq(90)));
}

TEST_F(FuzzyScoreTest, FzfScoringOrdersNearbyBoundaryAndGapVariants) {
  const int camel = FzfPercent("oBZ", "fooBarbaz1", true).value_or(-1);
  const int underscores = FzfPercent("oBZ", "foo_bar_baz1", true).value_or(-1);
  const int long_gaps = FzfPercent("oBZ", "foo---bar---baz1", true).value_or(-1);
  EXPECT_THAT(camel, Eq(72));
  EXPECT_THAT(underscores, Eq(60));
  EXPECT_THAT(long_gaps, Eq(55));
  EXPECT_THAT(camel, Gt(underscores));
  EXPECT_THAT(underscores, Gt(long_gaps));
}

TEST_F(FuzzyScoreTest, LevenshteinPercentPinsConcreteSpellingSimilarities) {
  EXPECT_THAT(LevenshteinPercent("foo", "foo", false), Eq(100));
  EXPECT_THAT(LevenshteinPercent("foo", "fool", false), Eq(75));
  EXPECT_THAT(LevenshteinPercent("foo", "fof", false), Eq(67));
  EXPECT_THAT(LevenshteinPercent("foo", "ffo", false), Eq(67));
  EXPECT_THAT(LevenshteinPercent("foo", "ofo", false), Eq(33));
  EXPECT_THAT(LevenshteinPercent("foo", "oof", false), Eq(33));
  EXPECT_THAT(LevenshteinPercent("foo", "off", false), Eq(0));
  EXPECT_THAT(LevenshteinPercent("bar", "baz", false), Eq(67));
}

TEST_F(FuzzyScoreTest, LevenshteinPercentHandlesCaseAndEmptyStrings) {
  EXPECT_THAT(LevenshteinPercent("Foo", "foo", false), Eq(67));
  EXPECT_THAT(LevenshteinPercent("Foo", "foo", true), Eq(100));
  EXPECT_THAT(LevenshteinPercent("", "", false), Eq(100));
  EXPECT_THAT(LevenshteinPercent("", "foo", false), Eq(0));
  EXPECT_THAT(LevenshteinPercent("foo", "", false), Eq(0));
}

TEST_F(FuzzyScoreTest, LevenshteinPercentPinsClassicMultiEditCases) {
  EXPECT_THAT(LevenshteinPercent("kitten", "sitting", false), Eq(57));
  EXPECT_THAT(LevenshteinPercent("flaw", "lawn", false), Eq(50));
  EXPECT_THAT(LevenshteinPercent("intention", "execution", false), Eq(44));
}

TEST_F(FuzzyScoreTest, SequencePercentIsPlainSubsequenceCoverage) {
  EXPECT_THAT(SequencePercent("foo", "foo", false), Optional(Eq(100)));
  EXPECT_THAT(SequencePercent("foo", "fool", false), Optional(Eq(75)));
  EXPECT_THAT(SequencePercent("foo", "f_o_o", false), Optional(Eq(60)));
  EXPECT_THAT(SequencePercent("foo", "ofo", false), Eq(std::nullopt));
  EXPECT_THAT(SequencePercent("bar", "baz", false), Eq(std::nullopt));
  EXPECT_THAT(SequencePercent("", "", false), Optional(Eq(100)));
}

TEST_F(FuzzyScoreTest, SequencePercentHandlesLongPathAbbreviationsWithoutTermSyntax) {
  EXPECT_THAT(SequencePercent("s/m/h", "src/main/include/main_header.h", false), Optional(Eq(17)));
  EXPECT_THAT(SequencePercent("s/m/h", "src/helper/main.cc", false), Eq(std::nullopt));
  EXPECT_THAT(SequencePercent("a | b", "alpha | beta", false), Optional(Eq(42)));
}

TEST_F(FuzzyScoreTest, ShinglePercentPinsTheSmallPermutationTable) {
  EXPECT_THAT(ShinglePercent("foo", "foo", false), Eq(100));
  EXPECT_THAT(ShinglePercent("foo", "oof", false), Eq(33));
  EXPECT_THAT(ShinglePercent("foo", "ofo", false), Eq(33));
  EXPECT_THAT(ShinglePercent("foo", "off", false), Eq(0));
  EXPECT_THAT(ShinglePercent("foo", "fof", false), Eq(33));
  EXPECT_THAT(ShinglePercent("foo", "ffo", false), Eq(33));
}

TEST_F(FuzzyScoreTest, ShinglePercentHandlesCaseAndShortStrings) {
  EXPECT_THAT(ShinglePercent("Foo", "foo", false), Eq(33));
  EXPECT_THAT(ShinglePercent("Foo", "foo", true), Eq(100));
  EXPECT_THAT(ShinglePercent("", "", false), Eq(100));
  EXPECT_THAT(ShinglePercent("f", "f", false), Eq(100));
  EXPECT_THAT(ShinglePercent("f", "o", false), Eq(0));
  EXPECT_THAT(ShinglePercent("f", "ff", false), Eq(0));
}

TEST_F(FuzzyScoreTest, ShinglePercentPinsLongerSetOverlapCases) {
  EXPECT_THAT(ShinglePercent("night", "nacht", false), Eq(14));
  EXPECT_THAT(ShinglePercent("context", "contact", false), Eq(33));
  EXPECT_THAT(ShinglePercent("directory", "dictionary", false), Eq(21));
}

TEST_F(FuzzyScoreTest, WordStartsBeatCharactersBuriedInsideWords) {
  // The whole point of ranking: `tmh` typed as an abbreviation means the initials.
  EXPECT_THAT(ScoreOf("tmh", "the_main_header.h"), Gt(ScoreOf("tmh", "automath.hpp")));
  EXPECT_THAT(ScoreOf("rdme", "README.md", true), Gt(ScoreOf("rdme", "unrelated_me.txt", true)));
}

TEST_F(FuzzyScoreTest, ConsecutiveCharactersBeatScatteredOnes) {
  EXPECT_THAT(ScoreOf("abc", "xabcx"), Gt(ScoreOf("abc", "xaxbxcx")));
}

TEST_F(FuzzyScoreTest, AnEarlierMatchBeatsALaterOne) {
  EXPECT_THAT(ScoreOf("abc", "abc_zzzzzzzz"), Gt(ScoreOf("abc", "zzzzzzzz_abc")));
}

TEST_F(FuzzyScoreTest, MoreMatchedCharactersAlwaysWin) {
  // The per-character weight is far larger than any bonus, so a longer match cannot lose to a
  // shorter one that happens to sit on nicer boundaries.
  EXPECT_THAT(ScoreOf("main", "the_main_header.h"), Gt(ScoreOf("mh", "the_main_header.h")));
}

TEST_F(FuzzyScoreTest, ALaterAlignmentIsTakenWhenItIsTheBetterOne) {
  // `a_ab` holds two alignments of `ab`: the greedy one (the first `a`, then the only `b`, scattered)
  // and the adjacent pair at the end. Taking the better one is the whole reason scoring needs an
  // alignment search rather than the left-to-right scan Matches gets away with - here it must score
  // as well as `_ab` does, where only the good alignment exists.
  EXPECT_THAT(ScoreOf("ab", "a_ab"), Eq(ScoreOf("ab", "x_ab")));
}

TEST_F(FuzzyScoreTest, CamelCaseHumpsCountAsWordStarts) {
  // Folding, because a hump is by definition upper case and the pattern people type is not.
  EXPECT_THAT(ScoreOf("mh", "theMainHeader", true), Gt(ScoreOf("mh", "themainheader", true)));
}

TEST_F(FuzzyScoreTest, FoldingCaseChangesWhatMatchesNotHowItRanks) {
  EXPECT_THAT(Score("TMH", "the_main_header.h", false), Eq(std::nullopt));
  EXPECT_THAT(Score("TMH", "the_main_header.h", true), Optional(Eq(ScoreOf("tmh", "the_main_header.h"))));
}

TEST_F(FuzzyScoreTest, EveryScoredMatchIsAlsoAMatch) {
  // The two entry points must never disagree: whatever Matches accepts, Score must score.
  static constexpr std::array kCases = std::to_array<std::pair<std::string_view, std::string_view>>({
      {"tmh", "the_main_header.h"},
      {"abc", "aabbcc"},
      {"h", "h"},
      {"zz", "z"},
      {"", "anything"},
      {"xyz", "the_main_header.h"},
  });
  for (const auto& [pattern, text] : kCases) {
    if (Matches(pattern, text, false)) {
      EXPECT_THAT(Score(pattern, text, false), Optional(_)) << pattern << " / " << text;
    } else {
      EXPECT_THAT(Score(pattern, text, false), Eq(std::nullopt)) << pattern << " / " << text;
    }
  }
}

}  // namespace
}  // namespace xff::fuzzy
