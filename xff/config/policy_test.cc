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

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
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
using ::testing::SizeIs;

struct PolicyTest : ::testing::Test {};

TEST_F(PolicyTest, RcGlobalsPermissionUsesTrustedGlobalsAndSystemDenialWins) {
  ConfigInputs inputs;
  EXPECT_THAT(RcGlobalsAllowed(inputs), IsFalse());
  inputs.system = ParseIni("--allow-rc-globals");
  EXPECT_THAT(RcGlobalsAllowed(inputs), IsTrue());
  inputs.user = ParseIni("--no-allow-rc-globals");
  EXPECT_THAT(RcGlobalsAllowed(inputs), IsFalse());
  inputs.no_user_config = true;
  EXPECT_THAT(RcGlobalsAllowed(inputs), IsFalse());
  inputs.user = ParseIni("--no-require-user-globals\n--no-allow-rc-globals");
  EXPECT_THAT(RcGlobalsAllowed(inputs), IsTrue());
  inputs.no_user_config = false;
  inputs.system = ParseIni("--no-allow-rc-globals");
  inputs.user = ParseIni("--allow-rc-globals");
  inputs.no_system_config = true;
  EXPECT_THAT(RcGlobalsAllowed(inputs), IsFalse());
  inputs.system = {};
  EXPECT_THAT(RcGlobalsAllowed(inputs), IsTrue());
  inputs.no_config = true;
  EXPECT_THAT(RcGlobalsAllowed(inputs), IsTrue());
}

TEST_F(PolicyTest, RcGlobalsPermissionIsUniqueAndOnlyInTrustedUnsectionedContent) {
  static constexpr auto kControls = std::to_array<std::string_view>({
      "--allow-rc-globals",
      "--no-allow-rc-globals",
  });
  for (const auto flag : kControls) {
    ConfigInputs inputs;
    inputs.system = ParseIni(flag);
    inputs.user = ParseIni(flag);
    EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
    inputs.system = ParseIni(absl::StrCat(flag, "\n", flag));
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("once")));
    inputs.system = {};
    inputs.user = ParseIni("--allow-rc-globals\n--no-allow-rc-globals");
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("once")));
    inputs.user = ParseIni(absl::StrCat("[named]\n", flag));
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("precede")));
    inputs.system = inputs.user;
    inputs.user = {};
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("precede")));
    inputs.system = {};
    inputs.xffrc.push_back({.path = "task.rc", .config = ParseIni(flag)});
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not permitted")));
    inputs.xffrc.front().config = ParseIni(absl::StrCat("[named]\n", flag));
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not permitted")));
  }
}

TEST_F(PolicyTest, RcModesCannotBeSetByExplicitOrAutoloadedFiles) {
  static constexpr auto kModes = std::to_array<std::string_view>({"--rc", "--rc-", "--rc+"});
  for (const auto mode : kModes) {
    ConfigInputs inputs;
    inputs.system = ParseIni(absl::StrCat("[named]\n", mode));
    inputs.user = inputs.system;
    EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
    inputs.xffrc.push_back({.path = "task.rc", .config = ParseIni(mode)});
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not permitted")));
    inputs.xffrc.front().automatic = true;
    inputs.xffrc.front().config = ParseIni(absl::StrCat("[named]\n", mode));
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not permitted")));
  }
}

TEST_F(PolicyTest, UserCannotDeclareTheSameDirectoryRootTwice) {
  ConfigInputs inputs;
  inputs.user = ParseIni("--output-root=/first\n--output-root=/second");
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("may occur only once")));
}

IniLine Line(std::vector<std::string> flags) {
  return IniLine{.tokens = std::move(flags)};
}

TEST_F(PolicyTest, ArchivePolicyIsUniqueAndRestrictedToTrustedGlobals) {
  const auto policies = std::to_array<std::string>({"", "archive"});
  for (const std::string& value : policies) {
    const std::string flag = "--block-policy-categories=" + value;
    ConfigInputs inputs;
    inputs.system = ParseIni(flag);
    inputs.user = ParseIni(flag);
    EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
    inputs.system = ParseIni(absl::StrCat(flag, "\n", flag));
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("only once")));
    inputs.system = ParseIni("[named]\n" + flag);
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("precede")));
    inputs.system = {};
    inputs.user = ParseIni("[named]\n" + flag);
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("precede")));
    inputs.user = ParseIni(absl::StrCat(flag, "\n", flag));
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("only once")));
    inputs.user = {};
    inputs.xffrc = {{.path = "task.rc", .config = ParseIni(flag)}};
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not permitted")));
  }
}

