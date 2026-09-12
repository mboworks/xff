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

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

struct XffrcTest : ::testing::Test {};

TEST_F(XffrcTest, SharedGrammarPreservesGlobalsSectionsAndLocations) {
  const ConfigFile file = ParseXffrc("# comment\n--color = auto\n[dev]\n-type f\n; comment");
  EXPECT_THAT(file.globals, ElementsAre("--color=auto"));
  ASSERT_THAT(file.named, SizeIs(1));
  EXPECT_THAT(file.named[0].name, "dev");
  EXPECT_THAT(file.named[0].number, 3);
  ASSERT_THAT(file.named[0].lines, SizeIs(1));
  EXPECT_THAT(file.named[0].lines[0].tokens, ElementsAre("-type", "f"));
  EXPECT_THAT(file.named[0].lines[0].number, 4);
}

TEST_F(XffrcTest, NamesWithColonsAreLiteralAndCommonIsAnOrdinaryName) {
  const ConfigFile file = ParseXffrc("[common]\n--hidden\n[xff:debug]\n--jobs=1");
  EXPECT_THAT(file.globals, IsEmpty());
  ASSERT_THAT(file.named, SizeIs(2));
  EXPECT_THAT(file.named[0].name, "common");
  EXPECT_THAT(file.named[1].name, "xff:debug");
}

TEST_F(XffrcTest, RepeatedEmptyHeadersArePreservedForValidation) {
  const ConfigFile file = ParseXffrc("[dev]\n[dev]");
  ASSERT_THAT(file.named, SizeIs(2));
  EXPECT_THAT(file.named[0].lines, IsEmpty());
  EXPECT_THAT(file.named[1].lines, IsEmpty());
}

TEST_F(XffrcTest, OldSelectorSyntaxHasNoSpecialMeaning) {
  const ConfigFile file = ParseXffrc("dev: --hidden");
  EXPECT_THAT(file.globals, ElementsAre("dev:", "--hidden"));
  EXPECT_THAT(file.named, IsEmpty());
}

}  // namespace
}  // namespace xff::config
