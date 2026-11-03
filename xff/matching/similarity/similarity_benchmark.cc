// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Reproducible cost model for the exact word-shingle verifier:
//
//   bazel run -c opt //xff/matching/similarity:similarity_benchmark

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "xff/matching/similarity/similarity.h"

namespace xff::similarity {
namespace {

// NOLINTBEGIN(*-magic-numbers)

std::string Document(std::size_t words, std::size_t changed_every, std::size_t padding = 0) {
  std::string result;
  result.reserve(words * (padding + 12));
  for (std::size_t index = 0; index < words; ++index) {
    if (changed_every != 0 && index % changed_every == 0) {
      absl::StrAppend(&result, "changed", index);
    } else {
      absl::StrAppend(&result, "token", index % 257);
    }
    result.append(padding, 'x');
    result.push_back(' ');
  }
  return result;
}

void BmWordShinglePercent(benchmark::State& state, std::size_t changed_every, std::size_t padding = 0) {
  const auto words = static_cast<std::size_t>(state.range(0));
  const auto width = static_cast<std::size_t>(state.range(1));
  const std::string lhs = Document(words, 0, padding);
  const std::string rhs = Document(words, changed_every, padding);
  // google-benchmark's loop value drives iteration and is deliberately not read.
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (const auto unused : state) {
    benchmark::DoNotOptimize(WordShinglePercent(lhs, rhs, width));
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(words * 2));
}

void BmReferencePercent(benchmark::State& state, std::size_t changed_every, std::size_t padding = 0) {
  const auto words = static_cast<std::size_t>(state.range(0));
  const auto width = static_cast<std::size_t>(state.range(1));
  const std::string lhs = Document(words, 0, padding);
  const std::string rhs = Document(words, changed_every, padding);
  const ByteWordShingleReference reference(rhs, width);
  // google-benchmark's loop value drives iteration and is deliberately not read.
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (const auto unused : state) {
    benchmark::DoNotOptimize(reference.Percent(lhs));
  }
  state.counters["reference_tracked_capacity"] = static_cast<double>(reference.StorageBytes());
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(words * 2));
}

// XFF_ABI_POINTER: google-benchmark registration API requires a mutable benchmark object.
void Args(benchmark::Benchmark* benchmark) {
  static constexpr std::array kWordCounts = std::to_array<std::int64_t>({32, 256, 2'048});
  static constexpr std::array kWidths = std::to_array<std::int64_t>({1, 3, 5});
  for (const std::int64_t words : kWordCounts) {
    for (const std::int64_t width : kWidths) {
      benchmark->Args({words, width});
    }
  }
}

void RegisterAll() {
  struct Shape {
    std::string_view name;
    std::size_t changed_every;
    std::size_t padding = 0;
  };

  static constexpr std::array kShapes = std::to_array<Shape>({
      {.name = "equal", .changed_every = 0},
      {.name = "nearby", .changed_every = 16},
      {.name = "disjoint", .changed_every = 1},
      {.name = "nearby-padding64", .changed_every = 16, .padding = 64},
      {.name = "disjoint-padding64", .changed_every = 1, .padding = 64},
      {.name = "nearby-padding256", .changed_every = 16, .padding = 256},
      {.name = "disjoint-padding256", .changed_every = 1, .padding = 256},
  });
  for (const Shape& shape : kShapes) {
    benchmark::RegisterBenchmark(
        absl::StrCat("BmWordShinglePercent/", shape.name),
        [changed_every = shape.changed_every, padding = shape.padding](benchmark::State& state) {
          BmWordShinglePercent(state, changed_every, padding);
        })
        ->Apply(Args);
    benchmark::RegisterBenchmark(
        absl::StrCat("BmReferencePercent/", shape.name),
        [changed_every = shape.changed_every, padding = shape.padding](benchmark::State& state) {
          BmReferencePercent(state, changed_every, padding);
        })
        ->Apply(Args);
  }
}

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace xff::similarity

int main(int argc, char** argv) {
  xff::similarity::RegisterAll();
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
