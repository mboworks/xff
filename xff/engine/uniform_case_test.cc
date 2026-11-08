// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;

enum class ParentObservation { kDirectory, kDenied, kNotDirectory };

struct CasePolicy {
  std::optional<bool> uniform;
  bool fail_leaf_metadata = false;
  bool fail_case_query = false;
  bool mixed_devices = false;
  bool symlink_leaf = false;
  std::optional<bool> symlink_target_case;
  std::uint64_t leaf_device = 1;
  bool bare_link_root = false;
  ParentObservation parent_observation = ParentObservation::kDirectory;
};

struct DeviceEntry {
  std::string_view name;
  std::uint64_t device;
};

constexpr auto kDeviceEntries = std::to_array<DeviceEntry>({
    {.name = "Foo0.TXT", .device = 1},
    {.name = "Foo1.TXT", .device = 2},
    {.name = "Foo2.TXT", .device = 3},
    {.name = "Foo3.txt", .device = 2},
    {.name = "Foo4.txt", .device = 3},
    {.name = "Foo5.TXT", .device = 1},
});

class CaseFs final : public vfs::FileSystem {
 public:
  explicit CaseFs(CasePolicy policy) : policy_(policy) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    if (path != "/fixture") {
      return absl::NotFoundError("unknown fixture directory");
    }
    if (policy_.mixed_devices) {
      std::vector<vfs::Entry> entries;
      entries.reserve(kDeviceEntries.size());
      for (const auto& entry : kDeviceEntries) {
        entries.push_back({
            .path = "/fixture/" + std::string(entry.name),
            .name = std::string(entry.name),
            .type = vfs::FileType::kRegular,
        });
      }
      return entries;
    }
    return std::vector<vfs::Entry>{
        {
            .path = "/fixture/f.txt",
            .name = "f.txt",
            .type = policy_.symlink_leaf ? vfs::FileType::kSymlink : vfs::FileType::kRegular,
        },
    };
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    if (path == "/fixture" || path == ".") {
      // Initial root metadata is valid; only the later optional parent observation can fail.
      if (leaf_stats_.load(std::memory_order_relaxed) != 0) {
        if (policy_.parent_observation == ParentObservation::kDenied) {
          return absl::PermissionDeniedError("parent metadata denied after observing link");
        }
        if (policy_.parent_observation == ParentObservation::kNotDirectory) {
          return vfs::Metadata{.type = vfs::FileType::kRegular, .ino = 1, .dev = 1};
        }
      }
      return vfs::Metadata{.type = vfs::FileType::kDirectory, .ino = 1, .dev = 1};
    }
    if (policy_.mixed_devices) {
      const auto index = static_cast<std::size_t>(
          std::ranges::find(kDeviceEntries, path.substr(path.find_last_of('/') + 1), &DeviceEntry::name)
          - kDeviceEntries.begin());
      if (index == kDeviceEntries.size()) {
        return absl::NotFoundError("unknown device fixture entry");
      }
      return vfs::Metadata{
          .type = vfs::FileType::kRegular,
          .size = 7,
          .mode = 0644,
          .ino = index + 2,
          .dev = kDeviceEntries.at(index).device,
      };
    }
    if (path != (policy_.bare_link_root ? "f.txt" : "/fixture/f.txt")) {
      return absl::NotFoundError("unknown fixture entry");
    }
    leaf_stats_.fetch_add(1, std::memory_order_relaxed);
    if (policy_.fail_leaf_metadata) {
      return absl::PermissionDeniedError("leaf metadata denied");
    }
    return vfs::Metadata{
        .type = policy_.symlink_leaf ? vfs::FileType::kSymlink : vfs::FileType::kRegular,
        .mode = 0644,
        .ino = 2,
        .dev = policy_.leaf_device,
    };
  }

  std::optional<bool> UniformCaseSensitivity() const override {
    uniform_queries_.fetch_add(1, std::memory_order_relaxed);
    return policy_.uniform;
  }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view path) const override {
    case_queries_.fetch_add(1, std::memory_order_relaxed);
    query_paths_.emplace_back(path);
    if (policy_.symlink_leaf && path != "/fixture" && path != ".") {
      // Darwin pathconf follows its final symlink, so a dangling target cannot answer.
      if (policy_.symlink_target_case) {
        return *policy_.symlink_target_case;
      }
      return absl::NotFoundError("dangling case-query target");
    }
    if (policy_.mixed_devices) {
      if (path == "/fixture") {
        return false;
      }
      const auto index = static_cast<std::size_t>(
          std::ranges::find(kDeviceEntries, path.substr(path.find_last_of('/') + 1), &DeviceEntry::name)
          - kDeviceEntries.begin());
      if (index == kDeviceEntries.size()) {
        return absl::NotFoundError("unknown case fixture entry");
      }
      if (kDeviceEntries.at(index).device == 3) {
        return absl::PermissionDeniedError("device case query denied");
      }
      return kDeviceEntries.at(index).device == 2;
    }
    if (policy_.fail_case_query) {
      return absl::PermissionDeniedError("case query denied");
    }
    return policy_.uniform.value_or(false);
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only fixture"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("not a link");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return absl::NotFoundError("no content"); }

  std::size_t LeafStats() const { return leaf_stats_.load(std::memory_order_relaxed); }

  std::size_t CaseQueries() const { return case_queries_.load(std::memory_order_relaxed); }

  const std::vector<std::string>& QueryPaths() const { return query_paths_; }

  std::size_t UniformQueries() const { return uniform_queries_.load(std::memory_order_relaxed); }

 private:
  const CasePolicy policy_;
  mutable std::vector<std::string> query_paths_;
  mutable std::atomic<std::size_t> leaf_stats_{0};
  mutable std::atomic<std::size_t> case_queries_{0};
  mutable std::atomic<std::size_t> uniform_queries_{0};
};

