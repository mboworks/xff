// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Fixture creation and caller-owned input preparation are excluded from timing. Each OpenBytes
// iteration receives a fresh owning string; construction and destruction of the filesystem are timed.

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_fs.h"
#include "xff/archive/memory_archive_test.h"
#include "xff/archive/memory_phar_test.h"
#include "xff/vfs/read_source.h"

namespace xff::archive {
namespace {

enum class Format { kTar, kPhar };

constexpr std::string_view NameFor(Format format) {
  return format == Format::kTar ? "memory.tar" : "memory.phar";
}

absl::StatusOr<std::string> MakeFixture(Format format, std::size_t size) {
  const std::string content(size, format == Format::kTar ? 't' : 'p');
  if (format == Format::kTar) {
    const auto members = std::to_array<MemoryArchiveMember>({
        {
            .path = "member.txt",
            .content = content,
        },
    });
    return MakeMemoryTar(members);
  }
  return MakeMemoryPhar("member.txt", content);
}

void OpenOwnedBytes(benchmark::State& state, Format format, std::size_t size) {
  const auto fixture = MakeFixture(format, size);
  if (!fixture.ok()) {
    state.SkipWithError(fixture.status().ToString());
    return;
  }
  for (const auto unused : state) {
    static_cast<void>(unused);
    state.PauseTiming();
    std::string bytes = *fixture;
    state.ResumeTiming();
    auto result = ArchiveFileSystem::OpenBytes(NameFor(format), std::move(bytes));
    if (!result.ok()) {
      state.SkipWithError(result.status().ToString());
      return;
    }
    benchmark::DoNotOptimize(result);
    benchmark::ClobberMemory();
  }
  state.counters["input_bytes"] = static_cast<double>(fixture->size());
}

void OpenExistingSource(benchmark::State& state, std::size_t size) {
  auto fixture = MakeFixture(Format::kTar, size);
  if (!fixture.ok()) {
    state.SkipWithError(fixture.status().ToString());
    return;
  }
  const auto source = vfs::MemoryReadSource(std::move(*fixture));
  for (const auto unused : state) {
    static_cast<void>(unused);
    auto result = ArchiveFileSystem::OpenSource("memory.tar", source);
    if (!result.ok()) {
      state.SkipWithError(result.status().ToString());
      return;
    }
    benchmark::DoNotOptimize(result);
    benchmark::ClobberMemory();
  }
  state.counters["input_bytes"] = static_cast<double>(size);
}

bool RegisterBenchmarks() {
  constexpr auto kSizes = std::to_array<std::size_t>({1'024, 65'536, 1'048'576, 16'777'216, 67'108'864});
  constexpr auto kFormats = std::to_array<Format>({Format::kTar, Format::kPhar});
  for (const auto size : kSizes) {
    for (const auto format : kFormats) {
      const std::string label = absl::StrCat("archive-open-bytes/", NameFor(format), "/", size);
      auto& measurement = *benchmark::RegisterBenchmark(label, OpenOwnedBytes, format, size);
      measurement.UseRealTime()->Unit(benchmark::kMicrosecond)->Repetitions(9);
      if (size >= 65'536) {
        // Each measured iteration needs a fresh owned input. Keep paused setup bounded for large
        // fixtures instead of letting Google Benchmark multiply excluded full-buffer copies based
        // on the fast candidate's iteration estimate.
        measurement.Iterations(10);
      } else {
        measurement.MinTime(0.1);
      }
    }
    const std::string label = absl::StrCat("archive-open-source/tar/", size);
    benchmark::RegisterBenchmark(label, OpenExistingSource, size)
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond)
        ->MinTime(0.1)
        ->Repetitions(9);
  }
  return true;
}

[[maybe_unused]] const bool kRegistered = RegisterBenchmarks();

}  // namespace
}  // namespace xff::archive

BENCHMARK_MAIN();
