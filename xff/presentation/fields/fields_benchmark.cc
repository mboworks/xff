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

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark/benchmark.h"
#include "xff/presentation/fields/fields.h"
#include "xff/vfs/entry.h"

namespace {

struct Scenario final {
  std::string_view name;
  std::string_view format;
};

void RenderFields(benchmark::State& state, Scenario scenario) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto length = static_cast<std::size_t>(state.range(1));
  std::vector<std::string> paths;
  paths.reserve(count);
  for (std::size_t at = 0; at < count; ++at) {
    paths.push_back(
        "root/" + std::string(length, 'd') + "/" + std::string(length, 'f') + std::to_string(at) + ".tar.gz");
  }
  const xff::fields::Template compiled = xff::fields::Template::Compile(scenario.format);
  const xff::vfs::Metadata metadata;
  for ([[maybe_unused]] const auto unused : state) {
    for (const std::string& path : paths) {
      std::string value = compiled.Render({.path = path, .metadata = metadata});
      benchmark::DoNotOptimize(value);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

// Repetitions retain the same fastest-seven protocol used by other expression benchmarks.
double FastestSeven(const std::vector<double>& values) {
  std::vector<double> sorted(values);
  std::sort(sorted.begin(), sorted.end());
  const std::size_t count = std::min<std::size_t>(7, sorted.size());
  return count == 0 ? 0
                    : std::accumulate(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count), 0.0)
                          / static_cast<double>(count);
}

}  // namespace

// XFF_ABI_POINTER: Google Benchmark requires the process argv ABI.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  static constexpr auto kScenarios = std::to_array<Scenario>({
      {.name = "name", .format = "{name}"},
      {.name = "components", .format = "{name}|{stem}|{ext}|{suffix}|{suffixes}|{core}"},
      {.name = "qualified", .format = "{path:name}|{path:stem}|{path:ext}|{path:suffix}|{path:suffixes}|{path:core}"},
      {.name = "dir-control", .format = "{dir}"},
  });
  for (const Scenario& scenario : kScenarios) {
    benchmark::RegisterBenchmark("fields/" + std::string(scenario.name), RenderFields, scenario)
        ->ArgsProduct({{10, 1'000, 100'000}, {8, 160}})
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond)
        ->Repetitions(9)
        ->MinTime(0.01)
        ->MinWarmUpTime(0.01)
        ->ComputeStatistics("fastest7of9", FastestSeven);
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
