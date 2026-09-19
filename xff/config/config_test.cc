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

#include "xff/config/config.h"

#include <array>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/config/safety.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace xff::config {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::WithDropIndent;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::SizeIs;

struct ConfigTest : ::testing::Test {};

TEST_F(ConfigTest, SafetyDefaultsAndProfilesRemainSeparateFromUnconditionalBlocks) {
  for (std::size_t index = 0; index < SafetyPolicy::kCapabilities; ++index) {
    const auto capability = static_cast<Capability>(index);
    const std::string name(CapabilityName(capability));
    EXPECT_THAT(ResolveSafety({}).Blocks(capability), IsFalse());
    EXPECT_THAT(ResolveSafety({"--safe"}).Blocks(capability), IsTrue());
    EXPECT_THAT(ResolveSafety({"--no-safe-block-" + name, "--safe"}).Blocks(capability), IsFalse());
    EXPECT_THAT(ResolveSafety({"--safe", "--no-safe-block-" + name}).Blocks(capability), IsFalse());
    EXPECT_THAT(
        ResolveSafety({"--no-safe-block-" + name, "--safe-block-" + name, "--safe"}).Blocks(capability), IsTrue());
    EXPECT_THAT(ResolveSafety({"--safe", "--no-safe"}).Blocks(capability), IsFalse());
    EXPECT_THAT(
        ResolveSafety({"--block-" + name, "--no-safe", "--no-safe-block-" + name}).Blocks(capability), IsTrue());
  }
  EXPECT_THAT(ResolveSafety({"--dry-run"}).dry_run, IsTrue());
}

TEST_F(ConfigTest, DirectoryPolicyTranslationPreservesEarlierMandatoryBlocks) {
  static constexpr std::array kPolicyCategorySelections = std::to_array<std::string_view>({
      "",
      "temp",
      "output",
      "temp,output",
      "archive,temp,output",
  });
  for (const std::string_view categories : kPolicyCategorySelections) {
    ConfigInputs inputs;
    inputs.system = ParseIni(std::string("--block-policy-categories=") + std::string(categories) + R"ini(
--temp-root=/system/temp
--output-root=/system/output
--block-file-writing
--block-directory-deletion
[restricted]
--block-output-file-overwrite
)ini");
    inputs.user = ParseIni(R"ini(--block-policy-categories=temp,output
--temp-root=/user/temp
--output-root=/user/output
--no-safe
[restricted]
--no-safe-block-output-file-overwrite
)ini");
    std::vector<std::string> globals;
    for (const auto& flag : ResolveConfigInOrder(inputs, {"--config=restricted"}, "xff")) {
      globals.push_back(flag.flag);
    }
    const auto policy = ResolveSafety(globals, true);
    EXPECT_THAT(policy.temp_root, Eq("/system/temp"));
    EXPECT_THAT(policy.output_root, Eq("/system/output"));
    EXPECT_THAT(policy.Blocks(Capability::kTempFileWriting), Eq(!categories.contains("temp")));
    EXPECT_THAT(policy.Blocks(Capability::kOutputFileWriting), Eq(!categories.contains("output")));
    EXPECT_THAT(policy.Blocks(Capability::kTempDirectoryDeletion), Eq(!categories.contains("temp")));
    EXPECT_THAT(policy.Blocks(Capability::kOutputDirectoryDeletion), Eq(!categories.contains("output")));
    EXPECT_THAT(policy.Blocks(Capability::kOutputFileOverwrite), IsTrue());
  }
}

TEST_F(ConfigTest, ArchivePoliciesAreTranslatedIndependentlyBeforeComposition) {
  const auto ordinary = ResolveSafety({"--block-file-writing", "--block-file-overwrite", "--block-file-deletion"});
  EXPECT_THAT(ordinary.ArchiveMutations().block_writing, IsTrue());
  EXPECT_THAT(ordinary.ArchiveMutations().block_overwrite, IsTrue());
  EXPECT_THAT(ordinary.ArchiveMutations().block_deletion, IsTrue());
  const auto policies = std::to_array<std::string>({"", "archive"});
  for (const std::string& system_choice : policies) {
    ConfigInputs inputs;
    inputs.system =
        ParseIni("--block-policy-categories=" + system_choice + "\n--block-file-writing\n--block-archive-overwrite");
    inputs.user = ParseIni("--block-policy-categories=" + std::string(system_choice.empty() ? "archive" : ""));
    std::vector<std::string> globals;
    for (const auto& flag : ResolveConfigInOrder(inputs, {}, "xff")) {
      globals.push_back(flag.flag);
    }
    const auto policy = ResolveSafety(globals, true);
    EXPECT_THAT(policy.FileMutations().block_writing, IsTrue());
    EXPECT_THAT(policy.ArchiveMutations().block_writing, Eq(system_choice.empty()));
    EXPECT_THAT(policy.ArchiveMutations().block_overwrite, IsTrue());
  }
}

