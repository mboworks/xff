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

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::values {
namespace {

using ::testing::Eq;
using ::testing::Optional;

struct ValuesTest : ::testing::Test {};

TEST_F(ValuesTest, ParseBoolAcceptsTheTrueSpellings) {
  EXPECT_THAT(ParseBool("yes"), Optional(true));
  EXPECT_THAT(ParseBool("true"), Optional(true));
  EXPECT_THAT(ParseBool("on"), Optional(true));  // switch-shaped flags spell it this way
  EXPECT_THAT(ParseBool("1"), Optional(true));
}

TEST_F(ValuesTest, ParseBoolAcceptsTheFalseSpellings) {
  EXPECT_THAT(ParseBool("no"), Optional(false));
  EXPECT_THAT(ParseBool("false"), Optional(false));
  EXPECT_THAT(ParseBool("off"), Optional(false));
  EXPECT_THAT(ParseBool("0"), Optional(false));
}

TEST_F(ValuesTest, ParseBoolIsCaseInsensitive) {
  EXPECT_THAT(ParseBool("YES"), Optional(true));
  EXPECT_THAT(ParseBool("True"), Optional(true));
  EXPECT_THAT(ParseBool("No"), Optional(false));
}

TEST_F(ValuesTest, ParseBoolRejectsEverythingElse) {
  EXPECT_THAT(ParseBool("auto"), Eq(std::nullopt));    // auto is tri-state only
  EXPECT_THAT(ParseBool("always"), Eq(std::nullopt));  // color idiom is tri-state only
  EXPECT_THAT(ParseBool("2"), Eq(std::nullopt));
  EXPECT_THAT(ParseBool(""), Eq(std::nullopt));
  EXPECT_THAT(ParseBool("y"), Eq(std::nullopt));
}

TEST_F(ValuesTest, ParseTristateAcceptsAuto) {
  EXPECT_THAT(ParseTristate("auto"), Optional(Tristate::kAuto));
  EXPECT_THAT(ParseTristate("AUTO"), Optional(Tristate::kAuto));
}

TEST_F(ValuesTest, ParseTristateMapsOnAndOffSpellings) {
  EXPECT_THAT(ParseTristate("always"), Optional(Tristate::kOn));
  EXPECT_THAT(ParseTristate("yes"), Optional(Tristate::kOn));
  EXPECT_THAT(ParseTristate("true"), Optional(Tristate::kOn));
  EXPECT_THAT(ParseTristate("1"), Optional(Tristate::kOn));
  EXPECT_THAT(ParseTristate("never"), Optional(Tristate::kOff));
  EXPECT_THAT(ParseTristate("no"), Optional(Tristate::kOff));
  EXPECT_THAT(ParseTristate("false"), Optional(Tristate::kOff));
  EXPECT_THAT(ParseTristate("0"), Optional(Tristate::kOff));
}

TEST_F(ValuesTest, ParseTristateRejectsUnknown) {
  EXPECT_THAT(ParseTristate("sometimes"), Eq(std::nullopt));
  EXPECT_THAT(ParseTristate(""), Eq(std::nullopt));
}

TEST_F(ValuesTest, ParseByteUnitDistinguishesSiAndIecScales) {
  using Case = std::pair<std::string_view, std::uint64_t>;
  static constexpr std::array<Case, 13> kCases = {
      {
          {"B", 1},
          {"kB", 1'000},
          {"MB", 1'000'000},
          {"GB", 1'000'000'000},
          {"TB", 1'000'000'000'000},
          {"PB", 1'000'000'000'000'000},
          {"EB", 1'000'000'000'000'000'000},
          {"KiB", 1ULL << 10U},
          {"MiB", 1ULL << 20U},
          {"GiB", 1ULL << 30U},
          {"TiB", 1ULL << 40U},
          {"PiB", 1ULL << 50U},
          {"EiB", 1ULL << 60U},
      },
  };
  for (const auto& [unit, bytes] : kCases) {
    EXPECT_THAT(ParseByteUnit(unit), Optional(Eq(bytes))) << unit;
  }
  EXPECT_THAT(ParseByteUnit("mb"), Optional(Eq(1'000'000U)));
  EXPECT_THAT(ParseByteUnit("mib"), Optional(Eq(1ULL << 20U)));
}

TEST_F(ValuesTest, ParseByteUnitRejectsImplicitAndMalformedUnits) {
  EXPECT_THAT(ParseByteUnit(""), Eq(std::nullopt));
  EXPECT_THAT(ParseByteUnit("M"), Eq(std::nullopt));
  EXPECT_THAT(ParseByteUnit("iB"), Eq(std::nullopt));
  EXPECT_THAT(ParseByteUnit("ZiB"), Eq(std::nullopt));
}

TEST_F(ValuesTest, ParseByteSizeRequiresANumberAndRejectsOverflow) {
  EXPECT_THAT(ParseByteSize("12MB"), Optional(Eq(12'000'000U)));
  EXPECT_THAT(ParseByteSize("12MiB"), Optional(Eq(12ULL * (1ULL << 20U))));
  EXPECT_THAT(ParseByteSize("18446744073709551615B"), Optional(Eq(UINT64_MAX)));
  EXPECT_THAT(ParseByteSize("19EB"), Eq(std::nullopt));
  EXPECT_THAT(ParseByteSize("16EiB"), Eq(std::nullopt));
  EXPECT_THAT(ParseByteSize("MB"), Eq(std::nullopt));
  EXPECT_THAT(ParseByteSize("12"), Eq(std::nullopt));
  EXPECT_THAT(ParseByteSize("-1B"), Eq(std::nullopt));
}

}  // namespace

TEST_F(ValuesTest, TheSwitchSpellingsReachTheTristateToo) {
  // The whole point of one shared vocabulary: a flag documented as on / off is not a different
  // parser from one documented as always / never. Before this, several flags' help listed on / off
  // as synonyms while the parser rejected them and silently kept the default.
  EXPECT_THAT(ParseTristate("on"), Optional(Tristate::kOn));
  EXPECT_THAT(ParseTristate("off"), Optional(Tristate::kOff));
  EXPECT_THAT(ParseTristate("ON"), Optional(Tristate::kOn));
  EXPECT_THAT(ParseTristate("Off"), Optional(Tristate::kOff));
}

}  // namespace xff::values
