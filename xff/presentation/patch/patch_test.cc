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
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;

struct PatchTest : ::testing::Test {};

TEST_F(PatchTest, MapPathsInFixedOrderAndAccumulateRewrites) {
  ASSERT_OK_AND_ASSIGN(
      const auto paths, Paths::Resolve({
                            "--diff-left-prefix=old/",
                            "--diff-left-rewrite=s|^injected/|mapped/|",
                            "--diff-left-root=discarded",
                            "--diff-left-strip=1",
                            "--diff-left-root=injected/",
                            "--diff-left-rewrite=s|generated|source|;s|[.]in$||",
                            "--diff-right-prefix=",
                        }));
  EXPECT_THAT(paths.left.Map("vendor/generated/file.cc.in"), IsOkAndHolds(Eq("old/mapped/source/file.cc")));
  EXPECT_THAT(paths.right.Map("vendor/generated/file.cc.in"), IsOkAndHolds(Eq("vendor/generated/file.cc.in")));
}

TEST_F(PatchTest, DefaultsAndLiteralPrefixes) {
  ASSERT_OK_AND_ASSIGN(const auto defaults, Paths::Resolve({}));
  EXPECT_THAT(defaults.left.Map("src/file"), IsOkAndHolds(Eq("a/src/file")));
  EXPECT_THAT(defaults.right.Map("src/file"), IsOkAndHolds(Eq("b/src/file")));
  ASSERT_OK_AND_ASSIGN(const auto paths, Paths::Resolve({"--diff-left-prefix=literal-", "--diff-left-root=project"}));
  EXPECT_THAT(paths.left.Map("file"), IsOkAndHolds(Eq("literal-project/file")));
}

TEST_F(PatchTest, ValidateOptionsBeforeAnyFilesAreMapped) {
  const auto invalid = std::to_array<std::string_view>({
      "--diff-left-strip=-1",
      "--diff-left-strip=+1",
      "--diff-left-strip=",
      "--diff-left-strip=99999999999999999999999999",
      "--diff-right-strip=2x",
      "--diff-left-root=/absolute",
      "--diff-right-root=../escape",
      "--diff-left-root=/",
      "--diff-left-prefix=../",
      "--diff-right-prefix=/",
      "--diff-left-root=.git",
      "--diff-left-rewrite=",
      "--diff-right-rewrite=s|[|x|",
      "--diff-left-rewrite=s|x|y|z",
      "--diff-left-rewrite=s|x|y|;",
      "--diff-right-rewrite=m|x|y|",
      "--diff-left-rewrite=s|x|y|;m|x|z|",
      R"(--diff-right-rewrite=s|x|\1|)",
  });
  for (const auto option : invalid) {
    EXPECT_THAT(Paths::Resolve({std::string(option)}), StatusIs(absl::StatusCode::kInvalidArgument)) << option;
  }
}

TEST_F(PatchTest, RejectInvalidFinalPathsAndExcessiveStripping) {
  ASSERT_OK_AND_ASSIGN(const auto stripped, Paths::Resolve({"--diff-left-strip=2"}));
  EXPECT_THAT(stripped.left.Map("dir/file"), StatusIs(absl::StatusCode::kInvalidArgument));
  constexpr auto kReplacements = std::to_array<std::string_view>({
      "",
      "/absolute",
      "../escape",
      ".git/config",
      "dir//file",
      "dir/./file",
  });
  for (const auto replacement : kReplacements) {
    ASSERT_OK_AND_ASSIGN(
        const auto paths, Paths::Resolve({std::string("--diff-right-rewrite=s|.*|") + std::string(replacement) + "|"}));
    EXPECT_THAT(paths.right.Map("file"), StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

TEST_F(PatchTest, RewriteCanRelocateAnOtherwiseForbiddenDestination) {
  ASSERT_OK_AND_ASSIGN(const auto paths, Paths::Resolve({"--diff-left-rewrite=s|^[.]git/|git-data/|"}));
  EXPECT_THAT(paths.left.Map(".git/config"), IsOkAndHolds(Eq("a/git-data/config")));
  EXPECT_THAT(paths.right.Map(".git/config"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(PatchTest, DetectCollisionsIndependentlyOnEachSide) {
  Destinations destinations;
  const Names names{.left = "a/file", .right = "b/file"};
  EXPECT_THAT(destinations.Claim(names, "physical-old", "physical-new"), IsOk());
  EXPECT_THAT(destinations.Claim(names, "physical-old", "physical-new"), IsOk());
  EXPECT_THAT(
      destinations.Claim(names, "other-old", "physical-new"),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("collision")));
  EXPECT_THAT(
      destinations.Claim(names, "physical-old", "other-new"),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("collision")));
  EXPECT_THAT(destinations.Claim({.left = "b/file", .right = "a/file"}, "other-old", "other-new"), IsOk());
}

TEST_F(PatchTest, DeletionSwapsHeadersContentAndBinaryLiterals) {
  const Names names{.left = "old/file", .right = "new/file"};
  EXPECT_THAT(
      AbsentSide(names, "text", "100755", true),
      IsOkAndHolds(AllOf(
          HasSubstr("diff --git \"old/file\" \"new/file\"\ndeleted file mode 100755\n"),
          HasSubstr("--- \"old/file\"\n+++ /dev/null\n@@ -1,1 +0,0 @@\n-text\n\\ No newline"))));
  EXPECT_THAT(
      AbsentSide(names, std::string(3, '\0'), "100644", true),
      IsOkAndHolds(AllOf(HasSubstr("GIT binary patch\nliteral 0\n"), HasSubstr("literal 3\n"))));
  EXPECT_THAT(
      AbsentSide(names, "", "120000", true),
      IsOkAndHolds(AllOf(HasSubstr("deleted file mode 120000"), Not(HasSubstr("@@")))));
}

TEST_F(PatchTest, HeaderQuotingOnlyWhenNecessary) {
  EXPECT_THAT(HeaderName("a/src/file.cc"), Eq("a/src/file.cc"));
  EXPECT_THAT(HeaderName("a/a\tb\n\"\\"), Eq(R"("a/a\011b\012\"\\")"));
}

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