TEST_F(ConfigTest, NamedSectionsKeepTheirOwnFileScopeAcrossLateComposition) {
  ConfigInputs inputs;
  inputs.system = ParseIni(R"ini(--block-policy-categories=archive
--block-archive-overwrite
[clean]
--block-file-writing
)ini");
  inputs.user = ParseIni(R"ini([clean]
--block-file-deletion
--no-safe-block-file-writing
)ini");
  inputs.xffrc = {{.path = "task.rc", .config = ParseIni("--config=clean")}};
  std::vector<std::string> globals;
  for (const auto& flag : ResolveConfigInOrder(inputs, {"--xffrc=task.rc", "--no-safe"}, "xff")) {
    globals.push_back(flag.flag);
  }
  const auto policy = ResolveSafety(globals, true);
  EXPECT_THAT(policy.FileMutations().block_writing, IsTrue());
  EXPECT_THAT(policy.ArchiveMutations().block_writing, IsFalse());
  EXPECT_THAT(policy.ArchiveMutations().block_overwrite, IsTrue());
  EXPECT_THAT(policy.FileMutations().block_deletion, IsTrue());
  EXPECT_THAT(policy.ArchiveMutations().block_deletion, IsTrue());
}

TEST_F(ConfigTest, PolicyTranslationNeverRewritesPrimaryArguments) {
  ConfigInputs inputs;
  inputs.user = ParseIni(R"ini(--block-policy-categories=archive
-exec echo --block-file-writing --block-policy-categories= \;
)ini");
  const auto resolved = ResolveConfigInOrder(inputs, {}, "xff");
  ASSERT_THAT(resolved, SizeIs(5));
  EXPECT_THAT(resolved[2].flag, Eq("--block-file-writing"));
  EXPECT_THAT(resolved[2].is_argument, IsTrue());
  EXPECT_THAT(resolved[3].flag, Eq("--block-policy-categories="));
  EXPECT_THAT(resolved[3].is_argument, IsTrue());
}

TEST_F(ConfigTest, SafetyPolicyComposesFullSystemUserAndExplicitFiles) {
  ConfigInputs inputs;
  inputs.system = ParseIni(R"ini(--block-execution
--no-safe
--no-safe-block-file-writing
--no-safe-block-file-overwrite
[safe]
--safe
)ini");
  inputs.user = ParseIni(R"ini([unsafe]
--no-safe
--no-safe-block-execution
--block-file-deletion
)ini");
  inputs.xffrc = {{.path = "task.rc", .config = ParseIni("--no-safe\n--no-safe-block-file-deletion")}};
  const auto resolved = ResolveConfigInOrder(inputs, {"--config=safe", "--config=unsafe", "--xffrc=task.rc"}, "xff");
  std::vector<std::string> globals;
  globals.reserve(resolved.size());
  for (const auto& flag : resolved) {
    globals.push_back(flag.flag);
  }
  const auto policy = ResolveSafety(globals, true);
  EXPECT_THAT(policy.safe, IsFalse());
  EXPECT_THAT(policy.Blocks(Capability::kExecution), IsTrue());
  EXPECT_THAT(policy.Blocks(Capability::kFileDeletion), IsTrue());
  EXPECT_THAT(policy.Blocks(Capability::kFileWriting), IsFalse());
}

// Matches a ResolvedFlag by both its flag text and its provenance, so an
// ElementsAre(...) assertion folds size, order, flag, and Source into one check.
testing::Matcher<ResolvedFlag> FlagIs(const std::string& flag, Source source) {
  return AllOf(Field("flag", &ResolvedFlag::flag, flag), Field("source", &ResolvedFlag::source, source));
}

TEST_F(ConfigTest, PolicyControlsShareApplicationOrderButStayOutOfRuntimeFlags) {
  ConfigInputs inputs;
  inputs.user = ParseIni("[deny]\n--no-allow-xffrc\n[allow]\n--allow-xffrc\n");
  const std::vector<std::string> cli = {"--config=allow", "--config=deny"};
  EXPECT_THAT(
      ResolveConfigInOrder(inputs, cli, "xff", ConfigControls::kInclude),
      ElementsAre(
          FlagIs("--config=allow", Source::kCli), FlagIs("--allow-xffrc", Source::kUser),
          FlagIs("--config=deny", Source::kCli), FlagIs("--no-allow-xffrc", Source::kUser)));
  EXPECT_THAT(
      ResolveConfigInOrder(inputs, cli, "xff"),
      ElementsAre(FlagIs("--config=allow", Source::kCli), FlagIs("--config=deny", Source::kCli)));
}

TEST_F(ConfigTest, SkipsKeepRequiredGlobals) {
  ConfigInputs in;
  in.system.globals = {"--color=auto"};
  in.user = ParseXffrc("--sort");
  in.no_system_config = true;
  in.no_user_config = true;
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--color=auto", Source::kSystem), FlagIs("--sort", Source::kUser)));
}

