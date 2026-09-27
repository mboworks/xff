// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/matching/language/language.h"

namespace xff::language {
namespace {

using ::mbo::testing::IsOk;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Optional;

struct LanguageDbIntegrationTest : ::testing::Test {};

TEST_F(LanguageDbIntegrationTest, LinkedCompressedDatabaseExtendsCoreAndCarriesMetadata) {
  EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(LanguageForName("module.bsl"), Eq("1C Enterprise"));
  EXPECT_THAT(LanguageForName("types.d.ts"), Eq("TypeScript"));
  EXPECT_THAT(
      InfoForName("main.cpp"),
      Optional(AllOf(
          Field(&LanguageInfo::name, "C++"), Field(&LanguageInfo::type, "programming"),
          Field(&LanguageInfo::color, "#f34b7d"), Field(&LanguageInfo::source, "github-linguist 9.6.0"),
          Field(&LanguageInfo::aliases, Contains("cpp")))));
}

TEST_F(LanguageDbIntegrationTest, BundledCatalogPreservesAmbiguousSuffixesWithoutPickingNewWinners) {
  EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(
      CandidatesForName("header.h"), ElementsAre(
                                         Field(&LanguageInfo::name, "C"), Field(&LanguageInfo::name, "C++"),
                                         Field(&LanguageInfo::name, "Objective-C")));
  EXPECT_THAT(LanguageForName("header.h"), Eq("C"));
  EXPECT_THAT(
      CandidatesForName("source.pl"), ElementsAre(
                                          Field(&LanguageInfo::name, "Perl"), Field(&LanguageInfo::name, "Prolog"),
                                          Field(&LanguageInfo::name, "Raku")));
  EXPECT_THAT(LanguageForName("source.pl"), Eq("Perl"));
  EXPECT_THAT(
      CandidatesForName("view.tsx"), ElementsAre(Field(&LanguageInfo::name, "TSX"), Field(&LanguageInfo::name, "XML")));
  EXPECT_THAT(LanguageForName("view.tsx"), IsEmpty());
}

}  // namespace
}  // namespace xff::language
