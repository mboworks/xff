// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "benchmark/benchmark.h"
#include "xff/matching/language/catalog.h"
#include "xff/matching/language/language.h"
#include "xff/matching/mime/mime.h"

namespace {
namespace language = ::xff::language;
namespace mime = ::xff::mime;

enum class Mode { kLanguage, kMime, kCatalog };

bool ResetAndCheck(Mode mode) {
  if (mode == Mode::kMime) {
    return mime::Configure({}, mime::ConflictPolicy::kError).ok() && mime::TypeForName("sample.txt") == "text/plain";
  }
  if (!language::Configure({}, language::ConflictPolicy::kError).ok()) {
    return false;
  }
  if (mode == Mode::kCatalog) {
    return !language::ActiveCatalog().CandidatesForName("main.cc").empty();
  }
  return language::ActiveSnapshot().LanguageForName("main.cc") == "C++";
}

void Reconfigure(benchmark::State& state, Mode mode) {
  // Warm once before timing. This measures repeated library invocations, not fresh CLI startup.
  if (!ResetAndCheck(mode)) {
    state.SkipWithError("incorrect default catalog before measurement");
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    bool correct = ResetAndCheck(mode);
    benchmark::DoNotOptimize(correct);
    if (!correct) {
      state.SkipWithError("incorrect reused catalog");
      break;
    }
  }
  state.SetItemsProcessed(state.iterations());
}
}  // namespace

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  // Fixed iterations bound the baseline's deliberately process-retained snapshots. Adaptive
  // calibration would retain ever more complete JSON vocabularies in the baseline.
  benchmark::RegisterBenchmark("reset-default/language", Reconfigure, Mode::kLanguage)
      ->Iterations(5)
      ->UseRealTime()
      ->Unit(benchmark::kMicrosecond);
  benchmark::RegisterBenchmark("reset-default/mime", Reconfigure, Mode::kMime)
      ->Iterations(5)
      ->UseRealTime()
      ->Unit(benchmark::kMicrosecond);
  benchmark::RegisterBenchmark("reset-default/catalog", Reconfigure, Mode::kCatalog)
      ->Iterations(5)
      ->UseRealTime()
      ->Unit(benchmark::kMicrosecond);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
