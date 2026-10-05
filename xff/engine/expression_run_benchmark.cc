// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "benchmark/benchmark.h"
#include "xff/engine/expression_qualification.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace {

using ::xff::engine::ExpressionExecutor;

class RunFs final : public xff::vfs::FileSystem {
 public:
  explicit RunFs(std::size_t count) {
    entries_.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      std::string name = "file" + std::to_string(index) + (index % 2 == 0 ? ".txt" : ".cc");
      entries_.push_back({.path = "root/" + name, .name = std::move(name), .type = xff::vfs::FileType::kRegular});
    }
  }

  absl::StatusOr<std::vector<xff::vfs::Entry>> ReadDir(std::string_view path) const override {
    if (path == "root") {
      return entries_;
    }
    return absl::NotFoundError("unknown benchmark directory");
  }

  absl::StatusOr<xff::vfs::Metadata> Stat(std::string_view path, bool) const override {
    return xff::vfs::Metadata{
        .type = path == "root" ? xff::vfs::FileType::kDirectory : xff::vfs::FileType::kRegular,
        .size = 7,
        .mode = 0644,
    };
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    return path.ends_with(".txt") ? "needle\n" : "absent\n";
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only benchmark"); }

  bool Access(std::string_view, xff::vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::NotFoundError("not a link"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

 private:
  std::vector<xff::vfs::Entry> entries_;
};

struct Scenario {
  std::string_view name;
  std::vector<std::string> arguments;
};

struct Executor {
  std::string_view name;
  ExpressionExecutor executor;
};

constexpr auto kExecutors = std::to_array<Executor>({
    {.name = "tree", .executor = ExpressionExecutor::kTree},
    {.name = "production", .executor = ExpressionExecutor::kProduction},
    {.name = "bound", .executor = ExpressionExecutor::kBound},
    {.name = "prepared", .executor = ExpressionExecutor::kPrepared},
    {.name = "prepared-eager", .executor = ExpressionExecutor::kPreparedEager},
    {.name = "program-switch", .executor = ExpressionExecutor::kProgramSwitch},
    {.name = "program-functions", .executor = ExpressionExecutor::kProgramFunctions},
    {.name = "program-optimized", .executor = ExpressionExecutor::kProgramOptimized},
});

double FastestSeven(const std::vector<double>& times) {
  auto sorted = times;
  std::ranges::sort(sorted);
  const auto count = std::min<std::size_t>(7, sorted.size());
  return std::accumulate(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count), 0.0)
         / static_cast<double>(count);
}

void Measure(benchmark::State& state, const Scenario& scenario, ExpressionExecutor executor) {
  const auto factory = xff::engine::QualifiedExpressionFactory(executor);
  const RunFs fs(static_cast<std::size_t>(state.range(0)));
  std::vector<std::string> arguments{
      "--exact", "--color=never", "--sort=dir", "--jobs=" + std::to_string(state.range(1)), "root",
  };
  arguments.insert(arguments.end(), scenario.arguments.begin(), scenario.arguments.end());
  auto command = xff::parser::Parse(arguments);
  if (!command.ok()) {
    state.SkipWithError(command.status().ToString());
    return;
  }
  xff::parser::BindMatchers(
      *command, xff::parser::GrammarFromGlobals(command->globals),
      xff::parser::ResolveCaseMode(command->globals, xff::registry::Style::kXff));
  std::string expected;
  std::string actual;
  bool error = false;
  const auto on_error = [&](std::string_view, absl::Status) { error = true; };
  const auto reference = xff::engine::RunFind(
      *command, fs, [&](std::string_view text) { expected.append(text); }, on_error, std::nullopt, 0, nullptr);
  const auto candidate = xff::engine::RunFind(
      *command, fs, [&](std::string_view text) { actual.append(text); }, on_error, std::nullopt, 0, factory);
  if (error || reference.errors != 0 || candidate.errors != 0 || expected != actual
      || reference.any_match != candidate.any_match) {
    state.SkipWithError("whole-engine expression output differs from reference");
    return;
  }
  std::size_t bytes = 0;
  const auto emit = [&](std::string_view text) { bytes += text.size(); };
  // Parsing and fixture construction are excluded. Every iteration includes run preflight,
  // expression preparation, worker startup, traversal, metadata/content, sinks and teardown.
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    auto result = xff::engine::RunFind(*command, fs, emit, on_error, std::nullopt, 0, factory);
    benchmark::DoNotOptimize(result);
  }
  if (error) {
    state.SkipWithError("whole-engine expression measurement failed");
  }
  benchmark::DoNotOptimize(bytes);
  state.counters["files"] = static_cast<double>(state.range(0));
  state.counters["workers"] = static_cast<double>(state.range(1));
}

}  // namespace

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  const auto scenarios = std::to_array<Scenario>({
      {.name = "first-limit", .arguments = {"-type", "f", "-first", "3", "-print"}},
      {.name = "mime", .arguments = {"-type", "f", "-mime", "TEXT/*"}},
      {.name = "name", .arguments = {"-name", "*.txt"}},
      {.name = "scalar", .arguments = {"-type", "f", "-size", "+2c", "-perm", "0644", "-name", "*.txt"}},
      {.name = "age", .arguments = {"-type", "f", "-mtime", "+2d", "-mmin", "+1", "-used", "0", "-name", "*.txt"}},
      {.name = "regex-output", .arguments = {"-type", "f", "-rxc", "needle", "-print"}},
      {.name = "regex-unreached", .arguments = {"-type", "f", "-name", "*.missing", "-rxc", "needle"}},
      {.name = "summary", .arguments = {"-type", "f", "-name", "*.txt", "--summary=ext"}},
      {.name = "scored-replay", .arguments = {"-type", "f", "-fuzzy", "file", "-top", "3", "-print"}},
  });
  for (const auto& scenario : scenarios) {
    for (const auto& executor : kExecutors) {
      benchmark::RegisterBenchmark(
          "engine/" + std::string(executor.name) + "/" + std::string(scenario.name), Measure, scenario,
          executor.executor)
          ->ArgsProduct({{0, 1, 10, 1'000, 10'000}, {1, 3}})
          ->UseRealTime()
          ->Unit(benchmark::kMicrosecond)
          ->Repetitions(9)
          ->MinTime(0.01)
          ->MinWarmUpTime(0.01)
          ->ComputeStatistics("fastest7of9", FastestSeven);
    }
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
