// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/matching/language/language_database_api.h"

namespace xff::language {
namespace {

using ::testing::Contains;
using ::testing::Field;

struct LanguageDatabaseTest : ::testing::Test {};

TEST_F(LanguageDatabaseTest, RegistrarContributesLazyLinkedDatabase) {
  const auto json = [] -> std::string_view { return R"({})"; };
  const DatabaseRegistrar registrar{{.name = "test", .json = json}};
  EXPECT_THAT(Databases(), Contains(Field(&Database::name, "test")));
}

}  // namespace
}  // namespace xff::language