struct Observation {
  RunResult result;
  std::string output;
  std::vector<absl::Status> diagnostics;
  std::size_t leaf_stats = 0;
  std::size_t case_queries = 0;
  std::size_t uniform_queries = 0;
  std::vector<std::string> case_paths;
};

struct UniformCaseTest : ::testing::Test {
  static absl::StatusOr<Observation> Observe(
      CasePolicy policy,
      const std::vector<std::string>& extra = {},
      std::string_view pattern = "F.TXT",
      registry::Style style = registry::Style::kXff,
      bool regular_files_only = true) {
    const CaseFs filesystem(policy);
    std::vector<std::string> arguments{
        "--color=never",
        "--no-ignore",
        "--jobs=1",
        policy.bare_link_root ? "f.txt" : "/fixture",
    };
    if (regular_files_only) {
      arguments.append_range(std::vector<std::string>{"-type", "f"});
    }
    arguments.append_range(std::vector<std::string>{"-name", std::string(pattern)});
    arguments.append_range(extra);
    MBO_ASSIGN_OR_RETURN(auto command, parser::Parse(arguments));
    parser::BindMatchers(
        command, parser::GrammarFromGlobals(command.globals), parser::ResolveCaseMode(command.globals, style));
    Observation result;
    result.result = RunFind(
        command, filesystem, [&](std::string_view text) { result.output.append(text); },
        [&](std::string_view, absl::Status status) { result.diagnostics.push_back(std::move(status)); }, style);
    result.leaf_stats = filesystem.LeafStats();
    result.case_queries = filesystem.CaseQueries();
    result.uniform_queries = filesystem.UniformQueries();
    result.case_paths = filesystem.QueryPaths();
    return result;
  }
};

TEST_F(UniformCaseTest, MixedDevicesCacheAnswersAndErrorsInOrdinaryAndDeferredPasses) {
  for (const bool deferred : {false, true}) {
    std::vector<std::string> extra;
    if (deferred) {
      extra = {R"(--shard-pattern=(?P<stem>Foo)(?P<index>\d)\.[Tt][Xx][Tt])", "-shard-status", "incomplete"};
    }
    MBO_ASSERT_OK_AND_ASSIGN(const auto result, Observe({.mixed_devices = true}, extra, "*.txt"));
    EXPECT_THAT(
        result.output, EqualsText("/fixture/Foo0.TXT\n/fixture/Foo3.txt\n/fixture/Foo4.txt\n/fixture/Foo5.TXT\n"));
    EXPECT_THAT(result.diagnostics, IsEmpty());
    EXPECT_THAT(result.result.errors, Eq(0));
    // The directory fails -type f before native-case matching; each device is probed at its
    // first reached leaf. Repeated sensitive, insensitive, and failed answers stay cached.
    EXPECT_THAT(result.case_paths, ElementsAre("/fixture/Foo0.TXT", "/fixture/Foo1.TXT", "/fixture/Foo2.TXT"));
    EXPECT_THAT(result.case_queries, Eq(3));
  }
}

TEST_F(UniformCaseTest, MixedDeviceExactAndFindModesDoNotQueryCasePolicy) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto exact, Observe({.mixed_devices = true}, {"--exact"}, "*.txt"));
  MBO_ASSERT_OK_AND_ASSIGN(const auto find, Observe({.mixed_devices = true}, {}, "*.txt", registry::Style::kFind));
  EXPECT_THAT(exact.output, EqualsText("/fixture/Foo3.txt\n/fixture/Foo4.txt\n"));
  EXPECT_THAT(find.output, EqualsText(exact.output));
  EXPECT_THAT(exact.case_paths, IsEmpty());
  EXPECT_THAT(find.case_paths, IsEmpty());
}

