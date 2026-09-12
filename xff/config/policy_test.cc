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
#include "mbo/testing/status.h"
#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace xff::config {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::ResultOf;
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

TEST_F(PolicyTest, PresentAutomaticSourcesMustAuthorizeBeingSkipped) {
  ConfigInputs inputs;
  inputs.sources = {
      {.path = "/etc/xff.ini", .layer = Source::kSystem, .found = true},
      {.path = "/home/u/.config/xff/config", .layer = Source::kUser, .found = true},
  };
  inputs.no_system_config = true;
  inputs.no_user_config = true;
  inputs.no_config = true;
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--allow-no-config")));

  inputs.system.globals = {"--allow-no-config"};
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
}

TEST_F(PolicyTest, UserMayAuthorizeSkippingItselfButNotTheSystemConfig) {
  ConfigInputs inputs;
  inputs.sources = {
      {.path = "/etc/xff.ini", .layer = Source::kSystem, .found = true},
      {.path = "/home/u/.config/xff/config", .layer = Source::kUser, .found = true},
  };
  inputs.user = ParseXffrc("--allow-no-user-config");
  inputs.no_user_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());

  inputs.no_user_config = false;
  inputs.no_system_config = true;
  EXPECT_THAT(
      ValidateConfigSkips(inputs),
      StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--allow-no-system-config")));
}

TEST_F(PolicyTest, NegativeControlsApplyOnlyToTheirCorrespondingSkipRequest) {
  ConfigInputs inputs;
  inputs.sources = {
      {.path = "/etc/xff.ini", .layer = Source::kSystem, .found = true},
      {.path = "/home/u/.config/xff/config", .layer = Source::kUser, .found = true},
  };
  inputs.system.globals = {"--allow-no-config", "--no-allow-no-system-config", "--no-allow-no-user-config"};
  inputs.user = ParseXffrc("--allow-no-user-config");
  inputs.no_config = true;
  inputs.no_system_config = true;
  inputs.no_user_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());

  inputs.no_config = false;
  EXPECT_THAT(
      ValidateConfigSkips(inputs),
      StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--allow-no-system-config")));

  inputs.no_system_config = false;
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--allow-no-user-config")));
}

TEST_F(PolicyTest, SystemControlsMustPrecedeEveryIniSection) {
  ConfigInputs inputs;
  inputs.system = ParseIni("[named]\n--allow-no-config");
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("precede every system")));
}

TEST_F(PolicyTest, ConfigControlPairsMayOccurOnlyOncePerPermittedFile) {
  ConfigInputs inputs;
  inputs.system.globals = {"--allow-no-system-config", "--no-allow-no-system-config"};
  EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("system config")));

  inputs.system.globals = {"--allow-no-user-config", "--no-allow-no-user-config"};
  EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("system config")));

  inputs.system.globals.clear();
  inputs.user = ParseXffrc("--allow-no-user-config\ndebug: --no-allow-no-user-config");
  EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("user config")));
}

TEST_F(PolicyTest, ConfigControlsAreRestrictedToTheirTrustedFiles) {
  ConfigInputs inputs;
  inputs.user = ParseXffrc("--allow-no-system-config");
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("only in the system")));

  inputs.user.clear();
  inputs.xffrc = {{.path = "/named", .lines = ParseXffrc("common: --allow-no-user-config")}};
  EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not permitted")));
}

TEST_F(PolicyTest, SelectedUserSettingsControlExplicitXffrcFiles) {
  ConfigInputs inputs;
  inputs.configs = {"locked"};
  inputs.user = ParseXffrc("locked: --allow-xffrc\nopen: --no-allow-xffrc");
  inputs.xffrc = {{.path = "/named", .lines = {}}};
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());

  inputs.configs = {"open"};
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--no-allow-xffrc")));
}

TEST_F(PolicyTest, AutomaticTransitiveSelectionControlsExplicitFileAdmission) {
  ConfigInputs inputs;
  inputs.system = ParseIni("--config=outer\n[outer]\n--config=locked");
  inputs.user = ParseXffrc("locked: --no-allow-xffrc");
  inputs.xffrc = {{.path = "/named", .lines = {}}};
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--no-allow-xffrc")));
  inputs.no_user_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
}

TEST_F(PolicyTest, SystemGlobalProhibitionMayPreventUserConfigFromEnablingXffrc) {
  ConfigInputs inputs;
  inputs.configs = {"locked"};
  inputs.system.globals = {"--no-allow-xffrc"};
  inputs.user = ParseXffrc("locked: --allow-xffrc");
  inputs.xffrc = {{.path = "/named", .lines = {}}};
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--no-allow-xffrc")));
}

