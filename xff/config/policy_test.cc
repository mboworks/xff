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

#include "xff/config/policy.h"

#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace xff::config {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::SizeIs;

struct PolicyTest : ::testing::Test {};

RcLine Line(std::vector<std::string> flags) {
  return RcLine{.flags = std::move(flags)};
}

TEST_F(PolicyTest, LineSafetyTakesTheWorstFlag) {
  EXPECT_THAT(LineSafety(Line({"--color=auto"})), registry::Safety::kNone);
  EXPECT_THAT(LineSafety(Line({"-exec", "rm", ";"})), registry::Safety::kSecurity);
  EXPECT_THAT(LineSafety(Line({"-delete"})), registry::Safety::kSafety);
  EXPECT_THAT(LineSafety(Line({"-capture:tag", "cmd", ";"})), registry::Safety::kSecurity);        // base before ':'
  EXPECT_THAT(LineSafety(Line({"-name", "x", "-exec", "rm", ";"})), registry::Safety::kSecurity);  // worst wins
}

TEST_F(PolicyTest, NoLayerIsDeniedByDefault) {
  const SystemConfig none;  // no [policy]
  // With the untrusted project layer gone (Option B), the trusted user/system layers may do
  // anything by default; only a system [policy] deny rule bars a line.
  EXPECT_TRUE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kUser, none));
  EXPECT_TRUE(LinePermitted(Line({"-delete"}), Source::kUser, none));
  EXPECT_TRUE(LinePermitted(Line({"-delete"}), Source::kSystem, none));
  EXPECT_TRUE(LinePermitted(Line({"--color=auto"}), Source::kUser, none));
}

TEST_F(PolicyTest, PolicyDenyTightensAFlagByName) {
  SystemConfig policy;
  policy.policy = {PolicyRule{.layer = "user", .allow = false, .tokens = {"--jobs"}}};
  EXPECT_FALSE(LinePermitted(Line({"--jobs=4"}), Source::kUser, policy));     // named flag denied
  EXPECT_TRUE(LinePermitted(Line({"--color=auto"}), Source::kUser, policy));  // unrelated flag fine
}

TEST_F(PolicyTest, PolicyDenyTightensByClassToken) {
  SystemConfig policy;
  policy.policy = {PolicyRule{.layer = "user", .allow = false, .tokens = {"@sensitive"}}};
  EXPECT_FALSE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kUser, policy));  // @sensitive denied
  EXPECT_TRUE(LinePermitted(Line({"-delete"}), Source::kUser, policy));            // @destructive not matched
}

TEST_F(PolicyTest, PolicyClassDenyFindsEveryDangerousClassOnMixedLines) {
  SystemConfig policy;
  policy.policy = {PolicyRule{.layer = "user", .allow = false, .tokens = {"@destructive"}}};
  EXPECT_THAT(LinePermitted(Line({"-delete", "-exec", "rm", ";"}), Source::kUser, policy), IsFalse());
  EXPECT_THAT(LinePermitted(Line({"-exec", "rm", ";", "-delete"}), Source::kUser, policy), IsFalse());

  policy.policy = {PolicyRule{.layer = "user", .allow = false, .tokens = {"@sensitive"}}};
  EXPECT_THAT(LinePermitted(Line({"-delete", "-exec", "rm", ";"}), Source::kUser, policy), IsFalse());
  EXPECT_THAT(LinePermitted(Line({"-exec", "rm", ";", "-delete"}), Source::kUser, policy), IsFalse());
}

TEST_F(PolicyTest, SafeClassDenyMatchesOnlyWhollySafeLines) {
  SystemConfig policy;
  policy.policy = {PolicyRule{.layer = "user", .allow = false, .tokens = {"@safe"}}};
  EXPECT_THAT(LinePermitted(Line({"-name", "*.cc"}), Source::kUser, policy), IsFalse());
  EXPECT_THAT(LinePermitted(Line({"-name", "*.cc", "-delete"}), Source::kUser, policy), IsTrue());
}

TEST_F(PolicyTest, AllowRuleIsInertAndDenyStillBars) {
  SystemConfig policy;
  // An allow rule has nothing to loosen now (no default denial), so it is inert; a deny rule still bars.
  policy.policy = {
      PolicyRule{.layer = "user", .allow = true, .tokens = {"-exec"}},
      PolicyRule{.layer = "user", .allow = false, .tokens = {"-exec"}},
  };
  EXPECT_FALSE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kUser, policy));
}

TEST_F(PolicyTest, PolicyRulesAreScopedToTheirLayer) {
  SystemConfig policy;
  policy.policy = {PolicyRule{.layer = "user", .allow = false, .tokens = {"-exec"}}};
  // The user.deny tightens the user layer...
  EXPECT_FALSE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kUser, policy));
  // ...but does not touch the system layer.
  EXPECT_TRUE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kSystem, policy));
}

TEST_F(PolicyTest, GateConfigDropsDeniedUserLinesAndRecordsThem) {
  ConfigInputs inputs;
  inputs.system.policy = {PolicyRule{.layer = "user", .allow = false, .tokens = {"-exec"}}};  // deny -exec in user
  inputs.user = {Line({"-exec", "rm", ";"}), Line({"--color=never"})};
  const GateResult gated = GateConfig(inputs, /*xffrc_armed=*/false);
  ASSERT_THAT(gated.config.user, SizeIs(1));
  EXPECT_THAT(gated.config.user.front().flags, ElementsAre("--color=never"));  // only permitted survives
  ASSERT_THAT(gated.drops, SizeIs(1));
  EXPECT_THAT(gated.drops.front().layer, Source::kUser);
  EXPECT_THAT(gated.drops.front().safety, registry::Safety::kSecurity);
  EXPECT_THAT(gated.drops.front().line.flags, ElementsAre("-exec", "rm", ";"));
}