TEST_F(UniformCaseTest, KnownSensitiveAndInsensitiveAnswersAvoidLeafMetadata) {
  struct Case {
    std::string_view name;
    std::optional<bool> uniform;
    bool matches;
    std::size_t leaf_stats;
    std::size_t queries;
  };

  static constexpr auto kCases = std::to_array<Case>({
      {.name = "sensitive", .uniform = true, .matches = false, .leaf_stats = 0, .queries = 0},
      {.name = "insensitive", .uniform = false, .matches = true, .leaf_stats = 0, .queries = 0},
      {.name = "per-device", .uniform = std::nullopt, .matches = true, .leaf_stats = 1, .queries = 1},
  });
  for (const auto& scenario : kCases) {
    SCOPED_TRACE(scenario.name);
    MBO_ASSERT_OK_AND_ASSIGN(const auto actual, Observe({.uniform = scenario.uniform}));
    EXPECT_THAT(actual.result.errors, Eq(0));
    EXPECT_THAT(actual.result.any_match, Eq(scenario.matches));
    EXPECT_THAT(actual.diagnostics, IsEmpty());
    EXPECT_THAT(actual.output, EqualsText(scenario.matches ? "/fixture/f.txt\n" : ""));
    EXPECT_THAT(actual.leaf_stats, Eq(scenario.leaf_stats));
    EXPECT_THAT(actual.case_queries, Eq(scenario.queries));
    EXPECT_THAT(actual.uniform_queries, Eq(1));
  }
}

TEST_F(UniformCaseTest, ExplicitExactCaseDoesNotRequestTheCapability) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto actual, Observe({.uniform = false}, {"--exact"}));
  EXPECT_THAT(actual.result.errors, Eq(0));
  EXPECT_THAT(actual.output, IsEmpty());
  EXPECT_THAT(actual.leaf_stats, Eq(0));
  EXPECT_THAT(actual.case_queries, Eq(0));
  EXPECT_THAT(actual.uniform_queries, Eq(0));
}

TEST_F(UniformCaseTest, ConditionalAndEagerMetadataConsumersStillLoadMetadata) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto conditional, Observe({.uniform = false}, {"-size", "0c"}));
  EXPECT_THAT(conditional.result.errors, Eq(0));
  EXPECT_THAT(conditional.output, EqualsText("/fixture/f.txt\n"));
  EXPECT_THAT(conditional.leaf_stats, Eq(1));
  MBO_ASSERT_OK_AND_ASSIGN(const auto eager, Observe({.uniform = false}, {"--summary=ext"}));
  EXPECT_THAT(eager.result.errors, Eq(0));
  EXPECT_THAT(eager.leaf_stats, Eq(1));
}

TEST_F(UniformCaseTest, DeferredReplayUsesTheSameKnownAndUnknownCasePolicies) {
  static constexpr auto kPolicies = std::to_array<CasePolicy>({
      {.uniform = true},
      {.uniform = false},
      {.uniform = std::nullopt},
  });
  for (const auto policy : kPolicies) {
    MBO_ASSERT_OK_AND_ASSIGN(const auto actual, Observe(policy, {"-fuzzy", "f", "-top", "1", "-print"}, "f.txt"));
    EXPECT_THAT(actual.result.errors, Eq(0));
    EXPECT_THAT(actual.output, EqualsText("/fixture/f.txt\n"));
    EXPECT_THAT(actual.diagnostics, IsEmpty());
    EXPECT_THAT(actual.leaf_stats, Eq(1));
    EXPECT_THAT(actual.case_queries, Eq(policy.uniform ? 0 : 1));
    EXPECT_THAT(actual.uniform_queries, Eq(1));
  }
}

TEST_F(UniformCaseTest, RequiredMetadataFailureStillReportsAnError) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto actual, Observe({.uniform = false, .fail_leaf_metadata = true}, {"-size", "0c"}));
  EXPECT_THAT(actual.result.errors, Eq(1));
  EXPECT_THAT(actual.output, IsEmpty());
  EXPECT_THAT(actual.leaf_stats, Eq(1));
  EXPECT_THAT(actual.diagnostics, ElementsAre(StatusIs(absl::StatusCode::kPermissionDenied)));
}

