// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/matching/language/catalog.h"

#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/matching/language/type_filter.h"

namespace xff::language {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::_;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct CatalogTest : ::testing::Test {
  void TearDown() override { EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk()); }
};

TEST_F(CatalogTest, CanonicalAliasesOverlapAndListingsUseTheSharedVocabulary) {
  const auto catalog = ActiveCatalog();
  for (const std::string_view name : {"cpp", "C++", "CPP", "cc"}) {
    EXPECT_THAT(catalog.Resolve(name), IsOkAndHolds(Eq("c++")));
  }
  EXPECT_THAT(catalog.Resolve("h"), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("ambiguous")));
  EXPECT_THAT(catalog.Resolve("missing"), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("--type-list")));
  EXPECT_THAT(
      catalog.CandidatesForName("a.h"),
      ElementsAre(
          Field(&FilterType::name, "c"), Field(&FilterType::name, "c++"), Field(&FilterType::name, "objective-c")));
  EXPECT_THAT(catalog.CandidatesForName("a.CPP"), ElementsAre(Field(&FilterType::name, "c++")));
  EXPECT_THAT(catalog.CandidatesForName("unknown.zznotatype"), IsEmpty());
  EXPECT_THAT(catalog.Listing(), HasSubstr("cpp: language C++\n"));
  EXPECT_THAT(catalog.Listing(), HasSubstr("py: language Python\n"));
}

TEST_F(CatalogTest, OrderedClearsImportsAndAdditionsPreserveClassificationAndOldHandles) {
  const auto before = ActiveCatalog();
  const std::vector<CatalogEdit> edits{
      {.kind = CatalogEdit::Kind::kClear, .value = "cpp"},
      {.kind = CatalogEdit::Kind::kAdd, .value = "C++:*.one"},
      {.kind = CatalogEdit::Kind::kAdd, .value = "local:include:cpp,py"},
      {.kind = CatalogEdit::Kind::kAdd, .value = "local:*.{two,three}"},
      {.kind = CatalogEdit::Kind::kAdd, .value = "local:include:local"},
  };
  EXPECT_THAT(Configure({}, ConflictPolicy::kError, edits), IsOk());
  const auto catalog = ActiveCatalog();
  EXPECT_THAT(catalog.CandidatesForName("a.cc"), IsEmpty());
  EXPECT_THAT(before.CandidatesForName("a.cc"), ElementsAre(Field(&FilterType::name, "c++")));
  EXPECT_THAT(LanguageForName("a.cc"), Eq("C++"));
  EXPECT_THAT(CandidatesForName("a.cc"), ElementsAre(Field(&LanguageInfo::name, "C++")));
  EXPECT_THAT(
      catalog.CandidatesForName("a.one"),
      ElementsAre(Field(&FilterType::name, "c++"), Field(&FilterType::name, "local")));
  EXPECT_THAT(catalog.CandidatesForName("a.ONE"), IsEmpty());
  for (const std::string_view name : {"a.two", "a.three", "a.py"}) {
    EXPECT_THAT(catalog.CandidatesForName(name), Contains(Field(&FilterType::name, "local")));
  }
  EXPECT_THAT(catalog.Resolve("cc"), IsOkAndHolds(Eq("c++")));
  EXPECT_THAT(catalog.Listing(), HasSubstr("cpp: *.one\n"));
}

TEST_F(CatalogTest, SelectionOrderAndAllUseEffectiveDefinitions) {
  ASSERT_OK_AND_ASSIGN(
      const auto cpp_then_c, TypeFilter::Compile({
                                 {.include = true, .value = "cpp"},
                                 {.include = false, .value = "c"},
                             }));
  EXPECT_THAT(cpp_then_c.Includes("a.cc"), IsTrue());
  EXPECT_THAT(cpp_then_c.Includes("a.h"), IsFalse());
  ASSERT_OK_AND_ASSIGN(
      const auto c_then_cpp, TypeFilter::Compile({
                                 {.include = false, .value = "c"},
                                 {.include = true, .value = "cpp"},
                             }));
  EXPECT_THAT(c_then_cpp.Includes("a.h"), IsTrue());
  EXPECT_THAT(c_then_cpp.Includes("a.py"), IsFalse());
  ASSERT_OK_AND_ASSIGN(const auto all, TypeFilter::Compile({{.include = true, .value = "all"}}));
  EXPECT_THAT(all.Includes("a.cc"), IsTrue());
  EXPECT_THAT(all.Includes("a.unknownxxx"), IsFalse());
  ASSERT_OK_AND_ASSIGN(const auto unknown, TypeFilter::Compile({{.include = false, .value = "all"}}));
  EXPECT_THAT(unknown.Includes("a.cc"), IsFalse());
  EXPECT_THAT(unknown.Includes("a.unknownxxx"), IsTrue());
  ASSERT_OK_AND_ASSIGN(const auto unrestricted, TypeFilter::Compile({}));
  EXPECT_THAT(unrestricted.Includes("a.unknownxxx"), IsTrue());
}

TEST_F(CatalogTest, ClearedTypesLoseAllAliasesUntilRecreated) {
  EXPECT_THAT(Configure({}, ConflictPolicy::kError, {{.kind = CatalogEdit::Kind::kClear, .value = "C++"}}), IsOk());
  EXPECT_THAT(ActiveCatalog().Resolve("cpp"), StatusIs(absl::StatusCode::kInvalidArgument, _));
  EXPECT_THAT(
      TypeFilter::Compile({{.include = true, .value = "cpp"}}), StatusIs(absl::StatusCode::kInvalidArgument, _));
  EXPECT_THAT(
      ActiveCatalog().CandidatesForName("a.h"),
      ElementsAre(Field(&FilterType::name, "c"), Field(&FilterType::name, "objective-c")));
}

TEST_F(CatalogTest, InvalidEditsDoNotReplaceTheConfiguredCatalog) {
  const auto before = ActiveCatalog();
  const std::vector<std::string> invalid{
      "bad-name:*.bad", ":x", "name", "name:", "all:*.all", "name:include:missing", "h:*.ambiguous",
  };
  for (const auto& spec : invalid) {
    SCOPED_TRACE(spec);
    EXPECT_THAT(
        Configure({}, ConflictPolicy::kError, {{.kind = CatalogEdit::Kind::kAdd, .value = spec}}),
        StatusIs(absl::StatusCode::kInvalidArgument, _));
    EXPECT_THAT(ActiveCatalog().Listing(), EqualsText(before.Listing()));
  }
  EXPECT_THAT(Configure({}, ConflictPolicy::kError, {{.kind = CatalogEdit::Kind::kClear, .value = "missing"}}), IsOk());
  EXPECT_THAT(ActiveCatalog().Listing(), EqualsText(before.Listing()));
}
}  // namespace
}  // namespace xff::language
