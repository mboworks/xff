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

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace xff::config {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct ConfigTest : ::testing::Test {};

// Matches a ResolvedFlag by both its flag text and its provenance, so an
// ElementsAre(...) assertion folds size, order, flag, and Source into one check.
testing::Matcher<ResolvedFlag> FlagIs(const std::string& flag, Source source) {
  return AllOf(Field("flag", &ResolvedFlag::flag, flag), Field("source", &ResolvedFlag::source, source));
}

TEST_F(ConfigTest, NoConfigYieldsEmpty) {
  ConfigInputs in;
  in.system.defaults = {"--color=auto"};
  in.user = ParseXffrc("common: --sort");
  in.no_system_config = true;
  in.no_user_config = true;
  EXPECT_THAT(ResolveConfig(in), IsEmpty());
}

TEST_F(ConfigTest, GranularSkipControlsSuppressOnlyTheirAutomaticTier) {
  ConfigInputs in;
  in.system.defaults = {"--color=auto", "--allow-no-system-config"};
  in.user = ParseXffrc("common: --sort\ncommon: --allow-no-user-config");
  in.xffrc = {{.path = "/named", .lines = ParseXffrc("common: --jobs=2")}};
  in.no_system_config = true;
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--sort", Source::kUser), FlagIs("--jobs=2", Source::kXffrc)));
  in.no_system_config = false;
  in.no_user_config = true;
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(FlagIs("--color=auto", Source::kSystem), FlagIs("--jobs=2", Source::kXffrc)));
}

TEST_F(ConfigTest, SkipPermissionDirectivesNeverBecomeRuntimeGlobals) {
  ConfigInputs in;
  in.system.defaults = {"--allow-no-config", "--allow-no-user-config", "--color=auto"};
  in.user = ParseXffrc("common: --allow-no-user-config --sort");
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--color=auto", Source::kSystem), FlagIs("--sort", Source::kUser)));
}

TEST_F(ConfigTest, SystemDefaultsAreLowestPrecedence) {
  ConfigInputs in;
  in.system.defaults = {"--color=auto", "--jobs=4"};
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(FlagIs("--color=auto", Source::kSystem), FlagIs("--jobs=4", Source::kSystem)));
}

TEST_F(ConfigTest, CommonAndBareLinesAlwaysApply) {
  ConfigInputs in;
  in.user = ParseXffrc("common: --color=never\n--sort");
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--color=never", Source::kUser), FlagIs("--sort", Source::kUser)));
}

TEST_F(ConfigTest, BaseSelectorGatedByActiveConfig) {
  // A named-config base gates the line on that --config being active (bare preset bases like
  // `xff:` are a separate concern rejected by GateConfig; ResolveConfig only does the gating).
  ConfigInputs in;
  in.user = ParseXffrc("myproj: --format=jsonl\nother: --warn");
  EXPECT_THAT(ResolveConfig(in), IsEmpty());  // no active --config -> neither base applies
  in.configs = {"myproj"};
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--format=jsonl", Source::kUser)));
}

TEST_F(ConfigTest, ConfigSelectorGatedByNamedConfig) {
  ConfigInputs in;
  in.user = ParseXffrc("xff:debug: --jobs=1");
  in.configs = {"xff"};  // style active, but not the :debug named config
  EXPECT_THAT(ResolveConfig(in), IsEmpty());
  in.configs = {"xff", "debug"};
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--jobs=1", Source::kUser)));
}

TEST_F(ConfigTest, LayerPrecedenceSystemThenUser) {
  ConfigInputs in;
  in.system.defaults = {"--color=auto"};
  in.user = ParseXffrc("common: --sort\ncommon: --color=never");  // user wins over the system default
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(
                             FlagIs("--color=auto", Source::kSystem), FlagIs("--sort", Source::kUser),
                             FlagIs("--color=never", Source::kUser)));
}

TEST_F(ConfigTest, XffrcTierResolvesAboveUser) {
  ConfigInputs in;
  in.user = ParseXffrc("common: --color=auto");
  in.xffrc = {{.path = "/named", .lines = ParseXffrc("common: --color=never")}};
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(FlagIs("--color=auto", Source::kUser), FlagIs("--color=never", Source::kXffrc)));
}

TEST_F(ConfigTest, ArmedFromTrustedTierAcceptsCliUserSystemNotXffrc) {
  ConfigInputs in;
  EXPECT_THAT(ArmedFromTrustedTier(in, {"--allow-exec"}, "--allow-exec"), IsTrue());  // typed on the CLI
  EXPECT_THAT(ArmedFromTrustedTier(in, {}, "--allow-exec"), IsFalse());               // nowhere
  in.system.defaults = {"--allow-exec"};
  EXPECT_THAT(ArmedFromTrustedTier(in, {}, "--allow-exec"), IsTrue());  // system defaults
  in.system.defaults = {};
  in.user = ParseXffrc("common: --allow-exec");
  EXPECT_THAT(ArmedFromTrustedTier(in, {}, "--allow-exec"), IsTrue());  // an applying user line
  in.user = {};
  in.xffrc = {{.path = "/named", .lines = ParseXffrc("common: --allow-exec")}};
  EXPECT_THAT(ArmedFromTrustedTier(in, {}, "--allow-exec"), IsFalse());  // NOT from an --xffrc file (no self-arming)
}

TEST_F(ConfigTest, ArmedFromTrustedTierRespectsActiveConfig) {
  ConfigInputs in;
  in.user = ParseXffrc("debug: --allow-exec");                           // only under --config=debug
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

TEST_F(ConfigTest, SelectorsExpandAtTheirCommandLinePosition) {
  ConfigInputs in;
  in.user = ParseXffrc("common: --color=auto\nearly: --jobs=2\nlate: --color=never");
  EXPECT_THAT(
      ResolveConfigInOrder(in, {"--config=early", "--warn", "--config=late"}, "xff"),
      ElementsAre(
          FlagIs("--color=auto", Source::kUser), FlagIs("--config=early", Source::kCli),
          FlagIs("--jobs=2", Source::kUser), FlagIs("--warn", Source::kCli), FlagIs("--config=late", Source::kCli),
          FlagIs("--color=never", Source::kUser)));
}

TEST_F(ConfigTest, ExplicitFileLoadsInPlaceAndCanBeActivatedLater) {
  ConfigInputs in;
  in.xffrc = {{.path = "/one", .lines = ParseXffrc("common: --jobs=2\ndebug: --warn")}};
  EXPECT_THAT(
      ResolveConfigInOrder(in, {"--color=auto", "--xffrc=/one", "--config=debug"}, "xff"),
      ElementsAre(
          FlagIs("--color=auto", Source::kCli), FlagIs("--xffrc=/one", Source::kCli),
          FlagIs("--jobs=2", Source::kXffrc), FlagIs("--config=debug", Source::kCli),
          FlagIs("--warn", Source::kXffrc)));
}

TEST_F(ConfigTest, ConfigSuppliedSelectorExpandsAtItsOwnPosition) {
  ConfigInputs in;
  in.user = ParseXffrc("outer: --warn --config=inner --sort\ninner: --jobs=2");
  EXPECT_THAT(
      ResolveConfigInOrder(in, {"--config=outer"}, "xff"),
      ElementsAre(
          FlagIs("--config=outer", Source::kCli), FlagIs("--warn", Source::kUser),
          FlagIs("--config=inner", Source::kUser), FlagIs("--jobs=2", Source::kUser), FlagIs("--sort", Source::kUser)));
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
