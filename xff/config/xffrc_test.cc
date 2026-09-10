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

#include "xff/config/xffrc.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::config {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Matcher;

struct XffrcTest : ::testing::Test {};

// Matches an RcLine by its base/config selectors and a matcher over its flags,
// so one ElementsAre(...) covers line count, order, selectors, and flags at once.
Matcher<RcLine> RcLineIs(
    const std::string& base,
    const std::string& config,
    const Matcher<std::vector<std::string>>& flags) {
  return AllOf(
      Field("base", &RcLine::base, base), Field("config", &RcLine::config, config),
      Field("flags", &RcLine::flags, flags));
}

TEST_F(XffrcTest, SkipsBlanksAndComments) {
  EXPECT_THAT(ParseXffrc("\n  \n# a comment\n   # indented comment\n"), IsEmpty());
}

TEST_F(XffrcTest, BareFlagsAreCommonAnyConfig) {
  EXPECT_THAT(ParseXffrc("--color=auto --sort"), ElementsAre(RcLineIs("", "", ElementsAre("--color=auto", "--sort"))));
}

TEST_F(XffrcTest, BaseSelector) {
  EXPECT_THAT(ParseXffrc("xff: --format=jsonl"), ElementsAre(RcLineIs("xff", "", ElementsAre("--format=jsonl"))));
}

TEST_F(XffrcTest, BaseAndConfigSelector) {
  EXPECT_THAT(
      ParseXffrc("xff:debug: --format=jsonl --jobs=1"),
      ElementsAre(RcLineIs("xff", "debug", ElementsAre("--format=jsonl", "--jobs=1"))));
}

TEST_F(XffrcTest, CommonSelectorPreservedVerbatim) {
  // The loader treats "common" == "", but the parser preserves it verbatim.
  EXPECT_THAT(ParseXffrc("common: --color=auto"), ElementsAre(RcLineIs("common", "", ElementsAre("--color=auto"))));
}

TEST_F(XffrcTest, SelectorOnlyLineHasNoFlags) {
  EXPECT_THAT(
      ParseXffrc("find: --warn\nxff:\nxff: --format=csv"),
      ElementsAre(
          RcLineIs("find", "", ElementsAre("--warn")), RcLineIs("xff", "", IsEmpty()),  // selector with no flags
          RcLineIs("xff", "", ElementsAre("--format=csv"))));
}

TEST_F(XffrcTest, FlagValueWithColonIsNotASelector) {
  EXPECT_THAT(
      ParseXffrc("--config=xff:2"),  // ends in '2', not ':'
      ElementsAre(RcLineIs("", "", ElementsAre("--config=xff:2"))));
}

}  // namespace
}  // namespace xff::config
