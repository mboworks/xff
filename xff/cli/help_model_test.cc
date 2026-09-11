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

#include "xff/cli/help_model.h"

#include <string>
#include <variant>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::cli {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Ge;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::SizeIs;

// A std::visit overload set, so the walker below reads as one case per node kind.
template<typename... Ts>
// NOLINTNEXTLINE(misc-multiple-inheritance): the std overloaded-visitor idiom for std::visit.
struct Overloaded : Ts... {
  using Ts::operator()...;
};

template<typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// Pre-order walk of a block list, appending each node's kind name. Recurses into
// the two container nodes (Entry.details, Subsection.children), which is the whole
// point: it only compiles / terminates if the model nests properly.
void CollectKinds(const Blocks& blocks, std::vector<std::string>& out) {
  for (const Content& content : blocks) {
    std::visit(
        Overloaded{
            [&](const Prose&) { out.emplace_back("Prose"); },
            [&](const Example&) { out.emplace_back("Example"); },
            [&](const Bullets&) { out.emplace_back("Bullets"); },
            [&](const Rows&) { out.emplace_back("Rows"); },
            [&](const Table&) { out.emplace_back("Table"); },
            [&](const Entry& entry) {
              out.emplace_back("Entry");
              CollectKinds(entry.details, out);
            },
            [&](const SeeAlso&) { out.emplace_back("SeeAlso"); },
            [&](const Subsection& subsection) {
              out.emplace_back("Subsection");
              CollectKinds(subsection.children, out);
            },
        },
        content.node);
  }
}

// A document exercising every node kind plus two levels of Subsection nesting and
// an Entry with detail blocks.
Section MakeFieldsSection() {
  return Section{
      .title = "Fields",
      .anchor = "fields",
      .children =
          {
              Content{
                  .node =
                      Prose{
                          .runs =
                              {{.style = Inline::Style::kText, .text = "See "},
                               {.style = Inline::Style::kRef,
                                .text = "the printf directives",
                                .target = RefTarget{.kind = RefTarget::Kind::kTopic, .id = "printf"}},
                               {.style = Inline::Style::kCode, .text = "-printf"}}}},
              Content{
                  .node =
                      Rows{
                          .rows =
                              {{.term = "{path}",
                                .description = {{.style = Inline::Style::kText, .text = "full path"}}}}}},
              Content{.node = Bullets{.items = {{{.style = Inline::Style::kText, .text = "a bullet"}}}}},
              Content{.node = Example{.text = "xff . -type f", .lang = "sh"}},
              Content{
                  .node =
                      Entry{
                          .term = "--summary",
                          .summary = {{.style = Inline::Style::kText, .text = "aggregate"}},
                          .details = {Content{.node = Prose{.runs = {{.text = "more detail"}}}}},
                          .xff = true,
                          .anchor = "summary"}},
              Content{
                  .node =
                      Subsection{
                          .title = "Qualifiers",
                          .anchor = "quals",
                          .children =
                              {
                                  Content{.node = Prose{.runs = {{.text = "nested"}}}},
                                  Content{
                                      .node =
                                          Subsection{
                                              .title = "Deep",
                                              .children = {Content{.node = Prose{.runs = {{.text = "deep"}}}}}}},
                              }}},
              Content{
                  .node =
                      SeeAlso{
                          .refs = {RefTarget{.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}},
                          .note = {{.text = "related"}}}},
          },
  };
}

struct HelpModelTest : ::testing::Test {};

TEST_F(HelpModelTest, WalksNestedContentInPreOrderIncludingRecursion) {
  const Section section = MakeFieldsSection();
  std::vector<std::string> kinds;
  CollectKinds(section.children, kinds);
  // Entry recurses into its one detail Prose; the Subsection recurses into its
  // Prose, a nested Subsection, and that nested subsection's Prose.
  EXPECT_THAT(
      kinds, ElementsAre(
                 "Prose", "Rows", "Bullets", "Example", "Entry", "Prose", "Subsection", "Prose", "Subsection", "Prose",
                 "SeeAlso"));
}

TEST_F(HelpModelTest, InlineRefCarriesASemanticTargetNotAFormattedLink) {
  const Section section = MakeFieldsSection();
  const auto& prose = std::get<Prose>(section.children.front().node);
  ASSERT_THAT(
      prose.runs, ElementsAre(
                      Field("style", &Inline::style, Eq(Inline::Style::kText)),
                      Field("style", &Inline::style, Eq(Inline::Style::kRef)),
                      Field("style", &Inline::style, Eq(Inline::Style::kCode))));
  const Inline& ref = prose.runs[1];
  EXPECT_THAT(ref.target, Optional(Field("kind", &RefTarget::kind, Eq(RefTarget::Kind::kTopic))));
  EXPECT_THAT(ref.target, Optional(Field("id", &RefTarget::id, Eq("printf"))));
}

TEST_F(HelpModelTest, EntryAndExampleCarryTheirMetadata) {
  const Section section = MakeFieldsSection();
  ASSERT_THAT(section.children, SizeIs(Ge(5U)));
  const auto& example = std::get<Example>(section.children[3].node);
  EXPECT_THAT(example.lang, Eq("sh"));
  const auto& entry = std::get<Entry>(section.children[4].node);
  EXPECT_THAT(entry.xff, IsTrue());
  EXPECT_THAT(entry.anchor, Eq("summary"));
  ASSERT_THAT(entry.details, SizeIs(1));
  EXPECT_THAT(std::holds_alternative<Prose>(entry.details.front().node), IsTrue());
}

}  // namespace
}  // namespace xff::cli