TEST_F(ConfigTest, GranularSkipControlsSuppressOnlyTheirAutomaticTier) {
  ConfigInputs in;
  in.system.globals = {"--color=auto", "--no-require-system-globals"};
  in.user = ParseXffrc("--sort\n--no-require-user-globals");
  in.xffrc = {{.path = "/named", .config = ParseXffrc("--jobs=2")}};
  in.no_system_config = true;
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--sort", Source::kUser), FlagIs("--jobs=2", Source::kXffrc)));
  in.no_system_config = false;
  in.no_user_config = true;
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(FlagIs("--color=auto", Source::kSystem), FlagIs("--jobs=2", Source::kXffrc)));
}

TEST_F(ConfigTest, SkipRequestsRemoveNamedSectionsButRetainRequiredGlobalsInBothResolvers) {
  ConfigInputs inputs;
  inputs.system = ParseIni("--color=auto\n[profile]\n--hidden\n");
  inputs.user = ParseIni("--jobs=3\n[profile]\n--sort\n");
  inputs.configs = {"profile"};
  inputs.no_config = true;
  EXPECT_THAT(
      ResolveConfig(inputs), ElementsAre(FlagIs("--color=auto", Source::kSystem), FlagIs("--jobs=3", Source::kUser)));
  EXPECT_THAT(
      ResolveConfigInOrder(inputs, {"--config=profile"}, "xff"),
      ElementsAre(
          FlagIs("--color=auto", Source::kSystem), FlagIs("--jobs=3", Source::kUser),
          FlagIs("--config=profile", Source::kCli)));
}

TEST_F(ConfigTest, SystemDecidesWhetherUserGlobalsSurviveEvenWhenSystemGlobalsAreSkipped) {
  ConfigInputs inputs;
  inputs.system = ParseIni("--no-require-system-globals\n--require-user-globals\n--color=auto\n");
  inputs.user = ParseIni("--no-require-user-globals\n--jobs=3\n");
  inputs.no_config = true;
  EXPECT_THAT(ResolveConfigInOrder(inputs, {}, "xff"), ElementsAre(FlagIs("--jobs=3", Source::kUser)));
  inputs.system = ParseIni("--no-require-system-globals\n--no-require-user-globals\n--color=auto\n");
  inputs.user = ParseIni("--require-user-globals\n--jobs=3\n");
  EXPECT_THAT(ResolveConfigInOrder(inputs, {}, "xff"), IsEmpty());
  inputs.no_config = false;
  EXPECT_THAT(
      ResolveConfigInOrder(inputs, {}, "xff"),
      ElementsAre(FlagIs("--color=auto", Source::kSystem), FlagIs("--jobs=3", Source::kUser)));
}

TEST_F(ConfigTest, OptionalGlobalsAreSkippedOnlyOnRequestAndLiteralArgumentsDoNotGrantPermission) {
  ConfigInputs inputs;
  inputs.user = ParseIni("--no-require-user-globals\n--jobs=3\n");
  EXPECT_THAT(ResolveConfigInOrder(inputs, {}, "xff"), ElementsAre(FlagIs("--jobs=3", Source::kUser)));
  inputs.no_user_config = true;
  EXPECT_THAT(ResolveConfigInOrder(inputs, {}, "xff"), IsEmpty());
  inputs.system = ParseIni("-name '--no-require-system-globals'\n--color=auto\n");
  inputs.no_system_config = true;
  EXPECT_THAT(
      ResolveConfigInOrder(inputs, {}, "xff"),
      ElementsAre(
          FlagIs("-name", Source::kSystem), FlagIs("--no-require-system-globals", Source::kSystem),
          FlagIs("--color=auto", Source::kSystem)));
}

TEST_F(ConfigTest, SkipPermissionDirectivesNeverBecomeRuntimeGlobals) {
  ConfigInputs in;
  in.system.globals = {"--no-require-system-globals", "--no-require-user-globals", "--color=auto"};
  in.user = ParseXffrc("--no-require-user-globals --sort");
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--color=auto", Source::kSystem), FlagIs("--sort", Source::kUser)));
}

TEST_F(ConfigTest, SelectedSystemSectionsResolveInFileOrderWithProvenance) {
  ConfigInputs inputs;
  inputs.system = ParseIni(R"ini(
--color=auto
[first]
--hidden --jobs=2
[unused]
--color=never
[second]
--sort=none
)ini");
  inputs.configs = {"second", "first"};
  EXPECT_THAT(
      ResolveConfig(inputs), ElementsAre(
                                 FlagIs("--color=auto", Source::kSystem), FlagIs("--hidden", Source::kSystem),
                                 FlagIs("--jobs=2", Source::kSystem), FlagIs("--sort=none", Source::kSystem)));
  inputs.no_system_config = true;
  EXPECT_THAT(ResolveConfig(inputs), ElementsAre(FlagIs("--color=auto", Source::kSystem)));
}

TEST_F(ConfigTest, TransitiveAutomaticSelectorsCanArmAnExplicitFile) {
  ConfigInputs inputs;
  inputs.system = ParseIni("[outer]\n--config=inner\n[inner]\n--allow-exec");
  inputs.configs = {"outer"};
  EXPECT_THAT(ArmedFromTrustedTier(inputs, {}, "--allow-exec"), IsTrue());
}

