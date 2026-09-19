// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/presentation/format/text_width.h"

namespace xff::format {
namespace {
using ::testing::ElementsAre;
using ::testing::Eq;

// Unicode-specific tests: multibyte letters, fullwidth Latin, combining marks,
// and emoji sequences exercise distinct column-width and grapheme properties.
struct UnicodeTextWidthTest : ::testing::Test {};

TEST_F(UnicodeTextWidthTest, CountsColumnsRatherThanUtf8Bytes) {
  EXPECT_THAT(TextColumns("café"), Eq(4));
  EXPECT_THAT(TextColumns("cafe\u0301"), Eq(4));
  EXPECT_THAT(TextColumns("ＡＢ"), Eq(4));
  EXPECT_THAT(TextColumns("\U0001f469\u200d\U0001f4bb"), Eq(2));
  EXPECT_THAT(TextColumns("\U0001f1ec\U0001f1e7"), Eq(2));
  EXPECT_THAT(TextColumns("1\ufe0f\u20e3"), Eq(2));
  EXPECT_THAT(TextColumns("\u2764\ufe0f"), Eq(2));
}

TEST_F(UnicodeTextWidthTest, WrapsWithoutSplittingCombiningOrJoinedGraphemes) {
  EXPECT_THAT(WrapColumns("cafe\u0301z", 4), ElementsAre("cafe\u0301", "z"));
  EXPECT_THAT(WrapColumns("ＡＢx", 3), ElementsAre("Ａ", "Ｂx"));
  EXPECT_THAT(WrapColumns("\U0001f469\u200d\U0001f4bbx", 2), ElementsAre("\U0001f469\u200d\U0001f4bb", "x"));
  EXPECT_THAT(WrapColumns("Ａx", 1), ElementsAre("Ａ", "x"));
}

}  // namespace
}  // namespace xff::format
