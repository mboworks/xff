// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/config/loader.h"
#include "xff/config/policy.h"

namespace xff::config {
namespace {
using ::mbo::testing::StatusIs;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::SizeIs;

struct DiscoveryFs final : vfs::FileSystem {
  void Add(std::string path, vfs::FileType type, std::string content = "", std::uint64_t ino = 0) {
    entries[path] = {.type = type, .ino = ino};
    contents[std::move(path)] = std::move(content);
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    probes.emplace_back(path);
    if (stat_errors.contains(std::string(path))) {
      return absl::PermissionDeniedError("stat denied");
    }
    const auto found = entries.find(std::string(path));
    if (found == entries.end()) {
      return absl::NotFoundError("absent");
    }
    return found->second;
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    if (path == unreadable_directory) {
      return absl::PermissionDeniedError("directory denied");
    }
    const std::string prefix = std::string(path) + "/";
    std::vector<vfs::Entry> result;
    // Deliberately reverse the input order; discovery must impose its own ordering.
    for (const auto& [path_name, metadata] : std::views::reverse(entries)) {
      if (path_name.starts_with(prefix) && !std::string_view(path_name).substr(prefix.size()).contains('/')) {
        result.push_back(
            {.path = path_name,
             .name = path_name.substr(prefix.size()),
             .type = unknown_types ? vfs::FileType::kUnknown : metadata.type});
      }
    }
    return result;
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    if (path == unreadable_file) {
      return absl::PermissionDeniedError("file denied");
    }
    return contents.at(std::string(path));
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read only"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::UnimplementedError("no links"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override {
    return absl::UnimplementedError("no host filesystem");
  }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  std::map<std::string, vfs::Metadata> entries;
  std::map<std::string, std::string> contents;
  std::set<std::string> stat_errors;
  mutable std::vector<std::string> probes;
  std::string unreadable_directory;
  std::string unreadable_file;
  bool unknown_types = false;
};

struct AutoloadTest : ::testing::Test {
  void SetUp() override {
    filesystem.Add("root", vfs::FileType::kDirectory, "", 1);
    filesystem.Add("root/.xffrc", vfs::FileType::kRegular, "[checks]\n--color=never");
    filesystem.Add("root/a", vfs::FileType::kDirectory, "", 2);
    filesystem.Add("root/a/.xffrc", vfs::FileType::kRegular, "[checks]\n--hidden");
    filesystem.Add("root/b", vfs::FileType::kDirectory, "", 3);
    filesystem.Add("root/b/.xffrc", vfs::FileType::kRegular, "[checks]\n--sort");
    filesystem.Add("root/link", vfs::FileType::kSymlink);
    filesystem.Add("root/link/.xffrc", vfs::FileType::kRegular, "--must-not-read");
    inputs.rc_mode = RcMode::kRoots;
  }

  static std::vector<std::string> Paths(const ConfigInputs& discovered) {
    std::vector<std::string> paths;
    paths.reserve(discovered.xffrc.size());
    for (const auto& file : discovered.xffrc) {
      paths.push_back(file.path);
    }
    return paths;
  }

  static std::vector<std::string> Flags(const ConfigInputs& discovered, const std::vector<std::string>& cli = {}) {
    std::vector<std::string> flags;
    for (const auto& flag : ResolveConfigInOrder(discovered, cli, "xff")) {
      flags.push_back(flag.flag);
    }
    return flags;
  }