TEST_F(PolicyTest, ExplicitCompositionCannotArmTrustedNamedActions) {
  ConfigInputs inputs;
  inputs.system = ParseIni("[task]\n-exec echo unsafe \\;");
  inputs.xffrc = {{.path = "task.rc", .config = ParseIni("--config=task")}};
  const auto indirect = GateConfig(inputs, false, {"--xffrc=task.rc"});
  EXPECT_THAT(indirect.drops, SizeIs(1));
  EXPECT_THAT(indirect.drops.front().reason, DropReason::kUntrustedSelection);
  EXPECT_THAT(
      DropMessage(indirect.drops.front()), HasSubstr("selected only through an explicit file, needs --allow-exec"));
  EXPECT_THAT(GateConfig(inputs, false, {"--config=task", "--xffrc=task.rc"}).drops, IsEmpty());
  EXPECT_THAT(GateConfig(inputs, true, {"--xffrc=task.rc"}).drops, IsEmpty());
  EXPECT_THAT(GateConfig(inputs, false, {}).drops, IsEmpty());
}

TEST_F(PolicyTest, LineSafetyTakesTheWorstFlag) {
  EXPECT_THAT(LineSafety(Line({"--color=auto"})), registry::Safety::kNone);
  EXPECT_THAT(LineSafety(Line({"-exec", "rm", ";"})), registry::Safety::kSecurity);
  EXPECT_THAT(LineSafety(Line({"-delete"})), registry::Safety::kSafety);
  EXPECT_THAT(LineSafety(Line({"-capture:tag", "cmd", ";"})), registry::Safety::kSecurity);        // base before ':'
  EXPECT_THAT(LineSafety(Line({"-name", "x", "-exec", "rm", ";"})), registry::Safety::kSecurity);  // worst wins
}

TEST_F(PolicyTest, CombinedSkipAcceptsRequiredAndOptionalGlobals) {
  ConfigInputs inputs;
  inputs.sources = {
      {.path = "/etc/xff.ini", .layer = Source::kSystem, .found = true},
      {.path = "/home/u/.config/xff/config", .layer = Source::kUser, .found = true},
  };
  inputs.no_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
  inputs.system = ParseIni("--no-require-system-globals");
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
  inputs.user = ParseIni("--no-require-user-globals");
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
  inputs.system = ParseIni("--no-require-system-globals\n--require-user-globals");
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
  inputs.system = ParseIni("--require-system-globals\n--no-require-user-globals");
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
}

TEST_F(PolicyTest, SystemGrantOverridesUserRequirementForIndividualAndCombinedSkips) {
  ConfigInputs inputs;
  inputs.sources = {
      {.path = "/etc/xff.ini", .layer = Source::kSystem, .found = true},
      {.path = "/home/u/.config/xff/config", .layer = Source::kUser, .found = true},
  };
  inputs.system = ParseIni("--no-require-system-globals\n--no-require-user-globals");
  inputs.user = ParseIni("--require-user-globals");
  inputs.no_user_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
  inputs.no_user_config = false;
  inputs.no_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
}

TEST_F(PolicyTest, CombinedSkipAcceptsMissingFilesAndEmptyGlobals) {
  ConfigInputs inputs;
  inputs.no_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
  inputs.sources = {{.path = "/home/u/.config/xff/config", .layer = Source::kUser, .found = true}};
  inputs.user = ParseIni("--no-require-user-globals");
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
  inputs.user = {};
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
  inputs.sources = {{.path = "/etc/xff.ini", .layer = Source::kSystem, .found = true}};
  inputs.system = ParseIni("--no-require-system-globals\n--require-user-globals");
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
}

TEST_F(PolicyTest, UserMayAuthorizeSkippingItselfButNotTheSystemConfig) {
  ConfigInputs inputs;
  inputs.sources = {
      {.path = "/etc/xff.ini", .layer = Source::kSystem, .found = true},
      {.path = "/home/u/.config/xff/config", .layer = Source::kUser, .found = true},
  };
  inputs.user = ParseXffrc("--no-require-user-globals");
  inputs.no_user_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());

  inputs.no_user_config = false;
  inputs.no_system_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
}

