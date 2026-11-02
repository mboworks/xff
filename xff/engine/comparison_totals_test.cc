// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/status.h"
#include "nlohmann/json.hpp"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Not;
using Json = nlohmann::json;

struct ComparisonTotalsFs final : vfs::FileSystem {
  const std::map<std::string, std::string> files{
      {"left/a.txt", "x\n"},       {"left/b.cc", "before\n"}, {"left/l.txt", "left\n"},  {"left/sub/x.h", "two\n"},
      {"left/change/item", "l\n"}, {"right/a.txt", "x\n"},    {"right/b.cc", "after\n"}, {"right/r.md", "right\n"},
      {"right/sub/x.h", "two\n"},  {"right/change", "r\n"},
  };
  bool empty_left = false;
  mutable std::atomic<std::size_t> reads = 0;
  // Comparison walks read concurrently; this counter measures calls only.
  // The fixture data is immutable and never synchronized through this counter.
  mutable std::atomic<std::size_t> observations = 0;

  bool Directory(std::string_view path) const {
    const std::string prefix = std::string(path) + "/";
    return std::ranges::any_of(files, [&](const auto& file) { return file.first.starts_with(prefix); });
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    ++observations;
    if (path == "left" && empty_left) {
      return std::vector<vfs::Entry>{};
    }
    if (!Directory(path)) {
      return absl::NotFoundError("unknown fixture directory");
    }
    const std::string prefix = std::string(path) + "/";
    std::map<std::string, vfs::Entry> entries;
    for (const auto& [name, content] : files) {
      if (!name.starts_with(prefix)) {
        continue;
      }
      const auto relative = std::string_view(name).substr(prefix.size());
      const auto slash = relative.find('/');
      const std::string child(relative.substr(0, slash));
      entries.try_emplace(
          child, vfs::Entry{
                     .path = prefix + child,
                     .name = child,
                     .type = slash == std::string_view::npos ? vfs::FileType::kRegular : vfs::FileType::kDirectory,
                 });
    }
    std::vector<vfs::Entry> result;
    result.reserve(entries.size());
    for (auto& [name, entry] : entries) {
      result.push_back(std::move(entry));
    }
    return result;
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    ++observations;
    if (const auto file = files.find(std::string(path)); file != files.end()) {
      return vfs::Metadata{
          .type = vfs::FileType::kRegular,
          .size = file->second.size(),
          .mode = 0644,
          .ino = std::hash<std::string_view>{}(path),
      };
    }
    if (Directory(path)) {
      // Distinct ancestors must have distinct inode identities for loop detection.
      return vfs::Metadata{.type = vfs::FileType::kDirectory, .ino = std::hash<std::string_view>{}(path)};
    }
    return absl::NotFoundError("unknown fixture entry");
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    ++observations;
    ++reads;
    if (const auto file = files.find(std::string(path)); file != files.end()) {
      return file->second;
    }
    return absl::NotFoundError("unknown fixture contents");
  }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "Comparison summaries must not mutate their input";
    return absl::PermissionDeniedError("read-only fixture");
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::NotFoundError("not a link"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }
};

struct TotalsOutput {
  RunResult result;
  std::vector<Json> rows;
  std::vector<absl::Status> errors;
  std::size_t reads = 0;
};

struct ComparisonTotalsTest : ::testing::Test {
  absl::StatusOr<TotalsOutput> Run(
      std::vector<std::string> options,
      bool action = false,
      const std::vector<std::string>& expression = {}) {
    options.insert(options.begin(), {"--compare=summary", "--format=jsonl"});
    options.insert(options.end(), {"left", "right"});
    options.insert(options.end(), expression.begin(), expression.end());
    if (action) {
      options.push_back("-print");
    }
    MBO_ASSIGN_OR_RETURN(const auto command, parser::Parse(options));
    TotalsOutput output;
    filesystem.reads = 0;
    const auto emit = [&](std::string_view text) {
      const auto row = Json::parse(text, nullptr, false);
      if (!row.is_discarded() && row.contains("left-total")) {
        output.rows.push_back(row);
      }
    };
    output.result = RunFind(command, filesystem, emit, [&](std::string_view, absl::Status error) {
      output.errors.push_back(std::move(error));
    });
    output.reads = filesystem.reads.load();
    return output;
  }

  ComparisonTotalsFs filesystem;
};

