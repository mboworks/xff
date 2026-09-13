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

#include "xff/cli/pager.h"

#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/env/env.h"

namespace xff::cli {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct PagerWhenTest : ::testing::Test {};

TEST_F(PagerWhenTest, AbsentResolvesToHelp) {
  EXPECT_THAT(ResolvePager({"xff", ".", "-type", "f"}), IsOkAndHolds(Field(&PagerConfig::when, PagerWhen::kHelp)));
}

TEST_F(PagerWhenTest, BarePagerIsAlways) {
  EXPECT_THAT(ResolvePager({"--pager"}), IsOkAndHolds(Field(&PagerConfig::when, PagerWhen::kAlways)));
}

TEST_F(PagerWhenTest, ExplicitValuesResolve) {
  EXPECT_THAT(ResolvePager({"--pager=help"}), IsOkAndHolds(Field(&PagerConfig::when, PagerWhen::kHelp)));
  EXPECT_THAT(ResolvePager({"--pager=always"}), IsOkAndHolds(Field(&PagerConfig::when, PagerWhen::kAlways)));
  EXPECT_THAT(ResolvePager({"--pager=never"}), IsOkAndHolds(Field(&PagerConfig::when, PagerWhen::kNever)));
  EXPECT_THAT(ResolvePager({"--pager=auto"}), IsOkAndHolds(Field(&PagerConfig::when, PagerWhen::kAuto)));
  EXPECT_THAT(ResolvePager({"--pager=most -s"}), IsOkAndHolds(Field(&PagerConfig::command, "most -s")));
}

TEST_F(PagerWhenTest, RemovedAllAndAnEmptyCommandAreErrors) {
  EXPECT_THAT(
      ResolvePager({"--pager=all"}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("invalid --pager value: all")));
  EXPECT_THAT(ResolvePager({"--pager="}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("requires")));
}

TEST_F(PagerWhenTest, NoPagerIsNever) {
  EXPECT_THAT(ResolvePager({"--no-pager"}), IsOkAndHolds(Field(&PagerConfig::when, PagerWhen::kNever)));
}

TEST_F(PagerWhenTest, LastOccurrenceWins) {
  EXPECT_THAT(
      ResolvePager({"--pager=always", "--pager=never"}), IsOkAndHolds(Field(&PagerConfig::when, PagerWhen::kNever)));
  EXPECT_THAT(ResolvePager({"--no-pager", "--pager"}), IsOkAndHolds(Field(&PagerConfig::when, PagerWhen::kAlways)));
}

struct PagerDecisionTest : ::testing::Test {};

constexpr std::array kPagerOutputs = std::to_array({PagerOutput::kMeta, PagerOutput::kListing});

TEST_F(PagerDecisionTest, HelpPagesOnlyMetaOutputOnATerminal) {
  EXPECT_THAT(DecidePager({.when = PagerWhen::kHelp}, PagerOutput::kMeta, true).action, PagerAction::kPage);
  EXPECT_THAT(DecidePager({.when = PagerWhen::kHelp}, PagerOutput::kListing, true).action, PagerAction::kDirect);
  EXPECT_THAT(DecidePager({.when = PagerWhen::kHelp}, PagerOutput::kMeta, false).action, PagerAction::kDirect);
}

TEST_F(PagerDecisionTest, AutoPagesEveryOutputOnlyOnATerminal) {
  for (const PagerOutput output : kPagerOutputs) {
    EXPECT_THAT(DecidePager({.when = PagerWhen::kAuto}, output, true).action, PagerAction::kPage);
    EXPECT_THAT(DecidePager({.when = PagerWhen::kAuto}, output, false).action, PagerAction::kDirect);
  }
}

TEST_F(PagerDecisionTest, AlwaysPagesAndNeverDoesNot) {
  for (const PagerOutput output : kPagerOutputs) {
    EXPECT_THAT(DecidePager({.when = PagerWhen::kAlways}, output, false).action, PagerAction::kPage);
    EXPECT_THAT(DecidePager({.when = PagerWhen::kNever}, output, true).action, PagerAction::kDirect);
  }
}

TEST_F(PagerDecisionTest, SuppressionOverridesPagingAndDecisionCarriesCommand) {
  EXPECT_THAT(
      DecidePager({.when = PagerWhen::kAlways, .command = "most"}, PagerOutput::kListing, true, true),
      AllOf(Field(&PagerDecision::action, PagerAction::kDirect), Field(&PagerDecision::command, IsEmpty())));
  EXPECT_THAT(
      DecidePager({.when = PagerWhen::kAlways, .command = "most"}, PagerOutput::kListing, false),
      AllOf(Field(&PagerDecision::action, PagerAction::kPage), Field(&PagerDecision::command, "most")));
}

// Man-pager resolution reads XFF_MANPAGER through the xff/env cache; inject it via the test seam.
// Start each case from a clean cache so real environment values and prior cases do not leak in.
struct PagerCommandTest : ::testing::Test {
  void SetUp() override {
    env::ClearForTesting();
    env::SetForTesting("XFF_PAGER", std::nullopt);
    env::SetForTesting("PAGER", std::nullopt);
    env::SetForTesting("XFF_MANPAGER", std::nullopt);
  }

