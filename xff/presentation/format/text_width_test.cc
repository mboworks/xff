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

// Unicode-specific tests: multibyte letters, fullwidth Latin, combining marks,
// and emoji sequences exercise distinct column-width and grapheme properties.
struct UnicodeTextWidthTest : ::testing::Test {};

TEST_F(UnicodeTextWidthTest, CountsColumnsRatherThanUtf8Bytes) {
  EXPECT_THAT(TextColumns("caf\u00e9"), Eq(4));
  EXPECT_THAT(TextColumns("cafe\u0301"), Eq(4));
  EXPECT_THAT(TextColumns("\uff21\uff22"), Eq(4));
  EXPECT_THAT(TextColumns("\U0001f469\u200d\U0001f4bb"), Eq(2));
  EXPECT_THAT(TextColumns("\U0001f1ec\U0001f1e7"), Eq(2));
  EXPECT_THAT(TextColumns("1\ufe0f\u20e3"), Eq(2));
  EXPECT_THAT(TextColumns("\u2764\ufe0f"), Eq(2));
}

TEST_F(UnicodeTextWidthTest, WrapsWithoutSplittingCombiningOrJoinedGraphemes) {
  EXPECT_THAT(WrapColumns("cafe\u0301z", 4), ElementsAre("cafe\u0301", "z"));
  EXPECT_THAT(WrapColumns("\uff21\uff22x", 3), ElementsAre("\uff21", "\uff22x"));
  EXPECT_THAT(WrapColumns("\U0001f469\u200d\U0001f4bbx", 2), ElementsAre("\U0001f469\u200d\U0001f4bb", "x"));
  EXPECT_THAT(WrapColumns("\uff21x", 1), ElementsAre("\uff21", "x"));
}

}  // namespace
}  // namespace xff::format
