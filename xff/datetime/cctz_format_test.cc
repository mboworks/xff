// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <string_view>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"

namespace xff::datetime {
namespace {

using ::mbo::testing::EqualsText;

struct CctzFormatTest : ::testing::Test {};

TEST_F(CctzFormatTest, EmptyLiteralAndEscapedPercentFragmentsKeepTheirExactOutput) {
  const auto time = absl::FromUnixSeconds(1'600'000'000);
  const auto zone = absl::UTCTimeZone();
  EXPECT_THAT(absl::FormatTime("", time, zone), EqualsText(""));
  EXPECT_THAT(absl::FormatTime("literal", time, zone), EqualsText("literal"));
  EXPECT_THAT(absl::FormatTime("%%", time, zone), EqualsText("%"));
  EXPECT_THAT(absl::FormatTime("[%F %T %z]", time, zone), EqualsText("[2020-09-13 12:26:40 +0000]"));
}

TEST_F(CctzFormatTest, NumericFieldsAndOffsetsUseTheRequestedZone) {
  const auto time = absl::FromUnixSeconds(1'600'000'000);
  EXPECT_THAT(
      absl::FormatTime("%Y-%m-%dT%H:%M:%S%Ez", time, absl::FixedTimeZone(19'800)),
      EqualsText("2020-09-13T17:56:40+05:30"));
  EXPECT_THAT(
      absl::FormatTime("%Y%m%d%H%M%S%z", time, absl::FixedTimeZone(-12'600)), EqualsText("20200913085640-0330"));
}

TEST_F(CctzFormatTest, NonemptyLocaleFallbackFragmentsRemainFormatted) {
  EXPECT_THAT(
      absl::FormatTime("%a %b %e %T %Y", absl::FromUnixSeconds(1'600'000'000), absl::UTCTimeZone()),
      EqualsText("Sun Sep 13 12:26:40 2020"));
}

TEST_F(CctzFormatTest, EmbeddedNulLiteralBytesAreRetainedAcrossNumericFragments) {
  // These exact ASCII/NUL bytes exercise deferred fragments rather than Unicode handling.
  EXPECT_THAT(
      absl::FormatTime(std::string_view("A\0%Y\0B", 6), absl::FromUnixSeconds(1'600'000'000), absl::UTCTimeZone()),
      EqualsText(
          std::string(
              "A\0"
              "2020"
              "\0B",
              8)));
}

}  // namespace
}  // namespace xff::datetime
