// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Candidate-generation design spike for near-duplicate grouping:
//
//   bazel run -c opt //xff/matching/similarity:near_duplicate_benchmark
//
// The inverted index is intentionally not production code. Its contract is the design boundary:
// candidates may be false positives, but only the exact WordShinglePercent verifier may emit a pair.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "benchmark/benchmark.h"
#include "xff/matching/similarity/similarity.h"

namespace xff::similarity {
namespace {

// NOLINTBEGIN(*-magic-numbers)

constexpr std::size_t kShingleWidth = 3;
constexpr int kThreshold = 80;

using Pair = std::pair<std::size_t, std::size_t>;

struct PairHash {
  std::size_t operator()(const Pair& pair) const { return (pair.first * 1'000'003) ^ pair.second; }
};

using PairSet = absl::flat_hash_set<Pair, PairHash>;

struct Result {
  PairSet matches;
  std::size_t compared = 0;
};

std::vector<std::string> Words(std::string_view text) {
  std::vector<std::string> words;
  std::string word;
  for (const char value : text) {
    const auto byte = static_cast<unsigned char>(value);
    if (byte >= 0x80 || absl::ascii_isalnum(value)) {
      word.push_back(absl::ascii_tolower(value));
    } else if (!word.empty()) {
      words.push_back(std::move(word));
      word.clear();
    }
  }
  if (!word.empty()) {
    words.push_back(std::move(word));
  }
  return words;
}

absl::flat_hash_set<std::string> Shingles(std::string_view text) {
  const std::vector<std::string> words = Words(text);
  absl::flat_hash_set<std::string> result;
  if (words.empty()) {
    return result;
  }
  const std::size_t width = std::min(kShingleWidth, words.size());
  for (std::size_t begin = 0; begin + width <= words.size(); ++begin) {
    result.insert(
        absl::StrJoin(
            words.begin() + static_cast<std::ptrdiff_t>(begin),
            words.begin() + static_cast<std::ptrdiff_t>(begin + width), std::string_view("\0", 1)));
  }
  return result;
}

std::vector<std::string> Corpus(std::size_t count, bool shared_boilerplate) {
  std::vector<std::string> documents;
  documents.reserve(count);
  for (std::size_t document = 0; document < count; ++document) {
    const std::size_t cluster = document / 8;
    const std::size_t variant = document % 8;
    std::string text = shared_boilerplate ? "common header boilerplate " : "";
    for (std::size_t word = 0; word < 128; ++word) {
      if (word % 24 == variant) {
        absl::StrAppend(&text, "edit", variant, "_", word, " ");
      } else {
        absl::StrAppend(&text, "cluster", cluster, "token", word, " ");
      }
    }
    documents.push_back(std::move(text));
  }
  return documents;
}

Result AllPairs(const std::vector<std::string>& documents) {
  PairSet matches;
  std::size_t compared = 0;
  for (std::size_t lhs = 0; lhs < documents.size(); ++lhs) {
    for (std::size_t rhs = lhs + 1; rhs < documents.size(); ++rhs) {
      ++compared;
      if (WordShinglePercent(documents[lhs], documents[rhs], kShingleWidth) >= kThreshold) {
        matches.emplace(lhs, rhs);
      }
    }
  }
  return {.matches = std::move(matches), .compared = compared};
}

Result Indexed(const std::vector<std::string>& documents) {
  absl::flat_hash_map<std::string, std::vector<std::size_t>> postings;
  std::vector<std::size_t> empty;
  for (std::size_t document = 0; document < documents.size(); ++document) {
    const absl::flat_hash_set<std::string> shingles = Shingles(documents[document]);
    if (shingles.empty()) {
      empty.push_back(document);
    }
    for (const std::string& shingle : shingles) {
      postings[shingle].push_back(document);
    }
  }

  PairSet candidates;
  const auto add_pairs = [&candidates](const std::vector<std::size_t>& documents_with_shingle) {
    for (std::size_t lhs = 0; lhs < documents_with_shingle.size(); ++lhs) {
      for (std::size_t rhs = lhs + 1; rhs < documents_with_shingle.size(); ++rhs) {
        candidates.emplace(documents_with_shingle[lhs], documents_with_shingle[rhs]);
      }
    }
  };
  for (const auto& posting : postings) {
    add_pairs(posting.second);
  }
  add_pairs(empty);  // Two empty shingle sets have exact similarity 100.

  PairSet matches;
  const std::size_t compared = candidates.size();
  for (const auto& [lhs, rhs] : candidates) {
    if (WordShinglePercent(documents[lhs], documents[rhs], kShingleWidth) >= kThreshold) {
      matches.emplace(lhs, rhs);
    }
  }
  return {.matches = std::move(matches), .compared = compared};
}

void VerifyCandidateContract(const std::vector<std::string>& documents) {
  if (AllPairs(documents).matches != Indexed(documents).matches) {
    std::abort();
  }
}

void BmAllPairs(benchmark::State& state, bool shared_boilerplate) {
  const std::vector<std::string> documents = Corpus(static_cast<std::size_t>(state.range(0)), shared_boilerplate);
  VerifyCandidateContract(documents);
  // google-benchmark's loop value drives iteration and is deliberately not read.
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (const auto unused : state) {
    Result result = AllPairs(documents);
    benchmark::DoNotOptimize(result.matches);
    state.counters["verified_pairs"] = static_cast<double>(result.compared);
  }
}

void BmInvertedIndex(benchmark::State& state, bool shared_boilerplate) {
  const std::vector<std::string> documents = Corpus(static_cast<std::size_t>(state.range(0)), shared_boilerplate);
  VerifyCandidateContract(documents);
  // google-benchmark's loop value drives iteration and is deliberately not read.
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (const auto unused : state) {
    Result result = Indexed(documents);
    benchmark::DoNotOptimize(result.matches);
    state.counters["verified_pairs"] = static_cast<double>(result.compared);
  }
}

void RegisterAll() {
  struct Shape {
    std::string_view name;
    bool shared_boilerplate;
  };

  static constexpr std::array kShapes = std::to_array<Shape>({
      {.name = "clustered", .shared_boilerplate = false},
      {.name = "boilerplate", .shared_boilerplate = true},
  });
  for (const Shape& shape : kShapes) {
    benchmark::RegisterBenchmark(
        absl::StrCat("BmAllPairs/", shape.name),
        [shared_boilerplate = shape.shared_boilerplate](benchmark::State& state) {
          BmAllPairs(state, shared_boilerplate);
        })
        ->Arg(64)
        ->Arg(256)
        ->Arg(1'024)
        ->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark(
        absl::StrCat("BmInvertedIndex/", shape.name),
        [shared_boilerplate = shape.shared_boilerplate](benchmark::State& state) {
          BmInvertedIndex(state, shared_boilerplate);
        })
        ->Arg(64)
        ->Arg(256)
        ->Arg(1'024)
        ->Unit(benchmark::kMillisecond);
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
