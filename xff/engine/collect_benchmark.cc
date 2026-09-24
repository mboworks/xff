// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "benchmark/benchmark.h"
#include "xff/engine/collect.h"
#include "xff/engine/walk.h"
#include "xff/vfs/filesystem.h"

namespace {

void Collect(benchmark::State& state) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const std::string root(static_cast<std::size_t>(state.range(1)), 'r');
  std::vector<std::string> paths;
  paths.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    paths.push_back(root + "/entry-" + std::to_string(index) + ".txt");
  }
  const xff::vfs::Metadata metadata{.type = xff::vfs::FileType::kRegular, .size = 42};
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    xff::engine::Collections collections;
    for (const std::string& path : paths) {
      const xff::engine::Visit visit{
          .path = path,
          .name = std::string_view(path).substr(root.size() + 1),
          .root = root,
          .depth = 1,
          .metadata = metadata,
      };
      if (!collections.Add("all", visit)) {
        state.SkipWithError("unexpected collection overflow");
        return;
      }
    }
    std::uint64_t checksum = 0;
    for (const auto& entry : collections.Entries("all")) {
      checksum += entry.path.size() + entry.name.size() + entry.root.size() + entry.metadata.size;
    }
    benchmark::DoNotOptimize(checksum);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

}  // namespace

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  benchmark::RegisterBenchmark("Collect", Collect)
      ->ArgsProduct({{10, 100, 1'000, 10'000, 100'000}, {1, 128}})
      ->Threads(1)
      ->Threads(4)
      ->Unit(benchmark::kMicrosecond);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