TEST_F(PolicyTest, SystemControlsMustPrecedeEveryIniSection) {
  ConfigInputs inputs;
  inputs.system = ParseIni("[named]\n--no-require-system-globals");
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("precede every system")));
}

TEST_F(PolicyTest, ConfigControlPairsMayOccurOnlyOncePerPermittedFile) {
  ConfigInputs inputs;
  inputs.system.globals = {"--no-require-system-globals", "--require-system-globals"};
  EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("system config")));

  inputs.system.globals = {"--no-require-user-globals", "--require-user-globals"};
  EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("system config")));

  inputs.system.globals.clear();
  inputs.user = ParseXffrc("--no-require-user-globals\n[debug]\n--require-user-globals");
  EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("user config")));
}

TEST_F(PolicyTest, ConfigControlsAreRestrictedToTheirTrustedFiles) {
  ConfigInputs inputs;
  inputs.user = ParseXffrc("--no-require-system-globals");
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("only in the system")));

  inputs.user = {};
  inputs.xffrc = {{.path = "/named", .config = ParseXffrc("--no-require-user-globals")}};
  EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not permitted")));
}

TEST_F(PolicyTest, SelectedUserSettingsControlExplicitXffrcFiles) {
  ConfigInputs inputs;
  inputs.configs = {"locked"};
  inputs.user = ParseXffrc("[locked]\n--allow-xffrc\n[open]\n--no-allow-xffrc");
  inputs.xffrc = {{.path = "/named", .config = {}}};
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());

  inputs.configs = {"open"};
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--no-allow-xffrc")));
}

TEST_F(PolicyTest, XffrcAdmissionFollowsSelectorOrderRegardlessOfSectionOrder) {
  static constexpr auto kFiles = std::to_array<std::string_view>({
      "[deny]\n--no-allow-xffrc\n[allow]\n--allow-xffrc",
      "[allow]\n--allow-xffrc\n[deny]\n--no-allow-xffrc",
  });
  for (const auto file : kFiles) {
    ConfigInputs inputs;
    inputs.user = ParseIni(file);
    inputs.xffrc = {{.path = "/not-opened"}};
    inputs.configs = {"allow", "deny"};
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied));
    inputs.configs = {"deny", "allow"};
    EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
    // Selecting a section again does not reapply its lines.
    inputs.configs = {"allow", "deny", "allow"};
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied));
    inputs.no_user_config = true;
    EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
    inputs.system = ParseIni("--no-allow-xffrc");
    inputs.no_system_config = true;
    EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied));
  }
}