TEST_F(ConfigTest, ExplicitFileCannotArmItselfThroughAnAutomaticNamedConfig) {
  ConfigInputs inputs;
  inputs.system = ParseIni("[arm]\n--allow-exec");
  inputs.xffrc = {{.path = "/named", .config = ParseXffrc("--config=arm")}};
  EXPECT_THAT(ArmedFromTrustedTier(inputs, {"--xffrc=/named"}, "--allow-exec"), IsFalse());
}

TEST_F(ConfigTest, DirectiveTokensExcludeFixedAndTerminatedArguments) {
  const std::vector<std::string> tokens = {
      "-printf", "--config=literal", "-exec", "echo", "--allow-exec", ";", "--hidden",
  };
  EXPECT_THAT(DirectiveTokens(tokens), ElementsAre("-printf", "-exec", "--hidden"));
}

TEST_F(ConfigTest, PrimaryArgumentsCannotSelectOrArmConfigurations) {
  ConfigInputs inputs;
  inputs.system = ParseIni("[inner]\n--color=never");
  inputs.user = ParseXffrc("-exec echo --config=inner --allow-exec \\;");
  EXPECT_THAT(ArmedFromTrustedTier(inputs, {}, "--allow-exec"), IsFalse());
  EXPECT_THAT(ResolveConfigInOrder(inputs, {}, "xff"), SizeIs(5));
}

TEST_F(ConfigTest, SystemDefaultsAreLowestPrecedence) {
  ConfigInputs in;
  in.system.globals = {"--color=auto", "--jobs=4"};
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(FlagIs("--color=auto", Source::kSystem), FlagIs("--jobs=4", Source::kSystem)));
}

TEST_F(ConfigTest, CommonAndBareLinesAlwaysApply) {
  ConfigInputs in;
  in.user = ParseXffrc("--color=never\n--sort");
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--color=never", Source::kUser), FlagIs("--sort", Source::kUser)));
}

TEST_F(ConfigTest, BaseSelectorGatedByActiveConfig) {
  // A named-config base gates the line on that --config being active (bare preset bases like
  // `xff:` are a separate concern rejected by GateConfig; ResolveConfig only does the gating).
  ConfigInputs in;
  in.user = ParseXffrc("[myproj]\n--format=jsonl\n[other]\n--warn");
  EXPECT_THAT(ResolveConfig(in), IsEmpty());  // no active --config -> neither base applies
  in.configs = {"myproj"};
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--format=jsonl", Source::kUser)));
}

TEST_F(ConfigTest, ColonInSectionNameRequiresAnExactSelector) {
  ConfigInputs in;
  in.user = ParseXffrc("[xff:debug]\n--jobs=1");
  in.configs = {"xff"};  // style active, but not the :debug named config
  EXPECT_THAT(ResolveConfig(in), IsEmpty());
  in.configs = {"xff", "debug"};
  EXPECT_THAT(ResolveConfig(in), IsEmpty());
  in.configs = {"xff:debug"};
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--jobs=1", Source::kUser)));
}

TEST_F(ConfigTest, LayerPrecedenceSystemThenUser) {
  ConfigInputs in;
  in.system.globals = {"--color=auto"};
  in.user = ParseXffrc("--sort\n--color=never");  // user wins over the system default
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(
                             FlagIs("--color=auto", Source::kSystem), FlagIs("--sort", Source::kUser),
                             FlagIs("--color=never", Source::kUser)));
}

TEST_F(ConfigTest, XffrcTierResolvesAboveUser) {
  ConfigInputs in;
  in.user = ParseXffrc("--color=auto");
  in.xffrc = {{.path = "/named", .config = ParseXffrc("--color=never")}};
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(FlagIs("--color=auto", Source::kUser), FlagIs("--color=never", Source::kXffrc)));
}

TEST_F(ConfigTest, ArmedFromTrustedTierAcceptsCliUserSystemNotXffrc) {
  ConfigInputs in;
  EXPECT_THAT(ArmedFromTrustedTier(in, {"--allow-exec"}, "--allow-exec"), IsTrue());  // typed on the CLI
  EXPECT_THAT(ArmedFromTrustedTier(in, {}, "--allow-exec"), IsFalse());               // nowhere
  in.system.globals = {"--allow-exec"};
  EXPECT_THAT(ArmedFromTrustedTier(in, {}, "--allow-exec"), IsTrue());  // system defaults
  in.system.globals = {};
  in.user = ParseXffrc("--allow-exec");
  EXPECT_THAT(ArmedFromTrustedTier(in, {}, "--allow-exec"), IsTrue());  // an applying user line
  in.user = {};
  in.xffrc = {{.path = "/named", .config = ParseXffrc("--allow-exec")}};
  EXPECT_THAT(ArmedFromTrustedTier(in, {}, "--allow-exec"), IsFalse());  // NOT from an --xffrc file (no self-arming)
}

