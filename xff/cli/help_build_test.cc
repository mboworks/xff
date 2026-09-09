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

#include "xff/cli/help_build.h"

#include <array>
#include <string>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/globals.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::SizeIs;

std::vector<std::string> SectionTitles(const Document& doc) {
  std::vector<std::string> titles;
  titles.reserve(doc.sections.size());
  for (const Section& section : doc.sections) {
    titles.push_back(section.title);
  }
  return titles;
}

// The subsection titles directly under a section (skips non-subsection children).
std::vector<std::string> SubsectionTitles(const Section& section) {
  std::vector<std::string> titles;
  for (const Content& child : section.children) {
    if (const auto* sub = std::get_if<Subsection>(&child.node)) {
      titles.push_back(sub->title);
    }
  }
  return titles;
}

const Section& SectionNamed(const Document& doc, std::string_view title) {
  for (const Section& section : doc.sections) {
    if (section.title == title) {
      return section;
    }
  }
  ADD_FAILURE() << "no section titled " << title;
  static const Section kEmpty;
  return kEmpty;
}

// Concatenates every Prose run anywhere under a section, so a test can assert on the wording the
// backends will render without re-implementing one.
std::string ProseTextOf(const Blocks& blocks);

std::string ProseTextOf(const Content& content) {
  std::string text;
  if (const auto* prose = std::get_if<Prose>(&content.node)) {
    for (const Inline& run : prose->runs) {
      absl::StrAppend(&text, run.text, " ");
    }
  } else if (const auto* entry = std::get_if<Entry>(&content.node)) {
    absl::StrAppend(&text, ProseTextOf(entry->details));
  } else if (const auto* sub = std::get_if<Subsection>(&content.node)) {
    absl::StrAppend(&text, ProseTextOf(sub->children));
  }
  return text;
}

std::string ProseTextOf(const Blocks& blocks) {
  std::string text;
  for (const Content& child : blocks) {
    absl::StrAppend(&text, ProseTextOf(child));
  }
  return text;
}

struct BuildReferenceTest : ::testing::Test {
  Document doc = BuildReference();
};

TEST_F(BuildReferenceTest, APublishedReferenceOmitsThePerBinaryNotBuiltNote) {
  // XFF.md documents the TOOL, so a note about the binary that happened to generate it would be
  // misleading. The feature is still documented, and its own text still says it is a build extra.
  const Document published = BuildReference(Audience::kPublished);
  EXPECT_THAT(ProseTextOf(SectionNamed(published, "Options").children), Not(HasSubstr("NOT built into this binary")));
}

TEST_F(BuildReferenceTest, ThisBinaryReferenceKeepsTheNotBuiltNoteForAMissingExtra) {
  // The interactive help IS about your build, so there the note must appear - checked against the
  // same predicate the builder uses, so this holds whichever extras the test binary links.
  const Document mine = BuildReference(Audience::kThisBinary);
  const std::string options = ProseTextOf(SectionNamed(mine, "Options").children);
  if (!ExtraEnabled("archive")) {
    EXPECT_THAT(options, HasSubstr("NOT built into this binary"));
  } else {
    EXPECT_THAT(options, Not(HasSubstr("NOT built into this binary")));
  }
}

TEST_F(BuildReferenceTest, BothAudiencesStillDocumentTheExtraItself) {
  // The callout a reader needs is the STATIC one, and it survives in both audiences.
  static constexpr std::array kAudiences = std::to_array<Audience>({
      Audience::kPublished,
      Audience::kThisBinary,
  });
  for (const Audience audience : kAudiences) {
    const std::string options = ProseTextOf(SectionNamed(BuildReference(audience), "Options").children);
    EXPECT_THAT(options, HasSubstr("--//xff:xff_archive")) << "audience " << static_cast<int>(audience);
  }
}

TEST_F(BuildReferenceTest, PreambleComesFromTheSot) {
  EXPECT_THAT(doc.name, Eq("xff"));
  EXPECT_THAT(doc.tagline, Not(IsEmpty()));
  EXPECT_THAT(doc.usage, Eq("[option...] [path...] [expression]"));
}

TEST_F(BuildReferenceTest, SectionsAppearInReferenceOrder) {
  EXPECT_THAT(
      SectionTitles(doc), ElementsAreArray({
                              "Description",     "Command structure",
                              "Configuration",   "Options",
                              "Expression",      "Output",
                              "Fields",          "Printf directives",
                              "Time formats",    "Size units",
                              "Regex grammars",  "Content",
                              "Comparing trees", "Ignore and VCS traversal",
                              "Archives",        "Statistics",
                              "Environment",     "Examples",
                              "Exit status",     "See also",
                          }));
}

TEST_F(BuildReferenceTest, ExpressionHasTheThreeKindSubsections) {
  EXPECT_THAT(SubsectionTitles(SectionNamed(doc, "Expression")), ElementsAre("Tests", "Actions", "Operators"));
}

TEST_F(BuildReferenceTest, OptionsGroupsFlagsIntoNonEmptySubsections) {
  const Section& options = SectionNamed(doc, "Options");
  ASSERT_THAT(options.children, Not(IsEmpty()));
  const auto* first = std::get_if<Subsection>(&options.children.front().node);
  ASSERT_THAT(first, NotNull());
  // Each grouped flag is an Entry nested under its subsection.
  bool has_entry = false;
  for (const Content& child : first->children) {
    has_entry = has_entry || std::holds_alternative<Entry>(child.node);
  }
  EXPECT_TRUE(has_entry);
}

TEST_F(BuildReferenceTest, FieldsCarriesTheBracesAndQualifiersSubsections) {
  const std::vector<std::string> subs = SubsectionTitles(SectionNamed(doc, "Fields"));
  EXPECT_THAT(subs, Contains("Braces"));
  EXPECT_THAT(subs, Contains("Dynamic namespaces"));
  EXPECT_THAT(subs, Contains("Qualifiers ({field:QUAL})"));
}

TEST_F(BuildReferenceTest, SeeAlsoCarriesManPageCrossReferences) {
  const Section& see_also = SectionNamed(doc, "See also");
  ASSERT_THAT(see_also.children, SizeIs(1));
  const auto* block = std::get_if<SeeAlso>(&see_also.children.front().node);
  ASSERT_THAT(block, NotNull());
  EXPECT_THAT(block->refs, Not(IsEmpty()));
  EXPECT_THAT(block->refs.front().id, Eq("find"));
}

}  // namespace
}  // namespace xff::cli
