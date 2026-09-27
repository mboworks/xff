// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
