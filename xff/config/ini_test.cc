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

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Matcher;

struct IniTest : ::testing::Test {};

// Matches a PolicyRule by its layer, allow/deny flag, and a matcher over its
// tokens, so one ElementsAre(...) covers rule count, order, and every field.
Matcher<PolicyRule> PolicyRuleIs(
    const std::string& layer,
    bool allow,
    const Matcher<std::vector<std::string>>& tokens) {
  return AllOf(
      Field("layer", &PolicyRule::layer, layer), Field("allow", &PolicyRule::allow, allow),
      Field("tokens", &PolicyRule::tokens, tokens));
}

TEST_F(IniTest, DefaultsRenderToCliTokens) {
  const SystemConfig cfg = ParseIni("[defaults]\n--color = auto\n--warn\n");
  EXPECT_THAT(cfg.defaults, ElementsAre("--color=auto", "--warn"));
  EXPECT_THAT(cfg.policy, IsEmpty());
}

TEST_F(IniTest, PolicyAllowDenyAndClassTokens) {
  const SystemConfig cfg = ParseIni(
      "[policy]\n"
      "user.allow = --sort, --color, --format\n"
      "xffrc.deny = --jobs\n"
      "user.deny  = @sensitive\n");
  EXPECT_THAT(
      cfg.policy,
      ElementsAre(
          PolicyRuleIs("user", true, ElementsAre("--sort", "--color", "--format")),
          PolicyRuleIs("xffrc", false, ElementsAre("--jobs")), PolicyRuleIs("user", false, ElementsAre("@sensitive"))));
}

TEST_F(IniTest, CommentsBlanksAndBothSections) {
  const SystemConfig cfg =
      ParseIni("; a comment\n# another\n[defaults]\n\n--color = never\n[policy]\nuser.allow = --sort\n");
  EXPECT_THAT(cfg.defaults, ElementsAre("--color=never"));
  EXPECT_THAT(cfg.policy, ElementsAre(PolicyRuleIs("user", true, ElementsAre("--sort"))));
}

TEST_F(IniTest, MalformedPolicyLinesIgnored) {
  // No '=', no '.', and an unknown kind are each ignored (forgiving parse).
  const SystemConfig cfg = ParseIni("[policy]\nnonsense\nuser = x\nuser.maybe = x\n");
  EXPECT_THAT(cfg.policy, IsEmpty());
}

TEST_F(IniTest, LinesOutsideKnownSectionsIgnored) {
  const SystemConfig cfg = ParseIni("--color = auto\n[unknown]\n--foo = bar\n");
  EXPECT_THAT(cfg.defaults, IsEmpty());
  EXPECT_THAT(cfg.policy, IsEmpty());
}

}  // namespace
}  // namespace xff::config