  void TearDown() override { env::ClearForTesting(); }
};

TEST_F(PagerCommandTest, DefaultsToLessWithColorSafeFlags) {
  EXPECT_THAT(
      ResolvePagerCommand(),
      AllOf(HasSubstr("less -FRX"), HasSubstr("more"), HasSubstr("$XFF_PAGER"), HasSubstr("$PAGER")));
}

TEST_F(PagerCommandTest, EnvironmentDoesNotPreemptAutomaticDiscovery) {
  const std::string automatic = ResolvePagerCommand();
  env::SetForTesting("PAGER", "more");
  env::SetForTesting("XFF_PAGER", "most");
  EXPECT_THAT(ResolvePagerCommand(), Eq(automatic));
}

TEST_F(PagerCommandTest, ExplicitCommandOverridesAutomaticAndXffPager) {
  env::SetForTesting("XFF_PAGER", "most");
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kText, "bat --paging=always"), Eq("bat --paging=always"));
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan, "most"), AllOf(HasSubstr("mandoc"), HasSubstr("most")));
}

TEST_F(PagerCommandTest, ManDefaultFormatsWithMandoc) {
  // The kMan default is a roff formatter, not the plain text pager: it runs mandoc.
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), HasSubstr("mandoc"));
}

TEST_F(PagerCommandTest, XffManPagerOverridesTheManDefault) {
  env::SetForTesting("XFF_MANPAGER", "groff -mandoc -Tutf8 | less -R");
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), Eq("groff -mandoc -Tutf8 | less -R"));
}

TEST_F(PagerCommandTest, EmptyManPagerEnvDisablesManPaging) {
  env::SetForTesting("XFF_MANPAGER", "");
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), Eq(""));
}

TEST_F(PagerCommandTest, ManPagerIsIndependentOfTheTextPager) {
  // $XFF_PAGER is only a fallback for text and must not become the man command (which needs a roff
  // formatter); the man default stays mandoc-based.
  env::SetForTesting("XFF_PAGER", "most");
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kText), HasSubstr("less -FRX"));
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), HasSubstr("mandoc"));
}

struct PagerEmitTest : ::testing::Test {};

TEST_F(PagerEmitTest, HelpPagesMetaOutputOnATerminal) {
  const std::string sink =
      absl::StrCat(::testing::TempDir(), "/", ::testing::UnitTest::GetInstance()->current_test_info()->name());
  EmitPaged(
      "help text", DecidePager(
                       {.when = PagerWhen::kHelp, .command = absl::StrCat("cat > ", sink)}, PagerOutput::kMeta,
                       /*stdout_is_tty=*/true));
  std::ifstream in(sink);
  EXPECT_THAT(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), Eq("help text"));
}

struct PagerStreamTest : ::testing::Test {
  void SetUp() override {
    env::ClearForTesting();
    env::SetForTesting("XFF_PAGER", std::nullopt);
    env::SetForTesting("PAGER", std::nullopt);
  }

  void TearDown() override { env::ClearForTesting(); }