TEST_F(UniformCaseTest, UnknownCaseQueryFailureKeepsTheConservativeExactFallback) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto actual, Observe({.uniform = std::nullopt, .fail_case_query = true}));
  EXPECT_THAT(actual.result.errors, Eq(0));
  EXPECT_THAT(actual.output, IsEmpty());
  EXPECT_THAT(actual.diagnostics, IsEmpty());
  EXPECT_THAT(actual.leaf_stats, Eq(1));
  EXPECT_THAT(actual.case_queries, Eq(1));
}

TEST_F(UniformCaseTest, SymlinkNamesKeepTheVerifiedContainingDeviceCasePolicy) {
  struct Case {
    std::string_view name;
    std::optional<bool> target_case;
  };

  constexpr auto kCases = std::to_array<Case>({
      {.name = "dangling", .target_case = std::nullopt},
      {.name = "folding-target", .target_case = false},
      {.name = "sensitive-target-volume", .target_case = true},
  });
  for (const auto& scenario : kCases) {
    SCOPED_TRACE(scenario.name);
    for (const bool deferred : {false, true}) {
      SCOPED_TRACE(deferred);
      std::vector<std::string> extra{"--archive=none"};
      if (deferred) {
        extra.append_range(std::vector<std::string>{"-fuzzy", "f", "-top", "1", "-print"});
      }
      MBO_ASSERT_OK_AND_ASSIGN(
          const auto actual, Observe(
                                 {.symlink_leaf = true, .symlink_target_case = scenario.target_case}, extra, "F.TXT",
                                 registry::Style::kXff, false));
      EXPECT_THAT(actual.output, EqualsText("/fixture/f.txt\n"));
      EXPECT_THAT(actual.result.errors, Eq(0));
      EXPECT_THAT(actual.diagnostics, IsEmpty());
      EXPECT_THAT(actual.case_paths, ElementsAre("/fixture"));
    }
  }
}

TEST_F(UniformCaseTest, SymlinkCaseNeverAssumesItsParentHasTheSameDevice) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto actual, Observe(
                             {.symlink_leaf = true, .symlink_target_case = true, .leaf_device = 2}, {"--archive=none"},
                             "F.TXT", registry::Style::kXff, false));
  EXPECT_THAT(actual.output, IsEmpty());
  EXPECT_THAT(actual.result.errors, Eq(0));
  EXPECT_THAT(actual.diagnostics, IsEmpty());
  EXPECT_THAT(actual.case_paths, ElementsAre("/fixture/f.txt"));
}

TEST_F(UniformCaseTest, UnavailableVerifiedSymlinkPolicyKeepsTheExactFallback) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto actual,
      Observe(
          {.fail_case_query = true, .symlink_leaf = true}, {"--archive=none"}, "F.TXT", registry::Style::kXff, false));
  EXPECT_THAT(actual.output, IsEmpty());
  EXPECT_THAT(actual.result.errors, Eq(0));
  EXPECT_THAT(actual.diagnostics, IsEmpty());
  EXPECT_THAT(actual.case_paths, ElementsAre("/fixture"));
}

TEST_F(UniformCaseTest, BareRelativeSymlinkRootUsesTheVerifiedCurrentDirectoryPolicy) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto actual,
      Observe(
          {.symlink_leaf = true, .bare_link_root = true}, {"--archive=none"}, "F.TXT", registry::Style::kXff, false));
  EXPECT_THAT(actual.output, EqualsText("f.txt\n"));
  EXPECT_THAT(actual.result.errors, Eq(0));
  EXPECT_THAT(actual.diagnostics, IsEmpty());
  EXPECT_THAT(actual.case_paths, ElementsAre("."));
}

TEST_F(UniformCaseTest, UnavailableOrNondirectoryParentKeepsEntryPolicyWithoutAnError) {
  constexpr auto kCases = std::to_array<ParentObservation>({
      ParentObservation::kDenied,
      ParentObservation::kNotDirectory,
  });
  for (const auto observation : kCases) {
    SCOPED_TRACE(static_cast<int>(observation));
    MBO_ASSERT_OK_AND_ASSIGN(
        const auto actual, Observe(
                               {.symlink_leaf = true, .symlink_target_case = false, .parent_observation = observation},
                               {"--archive=none"}, "F.TXT", registry::Style::kXff, false));
    EXPECT_THAT(actual.output, EqualsText("/fixture/f.txt\n"));
    EXPECT_THAT(actual.result.errors, Eq(0));
    EXPECT_THAT(actual.diagnostics, IsEmpty());
    EXPECT_THAT(actual.case_paths, ElementsAre("/fixture/f.txt"));
  }
}

}  // namespace
}  // namespace xff::engine
