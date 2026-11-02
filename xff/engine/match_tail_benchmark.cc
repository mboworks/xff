// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "benchmark/benchmark.h"
#include "xff/engine/expression_execution.h"
#include "xff/engine/owned_visit.h"
#include "xff/engine/parallel_match.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/read_source.h"

namespace {
namespace engine = ::xff::engine;
namespace parser = ::xff::parser;
namespace vfs = ::xff::vfs;

class TailFs final : public vfs::FileSystem {
 public:
  explicit TailFs(bool delayed) : delayed_(delayed) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::FailedPreconditionError("tail benchmark must not traverse");
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return absl::FailedPreconditionError("tail benchmark supplies metadata");
  }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override {
    Delay();
    return "hit\n";
  }

  absl::StatusOr<vfs::SharedReadSource> ContentSource(std::string_view) const override {
    Delay();
    return source_;
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only benchmark"); }

  bool Access(std::string_view, vfs::AccessMode mode) const override { return mode == vfs::AccessMode::kRead; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("not a link");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

 private:
  void Delay() const {
    if (delayed_) {
      // Controlled latency is a policy counterexample, not a native filesystem timing claim.
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }

  const vfs::SharedReadSource source_ = vfs::MemoryReadSource("hit\n");
  const bool delayed_;
};

std::vector<engine::OwnedVisit> Entries(const TailFs& fs, std::size_t count) {
  const vfs::Metadata metadata{.type = vfs::FileType::kRegular, .size = 4};
  std::vector<engine::OwnedVisit> entries;
  entries.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    entries.emplace_back(
        engine::Visit{
            .path = "tree/" + std::to_string(index),
            .root = "tree",
            .metadata = metadata,
            .fs = fs,
        });
  }
  return entries;
}

bool Correct(std::span<const engine::ParallelResult> results, std::size_t count) {
  return results.size() == count && std::ranges::all_of(results, [](const auto& result) {
           return result.evaluation.matched && !result.evaluation.deferred && !result.content;
         });
}

void WarmedTail(benchmark::State& state, bool delayed) {
  const TailFs fs(delayed);
  auto command = parser::Parse({"tree", "-content", "hit"});
  if (!command.ok()) {
    state.SkipWithError(command.status().ToString());
    return;
  }
  if (!command->expression) {
    state.SkipWithError("missing benchmark expression");
    return;
  }
  auto execution = engine::PrepareExpressionExecution(*command->expression);
  if (!execution.ok()) {
    state.SkipWithError(execution.status().ToString());
    return;
  }
  const auto count = static_cast<std::size_t>(state.range(0));
  engine::ParallelMatch matcher(*command->expression, static_cast<std::size_t>(state.range(1)), false, {}, *execution);
  // Grow the pool before timing. Every measured iteration is a later tail, not startup.
  if (!Correct(matcher.Match(Entries(fs, 256)), 256) || !Correct(matcher.Match(Entries(fs, count)), count)) {
    state.SkipWithError("incorrect warmed matcher output");
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    const auto& results = matcher.Match(Entries(fs, count));
    if (!Correct(results, count)) {
      state.SkipWithError("incorrect repeated tail output");
      break;
    }
    benchmark::DoNotOptimize(results.size());
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}
}  // namespace

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  benchmark::RegisterBenchmark("warmed-tail", WarmedTail, false)
      ->ArgsProduct({{16, 32, 64, 256}, {3, 10}})
      ->UseRealTime()
      ->Unit(benchmark::kMicrosecond);
  benchmark::RegisterBenchmark("warmed-slow-tail", WarmedTail, true)
      ->ArgsProduct({{16, 64}, {3, 10}})
      ->UseRealTime()
      ->Unit(benchmark::kMicrosecond);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
