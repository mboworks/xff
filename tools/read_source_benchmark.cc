// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "xff/vfs/read_source.h"

namespace xff::vfs {
namespace {
constexpr std::size_t kChunk = 64UZ * 1'024;

class BenchmarkStream final : public ReadStream {
 public:
  BenchmarkStream(std::shared_ptr<const std::string> bytes, bool caller_owned)
      : bytes_(std::move(bytes)), caller_owned_(caller_owned) {}

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    const auto count = std::min({max_bytes, kChunk, bytes_->size() - offset_});
    auto result = bytes_->substr(offset_, count);
    offset_ += count;
    return result;
  }

  bool SupportsReadInto() const noexcept override { return caller_owned_; }

  absl::StatusOr<std::size_t> ReadInto(std::span<char> buffer) override {
    const auto count = std::min({buffer.size(), kChunk, bytes_->size() - offset_});
    std::ranges::copy(std::string_view(*bytes_).substr(offset_, count), buffer.begin());
    offset_ += count;
    return count;
  }

 private:
  std::shared_ptr<const std::string> bytes_;
  bool caller_owned_;
  std::size_t offset_ = 0;
};

class BenchmarkSource final : public ReadSource {
 public:
  BenchmarkSource(std::string bytes, bool caller_owned)
      : bytes_(std::make_shared<const std::string>(std::move(bytes))), caller_owned_(caller_owned) {}

  absl::StatusOr<std::unique_ptr<ReadStream>> Open() const override {
    return std::make_unique<BenchmarkStream>(bytes_, caller_owned_);
  }

 private:
  std::shared_ptr<const std::string> bytes_;
  bool caller_owned_;
};

enum class SourceKind { kLegacy, kCallerOwned, kMemory };

SharedReadSource MakeSource(SourceKind kind, std::string bytes) {
  if (kind == SourceKind::kMemory) {
    return MemoryReadSource(std::move(bytes));
  }
  return std::make_shared<BenchmarkSource>(std::move(bytes), kind == SourceKind::kCallerOwned);
}

void Measure(benchmark::State& state, SourceKind kind, bool range) {
  const auto count = static_cast<std::size_t>(state.range(0));
  std::string expected(count, 'x');
  if (!expected.empty()) {
    expected.front() = 'a';
    expected.back() = 'z';
  }
  // Sequential custom sources replay this small prefix; memory sources seek directly.
  const auto source = MakeSource(kind, range ? absl::StrCat("prefix:", expected, ":suffix") : expected);
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores): benchmark controls the measured iterations.
  for (const auto unused : state) {
    if (range) {
      auto result = ReadSourceRange(*source, 7, count);
      if (!result.ok() || result->bytes != expected) {
        state.SkipWithError("range differs from fixture");
        return;
      }
      benchmark::DoNotOptimize(result);
    } else {
      auto result = ReadSourceBytes(*source, count);
      if (!result.ok() || *result != expected) {
        state.SkipWithError("materialization differs from fixture");
        return;
      }
      benchmark::DoNotOptimize(result);
    }
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

void RegisterAll() {
  struct SourceCase {
    std::string_view name;
    SourceKind kind;
  };

  constexpr auto kSources = std::to_array<SourceCase>({
      {.name = "legacy", .kind = SourceKind::kLegacy},
      {.name = "caller-owned", .kind = SourceKind::kCallerOwned},
      {.name = "memory", .kind = SourceKind::kMemory},
  });
  constexpr auto kCounts = std::to_array<std::int64_t>({0, 10, 4'096, 65'536, 1'048'576, 16'777'216});
  for (const auto& source : kSources) {
    for (const bool range : {false, true}) {
      const std::string name = absl::StrCat("source/", source.name, "/", range ? "range" : "bytes");
      // XFF_ABI_POINTER: Google Benchmark returns a borrowed mutable registration object.
      auto* registration = benchmark::RegisterBenchmark(name, Measure, source.kind, range);
      for (const auto count : kCounts) {
        registration->Arg(count);
      }
      registration->Unit(benchmark::kMicrosecond)->UseRealTime()->MinTime(0.05)->MinWarmUpTime(0.05)->Repetitions(9);
    }
  }
}
}  // namespace
}  // namespace xff::vfs

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  xff::vfs::RegisterAll();
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