TEST_F(ConfigTest, ArmedFromTrustedTierRespectsActiveConfig) {
  ConfigInputs in;
  in.user = ParseXffrc("[debug]\n--allow-exec");                         // only under --config=debug
  EXPECT_THAT(ArmedFromTrustedTier(in, {}, "--allow-exec"), IsFalse());  // debug not active -> line inert
  in.configs = {"debug"};
  EXPECT_THAT(ArmedFromTrustedTier(in, {}, "--allow-exec"), IsTrue());
}

TEST_F(ConfigTest, SourceNameMapsEachLayer) {
  EXPECT_THAT(SourceName(Source::kSystem), "system");
  EXPECT_THAT(SourceName(Source::kUser), "user");
  EXPECT_THAT(SourceName(Source::kXffrc), "xffrc");
  EXPECT_THAT(SourceName(Source::kCli), "cli");
  EXPECT_THAT(SourceName(Source::kUnset), "unset");
}

TEST_F(ConfigTest, ActiveStyleDefaultsToXffAndTracksTheConfigStack) {
  EXPECT_THAT(ActiveStyle({}), registry::Style::kXff);         // no selector -> the modern default
  EXPECT_THAT(ActiveStyle({"find"}), registry::Style::kFind);  // find expression vocabulary
  EXPECT_THAT(ActiveStyle({"xff"}), registry::Style::kXff);
  EXPECT_THAT(ActiveStyle({"debug"}), registry::Style::kXff);           // a custom config name is not a style
  EXPECT_THAT(ActiveStyle({"xff:2"}), registry::Style::kXff);           // version-pinned epoch -> base "xff"
  EXPECT_THAT(ActiveStyle({"find", "debug"}), registry::Style::kFind);  // a custom config keeps the style
  EXPECT_THAT(ActiveStyle({"xff", "find"}), registry::Style::kFind);    // the last style selector wins
  EXPECT_THAT(ActiveStyle({"find", "xff"}), registry::Style::kXff);
  EXPECT_THAT(ActiveStyle({"rg"}), registry::Style::kRg);            // ripgrep-like defaults
  EXPECT_THAT(ActiveStyle({"rg:2"}), registry::Style::kRg);          // version-pinned epoch -> base "rg"
  EXPECT_THAT(ActiveStyle({"rg", "find"}), registry::Style::kFind);  // last selector still wins
  EXPECT_THAT(ActiveStyle({"xfd"}), registry::Style::kXff);          // xfd was dropped: not a style -> default xff
}

TEST_F(ConfigTest, DefaultStyleForProgramSelectsByBasename) {
  EXPECT_THAT(DefaultStyleForProgram("find"), "find");
  EXPECT_THAT(DefaultStyleForProgram("/usr/local/bin/find"), "find");  // the basename, not the path
  EXPECT_THAT(DefaultStyleForProgram("./find"), "find");
  EXPECT_THAT(DefaultStyleForProgram("xff"), "xff");
  EXPECT_THAT(DefaultStyleForProgram("/opt/mboworks/xff"), "xff");
  EXPECT_THAT(DefaultStyleForProgram(""), "xff");  // no name -> the modern default
  EXPECT_THAT(DefaultStyleForProgram("rg"), "rg");
  // A non-preset invocation name is returned verbatim as a named-config selector (a `mytool`
  // symlink activates a `mytool:` config block; the base style stays the xff default). xfd was
  // dropped and fd was never a style, so both are plain verbatim names now (no magic remap).
  EXPECT_THAT(DefaultStyleForProgram("xfd"), "xfd");
  EXPECT_THAT(DefaultStyleForProgram("/usr/bin/fd"), "fd");  // basename, verbatim (not remapped to rg)
  EXPECT_THAT(DefaultStyleForProgram("myfind"), "myfind");
  EXPECT_THAT(DefaultStyleForProgram("findutils"), "findutils");
  EXPECT_THAT(DefaultStyleForProgram("/opt/bin/mytool"), "mytool");  // basename, verbatim
}

TEST_F(ConfigTest, DefaultStyleForProgramStripsFullSuffix) {
  // The extras-included full build is `<prefix>_full`; it behaves exactly as its lean twin, so the
  // `_full` suffix is stripped before the name is used as a selector.
  EXPECT_THAT(DefaultStyleForProgram("xff_full"), "xff");
  EXPECT_THAT(DefaultStyleForProgram("/opt/mboworks/xff_full"), "xff");  // basename, then strip
  EXPECT_THAT(DefaultStyleForProgram("find_full"), "find");              // still find, not the xff default
  EXPECT_THAT(DefaultStyleForProgram("rg_full"), "rg");
  EXPECT_THAT(DefaultStyleForProgram("mytool_full"), "mytool");  // a custom full binary keeps its base
  EXPECT_THAT(DefaultStyleForProgram("_full"), "xff");           // a bare `_full` -> the modern default
  EXPECT_THAT(DefaultStyleForProgram("fullbore"), "fullbore");   // only a `_full` *suffix* is stripped
  EXPECT_THAT(DefaultStyleForProgram("full"), "full");           // not a suffix match
}

