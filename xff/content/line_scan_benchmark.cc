// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "benchmark/benchmark.h"
#include "xff/content/line_match.h"
#include "xff/vfs/read_source.h"

namespace {
// The same bytes and owning Read implementation back both capabilities. Unlike the
// engine's memory source, this fixture can deliberately force the legacy path.
class FixtureStream final : public xff::vfs::ReadStream {
 public:
  FixtureStream(std::string_view data, std::size_t chunk, bool direct) : data_(data), chunk_(chunk), direct_(direct) {}

  absl::StatusOr<std::string> Read(std::size_t maximum) override {
    const auto size = std::min({maximum, chunk_, data_.size() - position_});
    const auto begin = position_;
    position_ += size;
    return std::string(data_.substr(begin, size));
  }

  bool SupportsReadInto() const noexcept override { return direct_; }

  absl::StatusOr<std::size_t> ReadInto(std::span<char> buffer) override {
    const auto size = std::min({buffer.size(), chunk_, data_.size() - position_});
    data_.copy(buffer.data(), size, position_);
    position_ += size;
    return size;
  }

 private:
  std::string_view data_;
  std::size_t chunk_;
  bool direct_;
  std::size_t position_ = 0;
};

struct Observed {
  std::size_t lines = 0;
  std::size_t bytes = 0;
};

void Scan(benchmark::State& state, bool direct, bool stop) {
  const auto size = static_cast<std::size_t>(state.range(0));
  const auto chunk = static_cast<std::size_t>(state.range(1));
  std::string text(size, 'x');
  for (std::size_t index = 79; index < size; index += 80) {
    text[index] = '\n';
  }
  const auto expected_lines = (size / 80) + (size % 80 == 0 ? 0UZ : 1UZ);
  const auto expected_bytes = size - (size / 80);
  const auto check = [&] {
    FixtureStream stream(text, chunk, direct);
    Observed observed;
    const auto result = xff::content::ScanLines(stream, 8'000, [&](std::size_t, std::string_view line) {
      ++observed.lines;
      observed.bytes += line.size();
      return !stop;
    });
    return result.ok() && !result->binary && observed.lines == (stop ? std::min(expected_lines, 1UZ) : expected_lines)
           && observed.bytes == (stop ? std::min(expected_bytes, 79UZ) : expected_bytes);
  };
  if (!check()) {
    state.SkipWithError("scanner output differs from fixture line/byte oracle");
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    FixtureStream stream(text, chunk, direct);
    Observed observed;
    const auto result = xff::content::ScanLines(stream, 8'000, [&](std::size_t, std::string_view line) {
      ++observed.lines;
      observed.bytes += line.size();
      return !stop;
    });
    if (!result.ok() || result->binary) {
      state.SkipWithError("scanner failed");
      break;
    }
    benchmark::DoNotOptimize(observed);
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(size));
}
}  // namespace

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  benchmark::RegisterBenchmark("legacy/all", Scan, false, false)
      ->ArgsProduct({{0, 256, 4'096, 65'536, 131'072}, {8'192, 65'536}})
      ->UseRealTime();
  benchmark::RegisterBenchmark("direct/all", Scan, true, false)
      ->ArgsProduct({{0, 256, 4'096, 65'536, 131'072}, {8'192, 65'536}})
      ->UseRealTime();
  benchmark::RegisterBenchmark("legacy/stop", Scan, false, true)
      ->ArgsProduct({{256, 65'536, 131'072}, {8'192, 65'536}})
      ->UseRealTime();
  benchmark::RegisterBenchmark("direct/stop", Scan, true, true)
      ->ArgsProduct({{256, 65'536, 131'072}, {8'192, 65'536}})
      ->UseRealTime();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
