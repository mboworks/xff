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
  for (const std::string_view token : std::to_array<std::string_view>(
           {"--sumary=ext", "--summaryy=ext", "--summxry=ext", "--summray=ext", "--summar=ext", "--xsummary=ext"})) {
    EXPECT_THAT(UnknownGlobalHint(token), HasSubstr("'--summary'"));
    EXPECT_THAT(UnknownGlobalHint(token), Not(HasSubstr("=ext")));
  }
}

TEST_F(DiagnosticsTest, DoesNotGuessUnrelatedShortOrConfigOnlyNames) {
  for (const std::string_view token :
       std::to_array<std::string_view>({"--zzzzzzzz", "-x", "--require-system-globalz", "--summary", "-sumary"})) {
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

TEST_F(DiagnosticsTest, SuppressesOverlyAmbiguousSuggestions) {
  const auto parsed = parser::Parse({".", "-time"});
  EXPECT_THAT(parsed, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ParseErrorHint(parsed.status()), Eq(""));
  EXPECT_THAT(UnknownGlobalHint("-time"), Eq(""));
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