TEST_F(ConfigTest, ExplainConfigTagsEachFlagWithProvenance) {
  const std::vector<ResolvedFlag> resolved = {
      {.flag = "--color=auto", .source = Source::kSystem}, {.flag = "--sort", .source = Source::kUser}};
  const std::string explained =
      ExplainConfig({resolved[0], resolved[1], {.flag = "--format=jsonl", .source = Source::kCli}});
  EXPECT_THAT(explained, HasSubstr("system\t--color=auto\n"));
  EXPECT_THAT(explained, HasSubstr("user\t--sort\n"));
  EXPECT_THAT(explained, HasSubstr("cli\t--format=jsonl\n"));
}

TEST_F(ConfigTest, ExplainConfigShowsPhysicalOriginWithoutChangingApplicationOrder) {
  const std::vector<ResolvedFlag> application = {
      {
          .flag = "--color=never",
          .source = Source::kUser,
          .origin = {.path = "/user.ini", .line = 4, .section = "first"},
      },
      {.flag = "--color=auto", .source = Source::kCli},
      {
          .flag = "--color=always",
          .source = Source::kUser,
          .origin = {.path = "/user.ini", .line = 7, .section = "last"},
      },
  };
  EXPECT_THAT(ExplainConfig(application), WithDropIndent(EqualsText(R"out(
    # xff effective configuration (application order; overrides follow each flag's rules)
    # at /user.ini:4 [first]
    user	--color=never
    cli	--color=auto
    # at /user.ini:7 [last]
    user	--color=always
  )out")));
}

TEST_F(ConfigTest, SelectorsExpandAtTheirCommandLinePosition) {
  ConfigInputs in;
  in.user = ParseXffrc("--color=auto\n[early]\n--jobs=2\n[late]\n--color=never");
  EXPECT_THAT(
      ResolveConfigInOrder(in, {"--config=early", "--warn", "--config=late"}, "xff"),
      ElementsAre(
          FlagIs("--color=auto", Source::kUser), FlagIs("--config=early", Source::kCli),
          FlagIs("--jobs=2", Source::kUser), FlagIs("--warn", Source::kCli), FlagIs("--config=late", Source::kCli),
          FlagIs("--color=never", Source::kUser)));
}

TEST_F(ConfigTest, ExplicitFileLoadsInPlaceAndCanBeActivatedLater) {
  ConfigInputs in;
  in.xffrc = {{.path = "/one", .config = ParseXffrc("--jobs=2\n[debug]\n--warn")}};
  EXPECT_THAT(
      ResolveConfigInOrder(in, {"--color=auto", "--xffrc=/one", "--config=debug"}, "xff"),
      ElementsAre(
          FlagIs("--color=auto", Source::kCli), FlagIs("--xffrc=/one", Source::kCli),
          FlagIs("--jobs=2", Source::kXffrc), FlagIs("--config=debug", Source::kCli),
          FlagIs("--warn", Source::kXffrc)));
}

TEST_F(ConfigTest, ConfigSuppliedSelectorExpandsAtItsOwnPosition) {
  ConfigInputs in;
  in.user = ParseXffrc("[outer]\n--warn --config=inner --sort\n[inner]\n--jobs=2");
  EXPECT_THAT(
      ResolveConfigInOrder(in, {"--config=outer"}, "xff"),
      ElementsAre(
          FlagIs("--config=outer", Source::kCli), FlagIs("--warn", Source::kUser),
          FlagIs("--config=inner", Source::kUser), FlagIs("--jobs=2", Source::kUser), FlagIs("--sort", Source::kUser)));
}

TEST_F(ConfigTest, SystemNamedSectionsExpandTransitivelyAtSelectorPosition) {
  ConfigInputs in;
  in.system = ParseIni(
      "--color=auto\n"
      "[outer]\n"
      "--hidden\n"
      "--config=inner\n"
      "[inner]\n"
      "-E\n");

  EXPECT_THAT(
      ResolveConfigInOrder(in, {"--config=outer"}, "xff"),
      ElementsAre(
          FlagIs("--color=auto", Source::kSystem), FlagIs("--config=outer", Source::kCli),
          FlagIs("--hidden", Source::kSystem), FlagIs("--config=inner", Source::kSystem),
          FlagIs("-E", Source::kSystem)));
}

TEST_F(ConfigTest, CompositionFinishesEarlierFilesBeforeApplyingRefinements) {
  ConfigInputs inputs;
  inputs.system = ParseIni("[dev]\n--config=checks\n--color=auto\n[checks]\n--hidden");
  inputs.user = ParseIni("[dev]\n--config=checks\n--color=always\n[checks]\n--jobs=2");
  inputs.xffrc = {
      {.path = "/first", .config = ParseIni("[dev]\n--config=checks\n--color=never")},
      {.path = "/second", .config = ParseIni("[dev]\n--color=auto")},
  };
  EXPECT_THAT(
      ResolveConfigInOrder(inputs, {"--xffrc=/first", "--xffrc=/second", "--config=dev"}, "xff"),
      ElementsAre(
          FlagIs("--xffrc=/first", Source::kCli), FlagIs("--xffrc=/second", Source::kCli),
          FlagIs("--config=dev", Source::kCli), FlagIs("--config=checks", Source::kSystem),
          FlagIs("--hidden", Source::kSystem), FlagIs("--color=auto", Source::kSystem),
          FlagIs("--config=checks", Source::kUser), FlagIs("--jobs=2", Source::kUser),
          FlagIs("--color=always", Source::kUser), FlagIs("--config=checks", Source::kXffrc),
          FlagIs("--color=never", Source::kXffrc), FlagIs("--color=auto", Source::kXffrc)));
}

TEST_F(ConfigTest, SameNameIsRefinedInSystemUserAndEachExplicitFile) {
  ConfigInputs inputs;
  inputs.system = ParseIni("[dev]\n--color=auto\n--config=checks\n[checks]\n--hidden");
  inputs.user = ParseXffrc("[dev]\n--color=always");
  inputs.xffrc = {
      {.path = "/first", .config = ParseXffrc("[dev]\n--color=never")},
      {.path = "/second", .config = ParseXffrc("[dev]\n--color=auto")},
  };
  EXPECT_THAT(
      ResolveConfigInOrder(inputs, {"--config=dev", "--xffrc=/first", "--xffrc=/second"}, "xff"),
      ElementsAre(
          FlagIs("--config=dev", Source::kCli), FlagIs("--color=auto", Source::kSystem),
          FlagIs("--config=checks", Source::kSystem), FlagIs("--hidden", Source::kSystem),
          FlagIs("--color=always", Source::kUser), FlagIs("--xffrc=/first", Source::kCli),
          FlagIs("--color=never", Source::kXffrc), FlagIs("--xffrc=/second", Source::kCli),
          FlagIs("--color=auto", Source::kXffrc)));
}

TEST_F(ConfigTest, OrderedResolutionRetainsOriginsAcrossExpansionAndComposition) {
  ConfigInputs inputs;
  inputs.system = ParseIni(R"ini(--block-file-writing
[locked]
--block-execution
)ini");
  inputs.user = ParseIni(R"ini([profile]
--config=locked
--no-safe-block-file-writing
)ini");
  inputs.xffrc = {{
      .path = "/task.rc",
      .config = ParseIni(R"ini([profile]
--safe
-name '--safe'
)ini"),
  }};
  inputs.sources = {
      {.path = "/etc/xff.ini", .layer = Source::kSystem, .found = true},
      {.path = "/home/user/xff.ini", .layer = Source::kUser, .found = true}};
  const auto resolved = ResolveConfigInOrder(inputs, {"--config=profile", "--xffrc=/task.rc", "--no-safe"}, "xff");
  const auto origin = [](std::string_view path, std::size_t line, std::string_view section) {
    return Field(
        &ResolvedFlag::origin, AllOf(
                                   Field(&FlagOrigin::path, Eq(path)), Field(&FlagOrigin::line, Eq(line)),
                                   Field(&FlagOrigin::section, Eq(section))));
  };
  EXPECT_THAT(
      resolved, Contains(AllOf(FlagIs("--block-archive-writing", Source::kSystem), origin("/etc/xff.ini", 1, ""))));
  EXPECT_THAT(
      resolved, Contains(AllOf(FlagIs("--block-execution", Source::kSystem), origin("/etc/xff.ini", 3, "locked"))));
  EXPECT_THAT(
      resolved,
      Contains(AllOf(
          FlagIs("--no-safe-block-temp-file-writing", Source::kUser), origin("/home/user/xff.ini", 3, "profile"))));
  EXPECT_THAT(
      resolved, Contains(AllOf(
                    FlagIs("--safe", Source::kXffrc), Field(&ResolvedFlag::is_argument, IsFalse()),
                    origin("/task.rc", 2, "profile"))));
  EXPECT_THAT(
      resolved, Contains(AllOf(
                    FlagIs("--safe", Source::kXffrc), Field(&ResolvedFlag::is_argument, IsTrue()),
                    origin("/task.rc", 3, "profile"))));
  EXPECT_THAT(resolved, Contains(AllOf(FlagIs("--no-safe", Source::kCli), origin("", 0, ""))));
}

TEST_F(ConfigTest, LegacyResolutionRetainsOriginsAndSyntheticGlobalsDoNotInventLines) {
  ConfigInputs inputs;
  inputs.user = ParseIni(R"ini(--safe
[profile]
--no-safe-block-execution
)ini");
  inputs.sources = {{.path = "/user.ini", .layer = Source::kUser, .found = true}};
  inputs.configs = {"profile"};
  const auto resolved = ResolveConfig(inputs);
  ASSERT_THAT(resolved, SizeIs(2));
  EXPECT_THAT(resolved.at(0).origin.path, Eq("/user.ini"));
  EXPECT_THAT(resolved.at(0).origin.line, Eq(1));
  EXPECT_THAT(resolved.at(1).origin.section, Eq("profile"));
  EXPECT_THAT(resolved.at(1).origin.line, Eq(3));
  inputs.user.globals = {"--no-safe"};
  const auto synthesized = ResolveConfigInOrder(inputs, {}, "xff");
  ASSERT_THAT(synthesized, SizeIs(1));
  EXPECT_THAT(synthesized.at(0).flag, Eq("--no-safe"));
  EXPECT_THAT(synthesized.at(0).origin.line, Eq(0));
}

TEST_F(ConfigTest, SafetyExplanationShowsMandatoryAndShadowedProfileOrigins) {
  ConfigInputs inputs;
  inputs.system = ParseIni("--block-file-writing");
  inputs.user = ParseIni("--safe --no-safe-block-file-writing");
  inputs.sources = {
      {.path = "/system.ini", .layer = Source::kSystem, .found = true},
      {.path = "/user.ini", .layer = Source::kUser, .found = true}};
  const auto resolved = ResolveConfigInOrder(inputs, {}, "xff");
  const auto explanation = ExplainSafety(resolved, inputs);
  EXPECT_THAT(explanation, HasSubstr("safe-mode\ton\tuser /user.ini:1 (--safe)"));
  EXPECT_THAT(
      explanation, HasSubstr(
                       "safety\tfile-writing\tblock\tblock\tallow\tunconditional block\t"
                       "system /system.ini:1 (--block-file-writing)\tuser /user.ini:1 (--no-safe-block-file-writing)"));
  EXPECT_THAT(explanation, HasSubstr("safety\tarchive-writing\tblock\tblock\tallow\tunconditional block"));
  EXPECT_THAT(explanation, HasSubstr("safety\texecution\tblock\tnone\tblock\tactive profile block\tdefault"));
  EXPECT_THAT(explanation, HasSubstr("root\ttemp\t(unset)\tdefault"));
}

TEST_F(ConfigTest, SafetyExplanationKeepsFirstMandatoryAndRootDeclarations) {
  ConfigInputs inputs;
  inputs.system = ParseIni(R"ini(--block-policy-categories=archive,temp
--block-execution
--temp-root=/admin/temp
--output-root=/admin/out
)ini");
  inputs.user = ParseIni(R"ini(--block-policy-categories=output
--block-execution
--temp-root=/user/temp
--output-root=/user/out
)ini");
  inputs.sources = {
      {.path = "/system.ini", .layer = Source::kSystem, .found = true},
      {.path = "/user.ini", .layer = Source::kUser, .found = true}};
  const auto resolved = ResolveConfigInOrder(inputs, {"--no-safe", "--block-execution"}, "xff");
  const auto explanation = ExplainSafety(resolved, inputs);
  EXPECT_THAT(explanation, HasSubstr("policy\tsystem\tarchive,temp"));
  EXPECT_THAT(explanation, HasSubstr("policy\tuser\toutput"));
  EXPECT_THAT(explanation, HasSubstr("unconditional block\tsystem /system.ini:2 (--block-execution)"));
  EXPECT_THAT(explanation, HasSubstr("root\ttemp\t/admin/temp\tsystem /system.ini:3"));
  EXPECT_THAT(explanation, HasSubstr("root\toutput\t/admin/out\tsystem /system.ini:4"));
}

TEST_F(ConfigTest, SafetyExplanationIgnoresLiteralArgumentsAndTracksCliDeactivation) {
  const std::vector<ResolvedFlag> literal = {{.flag = "--safe", .source = Source::kXffrc, .is_argument = true}};
  EXPECT_THAT(ExplainSafety(literal, {}), HasSubstr("safe-mode\toff\tdefault"));
  const auto resolved = ResolveConfigInOrder({}, {"--safe", "--no-safe", "--dry-run"}, "xff");
  const auto explanation = ExplainSafety(resolved, {});
  EXPECT_THAT(explanation, HasSubstr("safe-mode\toff\tcli (--no-safe)"));
  EXPECT_THAT(explanation, HasSubstr("dry-run\ton\tcli (--dry-run)"));
  EXPECT_THAT(explanation, HasSubstr("safety\texecution\tallow\tnone\tblock\tinactive profile\tcli (--no-safe)"));
}

TEST_F(ConfigTest, ExplainSourcesListsActiveStyleAndConsultedFiles) {
  const std::vector<ConfigSource> sources = {
      {.path = "/etc/xff.ini", .layer = Source::kSystem, .found = false},
      {.path = "/home/u/.config/xff/config", .layer = Source::kUser, .found = true}};
  const std::string out = ExplainSources(sources, registry::Style::kFind);
  EXPECT_THAT(out, HasSubstr("# xff active style: find\n"));
  EXPECT_THAT(out, HasSubstr("source\tsystem\tabsent\t/etc/xff.ini\n"));
  EXPECT_THAT(out, HasSubstr("source\tuser\tfound\t/home/u/.config/xff/config\n"));
}

}  // namespace
}  // namespace xff::config
