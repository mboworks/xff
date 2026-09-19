// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/diagnostics.h"

#include <array>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/parser/parser.h"

namespace xff::cli {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;

struct DiagnosticsTest : ::testing::Test {};

TEST_F(DiagnosticsTest, SuggestsOneEditIncludingTranspositionWithoutGuessingValues) {
  static constexpr std::array kSummarySpellingMistakes = std::to_array<std::string_view>({
      "--sumary=ext",
      "--summaryy=ext",
      "--summxry=ext",
      "--summray=ext",
      "--summar=ext",
      "--xsummary=ext",
  });
  for (const std::string_view token : kSummarySpellingMistakes) {
    EXPECT_THAT(UnknownGlobalHint(token), HasSubstr("'--summary'"));
    EXPECT_THAT(UnknownGlobalHint(token), Not(HasSubstr("=ext")));
  }
}

TEST_F(DiagnosticsTest, SuggestsHelpFlagsTopicsPrimariesAndAliases) {
  EXPECT_THAT(UnknownGlobalHint("--wildth"), HasSubstr("'--width'"));
  EXPECT_THAT(UnknownHelpHint("wildth"), HasSubstr("'--help=width'"));
  EXPECT_THAT(UnknownHelpHint("--wildth"), HasSubstr("'--help=--width'"));
  EXPECT_THAT(UnknownHelpHint("comapre"), Eq("Did you mean '--help=compare'?\n"));
  EXPECT_THAT(UnknownHelpHint("recpies"), HasSubstr("'--help=recipes'"));
  EXPECT_THAT(UnknownHelpHint("-naem"), HasSubstr("'--help=-name'"));
  EXPECT_THAT(UnknownHelpHint("WILDTH"), Eq(UnknownHelpHint("wildth")));
  EXPECT_THAT(UnknownHelpHint("--require-system-globalz"), HasSubstr("'--help=--require-system-globals'"));
}

TEST_F(DiagnosticsTest, HelpHintsDoNotGuessKnownSelectorsValuesOrUnrelatedInput) {
  static constexpr std::array kSelectorsWithoutSuggestions = std::to_array<std::string_view>({
      "width",
      "--width",
      "recipes",
      "regex",
      "zzzzzzzz",
      "x",
      "license=wildth",
  });
  for (const std::string_view selector : kSelectorsWithoutSuggestions) {
    EXPECT_THAT(UnknownHelpHint(selector), Eq("")) << selector;
  }
  EXPECT_THAT(UnknownHelpHint(std::string(10'000, 'x')), Eq(""));
}

TEST_F(DiagnosticsTest, RanksMultiEditLongNamesAboveWeakerMatches) {
  EXPECT_THAT(UnknownGlobalHint("--summary-scp=all"), HasSubstr("Did you mean '--summary-scope'"));
  EXPECT_THAT(UnknownGlobalHint("--summary-scp=all"), Not(HasSubstr("=all")));
}

TEST_F(DiagnosticsTest, DoesNotGuessUnrelatedShortOrConfigOnlyNames) {
  static constexpr std::array kFlagsWithoutSuggestions = std::to_array<std::string_view>({
      "--zzzzzzzz",
      "-x",
      "--require-system-globalz",
      "--summary",
      "-sumary",
  });
  for (const std::string_view token : kFlagsWithoutSuggestions) {
    EXPECT_THAT(UnknownGlobalHint(token), Eq(""));
  }
  EXPECT_THAT(UnknownGlobalHint(std::string(10'000, 'x')), Eq(""));
}

TEST_F(DiagnosticsTest, PredicateSuggestionUsesParserProvenance) {
  const auto parsed = parser::Parse({".", "-naem"});
  EXPECT_THAT(parsed, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ParseErrorHint(parsed.status()), HasSubstr("'-name'"));
  EXPECT_THAT(UnknownGlobalHint("-naem"), HasSubstr("'-name'"));
  EXPECT_THAT(ParseErrorHint(absl::InvalidArgumentError("unknown predicate: '-naem'")), Eq(""));
}

TEST_F(DiagnosticsTest, KeepsTheBestThreeSuggestionsWithDeterministicTies) {
  const auto parsed = parser::Parse({".", "-time"});
  EXPECT_THAT(parsed, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ParseErrorHint(parsed.status()), Eq("Did you mean '-Btime' or '-atime' or '-ctime'?\n"));
  EXPECT_THAT(UnknownGlobalHint("-time"), Eq(ParseErrorHint(parsed.status())));
}

TEST_F(DiagnosticsTest, ExplainsShortGlobalPlacementWithoutInventingLongForms) {
  const auto follow = parser::Parse({".", "-L"});
  EXPECT_THAT(follow, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ParseErrorHint(follow.status()), HasSubstr("before the roots"));
  EXPECT_THAT(ParseErrorHint(follow.status()), Not(HasSubstr("--L")));
  const auto ignore = parser::Parse({".", "-g+"});
  EXPECT_THAT(ignore, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ParseErrorHint(ignore.status()), HasSubstr("'--gitignore'"));
  EXPECT_THAT(parser::Parse({"-L", "-g+", "."}), IsOk());
}

TEST_F(DiagnosticsTest, LeavesArgumentRunsAndEndOfOptionsProtected) {
  EXPECT_THAT(parser::Parse({".", "-exec", "echo", "-naem", "--sumary", ";"}), IsOk());
  EXPECT_THAT(parser::Parse({".", "-printf", "--sumary"}), IsOk());
  const auto after_end = parser::Parse({".", "--", "-L"});
  EXPECT_THAT(after_end, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ParseErrorHint(after_end.status()), Eq(""));
}

}  // namespace
}  // namespace xff::cli
