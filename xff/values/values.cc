// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xff/values/values.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"

namespace xff::values {

std::optional<bool> ParseBool(std::string_view value) {
  const std::string lower = absl::AsciiStrToLower(value);
  if (lower == "yes" || lower == "true" || lower == "on" || lower == "1") {
    return true;
  }
  if (lower == "no" || lower == "false" || lower == "off" || lower == "0") {
    return false;
  }
  return std::nullopt;
}

std::optional<Tristate> ParseTristate(std::string_view value) {
  const std::string lower = absl::AsciiStrToLower(value);
  if (lower == "auto") {
    return Tristate::kAuto;
  }
  if (lower == "always") {
    return Tristate::kOn;
  }
  if (lower == "never") {
    return Tristate::kOff;
  }
  if (const std::optional<bool> parsed = ParseBool(lower); parsed.has_value()) {
    return *parsed ? Tristate::kOn : Tristate::kOff;
  }
  return std::nullopt;
}

std::optional<std::uint64_t> ParseByteUnit(std::string_view unit) {
  using Unit = std::pair<std::string_view, std::uint64_t>;
  static constexpr std::array<Unit, 13> kUnits = {
      {
          {"b", 1},
          {"kb", 1'000},
          {"mb", 1'000'000},
          {"gb", 1'000'000'000},
          {"tb", 1'000'000'000'000},
          {"pb", 1'000'000'000'000'000},
          {"eb", 1'000'000'000'000'000'000},
          {"kib", 1ULL << 10U},
          {"mib", 1ULL << 20U},
          {"gib", 1ULL << 30U},
          {"tib", 1ULL << 40U},
          {"pib", 1ULL << 50U},
          {"eib", 1ULL << 60U},
      },
  };
  const std::string lower = absl::AsciiStrToLower(unit);
  // This is an array iterator, not an optionally borrowed object. Its pointer representation is an
  // implementation detail of the contiguous iterator.
  // NOLINTNEXTLINE(llvm-qualified-auto,readability-qualified-auto)
  const auto found =
      std::find_if(kUnits.begin(), kUnits.end(), [&lower](const Unit& entry) { return entry.first == lower; });
  return found == kUnits.end() ? std::nullopt : std::optional<std::uint64_t>(found->second);
}

std::optional<std::uint64_t> ParseByteSize(std::string_view value) {
  const std::size_t suffix_at = value.find_first_not_of("0123456789");
  if (suffix_at == 0 || suffix_at == std::string_view::npos) {
    return std::nullopt;
  }
  std::uint64_t number = 0;
  if (!absl::SimpleAtoi(value.substr(0, suffix_at), &number)) {
    return std::nullopt;
  }
  const std::optional<std::uint64_t> unit = ParseByteUnit(value.substr(suffix_at));
  const std::uint64_t bytes_per_unit = unit.value_or(0);
  if (bytes_per_unit == 0 || number > std::numeric_limits<std::uint64_t>::max() / bytes_per_unit) {
    return std::nullopt;
  }
  return number * bytes_per_unit;
}

}  // namespace xff::values
