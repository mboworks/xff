// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <atomic>
#include <cstddef>
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

struct CasePolicy {
  std::optional<bool> uniform;
  bool fail_leaf_metadata = false;
  bool fail_case_query = false;
};

class CaseFs final : public vfs::FileSystem {
 public:
  explicit CaseFs(CasePolicy policy) : policy_(policy) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    if (path != "/fixture") {
      return absl::NotFoundError("unknown fixture directory");
    }
    return std::vector<vfs::Entry>{{.path = "/fixture/f.txt", .name = "f.txt", .type = vfs::FileType::kRegular}};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    if (path == "/fixture") {
      return vfs::Metadata{.type = vfs::FileType::kDirectory, .ino = 1, .dev = 1};
    }
    if (path != "/fixture/f.txt") {
      return absl::NotFoundError("unknown fixture entry");
    }
    leaf_stats_.fetch_add(1, std::memory_order_relaxed);
    if (policy_.fail_leaf_metadata) {
      return absl::PermissionDeniedError("leaf metadata denied");
    }
    return vfs::Metadata{.type = vfs::FileType::kRegular, .mode = 0644, .ino = 2, .dev = 1};
  }

  std::optional<bool> UniformCaseSensitivity() const override {
    uniform_queries_.fetch_add(1, std::memory_order_relaxed);
    return policy_.uniform;
  }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override {
    case_queries_.fetch_add(1, std::memory_order_relaxed);
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

  std::size_t UniformQueries() const { return uniform_queries_.load(std::memory_order_relaxed); }

 private:
  const CasePolicy policy_;
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
};

struct UniformCaseTest : ::testing::Test {
  static absl::StatusOr<Observation> Observe(
      CasePolicy policy,
      const std::vector<std::string>& extra = {},
      std::string_view pattern = "F.TXT") {
    const CaseFs filesystem(policy);
    std::vector<std::string> arguments{
        "--color=never", "--no-ignore", "--jobs=1", "/fixture", "-type", "f", "-name", std::string(pattern),
    };
    arguments.append_range(extra);
    MBO_ASSIGN_OR_RETURN(auto command, parser::Parse(arguments));
    parser::BindMatchers(
        command, parser::GrammarFromGlobals(command.globals),
        parser::ResolveCaseMode(command.globals, registry::Style::kXff));
    Observation result;
    result.result = RunFind(
        command, filesystem, [&](std::string_view text) { result.output.append(text); },
        [&](std::string_view, absl::Status status) { result.diagnostics.push_back(std::move(status)); },
        registry::Style::kXff);
    result.leaf_stats = filesystem.LeafStats();
    result.case_queries = filesystem.CaseQueries();
    result.uniform_queries = filesystem.UniformQueries();
    return result;
  }
};

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

}  // namespace
}  // namespace xff::engine
