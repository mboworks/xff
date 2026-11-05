// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"

namespace xff::datetime {
namespace {

void FormatTimestamp(benchmark::State& state, std::string_view pattern, std::string_view expected) {
  const auto timestamp = absl::FromUnixSeconds(1'600'000'000);
  const auto zone = absl::UTCTimeZone();
  if (absl::FormatTime(pattern, timestamp, zone) != expected) {
    state.SkipWithError("complete timestamp output differs from the independent expected value");
    return;
  }
  for (const auto unused : state) {
    static_cast<void>(unused);
    benchmark::DoNotOptimize(absl::FormatTime(pattern, timestamp, zone));
  }
  state.SetItemsProcessed(state.iterations());
}

bool RegisterBenchmarks() {
  struct FormatCase {
    std::string_view name;
    std::string_view pattern;
    std::string_view expected;
  };

  constexpr auto kCases = std::to_array<FormatCase>({
      {
          .name = "empty",
          .pattern = "",
          .expected = "",
      },
      {
          .name = "literal",
          .pattern = "literal",
          .expected = "literal",
      },
      {
          .name = "numeric-space",
          .pattern = "%Y-%m-%d %H:%M:%S %z",
          .expected = "2020-09-13 12:26:40 +0000",
      },
      {
          .name = "compact",
          .pattern = "%Y%m%d%H%M%S%z",
          .expected = "20200913122640+0000",
      },
      {
          .name = "iso",
          .pattern = "%Y-%m-%dT%H:%M:%S%Ez",
          .expected = "2020-09-13T12:26:40+00:00",
      },
      {
          .name = "date",
          .pattern = "%F",
          .expected = "2020-09-13",
      },
      {
          .name = "escaped",
          .pattern = "%%:%T:%%",
          .expected = "%:12:26:40:%",
      },
      {
          .name = "locale-fallback",
          .pattern = "%a %b %e %T %Y",
          .expected = "Sun Sep 13 12:26:40 2020",
      },
  });
  for (const auto& spec : kCases) {
    benchmark::RegisterBenchmark(absl::StrCat("time-format/", spec.name), FormatTimestamp, spec.pattern, spec.expected)
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond)
        ->MinWarmUpTime(0.01)
        ->MinTime(0.02);
  }
  return true;
}

[[maybe_unused]] const bool kRegistered = RegisterBenchmarks();

}  // namespace
}  // namespace xff::datetime

BENCHMARK_MAIN();
