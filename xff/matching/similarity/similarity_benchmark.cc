// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Reproducible cost model for the exact word-shingle verifier:
//
//   bazel run -c opt //xff/matching/similarity:similarity_benchmark

#include <algorithm>
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

void BmWordShinglePercent(
    benchmark::State& state,
    std::size_t changed_every,
    std::size_t padding = 0,
    bool reverse = false) {
  const auto words = static_cast<std::size_t>(state.range(0));
  const auto width = static_cast<std::size_t>(state.range(1));
  const std::string lhs = Document(words, reverse ? changed_every : 0, padding);
  const std::string rhs = Document(words, reverse ? 0 : changed_every, padding);
  // google-benchmark's loop value drives iteration and is deliberately not read.
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (const auto unused : state) {
    benchmark::DoNotOptimize(WordShinglePercent(lhs, rhs, width));
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(words * 2));
}

void BmReferencePercent(
    benchmark::State& state,
    std::size_t changed_every,
    std::size_t padding = 0,
    bool reverse = false) {
  const auto words = static_cast<std::size_t>(state.range(0));
  const auto width = static_cast<std::size_t>(state.range(1));
  const std::string lhs = Document(words, reverse ? changed_every : 0, padding);
  const std::string rhs = Document(words, reverse ? 0 : changed_every, padding);
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

enum class WordSetMode { kFreshPercent, kFreshThreshold, kReferencePercent, kReferenceThreshold };

void MeasureWordInsertion(benchmark::State& state, WordSetMode mode) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto unique = static_cast<std::size_t>(state.range(1));
  const auto padding = static_cast<std::size_t>(state.range(2));
  std::string source;
  std::string candidate;
  for (std::size_t index = 0; index < count; ++index) {
    const auto number = std::to_string(index % unique);
    source.append(" word").append(number).append(padding, 'x');
    candidate.append(",WORD").append(number).append(padding, 'X');
  }
  const ByteWordShingleReference reference(source, 1);
  if (WordShinglePercent(candidate, source, 1) != 100 || reference.Percent(candidate) != 100
      || !WordShingleAtLeast(candidate, source, 1, 100) || !reference.AtLeast(candidate, 100)) {
    state.SkipWithError("single-word normalization differs for equivalent word sets");
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    switch (mode) {
      case WordSetMode::kFreshPercent: benchmark::DoNotOptimize(WordShinglePercent(candidate, source, 1)); break;
      case WordSetMode::kFreshThreshold: benchmark::DoNotOptimize(WordShingleAtLeast(candidate, source, 1, 100)); break;
      case WordSetMode::kReferencePercent: benchmark::DoNotOptimize(reference.Percent(candidate)); break;
      case WordSetMode::kReferenceThreshold: benchmark::DoNotOptimize(reference.AtLeast(candidate, 100)); break;
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

void RegisterWordInsertion() {
  struct Mode {
    std::string_view name;
    WordSetMode mode;
  };

  static constexpr auto kModes = std::to_array<Mode>({
      {
          .name = "fresh-percent",
          .mode = WordSetMode::kFreshPercent,
      },
      {
          .name = "fresh-threshold",
          .mode = WordSetMode::kFreshThreshold,
      },
      {
          .name = "reference-percent",
          .mode = WordSetMode::kReferencePercent,
      },
      {
          .name = "reference-threshold",
          .mode = WordSetMode::kReferenceThreshold,
      },
  });
  for (const auto& entry : kModes) {
    const std::string name = "similarity/word-insertion/" + std::string(entry.name);
    benchmark::RegisterBenchmark(
        name, [mode = entry.mode](benchmark::State& state) { MeasureWordInsertion(state, mode); })
        ->ArgsProduct({{1, 32, 2'048}, {1, 32, 2'048}, {0, 32, 256}})
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond)
        ->Repetitions(9)
        ->MinTime(0.02)
        ->MinWarmUpTime(0.01);
  }
}

void RegisterAll() {
  struct Shape {
    std::string_view name;
    std::size_t changed_every;
    std::size_t padding = 0;
    bool reverse = false;
  };

  static constexpr std::array kShapes = std::to_array<Shape>({
      {.name = "equal", .changed_every = 0},
      {.name = "nearby", .changed_every = 16},
      {.name = "disjoint", .changed_every = 1},
      {.name = "nearby-padding64", .changed_every = 16, .padding = 64},
      {.name = "disjoint-padding64", .changed_every = 1, .padding = 64},
      {.name = "nearby-padding256", .changed_every = 16, .padding = 256},
      {.name = "disjoint-padding256", .changed_every = 1, .padding = 256},
      {.name = "nearby-reversed", .changed_every = 16, .padding = 0, .reverse = true},
      {.name = "disjoint-reversed", .changed_every = 1, .padding = 0, .reverse = true},
      {.name = "nearby-padding64-reversed", .changed_every = 16, .padding = 64, .reverse = true},
      {.name = "disjoint-padding64-reversed", .changed_every = 1, .padding = 64, .reverse = true},
      {.name = "nearby-padding256-reversed", .changed_every = 16, .padding = 256, .reverse = true},
      {.name = "disjoint-padding256-reversed", .changed_every = 1, .padding = 256, .reverse = true},
  });
  for (const Shape& shape : kShapes) {
    benchmark::RegisterBenchmark(
        absl::StrCat("BmWordShinglePercent/", shape.name),
        [changed_every = shape.changed_every, padding = shape.padding, reverse = shape.reverse](
            benchmark::State& state) { BmWordShinglePercent(state, changed_every, padding, reverse); })
        ->Apply(Args);
    benchmark::RegisterBenchmark(
        absl::StrCat("BmReferencePercent/", shape.name),
        [changed_every = shape.changed_every, padding = shape.padding, reverse = shape.reverse](
            benchmark::State& state) { BmReferencePercent(state, changed_every, padding, reverse); })
        ->Apply(Args);
  }
}

enum class ThresholdShape {
  kFull,
  kNear,
  kHalf,
  kDisjoint,
  kLeftSmall,
  kRightSmall,
};

std::string ThresholdDocument(std::size_t common, std::size_t unique, std::string_view prefix, char separator) {
  std::string text;
  for (std::size_t i = 0; i < common; ++i) {
    absl::StrAppend(&text, "shared", i);
    text.push_back(separator);
  }
  for (std::size_t i = 0; i < unique; ++i) {
    absl::StrAppend(&text, prefix, i);
    text.push_back(separator);
  }
  return text;
}

bool MeetsThreshold(std::string_view left, std::string_view right, std::size_t width, int threshold) {
  return WordShingleAtLeast(left, right, width, threshold);
}

bool MeetsReferenceThreshold(const ByteWordShingleReference& reference, std::string_view candidate, int threshold) {
  return reference.AtLeast(candidate, threshold);
}

void ThresholdVerification(benchmark::State& state, ThresholdShape shape) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto width = static_cast<std::size_t>(state.range(1));
  const int threshold = static_cast<int>(state.range(2));
  const bool cached = state.range(3) != 0;
  std::size_t left_count = count;
  std::size_t right_count = count;
  std::size_t common = 0;
  switch (shape) {
    case ThresholdShape::kFull: common = count; break;
    case ThresholdShape::kNear: common = count - 1; break;
    case ThresholdShape::kHalf: common = count / 2; break;
    case ThresholdShape::kDisjoint: break;
    case ThresholdShape::kLeftSmall:
      left_count = std::max(1UZ, count / 32);
      common = left_count;
      break;
    case ThresholdShape::kRightSmall:
      right_count = std::max(1UZ, count / 32);
      common = right_count;
      break;
  }
  const auto left = ThresholdDocument(common, left_count - common, "left", ' ');
  const auto right = ThresholdDocument(common, right_count - common, "right", ',');
  const ByteWordShingleReference reference(right, width);
  const bool expected = WordShinglePercent(left, right, width) >= threshold;
  if (MeetsThreshold(left, right, width, threshold) != expected
      || MeetsReferenceThreshold(reference, left, threshold) != expected) {
    state.SkipWithError("boolean threshold differs from exact rounded percentage");
    return;
  }
  if (cached) {
    for (auto iteration : state) {
      benchmark::DoNotOptimize(iteration);
      auto matched = MeetsReferenceThreshold(reference, left, threshold);
      benchmark::DoNotOptimize(matched);
    }
  } else {
    for (auto iteration : state) {
      benchmark::DoNotOptimize(iteration);
      auto matched = MeetsThreshold(left, right, width, threshold);
      benchmark::DoNotOptimize(matched);
    }
  }
}

void RegisterThresholds() {
  struct Shape {
    std::string_view name;
    ThresholdShape shape;
  };

  static constexpr auto kShapes = std::to_array<Shape>({
      {
          .name = "full",
          .shape = ThresholdShape::kFull,
      },
      {
          .name = "near",
          .shape = ThresholdShape::kNear,
      },
      {
          .name = "half",
          .shape = ThresholdShape::kHalf,
      },
      {
          .name = "disjoint",
          .shape = ThresholdShape::kDisjoint,
      },
      {
          .name = "left-small",
          .shape = ThresholdShape::kLeftSmall,
      },
      {
          .name = "right-small",
          .shape = ThresholdShape::kRightSmall,
      },
  });
  for (const auto& shape : kShapes) {
    benchmark::RegisterBenchmark(
        absl::StrCat("threshold/", shape.name),
        [shape = shape.shape](benchmark::State& state) { ThresholdVerification(state, shape); })
        ->ArgsProduct({{32, 1'000}, {1, 3}, {50, 100}, {0, 1}})
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond)
        ->Repetitions(9)
        ->MinTime(0.02)
        ->MinWarmUpTime(0.01);
  }
}

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace xff::similarity

int main(int argc, char** argv) {
  xff::similarity::RegisterAll();
  xff::similarity::RegisterWordInsertion();
  xff::similarity::RegisterThresholds();
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
