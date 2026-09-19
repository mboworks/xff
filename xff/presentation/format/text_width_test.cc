// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/presentation/format/text_width.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::format {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;

struct TextWidthTest : ::testing::Test {};

TEST_F(TextWidthTest, CountsAndWrapsAsciiAndEmptyText) {
  EXPECT_THAT(TextColumns(""), Eq(0));
  EXPECT_THAT(TextColumns("abc"), Eq(3));
  EXPECT_THAT(WrapColumns("abc", 0), ElementsAre("abc"));
  EXPECT_THAT(WrapColumns("", 4), ElementsAre(""));
}

}  // namespace
}  // namespace xff::format
