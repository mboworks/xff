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

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/config/config.h"

namespace xff::config {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::SizeIs;
using ::testing::StartsWith;

// A FileReader backed by an in-memory path->contents map; absent paths read as
// NotFound (missing file).
struct FakeFs {
  std::map<std::string, std::string> files;

  absl::StatusOr<std::string> Read(std::string_view path) const {
    if (const auto it = files.find(std::string(path)); it != files.end()) {
      return it->second;
    }
    return absl::NotFoundError("missing");
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

TEST_F(LoaderTest, UserConfigPathUsesOnlyAccountHome) {
  EXPECT_THAT(UserConfigPath("/home/u"), IsOkAndHolds("/home/u/.config/xff/config"));
  EXPECT_THAT(UserConfigPath("/home/u/"), IsOkAndHolds("/home/u/.config/xff/config"));
  EXPECT_THAT(UserConfigPath("/"), IsOkAndHolds("/.config/xff/config"));
  EXPECT_THAT(UserConfigPath(""), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(UserConfigPath("relative"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(LoaderTest, DiscoverAppliesSystemThenUserLayersWithActiveConfig) {
  FakeFs fs;
  fs.files["/etc/xff.ini"] = "--color=auto\n";
  fs.files["/home/u/.config/xff/config"] = "--sort\n\n[xff]\n--format=jsonl\n[find]\n--warn";
  DiscoveryOptions opts;
  opts.paths.user = "/home/u/.config/xff/config";
  opts.configs = {"xff"};  // the find: line stays inert
  ASSERT_OK_AND_ASSIGN(const auto in, Discover(opts, [&fs](std::string_view path) { return fs.Read(path); }));
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(
                             FlagIs("--color=auto", Source::kSystem), FlagIs("--sort", Source::kUser),
                             FlagIs("--format=jsonl", Source::kUser)));
}

TEST_F(LoaderTest, ExplicitXffrcFilesFormTheirOwnTierInOrder) {
  FakeFs fs;
  fs.files["/explicit.rc"] = "--jobs=2\n";
  fs.files["/extra.rc"] = "--color=never\n";
  DiscoveryOptions opts;
  opts.xffrc_files = {"/explicit.rc", "/extra.rc"};
  ASSERT_OK_AND_ASSIGN(const auto in, Discover(opts, [&fs](std::string_view path) { return fs.Read(path); }));
  // --xffrc files land in the xffrc tier (not the user layer), in order.
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(FlagIs("--jobs=2", Source::kXffrc), FlagIs("--color=never", Source::kXffrc)));
}

TEST_F(LoaderTest, AutomaticDiscoveryDoesNotReadExplicitPaths) {
  DiscoveryOptions opts;
  opts.xffrc_files = {"/explicit"};
  std::vector<std::string> reads;
  const auto read = [&reads](std::string_view path) -> absl::StatusOr<std::string> {
    reads.emplace_back(path);
    if (path == "/explicit") {
      return std::string();
    }
    return absl::NotFoundError("missing");
  };
  ASSERT_OK_AND_ASSIGN(const auto automatic, DiscoverAutomatic(opts, read));
  EXPECT_THAT(reads, ElementsAre("/etc/xff.ini"));
  EXPECT_THAT(
      automatic.xffrc, ElementsAre(FieldsAre("/explicit", Field("globals", &ConfigFile::globals, IsEmpty()), false)));
  ASSERT_OK_AND_ASSIGN(const auto complete, DiscoverExplicit(automatic, read));
  EXPECT_THAT(reads, ElementsAre("/etc/xff.ini", "/explicit"));
  EXPECT_THAT(
      complete.sources,
      ElementsAre(SourceIs("/etc/xff.ini", Source::kSystem, false), SourceIs("/explicit", Source::kXffrc, true)));
}

TEST_F(LoaderTest, NoConfigStillInspectsAutomaticSourcesAndKeepsExplicitXffrc) {
  FakeFs fs;
  fs.files["/extra.rc"] = "";
  fs.files["/etc/xff.ini"] = "--color=auto\n--block-execution\n";
  fs.files["/home/u/.config/xff/config"] = "--sort\n";
  DiscoveryOptions opts;
  opts.paths.user = "/home/u/.config/xff/config";
  opts.xffrc_files = {"/extra.rc"};
  opts.no_system_config = true;
  opts.no_user_config = true;
  ASSERT_OK_AND_ASSIGN(const auto in, Discover(opts, [&fs](std::string_view path) { return fs.Read(path); }));
  EXPECT_THAT(
      in.sources,
      ElementsAre(
          SourceIs("/etc/xff.ini", Source::kSystem, true), SourceIs("/home/u/.config/xff/config", Source::kUser, true),
          SourceIs("/extra.rc", Source::kXffrc, true)));
  EXPECT_THAT(in.system.globals, ElementsAre("--color=auto", "--block-execution"));

  EXPECT_THAT(in.user.global_lines, SizeIs(1));
  EXPECT_THAT(in.xffrc, ElementsAre(FieldsAre("/extra.rc", Field("globals", &ConfigFile::globals, IsEmpty()), false)));
}

TEST_F(LoaderTest, MissingFilesYieldEmptyLayers) {
  FakeFs fs;  // nothing on disk
  DiscoveryOptions opts;
  opts.paths.user = "/home/u/.config/xff/config";
  ASSERT_OK_AND_ASSIGN(const auto in, Discover(opts, [&fs](std::string_view path) { return fs.Read(path); }));
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
  fs.files["/extra.rc"] = "--sort\n";           // present (explicit --xffrc)
  DiscoveryOptions opts;
  opts.paths.user = "/home/u/.config/xff/config";  // user path computed, but the file is absent
  opts.xffrc_files = {"/extra.rc"};                // explicit file, present
  ASSERT_OK_AND_ASSIGN(const auto in, Discover(opts, [&fs](std::string_view path) { return fs.Read(path); }));
  // Every consulted path is recorded in precedence order with its found/absent state. There is no
  // Only system, the user path, and the explicit --xffrc file are consulted.
  EXPECT_THAT(
      in.sources,
      ElementsAre(
          SourceIs("/etc/xff.ini", Source::kSystem, true), SourceIs("/home/u/.config/xff/config", Source::kUser, false),
          SourceIs("/extra.rc", Source::kXffrc, true)));
}

TEST_F(LoaderTest, MissingExplicitFileIsAnErrorEvenWhenAutomaticConfigIsSkipped) {
  const FakeFs fs;
  DiscoveryOptions opts;
  opts.no_config = true;
  opts.xffrc_files = {"/missing.rc"};
  EXPECT_THAT(
      Discover(opts, [&fs](std::string_view path) { return fs.Read(path); }),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("/missing.rc")));
}

TEST_F(LoaderTest, ReadFailuresCannotBypassTrustedConfigEvenWithSkipFlags) {
  constexpr auto kCodes = std::to_array({absl::StatusCode::kPermissionDenied, absl::StatusCode::kUnavailable});
  constexpr auto kPaths = std::to_array<std::string_view>({"/etc/xff.ini", "/user.ini"});
  for (const auto code : kCodes) {
    for (const std::string_view blocked_path : kPaths) {
      DiscoveryOptions opts;
      opts.paths.user = "/user.ini";
      opts.no_config = true;
      const auto read = [&](std::string_view path) -> absl::StatusOr<std::string> {
        if (path == blocked_path) {
          return absl::Status(code, "cannot read");
        }
        return std::string();
      };
      EXPECT_THAT(Discover(opts, read), StatusIs(code, HasSubstr(std::string(blocked_path))));
    }
  }
}

TEST_F(LoaderTest, AccountLookupRetriesOnlyBufferExhaustion) {
  std::vector<std::size_t> sizes;
  const auto lookup = [&](std::size_t size) -> absl::StatusOr<std::string> {
    sizes.push_back(size);
    if (size < 65'536) {
      return absl::OutOfRangeError("account record exceeds buffer");
    }
    return "/account";
  };
  EXPECT_THAT(
      ConfigPathsFromAccountLookup(lookup), IsOkAndHolds(Field(&ConfigPaths::user, "/account/.config/xff/config")));
  EXPECT_THAT(sizes, ElementsAre(16'384, 32'768, 65'536));
}

TEST_F(LoaderTest, AccountLookupStopsAtTheBufferLimit) {
  std::vector<std::size_t> sizes;
  const auto lookup = [&](std::size_t size) -> absl::StatusOr<std::string> {
    sizes.push_back(size);
    return absl::OutOfRangeError("record too large");
  };
  EXPECT_THAT(
      ConfigPathsFromAccountLookup(lookup), StatusIs(absl::StatusCode::kOutOfRange, HasSubstr("record too large")));
  EXPECT_THAT(sizes, ElementsAre(16'384, 32'768, 65'536, 131'072, 262'144, 524'288, 1'048'576));
}

TEST_F(LoaderTest, AccountLookupPropagatesFailuresWithoutRetryingOrFallingBack) {
  constexpr auto kCodes =
      std::to_array({absl::StatusCode::kNotFound, absl::StatusCode::kPermissionDenied, absl::StatusCode::kUnavailable});
  for (const auto code : kCodes) {
    int calls = 0;
    const auto lookup = [&](std::size_t) -> absl::StatusOr<std::string> {
      ++calls;
      return absl::Status(code, "account lookup failed");
    };
    EXPECT_THAT(ConfigPathsFromAccountLookup(lookup), StatusIs(code, HasSubstr("account lookup failed")));
    EXPECT_THAT(calls, Eq(1));
  }
}

TEST_F(LoaderTest, AccountLookupRejectsAnInvalidHomeWithoutFallback) {
  const auto lookup = [](std::size_t) -> absl::StatusOr<std::string> { return "relative"; };
  EXPECT_THAT(ConfigPathsFromAccountLookup(lookup), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(LoaderTest, DefaultPathsUseAnAbsoluteOsAccountHome) {
  ASSERT_OK_AND_ASSIGN(const auto paths, DefaultConfigPaths());
  EXPECT_THAT(paths.system, "/etc/xff.ini");
  EXPECT_THAT(paths.user, AllOf(StartsWith("/"), EndsWith("/.config/xff/config")));
}

}  // namespace
}  // namespace xff::config