TEST_F(ComparisonTotalsTest, OrdinaryTotalsMatchTheActionFallbackAcrossScopesAndSinks) {
  constexpr auto kScopes = std::to_array<std::string_view>({
      "compare",
      "left-total,right-total",
      "root,left-total,right-total,all",
      "left-total,left-only",
  });
  constexpr auto kSummaries = std::to_array<std::string_view>({
      "overall",
      "type",
      "ext",
  });
  for (const auto scope : kScopes) {
    for (const auto summary : kSummaries) {
      SCOPED_TRACE(scope);
      SCOPED_TRACE(summary);
      const std::vector<std::string> arguments{
          "--summary-scope=" + std::string(scope),
          "--summary=" + std::string(summary),
      };
      ASSERT_OK_AND_ASSIGN(const auto ordinary, Run(arguments));
      ASSERT_OK_AND_ASSIGN(const auto action, Run(arguments, true));
      EXPECT_THAT(ordinary.result.errors, Eq(0));
      EXPECT_THAT(action.result.errors, Eq(0));
      EXPECT_THAT(ordinary.errors, IsEmpty());
      EXPECT_THAT(action.errors, IsEmpty());
      EXPECT_THAT(ordinary.rows, Not(IsEmpty()));
      EXPECT_THAT(ordinary.rows, Eq(action.rows));
      EXPECT_THAT(ordinary.reads, Eq(action.reads));
      EXPECT_THAT(ordinary.result.any_match, IsTrue());
    }
  }
}

TEST_F(ComparisonTotalsTest, IndependentFiltersAndMultipleSummarySinksMatchTheFallback) {
  const auto expressions = std::to_array<std::vector<std::string>>({
      {"-type", "f"},
      {"-name", "*.txt"},
      {"-name", "never-matches"},
      {"(", "-type", "d", "+", "-name", "*.h", ")"},
  });
  const std::vector<std::string> arguments{
      "--summary=overall",
      "--summary=type",
      "--summary=ext",
      "--summary-scope=compare",
  };
  for (const auto& expression : expressions) {
    ASSERT_OK_AND_ASSIGN(const auto ordinary, Run(arguments, false, expression));
    ASSERT_OK_AND_ASSIGN(const auto action, Run(arguments, true, expression));
    EXPECT_THAT(ordinary.result.errors, Eq(0));
    EXPECT_THAT(action.result.errors, Eq(0));
    EXPECT_THAT(ordinary.errors, IsEmpty());
    EXPECT_THAT(action.errors, IsEmpty());
    EXPECT_THAT(ordinary.rows, Not(IsEmpty()));
    EXPECT_THAT(ordinary.rows, Eq(action.rows));
    EXPECT_THAT(ordinary.reads, Eq(action.reads));
    EXPECT_THAT(ordinary.result.any_match, Eq(action.result.any_match));
  }
}

TEST_F(ComparisonTotalsTest, DirectoryRootsAndTypeTransitionsContributeOnceToEachTotal) {
  ASSERT_OK_AND_ASSIGN(const auto result, Run({"--summary=overall"}));
  EXPECT_THAT(result.result.errors, Eq(0));
  EXPECT_THAT(result.errors, IsEmpty());
  EXPECT_THAT(result.rows, Not(IsEmpty()));
  bool found = false;
  for (const auto& row : result.rows) {
    if (!row.value("is_total", false)) {
      continue;
    }
    found = true;
    EXPECT_THAT(row.at("left-total").at("count").get<std::uint64_t>(), Eq(8));
    EXPECT_THAT(row.at("right-total").at("count").get<std::uint64_t>(), Eq(7));
    EXPECT_THAT(row.at("left-total").at("bytes").get<std::uint64_t>(), Eq(20));
    EXPECT_THAT(row.at("right-total").at("bytes").get<std::uint64_t>(), Eq(20));
  }
  EXPECT_THAT(found, IsTrue());
}

TEST_F(ComparisonTotalsTest, EmptyPhysicalSideKeepsItsDirectoryRootContribution) {
  filesystem.empty_left = true;
  ASSERT_OK_AND_ASSIGN(const auto ordinary, Run({"--summary=overall"}));
  ASSERT_OK_AND_ASSIGN(const auto action, Run({"--summary=overall"}, true));
  EXPECT_THAT(ordinary.result.errors, Eq(0));
  EXPECT_THAT(ordinary.rows, Eq(action.rows));
  bool found = false;
  for (const auto& row : ordinary.rows) {
    if (row.value("is_total", false)) {
      found = true;
      EXPECT_THAT(row.at("left-total").at("count").get<std::uint64_t>(), Eq(1));
      EXPECT_THAT(row.at("right-total").at("count").get<std::uint64_t>(), Eq(7));
    }
  }
  EXPECT_THAT(found, IsTrue());
}
}  // namespace
}  // namespace xff::engine
