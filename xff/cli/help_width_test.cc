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

#include "xff/cli/help_width.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/env/env.h"

namespace xff::cli {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::HasSubstr;

struct ResolveHelpWidthTest : ::testing::Test {
  void TearDown() override {
    env::ClearForTesting();
    if (saved_stdout_ >= 0) {
      EXPECT_THAT(::dup2(saved_stdout_, STDOUT_FILENO), Eq(STDOUT_FILENO));
      EXPECT_THAT(::close(saved_stdout_), Eq(0));
    }
    if (pty_master_ >= 0) {
      EXPECT_THAT(::close(pty_master_), Eq(0));
    }
  }

  void AttachStdoutTerminal(std::uint16_t columns) {
    pty_master_ = ::posix_openpt(O_RDWR | O_CLOEXEC);
    ASSERT_THAT(pty_master_, Ge(0));
    ASSERT_THAT(::grantpt(pty_master_), Eq(0));
    ASSERT_THAT(::unlockpt(pty_master_), Eq(0));
    std::array<char, 128> slave_name{};
    ASSERT_THAT(::ptsname_r(pty_master_, slave_name.data(), slave_name.size()), Eq(0));
    // NOLINTNEXTLINE(hicpp-vararg,cppcoreguidelines-pro-type-vararg): POSIX open, without a mode argument.
    const int slave = ::open(slave_name.data(), O_RDWR | O_CLOEXEC);
    ASSERT_THAT(slave, Ge(0));
    // NOLINTNEXTLINE(hicpp-vararg,cppcoreguidelines-pro-type-vararg): fcntl is the POSIX close-on-exec dup API.
    saved_stdout_ = ::fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
    ASSERT_THAT(saved_stdout_, Ge(0));
    struct winsize size = {.ws_col = columns};
    // NOLINTNEXTLINE(hicpp-vararg,cppcoreguidelines-pro-type-vararg): ioctl is the POSIX terminal API.
    ASSERT_THAT(::ioctl(slave, TIOCSWINSZ, &size), Eq(0));
    ASSERT_THAT(::dup2(slave, STDOUT_FILENO), Eq(STDOUT_FILENO));
    ASSERT_THAT(::close(slave), Eq(0));
  }

