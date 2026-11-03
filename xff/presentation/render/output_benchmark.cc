// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Isolate record construction and cached digest consumption from host filesystem costs.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "xff/content/snapshot.h"
#include "xff/hash/hash.h"
#include "xff/presentation/fields/fields.h"
#include "xff/presentation/render/render.h"
#include "xff/vfs/filesystem.h"

namespace xff::render {
namespace {

struct OutputFs final : vfs::FileSystem {
  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return "alpha\nbeta\n"; }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::UnimplementedError("unused");
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return absl::UnimplementedError("unused");
  }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only"); }
};

void Record(benchmark::State& state) {
  const std::string prefix(static_cast<std::size_t>(state.range(0)), 'x');
  const std::string path = prefix + "\n\\\t";
  const bool escape = state.range(1) != 0;
  const std::string_view color = state.range(2) != 0 ? "1;34" : "";
  const Renderer renderer(Format::kPlain, escape ? PathEncoding::kEscape : PathEncoding::kRaw);
  const std::string body = escape ? prefix + R"(\n\\\t)" : path;
  const std::string expected = color.empty() ? body + "\n" : "\x1b[1;34m" + body + "\x1b[0m\n";
  if (renderer.Record(path, color) != expected) {
    state.SkipWithError("record bytes differ from independently constructed expectation");
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    auto record = renderer.Record(path, color);
    benchmark::DoNotOptimize(record);
  }
}

void HashFields(benchmark::State& state) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const bool warm = state.range(1) != 0;
  const OutputFs fs;
  const vfs::Metadata metadata;
  content::Snapshot snapshot;
  const fields::RenderContext context{
      .path = "virtual/file",
      .metadata = metadata,
      .fs = fs,
      .content = snapshot,
      .tz = absl::UTCTimeZone(),
  };
  std::string format;
  std::string expected;
  const std::string hash = hash::HashData("sha256", "alpha\nbeta\n").value_or("unsupported");
  for (std::size_t index = 0; index < count; ++index) {
    format.append("{hash}|");
    expected.append(hash).push_back('|');
  }
  const auto compiled = fields::Template::Compile(format);
  if (!compiled.Validate().ok() || compiled.Render(context) != expected) {
    state.SkipWithError("digest fields differ from known digest values");
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    if (!warm) {
      snapshot.Invalidate();
    }
    auto rendered = compiled.Render(context);
    benchmark::DoNotOptimize(rendered);
  }
}

}  // namespace
}  // namespace xff::render

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RegisterBenchmark("output/record", xff::render::Record)
      ->ArgsProduct({{1, 15, 22, 23, 64, 160, 4'096}, {0, 1}, {0, 1}})
      ->UseRealTime()
      ->Unit(benchmark::kNanosecond)
      ->Repetitions(9)
      ->MinTime(0.02)
      ->MinWarmUpTime(0.01);
  benchmark::RegisterBenchmark("output/hash-fields", xff::render::HashFields)
      ->ArgsProduct({{1, 4, 16}, {0, 1}})
      ->UseRealTime()
      ->Unit(benchmark::kNanosecond)
      ->Repetitions(9)
      ->MinTime(0.02)
      ->MinWarmUpTime(0.01);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