  DiscoveryFs filesystem;
  ConfigInputs inputs;
};

TEST_F(AutoloadTest, DefaultOffAndDeniedAdmissionNeverProbeRoots) {
  EXPECT_THAT(ResolveRcMode({}, {}, "xff"), Eq(RcMode::kOff));
  ASSERT_OK_AND_ASSIGN(const auto off, DiscoverRc({}, {"missing"}, filesystem));
  EXPECT_THAT(off.xffrc, IsEmpty());
  EXPECT_THAT(filesystem.probes, IsEmpty());
  inputs.system = ParseIni("--no-allow-xffrc");
  EXPECT_THAT(DiscoverRc(inputs, {"root"}, filesystem), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(filesystem.probes, IsEmpty());
}

TEST_F(AutoloadTest, LaterSelectedDenialStopsDiscoveryBeforeProbingAnyRoot) {
  inputs.user = ParseIni("[deny]\n--no-allow-xffrc\n[allow]\n--allow-xffrc");
  inputs.configs = {"allow", "deny"};
  EXPECT_THAT(DiscoverRc(inputs, {"missing"}, filesystem), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(filesystem.probes, IsEmpty());
  inputs.configs = {"deny", "allow"};
  ASSERT_OK_AND_ASSIGN(const auto found, DiscoverRc(inputs, {"root"}, filesystem));
  EXPECT_THAT(Paths(found), ElementsAre("root/.xffrc"));
}

TEST_F(AutoloadTest, ModeUsesTrustedSelectionsAndCliOrderButNotExplicitFilesOrArguments) {
  inputs.system = ParseIni("--rc+\n[root-only]\n--rc");
  EXPECT_THAT(ResolveRcMode(inputs, {"--config=root-only"}, "xff"), Eq(RcMode::kRoots));
  EXPECT_THAT(ResolveRcMode(inputs, {"--rc", "--rc-"}, "xff"), Eq(RcMode::kOff));
  EXPECT_THAT(ResolveRcMode(inputs, {"--rc-", "--rc+"}, "xff"), Eq(RcMode::kRecursive));
  inputs.no_config = true;
  EXPECT_THAT(ResolveRcMode(inputs, {"--rc+"}, "xff"), Eq(RcMode::kOff));
  inputs = {};
  inputs.user = ParseIni("-exec echo --rc+ \\;");
  inputs.xffrc = {{.path = "task", .config = ParseIni("--rc+")}};
  EXPECT_THAT(ResolveRcMode(inputs, {"--xffrc=task"}, "xff"), Eq(RcMode::kOff));
  EXPECT_THAT(RcModeName(RcMode::kOff), Eq("off"));
  EXPECT_THAT(RcModeName(RcMode::kRoots), Eq("roots"));
  EXPECT_THAT(RcModeName(RcMode::kRecursive), Eq("recursive"));
}

TEST_F(AutoloadTest, RootModeLoadsOnlyDirectoryRootsAndProfilesWaitForSelection) {
  ASSERT_OK_AND_ASSIGN(const auto found, DiscoverRc(inputs, {"root", "root/.xffrc", "root/link"}, filesystem));
  EXPECT_THAT(Paths(found), ElementsAre("root/.xffrc"));
  EXPECT_THAT(Flags(found), IsEmpty());
  EXPECT_THAT(
      Flags(found, {"--config=checks", "--color=always"}),
      ElementsAre("--config=checks", "--color=never", "--color=always"));
  EXPECT_THAT(found.xffrc.front().automatic, IsTrue());
}

TEST_F(AutoloadTest, RecursiveOrderIsStableAndOverlappingRootsAreNotLoadedTwice) {
  inputs.rc_mode = RcMode::kRecursive;
  filesystem.Add("alias", vfs::FileType::kDirectory, "", 1);
  ASSERT_OK_AND_ASSIGN(const auto found, DiscoverRc(inputs, {"root", "root/a", "alias"}, filesystem));
  EXPECT_THAT(Paths(found), ElementsAre("root/.xffrc", "root/a/.xffrc", "root/b/.xffrc"));
  EXPECT_THAT(Flags(found, {"--config=checks"}), ElementsAre("--config=checks", "--color=never", "--hidden", "--sort"));
  EXPECT_THAT(filesystem.probes, Not(Contains("root/link/.xffrc")));
}

TEST_F(AutoloadTest, UnknownEntryTypesAndMissingConfigFilesWorkWithoutFileIdentities) {
  inputs.rc_mode = RcMode::kRecursive;
  filesystem.unknown_types = true;
  filesystem.Add(".", vfs::FileType::kDirectory);
  filesystem.Add("./child", vfs::FileType::kDirectory);
  filesystem.Add("./child/.xffrc", vfs::FileType::kRegular, "[checks]\n--sort");
  ASSERT_OK_AND_ASSIGN(const auto found, DiscoverRc(inputs, {}, filesystem));
  EXPECT_THAT(Paths(found), ElementsAre("./child/.xffrc"));
  EXPECT_THAT(found.sources, Contains(Field(&ConfigSource::found, false)));
  ASSERT_OK_AND_ASSIGN(const auto repeated, DiscoverRc(inputs, {".", "."}, filesystem));
  EXPECT_THAT(Paths(repeated), ElementsAre("./child/.xffrc"));
}

TEST_F(AutoloadTest, ForbiddenGlobalsFailInsteadOfDroppingOrActivatingContent) {
  static constexpr auto kContents = std::to_array<std::string_view>({
      "--hidden",
      "--config=checks",
      "-delete",
      "--block-execution",
      "'unfinished",
  });
  for (const std::string_view content : kContents) {
    filesystem.contents["root/.xffrc"] = content;
    EXPECT_THAT(
        DiscoverRc(inputs, {"root"}, filesystem),
        StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("--allow-rc-globals")));
  }
}

TEST_F(AutoloadTest, GlobalGrantCannotOverrideSystemDenialAndExplicitGlobalsRemainIndependent) {
  inputs.user = ParseIni("--allow-rc-globals");
  filesystem.contents["root/.xffrc"] = "--hidden";
  ASSERT_OK_AND_ASSIGN(const auto granted, DiscoverRc(inputs, {"root"}, filesystem));
  EXPECT_THAT(Flags(granted), ElementsAre("--hidden"));
  inputs.system = ParseIni("--no-allow-rc-globals");
  EXPECT_THAT(DiscoverRc(inputs, {"root"}, filesystem), StatusIs(absl::StatusCode::kPermissionDenied));
  inputs.rc_mode = RcMode::kOff;
  inputs.xffrc = {{.path = "task"}};
  ASSERT_OK_AND_ASSIGN(auto explicit_only, DiscoverRc(inputs, {"root"}, filesystem));
  explicit_only = DiscoverExplicit(
      std::move(explicit_only), [](std::string_view) { return std::optional<std::string>("--hidden"); });
  EXPECT_THAT(Flags(explicit_only, {"--xffrc=task"}), ElementsAre("--xffrc=task", "--hidden"));
}

TEST_F(AutoloadTest, ExplicitFileRetainsItsCliPositionAfterAutoloadedDefaults) {
  inputs.user = ParseIni("--allow-rc-globals\n--color=auto");
  inputs.xffrc = {{.path = "task"}};
  filesystem.contents["root/.xffrc"] = "--color=never";
  ASSERT_OK_AND_ASSIGN(auto found, DiscoverRc(inputs, {"root"}, filesystem));
  found =
      DiscoverExplicit(std::move(found), [](std::string_view) { return std::optional<std::string>("--color=always"); });
  EXPECT_THAT(
      Flags(found, {"--color=auto", "--xffrc=task"}),
      ElementsAre("--color=auto", "--color=never", "--color=auto", "--xffrc=task", "--color=always"));
}

TEST_F(AutoloadTest, DiscoveryErrorsAbortBeforeReturningPartialConfiguration) {
  EXPECT_THAT(DiscoverRc(inputs, {"absent"}, filesystem), StatusIs(absl::StatusCode::kNotFound));
  filesystem.stat_errors.insert("root/.xffrc");
  EXPECT_THAT(DiscoverRc(inputs, {"root"}, filesystem), StatusIs(absl::StatusCode::kPermissionDenied));
  filesystem.stat_errors.clear();
  filesystem.unreadable_file = "root/.xffrc";
  EXPECT_THAT(DiscoverRc(inputs, {"root"}, filesystem), StatusIs(absl::StatusCode::kPermissionDenied));
  filesystem.unreadable_file.clear();
  filesystem.unreadable_directory = "root";
  inputs.rc_mode = RcMode::kRecursive;
  EXPECT_THAT(DiscoverRc(inputs, {"root"}, filesystem), StatusIs(absl::StatusCode::kPermissionDenied));
  filesystem.Add("root/.xffrc", vfs::FileType::kSymlink);
  EXPECT_THAT(DiscoverRc(inputs, {"root"}, filesystem), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AutoloadTest, GlobalsPermissionDoesNotArmActionsOrTrustedProfilesSelectedByAnRcFile) {
  inputs.user = ParseIni("--allow-rc-globals\n[dangerous]\n-delete");
  filesystem.contents["root/.xffrc"] = "--config=dangerous\n-exec echo {} \\;";
  ASSERT_OK_AND_ASSIGN(const auto found, DiscoverRc(inputs, {"root"}, filesystem));
  EXPECT_THAT(ArmedFromTrustedTier(found, {}, "--allow-exec"), IsFalse());
  const auto gated = GateConfig(found, false);
  EXPECT_THAT(gated.drops, SizeIs(2));
  EXPECT_THAT(Flags(gated.config), ElementsAre("--config=dangerous"));
}
}  // namespace
}  // namespace xff::config
