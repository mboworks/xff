// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/matching/mime/database.h"
#include "xff/matching/mime/mime.h"

namespace xff::mime {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Ref;

struct DefaultCacheTest : ::testing::Test {};

TEST_F(DefaultCacheTest, ReusesImmutableDefaultsAndRetainsViewsAcrossLaterRegistration) {
  RegisterDatabase({.name = "cache-default", .json = R"({"application/x-cache":{"extensions":["memo"]}})"});
  EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk());
  const auto initial = Types();
  ASSERT_THAT(initial, Not(IsEmpty()));
  const auto& first = initial.front();
  const auto original = InfoForName("file.memo");
  EXPECT_THAT(original.type, EqualsText("application/x-cache"));
  EXPECT_THAT(Configure({}, ConflictPolicy::kFirst), IsOk());
  EXPECT_THAT(Types().front(), Ref(first));

  RegisterDatabase({.name = "cache-later", .json = R"({"application/x-later":{"extensions":["memo"]}})"});
  EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(TypeForName("file.memo"), EqualsText("application/x-later"));
  EXPECT_THAT(original.type, EqualsText("application/x-cache"));
  const auto changed = Types();
  EXPECT_THAT(changed.front(), Not(Ref(first)));
  EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(Types().front(), Ref(changed.front()));
}
}  // namespace
}  // namespace xff::mime
