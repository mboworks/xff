// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/presentation/patch/patch.h"

#include <array>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::patch {
namespace {
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;

struct PatchTest : ::testing::Test {};

TEST_F(PatchTest, TextAndMissingFinalNewline) {
  EXPECT_THAT(
      Creation("file", "one\ntwo", "100644"),
      IsOkAndHolds(AllOf(
          HasSubstr("new file mode 100644\n"),
          HasSubstr("@@ -0,0 +1,2 @@\n+one\n+two\n\\ No newline at end of file\n"))));
  EXPECT_THAT(
      Creation("file", "one\n", "100755"),
      IsOkAndHolds(AllOf(HasSubstr("new file mode 100755\n"), Not(HasSubstr("No newline")))));
}

TEST_F(PatchTest, EmptyFileAndSymlink) {
  EXPECT_THAT(
      Creation("empty", "", "100644"),
      IsOkAndHolds(AllOf(HasSubstr("e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"), Not(HasSubstr("@@")))));
  EXPECT_THAT(
      Creation("link", "target", "120000"),
      IsOkAndHolds(AllOf(HasSubstr("new file mode 120000"), HasSubstr("+target\n"))));
}

TEST_F(PatchTest, BinaryLiteralAndMultipleStoredBlocks) {
  EXPECT_THAT(
      Creation("binary", std::string(70'000, '\0'), "100644"),
      IsOkAndHolds(AllOf(HasSubstr("GIT binary patch\nliteral 70000\n"), HasSubstr("literal 0\n"))));
}

TEST_F(PatchTest, QuoteUnusualNames) {
  EXPECT_THAT(Creation("space \t\n\"\\file", "x", "100644"), IsOkAndHolds(HasSubstr(R"("b/space \011\012\"\\file")")));
}

TEST_F(PatchTest, RejectUnsafePathsAndUnsupportedModes) {
  constexpr auto kPaths =
      std::to_array<std::string_view>({"", "/abs", "../file", "a/../b", "./file", "a//b", ".git/config"});
  for (const auto path : kPaths) {
    EXPECT_THAT(Creation(path, "x", "100644"), StatusIs(absl::StatusCode::kInvalidArgument));
  }
  EXPECT_THAT(Creation(std::string("a\0b", 3), "x", "100644"), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Creation("file", "x", "040755"), StatusIs(absl::StatusCode::kInvalidArgument));
}
}  // namespace
}  // namespace xff::patch
