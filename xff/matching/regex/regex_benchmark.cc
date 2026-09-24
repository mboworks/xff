// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
// Manual production-backend benchmark; all matching inputs stay in memory.
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

#include "absl/strings/str_format.h"
#include "xff/matching/regex/regex.h"

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::string_view kPattern =
    R"((?:WARN|ERROR) service=[a-z][a-z0-9_-]{2,15} request=[0-9a-f]{8} status=(?:4[0-9]{2}|5[0-9]{2}) latency=[0-9]{1,5}\.[0-9]{2}ms(?: retry=[1-9])?)";
constexpr std::string_view kMatch = "ERROR service=api_worker request=12abcdef status=503 latency=123.45ms retry=2\n";
constexpr std::string_view kMiss = "ERROR service=api_worker request=12abcdef status=503 latency=123.45xs retry=2\n";
constexpr auto kGrammars = std::to_array({xff::regex::Grammar::kRe2, xff::regex::Grammar::kPcre2});
constexpr auto kNames = std::to_array<std::string_view>({"RE2", "PCRE2"});
constexpr std::size_t kCompiles = 100;
constexpr std::size_t kSubjects = 1'000;

std::string Subject(bool match) {
  std::string result;
  result.reserve(16'384);
  while (result.size() < 16'384) {
    result.append(kMiss);
  }
  result.resize(16'384 - kMatch.size() - 1);
  result += '\n';
  result.append(match ? kMatch : kMiss);
  return result;
}

double Milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
}  // namespace

int main() {
  const auto subjects = std::to_array({Subject(true), Subject(false)});
  std::cout << "round,engine,compile_ms,match_ms,subjects,matches\n";
  for (std::size_t round = 0; round < 10; ++round) {
    for (std::size_t offset = 0; offset < kGrammars.size(); ++offset) {
      const std::size_t engine = (round + offset) % kGrammars.size();
      const auto compile_start = Clock::now();
      for (std::size_t repeat = 0; repeat < kCompiles; ++repeat) {
        const auto compiled = xff::regex::Matcher::Compile(kPattern, false, kGrammars.at(engine));
        if (!compiled.ok()) {
          return 1;
        }
      }
      const double compile_ms = Milliseconds(compile_start) / static_cast<double>(kCompiles);
      const auto compiled = xff::regex::Matcher::Compile(kPattern, false, kGrammars.at(engine));
      if (!compiled.ok()) {
        return 1;
      }
      const auto match_start = Clock::now();
      std::size_t matches = 0;
      for (std::size_t entry = 0; entry < kSubjects; ++entry) {
        const bool matched = compiled->PartialMatch(subjects.at(entry % subjects.size()));
        if (matched != (entry % subjects.size() == 0)) {
          return 2;
        }
        matches += static_cast<std::size_t>(matched);
      }
      const double match_ms = Milliseconds(match_start);
      std::cout << absl::StrFormat(
          "%d,%s,%.6f,%.6f,%d,%d\n", round, kNames.at(engine), compile_ms, match_ms, kSubjects, matches);
    }
  }
}