TEST_F(PolicyTest, SystemGlobalProhibitionMayDenyTheUsersSelfSkipPermission) {
  ConfigInputs inputs;
  inputs.sources = {{.path = "/home/u/.config/xff/config", .layer = Source::kUser, .found = true}};
  inputs.system.globals = {"--no-allow-no-user-config"};
  inputs.user = ParseXffrc("--allow-no-user-config");
  inputs.no_user_config = true;
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--allow-no-user-config")));
}

TEST_F(PolicyTest, MissingSourcesNeedNoSkipPermission) {
  ConfigInputs inputs;
  inputs.no_system_config = true;
  inputs.no_user_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
}

TEST_F(PolicyTest, GateConfigDropsDeniedUserLinesAndRecordsThem) {
  ConfigInputs inputs;
  inputs.system.globals = {"--no-allow-exec"};
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
  inputs.system.globals = {"--no-allow-exec"};
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
  EXPECT_THAT(DropMessage(drop), "'-exec' from the user .xffrc (sensitive; system --no-allow-exec)");
}

TEST_F(PolicyTest, XffrcDangerousLineIsInertUnlessArmed) {
  ConfigInputs inputs;
  inputs.xffrc = {{.path = "/named", .lines = {Line({"-exec", "rm", ";"}), Line({"--color=never"})}}};
  const GateResult unarmed = GateConfig(inputs, /*xffrc_armed=*/false);
  EXPECT_THAT(
      unarmed.config.xffrc,
      ElementsAre(FieldsAre("/named", ElementsAre(Field("flags", &RcLine::flags, ElementsAre("--color=never"))))));
  ASSERT_THAT(unarmed.drops, SizeIs(1));
  EXPECT_THAT(unarmed.drops.front().reason, DropReason::kUnarmedXffrc);
  EXPECT_THAT(unarmed.drops.front().layer, Source::kXffrc);
  EXPECT_THAT(unarmed.drops.front().safety, registry::Safety::kSecurity);
  // Armed: the -exec line is honored (both lines survive).
  EXPECT_THAT(GateConfig(inputs, /*xffrc_armed=*/true).config.xffrc, ElementsAre(FieldsAre("/named", SizeIs(2))));
}

TEST_F(PolicyTest, ArmingGatesOnlyTheXffrcTierNotTheUserLayer) {
  ConfigInputs inputs;
  inputs.user = {Line({"-exec", "rm", ";"})};  // a dangerous USER line is honored regardless of the arm
  EXPECT_THAT(GateConfig(inputs, /*xffrc_armed=*/false).config.user, SizeIs(1));
}

TEST_F(PolicyTest, SystemGlobalProhibitionHardDeniesAnArmedXffrcLine) {
  ConfigInputs inputs;
  inputs.system.globals = {"--no-allow-exec"};
  inputs.xffrc = {{.path = "/named", .lines = {Line({"-exec", "rm", ";"})}}};
  const GateResult gated = GateConfig(inputs, /*xffrc_armed=*/true);
  EXPECT_THAT(gated.config.xffrc, ElementsAre(FieldsAre("/named", IsEmpty())));  // armed, but policy denies
  ASSERT_THAT(gated.drops, SizeIs(1));
  EXPECT_THAT(gated.drops.front().reason, DropReason::kSystemProhibition);
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
  const auto overloads_preset = ResultOf("OverloadsPreset", OverloadsPreset, IsTrue());
  EXPECT_THAT(ParseXffrc("xff: --format=jsonl"), ElementsAre(overloads_preset));
  EXPECT_THAT(ParseXffrc("find: --warn"), ElementsAre(overloads_preset));
  EXPECT_THAT(ParseXffrc("rg: --x"), ElementsAre(overloads_preset));
  // common: is not a preset; a named config and a style-scoped named config are fine (they need
  // explicit activation, so they do not silently change a plain preset run).
  const auto does_not_overload = ResultOf("OverloadsPreset", OverloadsPreset, IsFalse());
  EXPECT_THAT(ParseXffrc("common: --sort"), ElementsAre(does_not_overload));
  EXPECT_THAT(ParseXffrc("myx: --format=jsonl"), ElementsAre(does_not_overload));
  EXPECT_THAT(ParseXffrc("xff:debug: --jobs=1"), ElementsAre(does_not_overload));
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