TEST_F(PolicyTest, AdmissionExpandsComposedSelectionsInPlaceAndIgnoresCommandArguments) {
  ConfigInputs inputs;
  inputs.rc_mode = RcMode::kRoots;
  inputs.system = ParseIni("--config=allow");
  inputs.user = ParseIni(
      "[deny]\n--no-allow-xffrc\n[allow]\n--allow-xffrc\n[wrapper]\n--config=deny\n"
      "-exec echo --allow-xffrc \\;\n");
  inputs.configs = {"wrapper"};
  EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied));
  inputs.system = {};
  inputs.configs = {"wrapper", "allow"};
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
  // An explicit file cannot select a trusted profile to admit itself.
  inputs.configs = {"deny"};
  inputs.xffrc = {{.path = "self", .config = ParseIni("--config=allow")}};
  EXPECT_THAT(ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(PolicyTest, AutomaticTransitiveSelectionControlsExplicitFileAdmission) {
  ConfigInputs inputs;
  inputs.system = ParseIni("--config=outer\n[outer]\n--config=locked");
  inputs.user = ParseXffrc("[locked]\n--no-allow-xffrc");
  inputs.xffrc = {{.path = "/named", .config = {}}};
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--no-allow-xffrc")));
  inputs.no_user_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
}

TEST_F(PolicyTest, SystemGlobalProhibitionMayPreventUserConfigFromEnablingXffrc) {
  ConfigInputs inputs;
  inputs.configs = {"locked"};
  inputs.system.globals = {"--no-allow-xffrc"};
  inputs.user = ParseXffrc("[locked]\n--allow-xffrc");
  inputs.xffrc = {{.path = "/named", .config = {}}};
  EXPECT_THAT(
      ValidateConfigSkips(inputs), StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--no-allow-xffrc")));
}

TEST_F(PolicyTest, SystemGlobalProhibitionMayDenyTheUsersSelfSkipPermission) {
  ConfigInputs inputs;
  inputs.sources = {{.path = "/home/u/.config/xff/config", .layer = Source::kUser, .found = true}};
  inputs.system.globals = {"--require-user-globals"};
  inputs.user = ParseXffrc("--no-require-user-globals");
  inputs.no_user_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
}

TEST_F(PolicyTest, MissingSourcesNeedNoSkipPermission) {
  ConfigInputs inputs;
  inputs.no_system_config = true;
  inputs.no_user_config = true;
  EXPECT_THAT(ValidateConfigSkips(inputs), IsOk());
}

TEST_F(PolicyTest, XffrcDangerousLineIsInertUnlessArmed) {
  ConfigInputs inputs;
  inputs.xffrc = {
      {.path = "/named", .config = {.global_lines = {Line({"-exec", "rm", ";"}), Line({"--color=never"})}}}};
  const GateResult unarmed = GateConfig(inputs, /*xffrc_armed=*/false);
  EXPECT_THAT(
      unarmed.config.xffrc,
      ElementsAre(FieldsAre("/named", Field("globals", &ConfigFile::globals, ElementsAre("--color=never")), false)));
  ASSERT_THAT(unarmed.drops, SizeIs(1));
  EXPECT_THAT(unarmed.drops.front().reason, DropReason::kUnarmedXffrc);
  EXPECT_THAT(unarmed.drops.front().layer, Source::kXffrc);
  EXPECT_THAT(unarmed.drops.front().safety, registry::Safety::kSecurity);
  // Armed: the -exec line is honored (both lines survive).
  EXPECT_THAT(
      GateConfig(inputs, /*xffrc_armed=*/true).config.xffrc,
      ElementsAre(FieldsAre("/named", Field("global_lines", &ConfigFile::global_lines, SizeIs(2)), false)));
}

TEST_F(PolicyTest, ArmingGatesOnlyTheXffrcTierNotTheUserLayer) {
  ConfigInputs inputs;
  inputs.user = {
      .global_lines = {Line({"-exec", "rm", ";"})},
  };  // a dangerous USER line is honored regardless of the arm
  EXPECT_THAT(GateConfig(inputs, /*xffrc_armed=*/false).config.user.global_lines, SizeIs(1));
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

TEST_F(PolicyTest, OverloadsPresetDetectsOnlyExactBuiltinNames) {
  EXPECT_THAT(OverloadsPreset("xff"), IsTrue());
  EXPECT_THAT(OverloadsPreset("find"), IsTrue());
  EXPECT_THAT(OverloadsPreset("rg"), IsTrue());
  EXPECT_THAT(OverloadsPreset(""), IsFalse());
  EXPECT_THAT(OverloadsPreset("common"), IsFalse());
  EXPECT_THAT(OverloadsPreset("myx"), IsFalse());
  EXPECT_THAT(OverloadsPreset("xff:debug"), IsFalse());
}

TEST_F(PolicyTest, GateConfigDropsPresetOverloadWithReason) {
  ConfigInputs inputs;
  // Exact preset names are dropped; defaults and custom named sections survive.
  inputs.user =
      ParseXffrc("--sort\n[xff]\n--format=jsonl\n[find]\n--warn\n[myx]\n--color=never\n[xff:debug]\n--jobs=1");
  const GateResult gated = GateConfig(inputs, /*xffrc_armed=*/false);
  EXPECT_THAT(gated.config.user.globals, ElementsAre("--sort"));
  EXPECT_THAT(
      gated.config.user.named,
      ElementsAre(
          AllOf(Field("name", &IniSection::name, "xff"), Field("lines", &IniSection::lines, IsEmpty())),
          AllOf(Field("name", &IniSection::name, "find"), Field("lines", &IniSection::lines, IsEmpty())),
          Field("name", &IniSection::name, "myx"), Field("name", &IniSection::name, "xff:debug")));
  ASSERT_THAT(gated.drops, SizeIs(2));
  EXPECT_THAT(gated.drops[0].reason, DropReason::kPresetOverload);  // xff: (file order)
  EXPECT_THAT(gated.drops[1].reason, DropReason::kPresetOverload);  // find:
  EXPECT_THAT(DropMessage(gated.drops[0]), "'[xff]' in the user .xffrc");
}

}  // namespace
}  // namespace xff::config
