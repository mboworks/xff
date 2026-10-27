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
#include <string_view>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/globals.h"
#include "xff/cli/help_model.h"
#include "xff/parser/rg.h"
#include "xff/registry/compatibility.h"

namespace xff::cli {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Optional;
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
                              "Description",
                              "Command structure",
                              "Configuration",
                              "Safety",
                              "Options",
                              "Expression",
                              "Output",
                              "Fields",
                              "Printf directives",
                              "Time formats",
                              "Size units",
                              "Regex grammars",
                              "Regex matching",
                              "Ripgrep-style searches",
                              "Content",
                              "Diff and patch output",
                              "Comparing trees",
                              "Ignore and VCS traversal",
                              "Archives",
                              "Statistics",
                              "Environment",
                              "Examples",
                              "Exit status",
                              "See also",
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
  EXPECT_THAT(has_entry, IsTrue());
}

TEST_F(BuildReferenceTest, FieldsCarriesTheBracesAndQualifiersSubsections) {
  const std::vector<std::string> subs = SubsectionTitles(SectionNamed(doc, "Fields"));
  EXPECT_THAT(subs, Contains("Braces"));
  EXPECT_THAT(subs, Contains("Dynamic namespaces"));
  EXPECT_THAT(subs, Contains("Qualifiers ({field:QUAL})"));
}

TEST_F(BuildReferenceTest, RgHelpDocumentsEveryParserOptionAndItsArgument) {
  std::vector<std::string> terms;
  for (const Content& child : SectionNamed(doc, "Ripgrep-style searches").children) {
    if (std::holds_alternative<Rows>(child.node)) {
      for (const Row& row : std::get<Rows>(child.node).rows) {
        terms.push_back(row.term);
        EXPECT_THAT(row.description, Not(IsEmpty()));
      }
    }
  }
  EXPECT_THAT(terms, SizeIs(cli::CompatibilityOptions().size()));
  for (const registry::CompatibilityOption& option : cli::CompatibilityOptions()) {
    std::string term = option.alias.empty() ? "" : absl::StrCat(option.alias, " / ");
    absl::StrAppend(&term, option.name);
    if (!option.argument.empty()) {
      absl::StrAppend(&term, " ", option.argument);
    }
    EXPECT_THAT(terms, Contains(term));
  }
}

TEST_F(BuildReferenceTest, RgExampleBelongsToCommandStructureInsteadOfComparisonHelp) {
  constexpr std::string_view kRecipe = "search matching lines with rg arguments";
  EXPECT_THAT(ProseTextOf(SectionNamed(doc, "Comparing trees").children), Not(HasSubstr(kRecipe)));
  EXPECT_THAT(ProseTextOf(SectionNamed(doc, "Command structure").children), HasSubstr(kRecipe));
}

TEST_F(BuildReferenceTest, RgHelpExplainsNativeFileSelectionBeforeLineSelection) {
  const auto text = ProseTextOf(SectionNamed(doc, "Ripgrep-style searches").children);
  EXPECT_THAT(text, HasSubstr("xff [FLAGS] PATH... [MATCHERS] --rg PATTERN [RG-OPTIONS]"));
  EXPECT_THAT(text, HasSubstr("Roots and matchers before"));
  EXPECT_THAT(text, HasSubstr("remain active; the first rg positional argument is the search pattern"));
  EXPECT_THAT(text, HasSubstr("independently selects which lines to print"));
  EXPECT_THAT(text, Not(HasSubstr("Switching back into rg grammar is not supported")));
}

TEST_F(BuildReferenceTest, DiffTopicExplainsImportAndRecurringUpdatePathMapping) {
  const std::optional<Document> diff = TopicReference("diff");
  ASSERT_THAT(diff, Optional(::testing::_));
  ASSERT_THAT(diff->sections, SizeIs(1));
  EXPECT_THAT(diff->sections.front().title, Eq("Diff and patch output"));
  const std::string prose = ProseTextOf(diff->sections.front().children);
  EXPECT_THAT(prose, HasSubstr("new root directory"));
  EXPECT_THAT(prose, HasSubstr("ongoing update"));
  EXPECT_THAT(prose, HasSubstr("current destination paths"));
  EXPECT_THAT(prose, HasSubstr("comparison pairing"));
  EXPECT_THAT(prose, HasSubstr("--compare-ignore=respective"));
  EXPECT_THAT(prose, HasSubstr("either stack excludes it"));
  const Table* direction = nullptr;
  for (const Content& content : diff->sections.front().children) {
    if (const auto* table = std::get_if<Table>(&content.node)) {
      direction = table;
      break;
    }
  }
  ASSERT_THAT(direction, NotNull());
  EXPECT_THAT(direction->header, ElementsAre("Mode", "Left / old", "Right / new"));
  EXPECT_THAT(
      direction->cells, ElementsAre(
                            std::vector<std::string>{"-diff (TARGET omitted)", "Absent (/dev/null)", "Found entry"},
                            std::vector<std::string>{"-diff TARGET", "TARGET", "Found entry"},
                            std::vector<std::string>{"-rdiff (TARGET omitted)", "Found entry", "Absent (/dev/null)"},
                            std::vector<std::string>{"-rdiff TARGET", "Found entry", "TARGET"},
                            std::vector<std::string>{
                                "--compare=diff LEFT RIGHT", "Matching entry under LEFT, or absent",
                                "Matching entry under RIGHT, or absent"}));
}

TEST_F(BuildReferenceTest, CompareTopicCrossReferencesDiff) {
  const Document navigation = TopicNavigation("compare");
  ASSERT_THAT(navigation.sections, SizeIs(1));
  ASSERT_THAT(navigation.sections.front().children, SizeIs(1));
  const auto* block = std::get_if<SeeAlso>(&navigation.sections.front().children.front().node);
  ASSERT_THAT(block, NotNull());
  EXPECT_THAT(block->refs, Contains(Field(&RefTarget::id, Eq("diff"))));
}

TEST_F(BuildReferenceTest, DiffTopicCrossReferencesCompare) {
  const Document navigation = TopicNavigation("diff");
  ASSERT_THAT(navigation.sections, SizeIs(1));
  ASSERT_THAT(navigation.sections.front().children, SizeIs(1));
  const auto* block = std::get_if<SeeAlso>(&navigation.sections.front().children.front().node);
  ASSERT_THAT(block, NotNull());
  EXPECT_THAT(block->refs, Contains(Field(&RefTarget::id, Eq("compare"))));
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
