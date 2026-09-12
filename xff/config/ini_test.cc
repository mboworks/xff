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

#include "xff/config/ini.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::config {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

struct IniTest : ::testing::Test {};

TEST_F(IniTest, GlobalLinesRenderToCliTokens) {
  const SystemConfig cfg = ParseIni("--allow-no-config\n--color = auto\n-E\n");
  EXPECT_THAT(cfg.globals, ElementsAre("--allow-no-config", "--color=auto", "-E"));
  EXPECT_THAT(cfg.global_lines, SizeIs(3));
}

TEST_F(IniTest, GlobalLinesMayContainMultipleDirectivesAndArguments) {
  const SystemConfig cfg = ParseIni("--hidden --color=never\n-name foo -name bar");
  EXPECT_THAT(cfg.globals, ElementsAre("--hidden", "--color=never", "-name", "foo", "-name", "bar"));
}

TEST_F(IniTest, PolicyAndDefaultsHaveNoReservedMeaning) {
  const SystemConfig cfg = ParseIni("[defaults]\n--color=auto\n[policy]\n--hidden\n");
  ASSERT_THAT(cfg.named, SizeIs(2));
  EXPECT_THAT(cfg.named[0].name, Eq("defaults"));
  EXPECT_THAT(cfg.named[1].name, Eq("policy"));
}

TEST_F(IniTest, EverySectionNameDefinesAConfig) {
  const SystemConfig cfg = ParseIni("[unknown]\n--foo = bar\n");
  EXPECT_THAT(cfg.globals, IsEmpty());
  ASSERT_THAT(cfg.named, SizeIs(1));
  EXPECT_THAT(cfg.named[0].name, Eq("unknown"));
  EXPECT_THAT(cfg.named[0].lines[0].tokens, ElementsAre("--foo=bar"));
}

TEST_F(IniTest, ParsesGlobalOptionsAndPlainNamedSectionsWithSourceLines) {
  const SystemConfig cfg = ParseIni(
      "--allow-no-config\n"
      "--color=auto\n"
      "[dev]\n"
      "--color=always\n"
      "-E\n"
      "[prod]\n"
      "--color=never\n");

  ASSERT_THAT(cfg.global_lines, SizeIs(2));
  EXPECT_THAT(cfg.global_lines[0].tokens, ElementsAre("--allow-no-config"));
  ASSERT_THAT(cfg.named, SizeIs(2));
  EXPECT_THAT(cfg.named[0].name, Eq("dev"));
  EXPECT_THAT(cfg.named[0].lines, SizeIs(2));
  EXPECT_THAT(cfg.named[0].lines[1].tokens, ElementsAre("-E"));
  EXPECT_THAT(cfg.named[1].name, Eq("prod"));
}

}  // namespace
}  // namespace xff::config
