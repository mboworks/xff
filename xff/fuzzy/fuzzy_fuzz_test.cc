// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>

#include "xff/fuzzy/fuzzy.h"

namespace {

constexpr std::size_t kMaxInputBytes = 1'024;
constexpr auto kFoldCaseOptions = std::to_array<bool>({false, true});

void RequirePercent(const std::optional<int> score) {
  if (score.has_value() && (*score < 0 || *score > 100)) {
    std::abort();
  }
}

void RequirePercent(const int score) {
  if (score < 0 || score > 100) {
    std::abort();
  }
}

}  // namespace

// XFF_ABI_POINTER: rules_fuzzing requires libFuzzer's C entry-point signature.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > kMaxInputBytes) {
    return 0;
  }
  // libFuzzer exposes object-representation bytes; fuzzy matching deliberately accepts arbitrary
  // byte strings rather than requiring UTF-8.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const std::size_t separator = input.find('\n');
  const std::string_view pattern = input.substr(0, separator);
  const std::string_view text = separator == std::string_view::npos ? std::string_view{} : input.substr(separator + 1);

  for (const bool fold_case : kFoldCaseOptions) {
    const bool matches = xff::fuzzy::Matches(pattern, text, fold_case);
    const std::optional<int> percent = xff::fuzzy::Percent(pattern, text, fold_case);
    const std::optional<int> sequence = xff::fuzzy::SequencePercent(pattern, text, fold_case);
    RequirePercent(percent);
    RequirePercent(sequence);
    const xff::fuzzy::FzfQuery query(pattern);
    const auto ranked = query.Percent(text, fold_case);
    RequirePercent(ranked);
    if (query.Matches(text, fold_case) != ranked.has_value()) {
      std::abort();
    }
    RequirePercent(xff::fuzzy::LevenshteinPercent(pattern, text, fold_case));
    RequirePercent(xff::fuzzy::ShinglePercent(pattern, text, fold_case));
    if (matches != percent.has_value() || matches != sequence.has_value()) {
      std::abort();
    }
  }
  return 0;
}
