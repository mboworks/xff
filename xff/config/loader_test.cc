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

#include "xff/config/loader.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/config/config.h"

namespace xff::config {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::SizeIs;

// A FileReader backed by an in-memory path->contents map; absent paths read as
// nullopt (missing file).
struct FakeFs {
  std::map<std::string, std::string> files;

  std::optional<std::string> Read(std::string_view path) const {
    if (const auto it = files.find(std::string(path)); it != files.end()) {
      return it->second;
    }
    return std::nullopt;
  }
};

testing::Matcher<ResolvedFlag> FlagIs(const std::string& flag, Source source) {
  return AllOf(Field("flag", &ResolvedFlag::flag, flag), Field("source", &ResolvedFlag::source, source));
}

testing::Matcher<ConfigSource> SourceIs(const std::string& path, Source layer, bool found) {
  return AllOf(
      Field("path", &ConfigSource::path, path), Field("layer", &ConfigSource::layer, layer),
      Field("found", &ConfigSource::found, found));
}

struct LoaderTest : ::testing::Test {};

TEST_F(LoaderTest, UserConfigPathPrefersXffConfigThenXdgThenHome) {
  DiscoveryOptions opts;
  opts.home = "/home/u";
  EXPECT_THAT(UserConfigPath(opts), "/home/u/.config/xff/config");
  opts.xdg_config_home = "/xdg";
  EXPECT_THAT(UserConfigPath(opts), "/xdg/xff/config");
  opts.xff_config = "/explicit/rc";
  EXPECT_THAT(UserConfigPath(opts), "/explicit/rc");
}

TEST_F(LoaderTest, UserConfigPathEmptyWhenNoEnv) {
  EXPECT_THAT(UserConfigPath(DiscoveryOptions{}), IsEmpty());
}

TEST_F(LoaderTest, DiscoverAppliesSystemThenUserLayersWithActiveConfig) {
  FakeFs fs;
  fs.files["/etc/xff.ini"] = "--color=auto\n";
  fs.files["/home/u/.config/xff/config"] = "common: --sort\nxff: --format=jsonl\nfind: --warn\n";
  DiscoveryOptions opts;
  opts.home = "/home/u";
  opts.configs = {"xff"};  // the find: line stays inert
  const ConfigInputs in = Discover(opts, [&fs](std::string_view path) { return fs.Read(path); });
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(
                             FlagIs("--color=auto", Source::kSystem), FlagIs("--sort", Source::kUser),
                             FlagIs("--format=jsonl", Source::kUser)));
}

TEST_F(LoaderTest, ExplicitXffrcFilesFormTheirOwnTierInOrder) {
  FakeFs fs;
  fs.files["/explicit.rc"] = "common: --jobs=2\n";
  fs.files["/extra.rc"] = "common: --color=never\n";
  DiscoveryOptions opts;
  opts.xffrc_files = {"/explicit.rc", "/extra.rc"};
  const ConfigInputs in = Discover(opts, [&fs](std::string_view path) { return fs.Read(path); });
  // --xffrc files land in the xffrc tier (not the user layer), in order.
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(FlagIs("--jobs=2", Source::kXffrc), FlagIs("--color=never", Source::kXffrc)));
}

TEST_F(LoaderTest, AutomaticDiscoveryDoesNotReadExplicitPaths) {
  DiscoveryOptions opts;
  opts.xffrc_files = {"/explicit"};
  std::vector<std::string> reads;
  const auto read = [&reads](std::string_view path) -> std::optional<std::string> {
    reads.emplace_back(path);
    return std::nullopt;
  };
  const ConfigInputs automatic = DiscoverAutomatic(opts, read);
  EXPECT_THAT(reads, ElementsAre("/etc/xff.ini"));
  EXPECT_THAT(automatic.xffrc, ElementsAre(FieldsAre("/explicit", IsEmpty())));
  const ConfigInputs complete = DiscoverExplicit(automatic, read);
  EXPECT_THAT(reads, ElementsAre("/etc/xff.ini", "/explicit"));
  EXPECT_THAT(
      complete.sources,
      ElementsAre(SourceIs("/etc/xff.ini", Source::kSystem, false), SourceIs("/explicit", Source::kXffrc, false)));
}

TEST_F(LoaderTest, NoConfigStillInspectsAutomaticSourcesAndKeepsExplicitXffrc) {
  FakeFs fs;
  fs.files["/etc/xff.ini"] = "--color=auto\n--no-allow-exec\n";
  fs.files["/home/u/.config/xff/config"] = "common: --sort\n";
  DiscoveryOptions opts;
  opts.home = "/home/u";
  opts.xffrc_files = {"/extra.rc"};
  opts.no_system_config = true;
  opts.no_user_config = true;
  const ConfigInputs in = Discover(opts, [&fs](std::string_view path) { return fs.Read(path); });
  EXPECT_THAT(
      in.sources,
      ElementsAre(
          SourceIs("/etc/xff.ini", Source::kSystem, true), SourceIs("/home/u/.config/xff/config", Source::kUser, true),
          SourceIs("/extra.rc", Source::kXffrc, false)));
  EXPECT_THAT(in.system.globals, ElementsAre("--color=auto", "--no-allow-exec"));

  EXPECT_THAT(in.user, SizeIs(1));
  EXPECT_THAT(in.xffrc, ElementsAre(FieldsAre("/extra.rc", IsEmpty())));
}

TEST_F(LoaderTest, MissingFilesYieldEmptyLayers) {
  FakeFs fs;  // nothing on disk
  DiscoveryOptions opts;
  opts.home = "/home/u";
  const ConfigInputs in = Discover(opts, [&fs](std::string_view path) { return fs.Read(path); });
  EXPECT_THAT(in.system.globals, IsEmpty());
  EXPECT_THAT(ResolveConfig(in), IsEmpty());
}

TEST_F(LoaderTest, SelectorsFromGlobalsExtractsConfigSelectorsInOrder) {
  const DiscoveryOptions opts = SelectorsFromGlobals(
      {"-L", "--config=xff", "--no-config", "--no-system-config", "--no-user-config", "--xffrc=/a", "--config=debug",
       "--xffrc=/b", "--color=auto"});
  EXPECT_THAT(opts.no_config, IsTrue());
  EXPECT_THAT(opts.no_system_config, IsTrue());
  EXPECT_THAT(opts.no_user_config, IsTrue());
  EXPECT_THAT(opts.configs, ElementsAre("xff", "debug"));
  EXPECT_THAT(opts.xffrc_files, ElementsAre("/a", "/b"));
}

TEST_F(LoaderTest, DiscoverRecordsConsultedSourcesForExplain) {
  FakeFs fs;
  fs.files["/etc/xff.ini"] = "--color=auto\n";  // present
  fs.files["/extra.rc"] = "common: --sort\n";   // present (explicit --xffrc)
  DiscoveryOptions opts;
  opts.home = "/home/u";             // user path computed, but the file is absent
  opts.xffrc_files = {"/extra.rc"};  // explicit file, present
  const ConfigInputs in = Discover(opts, [&fs](std::string_view path) { return fs.Read(path); });
  // Every consulted path is recorded in precedence order with its found/absent state. There is no
  // Only system, the user path, and the explicit --xffrc file are consulted.
  EXPECT_THAT(
      in.sources,
      ElementsAre(
          SourceIs("/etc/xff.ini", Source::kSystem, true), SourceIs("/home/u/.config/xff/config", Source::kUser, false),
          SourceIs("/extra.rc", Source::kXffrc, true)));
}

}  // namespace
}  // namespace xff::config
