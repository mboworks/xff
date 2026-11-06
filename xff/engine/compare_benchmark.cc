// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "benchmark/benchmark.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/read_source.h"

namespace {
class CompareTree final : public xff::vfs::FileSystem {
 public:
  CompareTree(std::size_t count, std::size_t bytes, bool different) : count_(count), left_(bytes, 'a'), right_(left_) {
    left_.back() = right_.back() = '\n';
    if (different) {
      right_.front() = 'b';
    }
    left_source_ = xff::vfs::MemoryReadSource(left_);
    right_source_ = xff::vfs::MemoryReadSource(right_);
  }

  absl::StatusOr<std::vector<xff::vfs::Entry>> ReadDir(std::string_view path) const override {
    if (path != "left" && path != "right") {
      return absl::NotFoundError("not a directory");
    }
    std::vector<xff::vfs::Entry> entries;
    entries.reserve(count_);
    for (std::size_t index = 0; index < count_; ++index) {
      const auto name = std::to_string(index);
      entries.push_back({.path = std::string(path) + "/" + name, .name = name, .type = xff::vfs::FileType::kRegular});
    }
    return entries;
  }

  absl::StatusOr<xff::vfs::Metadata> Stat(std::string_view path, bool) const override {
    const bool root = path == "left" || path == "right";
    return xff::vfs::Metadata{
        .type = root ? xff::vfs::FileType::kDirectory : xff::vfs::FileType::kRegular,
        .size = root ? 0 : left_.size(),
    };
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override { return Content(path); }

  absl::StatusOr<std::string> ReadContentRange(std::string_view path, std::uint64_t offset, std::size_t length)
      const override {
    const auto& bytes = Content(path);
    return offset >= bytes.size() ? std::string{} : bytes.substr(static_cast<std::size_t>(offset), length);
  }

  absl::StatusOr<xff::vfs::SharedReadSource> ContentSource(std::string_view path) const override {
    return path.starts_with("left/") ? left_source_ : right_source_;
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only"); }

  bool Access(std::string_view, xff::vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("not a link");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

 private:
  const std::string& Content(std::string_view path) const { return path.starts_with("left/") ? left_ : right_; }

  std::size_t count_;
  std::string left_;
  std::string right_;
  xff::vfs::SharedReadSource left_source_;
  xff::vfs::SharedReadSource right_source_;
};

class NestedCompareTree final : public xff::vfs::FileSystem {
 public:
  NestedCompareTree(std::size_t count, std::size_t bytes, bool deep) : content_(bytes, 'a'), right_content_(content_) {
    content_.back() = right_content_.back() = '\n';
    left_source_ = xff::vfs::MemoryReadSource(content_);
    right_source_ = xff::vfs::MemoryReadSource(right_content_);
    AddDirectory("left");
    AddDirectory("right");
    std::size_t depth = 1;
    for (std::size_t leaves = 4; deep && leaves < count; leaves *= 4) {
      ++depth;
    }
    for (std::size_t index = 0; index < count; ++index) {
      std::string relative;
      if (deep) {
        std::size_t value = index;
        std::vector<std::size_t> digits(depth);
        for (std::size_t position = depth; position > 0; --position) {
          digits[position - 1] = value % 4;
          value /= 4;
        }
        for (const std::size_t digit : digits) {
          relative += "d" + std::to_string(digit) + "/";
        }
      } else {
        relative = "d" + std::to_string(index % 100) + "/";
      }
      relative += "f" + std::to_string(index);
      AddFile("left", relative);
      AddFile("right", relative);
    }
  }

  absl::StatusOr<std::vector<xff::vfs::Entry>> ReadDir(std::string_view path) const override {
    const auto found = directories_.find(path);
    return found == directories_.end()
               ? absl::StatusOr<std::vector<xff::vfs::Entry>>(absl::NotFoundError("not a directory"))
               : absl::StatusOr<std::vector<xff::vfs::Entry>>(found->second);
  }

  absl::StatusOr<xff::vfs::Metadata> Stat(std::string_view path, bool) const override {
    if (directories_.contains(path)) {
      return xff::vfs::Metadata{
          .type = xff::vfs::FileType::kDirectory,
          .ino = std::hash<std::string_view>{}(path),
          .dev = 1,
      };
    }
    return xff::vfs::Metadata{
        .type = xff::vfs::FileType::kRegular,
        .size = content_.size(),
        .ino = std::hash<std::string_view>{}(path),
        .dev = 1,
    };
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override { return Content(path); }

  absl::StatusOr<std::string> ReadContentRange(std::string_view path, std::uint64_t offset, std::size_t length)
      const override {
    const auto& bytes = Content(path);
    return offset >= bytes.size() ? std::string{} : bytes.substr(static_cast<std::size_t>(offset), length);
  }

  absl::StatusOr<xff::vfs::SharedReadSource> ContentSource(std::string_view path) const override {
    return path.starts_with("left/") ? left_source_ : right_source_;
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only"); }

  bool Access(std::string_view, xff::vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("not a link");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

 private:
  void AddDirectory(std::string path) { directories_.try_emplace(std::move(path)); }

  void AddFile(std::string_view root, std::string_view relative) {
    std::string directory(root);
    std::size_t offset = 0;
    while (true) {
      directories_.try_emplace(directory);
      const std::size_t separator = relative.find('/', offset);
      if (separator == std::string_view::npos) {
        const std::string name(relative.substr(offset));
        std::string path(directory);
        path.push_back('/');
        path.append(name);
        directories_.at(directory).push_back(
            {.path = std::move(path), .name = name, .type = xff::vfs::FileType::kRegular});
        return;
      }
      const std::string name(relative.substr(offset, separator - offset));
      std::string child(directory);
      child.push_back('/');
      child.append(name);
      const bool inserted = directories_.try_emplace(child).second;
      if (inserted) {
        directories_.at(directory).push_back({.path = child, .name = name, .type = xff::vfs::FileType::kDirectory});
      }
      directory = child;
      offset = separator + 1;
    }
  }

  const std::string& Content(std::string_view path) const {
    return path.starts_with("left/") ? content_ : right_content_;
  }

  std::string content_;
  std::string right_content_;
  xff::vfs::SharedReadSource left_source_;
  xff::vfs::SharedReadSource right_source_;
  std::map<std::string, std::vector<xff::vfs::Entry>, std::less<>> directories_;
};

void Compare(benchmark::State& state, std::size_t bytes, bool different, bool patch) {
  const CompareTree tree(static_cast<std::size_t>(state.range(0)), bytes, different);
  const std::vector<std::string> arguments{
      "--compare=" + std::string(patch ? "diff" : "status"),
      "--compare-select=" + std::string(patch ? "different" : "all"),
      "--jobs=" + std::to_string(state.range(1)),
      "--archive=none",
      "--sort=none",
      "left",
      "right",
      "-type",
      "f",
  };
  auto command = xff::parser::Parse(arguments);
  if (!command.ok()) {
    state.SkipWithError(command.status().ToString());
    return;
  }
  std::size_t records = 0;
  std::size_t output_bytes = 0;
  const auto emit = [&](std::string_view text) {
    ++records;
    output_bytes += text.size();
  };
  const auto error = [](std::string_view, absl::Status) {};
  const auto initial = xff::engine::RunFind(*command, tree, emit, error);
  if (initial.errors != 0 || std::cmp_not_equal(records, state.range(0)) || output_bytes == 0) {
    state.SkipWithError("incorrect comparison output");
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    records = output_bytes = 0;
    const auto result = xff::engine::RunFind(*command, tree, emit, error);
    if (result.errors != 0) {
      state.SkipWithError("comparison failed");
      break;
    }
    benchmark::DoNotOptimize(output_bytes);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void CompareTotals(benchmark::State& state, std::string_view summary, std::string_view scopes) {
  const CompareTree tree(static_cast<std::size_t>(state.range(0)), 8, true);
  std::vector<std::string> arguments{
      "--compare=summary",
      "--jobs=" + std::to_string(state.range(1)),
      "--archive=none",
      "--sort=none",
      "left",
      "right",
      "-type",
      "f",
  };
  if (summary != "compare") {
    arguments.push_back("--summary=" + std::string(summary));
    arguments.push_back("--summary-scope=" + std::string(scopes));
  }
  auto command = xff::parser::Parse(arguments);
  if (!command.ok()) {
    state.SkipWithError(command.status().ToString());
    return;
  }
  std::size_t output_bytes = 0;
  const auto emit = [&](std::string_view text) { output_bytes += text.size(); };
  const auto error = [](std::string_view, absl::Status) {};
  const auto initial = xff::engine::RunFind(*command, tree, emit, error);
  if (initial.errors != 0 || output_bytes == 0) {
    state.SkipWithError("incorrect comparison summary output");
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    output_bytes = 0;
    const auto result = xff::engine::RunFind(*command, tree, emit, error);
    if (result.errors != 0) {
      state.SkipWithError("comparison summary failed");
      break;
    }
    benchmark::DoNotOptimize(output_bytes);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void CompareNestedTrees(benchmark::State& state, bool deep) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const NestedCompareTree tree(count, 8, deep);
  const std::vector<std::string> arguments{
      "--compare=summary", "--jobs=" + std::to_string(state.range(1)), "--archive=none", "--sort=none", "left", "right",
  };
  auto command = xff::parser::Parse(arguments);
  if (!command.ok()) {
    state.SkipWithError(command.status().ToString());
    return;
  }
  std::size_t output_bytes = 0;
  const auto emit = [&](std::string_view text) { output_bytes += text.size(); };
  std::mutex error_mutex;
  std::string first_error;
  const auto error = [&](std::string_view path, absl::Status status) {
    const std::scoped_lock lock(error_mutex);
    if (first_error.empty()) {
      first_error = std::string(path) + ": " + status.ToString();
    }
  };
  const auto initial = xff::engine::RunFind(*command, tree, emit, error);
  if (initial.errors != 0 || output_bytes == 0) {
    const std::string message = "nested comparison errors=" + std::to_string(initial.errors)
                                + " output_bytes=" + std::to_string(output_bytes) + " first=" + first_error;
    state.SkipWithError(message);
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    output_bytes = 0;
    const auto result = xff::engine::RunFind(*command, tree, emit, error);
    if (result.errors != 0 || output_bytes == 0) {
      state.SkipWithError("nested comparison failed");
      break;
    }
    benchmark::DoNotOptimize(output_bytes);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}
}  // namespace

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  struct Scenario {
    std::string_view name;
    std::size_t bytes;
    bool different;
    bool patch;
  };

  constexpr auto kScenarios = std::to_array<Scenario>({
      {.name = "small-identical", .bytes = 4'096, .different = false, .patch = false},
      {.name = "large-identical", .bytes = 262'144, .different = false, .patch = false},
      {.name = "large-early-different", .bytes = 262'144, .different = true, .patch = false},
      {.name = "small-patches", .bytes = 4'096, .different = true, .patch = true},
  });
  for (const auto& scenario : kScenarios) {
    benchmark::RegisterBenchmark(
        std::string(scenario.name), Compare, scenario.bytes, scenario.different, scenario.patch)
        ->ArgsProduct({{10, 100, 1'000, 10'000}, {1, 4}})
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond);
  }

  struct TotalsScenario {
    std::string_view summary;
    std::string_view scopes;
  };

  constexpr auto kTotals = std::to_array<TotalsScenario>({
      {
          .summary = "compare",
          .scopes = "default",
      },
      {.summary = "overall", .scopes = "compare"},
      {.summary = "type", .scopes = "compare"},
      {.summary = "ext", .scopes = "compare"},
      {.summary = "overall", .scopes = "left-only,right-only,different,identical"},
      {.summary = "ext", .scopes = "left-total,left-only"},
  });
  for (const auto& scenario : kTotals) {
    benchmark::RegisterBenchmark(
        "totals/" + std::string(scenario.summary) + "/" + std::string(scenario.scopes), CompareTotals, scenario.summary,
        scenario.scopes)
        ->ArgsProduct({{10, 100, 1'000, 10'000, 100'000}, {1, 3}})
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond);
  }
  constexpr auto kFrontierScenarios = std::to_array<std::pair<std::string_view, bool>>({
      {"comparison/broad", false},
      {"comparison/deep", true},
  });
  for (const auto& [name, deep] : kFrontierScenarios) {
    benchmark::RegisterBenchmark(std::string(name), CompareNestedTrees, deep)
        ->ArgsProduct({{10, 100, 1'000, 10'000}, {1, 3}})
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond);
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
