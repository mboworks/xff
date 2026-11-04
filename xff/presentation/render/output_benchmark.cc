// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Isolate record construction and cached digest consumption from host filesystem costs.
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "xff/content/snapshot.h"
#include "xff/hash/hash.h"
#include "xff/presentation/fields/fields.h"
#include "xff/presentation/render/record_buffer.h"
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

void RawRecord(benchmark::State& state) {
  const std::string path = std::string(static_cast<std::size_t>(state.range(0)), 'x') + "\n\\\t";
  const char terminator = state.range(1) == 0 ? '\n' : '\0';
  const bool reuse = state.range(2) != 0;
  RawRecordBuffer buffer(terminator);
  const Renderer renderer(terminator == '\0' ? Format::kNul : Format::kPlain);
  std::string expected = path;
  expected.push_back(terminator);
  std::string actual;
  if (reuse) {
    buffer.EmitRecord(path, [&](std::string_view text) { actual.assign(text); });
  } else {
    actual = renderer.Record(path);
  }
  if (actual != expected) {
    state.SkipWithError("raw record differs from independent byte expectation");
    return;
  }
  const auto consume = [](std::string_view text) { benchmark::DoNotOptimize(text); };
  if (reuse) {
    for (auto iteration : state) {
      benchmark::DoNotOptimize(iteration);
      buffer.EmitRecord(path, consume);
    }
  } else {
    for (auto iteration : state) {
      benchmark::DoNotOptimize(iteration);
      const auto record = renderer.Record(path);
      consume(record);
    }
  }
}

struct EncodedProfile {
  std::string_view name;
  Format format;
  PathEncoding encoding = PathEncoding::kRaw;
  std::string_view color;
};

void EncodedRecord(benchmark::State& state, const EncodedProfile& profile) {
  const std::string path = std::string(static_cast<std::size_t>(state.range(0)), 'x') + ",\"\\\n\t";
  const bool reuse = state.range(1) != 0;
  EncodedRecordBuffer buffer(profile.format, profile.encoding);
  const Renderer renderer(profile.format, profile.encoding);
  const std::string expected = renderer.Record(path, profile.color);
  std::string actual;
  buffer.EmitRecord(path, [&](std::string_view text) { actual.assign(text); }, profile.color);
  if (actual != expected) {
    state.SkipWithError("encoded record differs from stateless byte reference");
    return;
  }
  const auto consume = [](std::string_view text) { benchmark::DoNotOptimize(text); };
  if (reuse) {
    for (auto iteration : state) {
      benchmark::DoNotOptimize(iteration);
      buffer.EmitRecord(path, consume, profile.color);
    }
  } else {
    for (auto iteration : state) {
      benchmark::DoNotOptimize(iteration);
      const auto record = renderer.Record(path, profile.color);
      consume(record);
    }
  }
}

void RegisterEncodedRecords() {
  static constexpr auto kProfiles = std::to_array<EncodedProfile>({
      {
          .name = "plain",
          .format = Format::kPlain,
      },
      {
          .name = "escaped",
          .format = Format::kPlain,
          .encoding = PathEncoding::kEscape,
      },
      {
          .name = "colored",
          .format = Format::kPlain,
          .color = "1;34",
      },
      {
          .name = "nul",
          .format = Format::kNul,
      },
      {
          .name = "csv",
          .format = Format::kCsv,
      },
      {
          .name = "tsv",
          .format = Format::kTsv,
      },
      {
          .name = "jsonl",
          .format = Format::kJsonl,
      },
  });
  for (const auto& profile : kProfiles) {
    const std::string name = "output/encoded-record/" + std::string(profile.name);
    benchmark::RegisterBenchmark(name, [profile](benchmark::State& state) { EncodedRecord(state, profile); })
        ->ArgsProduct({{0, 1, 15, 22, 23, 64, 160, 4'096}, {0, 1}})
        ->UseRealTime()
        ->Unit(benchmark::kNanosecond)
        ->Repetitions(9)
        ->MinTime(0.02)
        ->MinWarmUpTime(0.01);
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

// Explicit/partial/default specs isolate binding from digest work, with invalid context defaults
// proving that independent fields do not accidentally consume those defaults.
void HashBindingFields(benchmark::State& state) {
  struct Scenario {
    std::string_view format;
    std::string_view algorithm;
    std::string_view encoding;
    std::string_view prefix;
  };

  static constexpr auto kScenarios = std::to_array<Scenario>({
      {.format = "{hash}", .algorithm = "md5", .encoding = "base64"},
      {.format = "{hash:md5/base64}", .algorithm = "invalid", .encoding = "invalid"},
      {.format = "{hash:md5}", .algorithm = "invalid", .encoding = "base64"},
      {.format = "{hash:/base64}", .algorithm = "md5", .encoding = "invalid"},
      {.format = "{hash:s/^/SUM-/}", .algorithm = "md5", .encoding = "base64", .prefix = "SUM-"},
  });
  const auto& scenario = kScenarios.at(static_cast<std::size_t>(state.range(2)));
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
      .hash_algorithm = scenario.algorithm,
      .hash_encoding = scenario.encoding,
  };
  std::string format;
  std::string expected;
  const auto digest = hash::HashData("md5", "alpha\nbeta\n", hash::Encoding::kBase64).value_or("unsupported");
  for (std::size_t index = 0; index < count; ++index) {
    format.append(scenario.format).push_back('|');
    expected.append(scenario.prefix).append(digest).push_back('|');
  }
  const auto compiled = fields::Template::Compile(format);
  if (!compiled.Validate().ok() || compiled.Render(context) != expected) {
    state.SkipWithError("bound digest field differs from known fixture");
    return;
  }
  state.counters["template_storage_bytes"] = static_cast<double>(compiled.StorageBytes());
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    if (!warm) {
      snapshot.Invalidate();
    }
    auto rendered = compiled.Render(context);
    benchmark::DoNotOptimize(rendered);
  }
}

void CompileFields(benchmark::State& state) {
  static constexpr auto kFormats = std::to_array<std::string_view>({
      "{name}",
      "literal-{path}-{name}-{size}",
      "{hash}",
      "{hash:md5/base64}",
      "{hash:s/^/SUM-/}|{hash:sha256/hex}|{hash:/base64}",
  });
  const auto format = kFormats.at(static_cast<std::size_t>(state.range(0)));
  const auto expected = fields::Template::Compile(format).StorageBytes();
  state.counters["template_storage_bytes"] = static_cast<double>(expected);
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    auto compiled = fields::Template::Compile(format);
    benchmark::DoNotOptimize(compiled);
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
  benchmark::RegisterBenchmark("output/raw-record", xff::render::RawRecord)
      ->ArgsProduct({{0, 1, 8, 12, 15, 18, 22, 23, 160, 4'096}, {0, 1}, {0, 1}})
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
  benchmark::RegisterBenchmark("output/hash-binding", xff::render::HashBindingFields)
      ->ArgsProduct({{1, 16}, {0, 1}, {0, 1, 2, 3, 4}})
      ->UseRealTime()
      ->Unit(benchmark::kNanosecond)
      ->Repetitions(9)
      ->MinTime(0.02)
      ->MinWarmUpTime(0.01);
  benchmark::RegisterBenchmark("output/compile-fields", xff::render::CompileFields)
      ->DenseRange(0, 4)
      ->UseRealTime()
      ->Unit(benchmark::kNanosecond)
      ->Repetitions(9)
      ->MinTime(0.02)
      ->MinWarmUpTime(0.01);
  xff::render::RegisterEncodedRecords();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
