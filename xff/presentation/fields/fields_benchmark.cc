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

#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "xff/content/snapshot.h"
#include "xff/presentation/fields/fields.h"
#include "xff/vfs/entry.h"

namespace {

struct Scenario final {
  std::string_view name;
  std::string_view format;
};

void RenderFields(benchmark::State& state, Scenario scenario, bool supplied) {
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
  const xff::content::Snapshot snapshot;
  xff::fields::RenderContext context{
      .metadata = metadata,
      .tz = absl::UTCTimeZone(),
  };
  if (supplied) {
    context.content.set_ref(snapshot);
  }
  for ([[maybe_unused]] const auto unused : state) {
    for (const std::string& path : paths) {
      context.path = path;
      std::string value = compiled.Render(context);
      benchmark::DoNotOptimize(value);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

void AppendField(benchmark::State& state, Scenario scenario, bool direct, bool supplied) {
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
  const xff::content::Snapshot snapshot;
  xff::fields::RenderContext context{
      .metadata = metadata,
      .tz = absl::UTCTimeZone(),
  };
  if (supplied) {
    context.content.set_ref(snapshot);
  }
  for ([[maybe_unused]] const auto unused : state) {
    for (const std::string& path : paths) {
      context.path = path;
      std::string output;
      if (direct) {
        compiled.AppendTo(output, context);
      } else {
        output.append(compiled.Render(context));
      }
      benchmark::DoNotOptimize(output);
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
      {.name = "path", .format = "{path}"},
      {.name = "name", .format = "{name}"},
      {.name = "components", .format = "{name}|{stem}|{ext}|{suffix}|{suffixes}|{core}"},
      {.name = "qualified", .format = "{path:name}|{path:stem}|{path:ext}|{path:suffix}|{path:suffixes}|{path:core}"},
      {.name = "dir-control", .format = "{dir}"},
      {.name = "dir-component", .format = "{path:dir}"},
      {.name = "size-control", .format = "{size}"},
      {.name = "rewrite-name", .format = "{name:s/tar/pack/g}"},
      {.name = "rewrite-path", .format = "{path:s#/#_#g}"},
      {.name = "extract-name", .format = "{name:m/(.*)/\\1/;join(,)}"},
      {.name = "literal-control", .format = "literal output"},
      {.name = "composite-dir", .format = "prefix:{dir}:{name}"},
  });
  static constexpr auto kSupplied = std::to_array<bool>({false, true});
  for (const Scenario& scenario : kScenarios) {
    for (const bool supplied : kSupplied) {
      benchmark::RegisterBenchmark(
          "fields-context/" + std::string(supplied ? "supplied/" : "fallback/") + std::string(scenario.name),
          RenderFields, scenario, supplied)
          ->ArgsProduct({{10, 1'000, 100'000}, {8, 160}})
          ->UseRealTime()
          ->Unit(benchmark::kMicrosecond)
          ->Repetitions(9)
          ->MinTime(0.01)
          ->MinWarmUpTime(0.01)
          ->ComputeStatistics("fastest7of9", FastestSeven);
    }
  }
  static constexpr auto kAppendScenarios = std::to_array<Scenario>({
      {.name = "path", .format = "{path}"},
      {.name = "dir", .format = "{dir}"},
      {.name = "name", .format = "{name}"},
      {.name = "transformed", .format = "{path:stem}"},
      {.name = "composite", .format = "{dir}/{name}"},
  });
  static constexpr auto kDirect = std::to_array<bool>({false, true});
  static constexpr auto kAppendContext = std::to_array<bool>({false, true});
  for (const Scenario& scenario : kAppendScenarios) {
    for (const bool direct : kDirect) {
      for (const bool supplied : kAppendContext) {
        benchmark::RegisterBenchmark(
            "fields-append/" + std::string(direct ? "direct/" : "render-copy/")
                + std::string(supplied ? "supplied/" : "fallback/") + std::string(scenario.name),
            AppendField, scenario, direct, supplied)
            ->ArgsProduct({{10, 1'000, 100'000}, {8, 160}})
            ->UseRealTime()
            ->Unit(benchmark::kMicrosecond)
            ->Repetitions(9)
            ->MinTime(0.01)
            ->MinWarmUpTime(0.01)
            ->ComputeStatistics("fastest7of9", FastestSeven);
      }
    }
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
