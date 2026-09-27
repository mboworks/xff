// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xff/matching/language/language.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::language {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Optional;

struct LanguageTest : ::testing::Test {
  void TearDown() override { EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk()); }

  static std::string Write(std::string_view text) {
    static std::atomic<unsigned> sequence = 0;
    const std::filesystem::path path = std::filesystem::path(::testing::TempDir())
                                       / (std::string(::testing::UnitTest::GetInstance()->current_test_info()->name())
                                          + "-" + std::to_string(sequence.fetch_add(1)));
    std::ofstream(path) << text;
    return path.string();
  }
};

TEST_F(LanguageTest, ByExtension) {
  EXPECT_THAT(LanguageForName("main.cc"), Eq("C++"));
  EXPECT_THAT(LanguageForName("main.cpp"), Eq("C++"));
  EXPECT_THAT(LanguageForName("util.h"), Eq("C"));  // linguist's default for .h
  EXPECT_THAT(LanguageForName("a.py"), Eq("Python"));
  EXPECT_THAT(LanguageForName("a.rs"), Eq("Rust"));
  EXPECT_THAT(LanguageForName("a.go"), Eq("Go"));
  EXPECT_THAT(LanguageForName("a.ts"), Eq("TypeScript"));
  EXPECT_THAT(LanguageForName("view.m"), Eq("Objective-C"));  // linguist's default for .m
}

TEST_F(LanguageTest, SharedHeaderCandidatesKeepOnePreferredLabel) {
  const auto candidates = CandidatesForName("util.H");
  EXPECT_THAT(
      candidates, ElementsAre(
                      Field(&LanguageInfo::name, "C"), Field(&LanguageInfo::name, "C++"),
                      Field(&LanguageInfo::name, "Objective-C")));
  EXPECT_THAT(LanguageForName("util.h"), Eq("C"));
  EXPECT_THAT(CandidatesForName("impl.c"), ElementsAre(Field(&LanguageInfo::name, "C")));
  EXPECT_THAT(CandidatesForName("impl.hpp"), ElementsAre(Field(&LanguageInfo::name, "C++")));
  EXPECT_THAT(CandidatesForName("unknown."), IsEmpty());
  EXPECT_THAT(CandidatesForName("unknown"), IsEmpty());
}

TEST_F(LanguageTest, SharedClaimsNeedNoConflictEscapeHatchAndDoNotChooseADisplayLabel) {
  const std::string file = Write(R"({
    "First": {"shared_extensions": [".shared", ".shared"], "shared_filenames": ["Build[1]"]},
    "Second": {"shared_extensions": ["shared"], "shared_filenames": ["Build[1]"]}
  })");
  EXPECT_THAT(Configure({file}, ConflictPolicy::kError), IsOk());
  const auto both = ElementsAre(Field(&LanguageInfo::name, "First"), Field(&LanguageInfo::name, "Second"));
  EXPECT_THAT(CandidatesForName("a.SHARED"), both);
  EXPECT_THAT(CandidatesForName("Build[1]"), both);
  EXPECT_THAT(CandidatesForName("build[1]"), IsEmpty());
  EXPECT_THAT(CandidatesForName("Build1"), IsEmpty());
  EXPECT_THAT(LanguageForName("a.shared"), IsEmpty());
  EXPECT_THAT(InfoForName("a.shared"), Eq(std::nullopt));
  EXPECT_THAT(TerminalColorForName("a.shared"), IsEmpty());
}

