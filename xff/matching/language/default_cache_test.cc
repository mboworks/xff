// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/matching/language/catalog.h"
#include "xff/matching/language/language.h"
#include "xff/matching/language/language_database_api.h"

namespace xff::language {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Ref;

std::size_t& ProviderCalls() {
  static std::size_t calls = 0;
  return calls;
}

std::string_view DefaultJson() {
  ++ProviderCalls();
  return R"({"CacheLanguage":{"extensions":["memo"]}})";
}

std::string_view LaterJson() {
  return R"({"LaterCacheLanguage":{"extensions":["late-memo"]}})";
}

struct DefaultCacheTest : ::testing::Test {};

TEST_F(DefaultCacheTest, ReusesDefaultsRestoresCatalogAndObservesLaterRegistration) {
  RegisterDatabase({.name = "cache-default", .json = DefaultJson});
  EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk());
  const auto initial = ActiveSnapshot();
  ASSERT_THAT(initial.Languages(), Not(IsEmpty()));
  const auto& first = initial.Languages().front();
  const auto catalog = ActiveCatalog();
  const std::string listing = catalog.Listing();
  EXPECT_THAT(ProviderCalls(), Eq(1));
  EXPECT_THAT(initial.LanguageForName("file.memo"), EqualsText("CacheLanguage"));

  EXPECT_THAT(Configure({}, ConflictPolicy::kFirst), IsOk());
  EXPECT_THAT(ActiveSnapshot().Languages().front(), Ref(first));
  EXPECT_THAT(ActiveCatalog().Listing(), EqualsText(listing));
  EXPECT_THAT(ProviderCalls(), Eq(1));

  const std::array edits{
      CatalogEdit{
          .kind = CatalogEdit::Kind::kAdd,
          .value = "o14cache:*.memo-extra",
      },
  };
  EXPECT_THAT(Configure({}, ConflictPolicy::kError, edits), IsOk());
  EXPECT_THAT(ActiveCatalog().CandidatesForName("file.memo-extra"), Contains(Field(&FilterType::name, "o14cache")));
  EXPECT_THAT(catalog.Listing(), EqualsText(listing));
  EXPECT_THAT(ProviderCalls(), Eq(2));
  EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(ActiveSnapshot().Languages().front(), Ref(first));
  EXPECT_THAT(ActiveCatalog().Listing(), EqualsText(listing));
  EXPECT_THAT(ProviderCalls(), Eq(2));

  RegisterDatabase({.name = "cache-later", .json = LaterJson});
  EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk());
  const auto later = ActiveSnapshot();
  EXPECT_THAT(later.LanguageForName("file.late-memo"), EqualsText("LaterCacheLanguage"));
  EXPECT_THAT(initial.LanguageForName("file.late-memo"), IsEmpty());
  EXPECT_THAT(later.Languages().front(), Not(Ref(first)));
  EXPECT_THAT(ProviderCalls(), Eq(3));
}
}  // namespace
}  // namespace xff::language