 private:
  int saved_stdout_ = -1;
  int pty_master_ = -1;
};

TEST_F(ResolveHelpWidthTest, AbsentFlagCapsTheDetectedTerminalWidth) {
  EXPECT_THAT(ResolveHelpWidth(std::nullopt, 120), IsOkAndHolds(Eq(110U)));
}

TEST_F(ResolveHelpWidthTest, AbsentFlagUsesDefaultCapWhenTerminalUnknown) {
  EXPECT_THAT(ResolveHelpWidth(std::nullopt, 0), IsOkAndHolds(Eq(110U)));
}

TEST_F(ResolveHelpWidthTest, ExplicitAutoIsUncappedAndCaseInsensitive) {
  EXPECT_THAT(ResolveHelpWidth("auto", 100), IsOkAndHolds(Eq(100U)));
  EXPECT_THAT(ResolveHelpWidth("AUTO", 0), IsOkAndHolds(Eq(0U)));
}

TEST_F(ResolveHelpWidthTest, CappedAutoHandlesKnownAndUnknownWidths) {
  EXPECT_THAT(ResolveHelpWidth("auto:120", 80), IsOkAndHolds(Eq(80U)));
  EXPECT_THAT(ResolveHelpWidth("auto:120", 120), IsOkAndHolds(Eq(120U)));
  EXPECT_THAT(ResolveHelpWidth("auto:120", 200), IsOkAndHolds(Eq(120U)));
  EXPECT_THAT(ResolveHelpWidth("AUTO:120", 0), IsOkAndHolds(Eq(120U)));
  EXPECT_THAT(ResolveHelpWidth("auto:40", 20), IsOkAndHolds(Eq(40U)));
  EXPECT_THAT(ResolveHelpWidth(std::nullopt, 80), IsOkAndHolds(Eq(80U)));
  EXPECT_THAT(ResolveHelpWidth("auto", 200), IsOkAndHolds(Eq(200U)));
}

TEST_F(ResolveHelpWidthTest, RejectsInvalidCaps) {
  static constexpr std::array kInvalidWidthCaps = std::to_array<std::string_view>({
      "auto:",
      "auto:wide",
      "auto:-1",
      "auto:0",
      "auto:39",
      "auto:999999999999999999999999999999999999",
  });
  for (const std::string_view value : kInvalidWidthCaps) {
    EXPECT_THAT(ResolveHelpWidth(value, 80), StatusIs(absl::StatusCode::kInvalidArgument)) << value;
  }
}

TEST_F(ResolveHelpWidthTest, NoneAndZeroDisableWrapping) {
  EXPECT_THAT(ResolveHelpWidth("none", 120), IsOkAndHolds(Eq(0U)));
  EXPECT_THAT(ResolveHelpWidth("NONE", 120), IsOkAndHolds(Eq(0U)));
  EXPECT_THAT(ResolveHelpWidth("0", 120), IsOkAndHolds(Eq(0U)));
}

TEST_F(ResolveHelpWidthTest, APositiveIntegerIsAFixedWidthIgnoringTheTerminal) {
  EXPECT_THAT(ResolveHelpWidth("72", 120), IsOkAndHolds(Eq(72U)));
}

TEST_F(ResolveHelpWidthTest, ClampsAPositiveWidthUpToTheMinimum) {
  EXPECT_THAT(ResolveHelpWidth("20", 0), IsOkAndHolds(Eq(kMinHelpWidth)));           // an explicit narrow width
  EXPECT_THAT(ResolveHelpWidth(std::nullopt, 25), IsOkAndHolds(Eq(kMinHelpWidth)));  // a narrow terminal
  EXPECT_THAT(ResolveHelpWidth("none", 0), IsOkAndHolds(Eq(0U)));                    // 0 (no wrap) is exempt
}

TEST_F(ResolveHelpWidthTest, ANonNumericValueIsAnError) {
  EXPECT_THAT(ResolveHelpWidth("wide", 0), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("--width")));
}

TEST_F(ResolveHelpWidthTest, ANegativeValueIsAnError) {
  EXPECT_THAT(ResolveHelpWidth("-5", 0), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ResolveHelpWidthTest, ColumnsEnvironmentOverridesTerminalDetection) {
  env::SetForTesting("COLUMNS", "137");
  EXPECT_THAT(DetectTerminalWidth(), Eq(137U));
}

TEST_F(ResolveHelpWidthTest, InvalidColumnsEnvironmentFallsBackToTerminalDetection) {
  env::SetForTesting("COLUMNS", "wide");
  EXPECT_THAT(DetectTerminalWidth(), Eq(0U));
  env::SetForTesting("COLUMNS", "0");
  EXPECT_THAT(DetectTerminalWidth(), Eq(0U));
  env::ClearForTesting();
  env::SetForTesting("COLUMNS", std::nullopt);
  EXPECT_THAT(DetectTerminalWidth(), Eq(0U));
}

TEST_F(ResolveHelpWidthTest, TerminalIoctlReportsPositiveAndZeroWidths) {
  env::SetForTesting("COLUMNS", std::nullopt);
  AttachStdoutTerminal(123);
  EXPECT_THAT(DetectTerminalWidth(), Eq(123U));

  struct winsize size = {};
  // NOLINTNEXTLINE(hicpp-vararg,cppcoreguidelines-pro-type-vararg): ioctl is the POSIX terminal API.
  ASSERT_THAT(::ioctl(STDOUT_FILENO, TIOCSWINSZ, &size), Eq(0));
  EXPECT_THAT(DetectTerminalWidth(), Eq(0U));
}

}  // namespace
}  // namespace xff::cli