TEST_F(LanguageTest, CandidateLookupUsesExactNamesThenLongestSuffix) {
  const std::string file = Write(R"({
    "Exact": {"shared_filenames": ["Exact.special.py"]},
    "Specific": {"shared_extensions": ["special.py"]}
  })");
  EXPECT_THAT(Configure({file}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(CandidatesForName("Exact.special.py"), ElementsAre(Field(&LanguageInfo::name, "Exact")));
  EXPECT_THAT(CandidatesForName("other.SPECIAL.PY"), ElementsAre(Field(&LanguageInfo::name, "Specific")));
  EXPECT_THAT(CandidatesForName("normal.py"), ElementsAre(Field(&LanguageInfo::name, "Python")));
  EXPECT_THAT(LanguageForName("other.special.py"), IsEmpty());
}

TEST_F(LanguageTest, PreferredOverlayReplacesOlderSharedMembershipAndCanExplicitlyShareAgain) {
  const auto before = ActiveSnapshot();
  const auto old_candidates = before.CandidatesForName("a.h");
  const std::string first = Write(R"({"Only": {"extensions": ["h"], "filenames": ["Exact"]},
                                       "Other": {"shared_filenames": ["Exact"]}})");
  EXPECT_THAT(Configure({first}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(CandidatesForName("a.h"), ElementsAre(Field(&LanguageInfo::name, "Only")));
  EXPECT_THAT(LanguageForName("a.h"), Eq("Only"));
  EXPECT_THAT(
      old_candidates, ElementsAre(
                          Field(&LanguageInfo::name, "C"), Field(&LanguageInfo::name, "C++"),
                          Field(&LanguageInfo::name, "Objective-C")));
  EXPECT_THAT(before.CandidatesForName("a.h").data(), Eq(old_candidates.data()));
  const std::string second = Write(R"({"New": {"extensions": ["h"], "filenames": ["Exact"]},
                                        "C++": {"shared_extensions": ["h"]}})");
  EXPECT_THAT(Configure({first, second}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(
      CandidatesForName("a.h"), ElementsAre(Field(&LanguageInfo::name, "C++"), Field(&LanguageInfo::name, "New")));
  EXPECT_THAT(CandidatesForName("Exact"), ElementsAre(Field(&LanguageInfo::name, "New")));
}

TEST_F(LanguageTest, ReplacingSharedListsRemovesOnlyThatLanguagesSharedClaims) {
  const std::string first = Write(R"({
    "First": {"extensions": ["preferred"], "shared_extensions": ["old"], "shared_filenames": ["Exact"]},
    "Second": {"shared_extensions": ["old"], "shared_filenames": ["Exact"]}
  })");
  const std::string second = Write(R"({"First": {"shared_extensions": ["new"], "shared_filenames": []}})");
  EXPECT_THAT(Configure({first, second}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(CandidatesForName("a.old"), ElementsAre(Field(&LanguageInfo::name, "Second")));
  EXPECT_THAT(CandidatesForName("Exact"), ElementsAre(Field(&LanguageInfo::name, "Second")));
  EXPECT_THAT(CandidatesForName("a.new"), ElementsAre(Field(&LanguageInfo::name, "First")));
  EXPECT_THAT(CandidatesForName("a.preferred"), ElementsAre(Field(&LanguageInfo::name, "First")));
  const std::string third = Write(R"({"Second": {"shared_extensions": [], "shared_filenames": []}})");
  EXPECT_THAT(Configure({first, second, third}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(CandidatesForName("a.old"), IsEmpty());
  EXPECT_THAT(CandidatesForName("Exact"), IsEmpty());
}

TEST_F(LanguageTest, PreferredAndSharedClaimForSameLanguageIsOneCandidate) {
  const std::string file = Write(R"({"Only": {"extensions": ["same"], "shared_extensions": ["same"]}})");
  EXPECT_THAT(Configure({file}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(CandidatesForName("a.same"), ElementsAre(Field(&LanguageInfo::name, "Only")));
}

TEST_F(LanguageTest, ExtensionIsCaseInsensitive) {
  EXPECT_THAT(LanguageForName("A.PY"), Eq("Python"));
  EXPECT_THAT(LanguageForName("MAIN.CPP"), Eq("C++"));
}

TEST_F(LanguageTest, ByFilename) {
  EXPECT_THAT(LanguageForName("Makefile"), Eq("Makefile"));
  EXPECT_THAT(LanguageForName("GNUmakefile"), Eq("Makefile"));
  EXPECT_THAT(LanguageForName("Dockerfile"), Eq("Dockerfile"));
  EXPECT_THAT(LanguageForName("CMakeLists.txt"), Eq("CMake"));
  EXPECT_THAT(LanguageForName("BUILD.bazel"), Eq("Starlark"));
  EXPECT_THAT(LanguageForName("MODULE.bazel"), Eq("Starlark"));
  EXPECT_THAT(LanguageForName(".bashrc"), Eq("Shell"));  // a dotfile: no extension, matched by name
}

TEST_F(LanguageTest, FilenameWinsOverExtension) {
  // CMakeLists.txt is CMake even though `.txt` is not a mapped extension; the exact filename
  // match is consulted first.
  EXPECT_THAT(LanguageForName("CMakeLists.txt"), Eq("CMake"));
}

TEST_F(LanguageTest, UnknownIsEmpty) {
  EXPECT_THAT(LanguageForName("photo.jpg"), IsEmpty());  // an extension, but not a language
  EXPECT_THAT(LanguageForName("README"), IsEmpty());     // no extension, no filename match
  EXPECT_THAT(LanguageForName("a.unknownext"), IsEmpty());
  EXPECT_THAT(LanguageForName(""), IsEmpty());
}

TEST_F(LanguageTest, JsonLayerOverridesLongestSuffixAndSuppliesMetadata) {
  const std::string file = Write(R"({
    "TypeScript declaration": {
      "type": "programming",
      "color": "#3178c6",
      "group": "TypeScript",
      "source": "local",
      "aliases": ["dts"],
      "extensions": [".d.ts"],
      "filenames": ["types.special"]
    }
  })");
  EXPECT_THAT(Configure({file}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(LanguageForName("types.d.ts"), Eq("TypeScript declaration"));
  EXPECT_THAT(LanguageForName("types.D.TS"), Eq("TypeScript declaration"));
  EXPECT_THAT(LanguageForName("types.special"), Eq("TypeScript declaration"));
  EXPECT_THAT(
      InfoForName("types.d.ts"), Optional(AllOf(
                                     Field(&LanguageInfo::type, "programming"), Field(&LanguageInfo::color, "#3178c6"),
                                     Field(&LanguageInfo::group, "TypeScript"), Field(&LanguageInfo::source, "local"),
                                     Field(&LanguageInfo::aliases, Contains("dts")))));
  EXPECT_THAT(TerminalColorForName("types.d.ts"), Eq("38;2;49;120;198"));
}

TEST_F(LanguageTest, TerminalColorRequiresSixHexDigits) {
  const std::string file = Write(R"({
    "Black": {"color": "#000000", "extensions": ["black"]},
    "White": {"color": "#FFFFFF", "extensions": ["white"]},
    "Short": {"color": "#123", "extensions": ["short"]},
    "Invalid": {"color": "#12xx56", "extensions": ["invalid"]},
    "Named": {"color": "blue", "extensions": ["named"]}
  })");
  EXPECT_THAT(Configure({file}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(TerminalColorForName("a.black"), Eq("38;2;0;0;0"));
  EXPECT_THAT(TerminalColorForName("a.white"), Eq("38;2;255;255;255"));
  EXPECT_THAT(TerminalColorForName("a.short"), IsEmpty());
  EXPECT_THAT(TerminalColorForName("a.invalid"), IsEmpty());
  EXPECT_THAT(TerminalColorForName("a.named"), IsEmpty());
  EXPECT_THAT(TerminalColorForName("a.unknown"), IsEmpty());
}

TEST_F(LanguageTest, LaterLayerOverridesEarlierLayer) {
  const std::string first = Write(R"({"First": {"extensions": ["one"]}})");
  const std::string second = Write(R"({"Second": {"extensions": ["one"]}})");
  EXPECT_THAT(Configure({first, second}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(LanguageForName("a.one"), Eq("Second"));
}

TEST_F(LanguageTest, PublishedMetadataViewsSurviveLaterSnapshots) {
  const std::string original_file = Write(R"({
    "Original": {"extensions": ["cc"], "aliases": ["orig"], "type": "programming", "color": "#010203"}
  })");
  EXPECT_THAT(Configure({original_file}, ConflictPolicy::kError), IsOk());
  const LanguageSnapshot original_snapshot = ActiveSnapshot();
  const std::optional<LanguageInfo> original_info = InfoForName("main.cc");
  ASSERT_THAT(original_info, Optional(Field(&LanguageInfo::name, "Original")));
  const std::string_view original_color = original_snapshot.TerminalColorForName("main.cc");
  const LanguageInfo original = original_info.value_or(LanguageInfo{});
  const std::string replacement = Write(R"({"Replacement": {"extensions": ["cc"]}})");
  EXPECT_THAT(Configure({replacement}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(LanguageForName("main.cc"), Eq("Replacement"));
  EXPECT_THAT(original.name, Eq("Original"));
  EXPECT_THAT(original.type, Eq("programming"));
  EXPECT_THAT(original.aliases, Contains("orig"));
  EXPECT_THAT(original.extensions, Contains("cc"));
  EXPECT_THAT(original_color, Eq("38;2;1;2;3"));
  EXPECT_THAT(original_snapshot.LanguageForName("main.cc"), Eq("Original"));
  EXPECT_THAT(ActiveSnapshot().LanguageForName("main.cc"), Eq("Replacement"));
}

TEST_F(LanguageTest, LanguageCatalogReusesThePublishedSnapshot) {
  const absl::Span<const LanguageInfo> first = Languages();
  const absl::Span<const LanguageInfo> second = Languages();
  EXPECT_THAT(second.data(), Eq(first.data()));
  EXPECT_THAT(second.size(), Eq(first.size()));
}

TEST_F(LanguageTest, ReplacingOneClaimKindPreservesTheOther) {
  const std::string base = Write(R"({"Both": {"extensions": ["old"], "filenames": ["Exact"]}})");
  const std::string extension = Write(R"({"Both": {"extensions": ["new"]}})");
  EXPECT_THAT(Configure({base, extension}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(LanguageForName("a.old"), IsEmpty());
  EXPECT_THAT(LanguageForName("a.new"), Eq("Both"));
  EXPECT_THAT(LanguageForName("Exact"), Eq("Both"));

  const std::string filename = Write(R"({"Both": {"filenames": ["Replacement"]}})");
  EXPECT_THAT(Configure({base, filename}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(LanguageForName("a.old"), Eq("Both"));
  EXPECT_THAT(LanguageForName("Exact"), IsEmpty());
  EXPECT_THAT(LanguageForName("Replacement"), Eq("Both"));
}

TEST_F(LanguageTest, ConflictPolicyControlsClaimsWithinOneLayer) {
  const std::string file = Write(R"({
    "First": {"extensions": ["same"]},
    "Last": {"extensions": ["same"]}
  })");
  EXPECT_THAT(
      Configure({file}, ConflictPolicy::kError),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("extension 'same' is claimed by First and Last")));
  EXPECT_THAT(Configure({file}, ConflictPolicy::kFirst), IsOk());
  EXPECT_THAT(LanguageForName("a.same"), Eq("First"));
  EXPECT_THAT(Configure({file}, ConflictPolicy::kLast), IsOk());
  EXPECT_THAT(LanguageForName("a.same"), Eq("Last"));
}

TEST_F(LanguageTest, RejectsInvalidLayerShapeAndValues) {
  EXPECT_THAT(
      Configure({Write("[]")}, ConflictPolicy::kError),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("top level must be an object")));
  EXPECT_THAT(
      Configure({Write(R"({"Broken":{"extensions":"cc"}})")}, ConflictPolicy::kError),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("extensions must be an array of strings")));
}

TEST_F(LanguageTest, RejectsMalformedLanguageRecords) {
  struct Case {
    std::string_view json;
    std::string_view message;
  };

  constexpr auto kCases = std::to_array<Case>({
      {.json = "not JSON", .message = "invalid JSON"},
      {.json = R"({"":{}})", .message = "invalid language entry"},
      {.json = R"({"Broken":[]})", .message = "invalid language entry"},
      {.json = R"({"Broken":{"type":[]}})", .message = "Broken.type must be a string"},
      {.json = R"({"Broken":{"aliases":[1]}})", .message = "aliases must contain only strings"},
      {.json = R"({"Broken":{"extensions":[""]}})", .message = "invalid extension"},
      {.json = R"({"Broken":{"extensions":["a/b"]}})", .message = "invalid extension"},
      {.json = R"({"Broken":{"filenames":"Exact"}})", .message = "filenames must be an array of strings"},
      {.json = R"({"Broken":{"filenames":[""]}})", .message = "invalid filename"},
      {.json = R"({"Broken":{"filenames":["a/b"]}})", .message = "invalid filename"},
      {.json = R"({"Broken":{"shared_extensions":"h"}})", .message = "shared_extensions must be an array"},
      {.json = R"({"Broken":{"shared_extensions":[3]}})", .message = "shared_extensions must contain only strings"},
      {.json = R"({"Broken":{"shared_extensions":["."]}})", .message = "invalid extension"},
      {.json = R"({"Broken":{"shared_filenames":["a/b"]}})", .message = "invalid filename"},
  });
  for (const Case& test : kCases) {
    EXPECT_THAT(
        Configure({Write(test.json)}, ConflictPolicy::kError),
        StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(test.message)));
  }
}

TEST_F(LanguageTest, ListsCanonicalRecordsDeterministically) {
  EXPECT_THAT(Languages(), Contains(Field(&LanguageInfo::name, "C++")));
}

}  // namespace
}  // namespace xff::language