TEST_F(PolicyTest, GateConfigAlwaysReturnsDroppedLines) {
  ConfigInputs inputs;
  inputs.system.policy = {PolicyRule{.layer = "user", .allow = false, .tokens = {"-delete"}}};
  inputs.user = {Line({"-delete"})};
  const GateResult gated = GateConfig(inputs, /*xffrc_armed=*/false);
  EXPECT_THAT(gated.config.user, IsEmpty());
  EXPECT_THAT(gated.drops, SizeIs(1));
}

TEST_F(PolicyTest, DropMessageNamesPrimaryLayerAndClass) {
  const Drop drop{
      .line = Line({"-exec", "rm", ";"}),
      .layer = Source::kUser,
      .safety = registry::Safety::kSecurity,
  };
  EXPECT_THAT(DropMessage(drop), "'-exec' from the user .xffrc (sensitive)");
}

TEST_F(PolicyTest, XffrcDangerousLineIsInertUnlessArmed) {
  ConfigInputs inputs;
  inputs.xffrc = {Line({"-exec", "rm", ";"}), Line({"--color=never"})};
  const GateResult unarmed = GateConfig(inputs, /*xffrc_armed=*/false);
  ASSERT_THAT(unarmed.config.xffrc, SizeIs(1));
  EXPECT_THAT(unarmed.config.xffrc.front().flags, ElementsAre("--color=never"));  // safe line survives
  ASSERT_THAT(unarmed.drops, SizeIs(1));
  EXPECT_THAT(unarmed.drops.front().reason, DropReason::kUnarmedXffrc);
  EXPECT_THAT(unarmed.drops.front().layer, Source::kXffrc);
  EXPECT_THAT(unarmed.drops.front().safety, registry::Safety::kSecurity);
  // Armed: the -exec line is honored (both lines survive).
  EXPECT_THAT(GateConfig(inputs, /*xffrc_armed=*/true).config.xffrc, SizeIs(2));
}

TEST_F(PolicyTest, ArmingGatesOnlyTheXffrcTierNotTheUserLayer) {
  ConfigInputs inputs;
  inputs.user = {Line({"-exec", "rm", ";"})};  // a dangerous USER line is honored regardless of the arm
  EXPECT_THAT(GateConfig(inputs, /*xffrc_armed=*/false).config.user, SizeIs(1));
}

TEST_F(PolicyTest, SystemPolicyHardDeniesAnArmedXffrcLine) {
  ConfigInputs inputs;
  inputs.system.policy = {PolicyRule{.layer = "xffrc", .allow = false, .tokens = {"@sensitive"}}};
  inputs.xffrc = {Line({"-exec", "rm", ";"})};
  const GateResult gated = GateConfig(inputs, /*xffrc_armed=*/true);
  EXPECT_THAT(gated.config.xffrc, IsEmpty());  // armed, but policy denies
  ASSERT_THAT(gated.drops, SizeIs(1));
  EXPECT_THAT(gated.drops.front().reason, DropReason::kSafetyPolicy);
}

TEST_F(PolicyTest, DropMessageForUnarmedXffrcNamesTheArm) {
  const Drop drop{
      .line = Line({"-exec", "rm", ";"}),
      .layer = Source::kXffrc,
      .safety = registry::Safety::kSecurity,
      .reason = DropReason::kUnarmedXffrc,
  };
  EXPECT_THAT(DropMessage(drop), "'-exec' from the --xffrc file (sensitive; needs --allow-exec)");
}

TEST_F(PolicyTest, OverloadsPresetDetectsBarePresetSelectors) {
  // A bare preset selector (base is a built-in style, no named config) overloads the preset.
  EXPECT_THAT(OverloadsPreset(ParseXffrc("xff: --format=jsonl").front()), IsTrue());
  EXPECT_THAT(OverloadsPreset(ParseXffrc("find: --warn").front()), IsTrue());
  EXPECT_THAT(OverloadsPreset(ParseXffrc("rg: --x").front()), IsTrue());
  // common: is not a preset; a named config and a style-scoped named config are fine (they need
  // explicit activation, so they do not silently change a plain preset run).
  EXPECT_THAT(OverloadsPreset(ParseXffrc("common: --sort").front()), IsFalse());
  EXPECT_THAT(OverloadsPreset(ParseXffrc("myx: --format=jsonl").front()), IsFalse());
  EXPECT_THAT(OverloadsPreset(ParseXffrc("xff:debug: --jobs=1").front()), IsFalse());
}

TEST_F(PolicyTest, GateConfigDropsPresetOverloadWithReason) {
  ConfigInputs inputs;
  // xff: and find: are preset-overloads (dropped in any layer); common: / myx: / xff:debug: survive.
  inputs.user =
      ParseXffrc("xff: --format=jsonl\ncommon: --sort\nfind: --warn\nmyx: --color=never\nxff:debug: --jobs=1");
  const GateResult gated = GateConfig(inputs, /*xffrc_armed=*/false);
  EXPECT_THAT(gated.config.user, SizeIs(3));  // common:, myx:, xff:debug:
  ASSERT_THAT(gated.drops, SizeIs(2));
  EXPECT_THAT(gated.drops[0].reason, DropReason::kPresetOverload);  // xff: (file order)
  EXPECT_THAT(gated.drops[1].reason, DropReason::kPresetOverload);  // find:
  EXPECT_THAT(DropMessage(gated.drops[0]), "'xff:' in the user .xffrc");
}

}  // namespace
}  // namespace xff::config
