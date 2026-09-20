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

#include "xff/cli/main.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/license/notice.h"

namespace xff::cli {
namespace {
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

// This component exists only in the test binary and exercises per-component explanation paragraphs.
const license::Registrar kExplainedComponent{{
    .component = "Explained test component",
    .spdx = "MIT",
    .text = "Copyright test authors.\n\nExplanation kept with its component.",
}};

struct MainTest : ::testing::Test {};

TEST_F(MainTest, AccountLookupFailureStopsTheCliEvenWhenConfigSkipsAreRequested) {
  int calls = 0;
  const auto lookup = [&]() -> absl::StatusOr<config::ConfigPaths> {
    ++calls;
    return absl::UnavailableError("account service unavailable");
  };
  EXPECT_THAT(::xff::cli::Run("xff", {"--no-config", "--explain"}, lookup), Eq(2));
  EXPECT_THAT(calls, Eq(1));
}

TEST_F(MainTest, ExplicitHelpWidthAvoidsAccountLookup) {
  int calls = 0;
  const auto lookup = [&]() -> absl::StatusOr<config::ConfigPaths> {
    ++calls;
    return absl::UnavailableError("account service unavailable");
  };
  EXPECT_THAT(::xff::cli::Run("xff", {"--help=config", "--width=auto:100"}, lookup), Eq(0));
  EXPECT_THAT(calls, Eq(0));
}

TEST_F(MainTest, MetaHelpRemainsAvailableWhenAccountLookupFails) {
  int calls = 0;
  const auto lookup = [&]() -> absl::StatusOr<config::ConfigPaths> {
    ++calls;
    return absl::UnavailableError("account service unavailable");
  };
  EXPECT_THAT(::xff::cli::Run("xff", {"--help=config"}, lookup), Eq(0));
  EXPECT_THAT(calls, Eq(1));
}

TEST_F(MainTest, NoticeExplanationStartsASeparatePlainParagraph) {
  ::testing::internal::CaptureStdout();
  const int result = ::xff::cli::Run("xff", {"--help=notice", "--width=110", "--no-pager"});
  const std::string output = ::testing::internal::GetCapturedStdout();
  EXPECT_THAT(result, Eq(0));
  EXPECT_THAT(output, HasSubstr("Copyright test authors.\n\n  Explanation kept with its component."));
}

struct HelpFormatCase {
  std::string name;
  std::vector<std::string> args;
  std::string marker;
};

struct FormattedHelpTest : ::testing::TestWithParam<HelpFormatCase> {};

TEST_P(FormattedHelpTest, RendersSelectedTargetWithoutLoadingConfiguration) {
  const auto& param = GetParam();
  int calls = 0;
  const auto lookup = [&]() -> absl::StatusOr<config::ConfigPaths> {
    ++calls;
    return absl::UnavailableError("configuration must not be read for formatted help");
  };
  auto args = param.args;
  args.emplace_back("--no-pager");
  ::testing::internal::CaptureStdout();
  ::testing::internal::CaptureStderr();
  const int result = ::xff::cli::Run("/bin/xff", args, lookup);
  const std::string output = ::testing::internal::GetCapturedStdout();
  const std::string error = ::testing::internal::GetCapturedStderr();
  EXPECT_THAT(result, Eq(0));
  EXPECT_THAT(calls, Eq(0));
  EXPECT_THAT(output, HasSubstr(param.marker));
  EXPECT_THAT(error, IsEmpty());
}

INSTANTIATE_TEST_SUITE_P(
    Targets,
    FormattedHelpTest,
    ::testing::Values(
        HelpFormatCase{.name = "Usage", .args = {"--help", "--help-format=markdown"}, .marker = "**Usage:**"},
        HelpFormatCase{.name = "Topic", .args = {"--help=regex", "--help-format=html"}, .marker = "Regex grammars"},
        HelpFormatCase{.name = "Index", .args = {"--help=list", "--help-format=md"}, .marker = "## Help topics"},
        HelpFormatCase{.name = "Flag", .args = {"--help=width", "--help-format=markdown"}, .marker = "auto:110"},
        HelpFormatCase{.name = "Styles", .args = {"--help=styles", "--help-format=html"}, .marker = "<pre>"},
        HelpFormatCase{.name = "Extras", .args = {"--help=extras", "--help-format=markdown"}, .marker = "```"},
        HelpFormatCase{.name = "Notices", .args = {"--help=notices", "--help-format=md"}, .marker = "# Notices"},
        HelpFormatCase{
            .name = "NoticeMarkdownParagraph",
            .args = {"--help=notice", "--help-format=md"},
            .marker = "Copyright test authors.\n\n  Explanation kept with its component.",
        },
        HelpFormatCase{
            .name = "NoticeHtmlParagraph",
            .args = {"--help=notice", "--help-format=html"},
            .marker = "Copyright test authors.</p>\n<p>Explanation kept with its component.</p>",
        },
        HelpFormatCase{
            .name = "NoticeRoffParagraph",
            .args = {"--help=notice", "--help-format=roff"},
            .marker = "Copyright test authors.\n.PP\nExplanation kept with its component.",
        },
        HelpFormatCase{.name = "Full", .args = {"--help=full", "--help-format=markdown"}, .marker = "**Usage:**"},
        HelpFormatCase{.name = "Long", .args = {"--help=long", "--help-format=html"}, .marker = "<!doctype html>"},
        HelpFormatCase{.name = "Roff", .args = {"--help=regex", "--help-format=roff"}, .marker = ".TH"},
        HelpFormatCase{.name = "Man", .args = {"--man", "--help-format=roff"}, .marker = ".TH"},
        HelpFormatCase{
            .name = "RepeatedFormat",
            .args = {"--help=width", "--help-format=md", "--help-format=markdown"},
            .marker = "auto:110",
        }),
    [](const ::testing::TestParamInfo<HelpFormatCase>& info) { return info.param.name; });

struct HelpFormatErrorTest : ::testing::TestWithParam<HelpFormatCase> {};

TEST_P(HelpFormatErrorTest, RejectsInvalidSelectionWithoutPrintingHelp) {
  const auto& param = GetParam();
  ::testing::internal::CaptureStdout();
  ::testing::internal::CaptureStderr();
  const int result = ::xff::cli::Run("xff", param.args);
  const std::string output = ::testing::internal::GetCapturedStdout();
  const std::string error = ::testing::internal::GetCapturedStderr();
  EXPECT_THAT(result, Eq(2));
  EXPECT_THAT(output, IsEmpty());
  EXPECT_THAT(error, HasSubstr(param.marker));
}

INSTANTIATE_TEST_SUITE_P(
    InvalidSelections,
    HelpFormatErrorTest,
    ::testing::Values(
        HelpFormatCase{.name = "MissingValue", .args = {"--help", "--help-format"}, .marker = "requires a value"},
        HelpFormatCase{.name = "WithoutHelp", .args = {"--help-format=md"}, .marker = "requires help output"},
        HelpFormatCase{
            .name = "WithVersion",
            .args = {"--version", "--help-format=md"},
            .marker = "requires help output",
        },
        HelpFormatCase{
            .name = "Conflict",
            .args = {"--help", "--help-format=html", "--help-format=markdown"},
            .marker = "conflicting help formats",
        },
        HelpFormatCase{
            .name = "ManConflict",
            .args = {"--man", "--help-format=md"},
            .marker = "conflicting help formats"},
        HelpFormatCase{
            .name = "UnknownFormat",
            .args = {"--help", "--help-format=json"},
            .marker = "unknown help format"},
        HelpFormatCase{
            .name = "UnknownTopic",
            .args = {"--help=wildth", "--help-format=html"},
            .marker = "--help=width",
        },
        HelpFormatCase{
            .name = "RemovedShorthand",
            .args = {"--help=full:markdown", "--help-format=html"},
            .marker = "no help topic 'full:markdown'",
        }),
    [](const ::testing::TestParamInfo<HelpFormatCase>& info) { return info.param.name; });

}  // namespace
}  // namespace xff::cli