  // Runs `body` with a PagerStream built from `when` / `stdout_is_tty` / `suppressed` and a pager
  // that writes what it reads to `sink_path`, then returns what the pager received. An INACTIVE
  // stream writes nothing there (it never redirects stdout), which is exactly the distinction the
  // tests below need.
  static std::string PagedThrough(
      PagerConfig pager,
      bool stdout_is_tty,
      bool suppressed,
      const std::string& sink_path,
      const std::function<void()>& body) {
    pager.command = absl::StrCat("cat > ", sink_path);
    {
      const PagerStream stream(DecidePager(pager, PagerOutput::kListing, stdout_is_tty, suppressed));
      body();
    }  // the destructor restores stdout and waits for the pager, so the file is complete here
    std::ifstream in(sink_path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  }

  static std::string SinkPath() {
    return absl::StrCat(::testing::TempDir(), "/", ::testing::UnitTest::GetInstance()->current_test_info()->name());
  }
};

TEST_F(PagerStreamTest, AutoAndAlwaysPageEverythingOnATerminal) {
  // The whole point of the streaming form: the writer does not know it is being paged, so the
  // listing needs no pager-aware code path of its own.
  static constexpr std::array kListingModes = std::to_array({PagerWhen::kAuto, PagerWhen::kAlways});
  for (const PagerWhen when : kListingModes) {
    const std::string sink = absl::StrCat(SinkPath(), static_cast<int>(when));
    EXPECT_THAT(
        PagedThrough(
            {.when = when}, /*stdout_is_tty=*/true, /*suppressed=*/false, sink, [] { std::cout << "one\ntwo\n"; }),
        Eq("one\ntwo\n"));
  }
}

TEST_F(PagerStreamTest, NeverLeavesStdoutAlone) {
  EXPECT_THAT(
      PagedThrough(
          {.when = PagerWhen::kNever}, /*stdout_is_tty=*/true, /*suppressed=*/false, SinkPath(),
          [] { std::cout << "x" << std::flush; }),
      IsEmpty());
}

TEST_F(PagerStreamTest, HelpLeavesListingsAloneEvenOnATerminal) {
  EXPECT_THAT(
      PagedThrough(
          {.when = PagerWhen::kHelp}, /*stdout_is_tty=*/true, /*suppressed=*/false, SinkPath(),
          [] { std::cout << "x" << std::flush; }),
      IsEmpty());
}

TEST_F(PagerStreamTest, NoTerminalMeansNoPager) {
  // A listing forced through a pager in a pipeline would hand the pager's screen handling to the
  // next command, so auto is terminal-only. Always deliberately escapes that safety rule.
  EXPECT_THAT(
      PagedThrough(
          {.when = PagerWhen::kAuto}, /*stdout_is_tty=*/false, /*suppressed=*/false, SinkPath(),
          [] { std::cout << "x" << std::flush; }),
      IsEmpty());
  EXPECT_THAT(
      PagedThrough(
          {.when = PagerWhen::kAlways}, /*stdout_is_tty=*/false, /*suppressed=*/false,
          absl::StrCat(SinkPath(), "Always"), [] { std::cout << "x" << std::flush; }),
      Eq("x"));
}

TEST_F(PagerStreamTest, SuppressedMeansNoPager) {
  // The caller's veto for -ok / -exec and --quiet: the pager must not sit between the primary and
  // the user.
  static constexpr std::array kListingModes = std::to_array({PagerWhen::kAuto, PagerWhen::kAlways});
  for (const PagerWhen when : kListingModes) {
    const std::string sink = absl::StrCat(SinkPath(), static_cast<int>(when));
    EXPECT_THAT(
        PagedThrough(
            {.when = when}, /*stdout_is_tty=*/true, /*suppressed=*/true, sink, [] { std::cout << "x" << std::flush; }),
        IsEmpty());
  }
}

TEST_F(PagerStreamTest, FinishIsIdempotentAndRestoresStdout) {
  const std::string sink = SinkPath();
  PagerStream pager(DecidePager(
      PagerConfig{.when = PagerWhen::kAuto, .command = absl::StrCat("cat > ", sink)}, PagerOutput::kListing,
      /*stdout_is_tty=*/true));
  EXPECT_THAT(pager.Active(), IsTrue());
  std::cout << "paged\n";
  pager.Finish();
  EXPECT_THAT(pager.Active(), IsFalse());
  pager.Finish();  // a second call (and the destructor after it) must be harmless
  std::ifstream in(sink);
  EXPECT_THAT(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), Eq("paged\n"));
}

}  // namespace
}  // namespace xff::cli
