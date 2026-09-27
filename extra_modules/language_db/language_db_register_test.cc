// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "language_database.h"
#include "language_db_data_cc_generated.h"
#include "mbo/testing/status.h"
#include "xff/license/notice.h"
#include "xff/matching/language/language_database_api.h"

namespace xff::language_db {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Contains;
using ::testing::Field;
using ::testing::HasSubstr;

struct LanguageDbRegisterTest : ::testing::Test {};

TEST_F(LanguageDbRegisterTest, RegistersCompressedDataAndNotices) {
  EXPECT_THAT(language::Databases(), Contains(Field(&language::Database::name, "github-linguist 9.6.0")));
  EXPECT_THAT(license::Notices(), Contains(Field(&license::Notice::component, "github-linguist 9.6.0")));
}

TEST_F(LanguageDbRegisterTest, DecodesTheEmbeddedVocabulary) {
  EXPECT_THAT(
      Decode(data::Compressed(), data::UncompressedSize()),
      IsOkAndHolds(HasSubstr(R"("C++":{"source":"github-linguist 9.6.0")")));
}

TEST_F(LanguageDbRegisterTest, RejectsInvalidBrotliAndWrongDeclaredSize) {
  constexpr std::array<std::uint8_t, 3> kInvalid = {1, 2, 3};
  EXPECT_THAT(Decode(kInvalid, 20), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("not valid Brotli data")));
  EXPECT_THAT(
      Decode(data::Compressed(), data::UncompressedSize() + 1),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("decoded to 109261 bytes, expected 109262")));
}

}  // namespace
}  // namespace xff::language_db
