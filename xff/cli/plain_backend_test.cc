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

#include "xff/cli/plain_backend.h"

#include <string>
#include <string_view>

#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::WithDropIndent;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Le;

Inline Text(std::string text) {
  return {.style = Inline::Style::kText, .text = std::move(text)};
}

Inline Code(std::string text) {
  return {.style = Inline::Style::kCode, .text = std::move(text)};
}

Inline Ref(std::string label, RefTarget target) {
  return {.style = Inline::Style::kRef, .text = std::move(label), .target = std::move(target)};
}

// ---- PlainRefLocator ----

struct PlainRefLocatorTest : ::testing::Test {};

TEST_F(PlainRefLocatorTest, ResolvesEachKindToItsPlainLocator) {
  EXPECT_THAT(PlainRefLocator({.kind = RefTarget::Kind::kTopic, .id = "fields"}), Eq("--help=fields"));
  EXPECT_THAT(PlainRefLocator({.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}), Eq("find(1)"));
  EXPECT_THAT(PlainRefLocator({.kind = RefTarget::Kind::kFlag, .id = "--summary"}), Eq("--summary"));
  EXPECT_THAT(PlainRefLocator({.kind = RefTarget::Kind::kPrimary, .id = "-printf"}), Eq("-printf"));
  EXPECT_THAT(PlainRefLocator({.kind = RefTarget::Kind::kUrl, .id = "https://helly25.com"}), Eq("https://helly25.com"));
}

// ---- RenderInlinesPlain ----

struct RenderInlinesPlainTest : ::testing::Test {};

TEST_F(RenderInlinesPlainTest, DropsEmphasisMarkupAndKeepsText) {
  EXPECT_THAT(RenderInlinesPlain({Text("run "), Code("xff"), Text(" now")}), Eq("run xff now"));
}

TEST_F(RenderInlinesPlainTest, RefWithALabelShowsTheLabel) {
  EXPECT_THAT(
      RenderInlinesPlain({Text("see "), Ref("the fields", {.kind = RefTarget::Kind::kTopic, .id = "fields"})}),
      Eq("see the fields"));
}

TEST_F(RenderInlinesPlainTest, RefWithoutALabelShowsItsLocator) {
  EXPECT_THAT(
      RenderInlinesPlain({Text("see "), Ref("", {.kind = RefTarget::Kind::kTopic, .id = "fields"})}),
      Eq("see --help=fields"));
}

// ---- RenderDocument over PlainTextBackend ----

struct PlainBackendTest : ::testing::Test {};

TEST_F(PlainBackendTest, RendersAWholeDocumentInOrder) {
  const Document doc{
      .name = "xff",
      .tagline = "eXtended File Find",
      .usage = "[path...] [expr]",
      .sections =
          {
              Section{
                  .title = "DESCRIPTION",
                  .children =
                      {
                          Content{
                              .node =
                                  Prose{
                                      .runs =
                                          {Text("Find files; see "),
                                           Ref("", {.kind = RefTarget::Kind::kTopic, .id = "fields"}), Text(".")}}},
                      },
              },
              Section{
                  .title = "OPTIONS",
                  .children =
                      {
                          Content{
                              .node =
                                  Entry{
                                      .term = "--summary",
                                      .summary = {Text("group + aggregate")},
                                      .details = {Content{.node = Prose{.runs = {Text("more detail.")}}}},
                                      .xff = true}},
                          Content{
                              .node =
                                  Rows{
                                      .rows =
                                          {{.term = "%p", .description = {Text("path")}},
                                           {.term = "%f", .description = {Text("name")}}}}},
                          Content{.node = Bullets{.items = {{Text("first")}, {Text("second")}}}},
                          Content{.node = Example{.text = "xff . -type f", .lang = "sh"}},
                          Content{
                              .node =
                                  SeeAlso{
                                      .refs = {{.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}},
                                      .note = {Text("the classic.")}}},
                      },
              },
          },
  };

  PlainTextBackend backend;
  RenderDocument(doc, backend);
  EXPECT_THAT(backend.Take(), WithDropIndent(EqualsText(R"out(
      xff - eXtended File Find

      Usage: xff [path...] [expr]

      DESCRIPTION

        Find files; see --help=fields.

      OPTIONS

        --summary  (xff)
          group + aggregate
          more detail.
        %p  path
        %f  name
        - first
        - second

        xff . -type f

        find(1)

        the classic.
      )out")));
}

TEST_F(PlainBackendTest, SeeAlsoUsesCopyableHelpCommands) {
  PlainTextBackend backend;
  backend.EmitSeeAlso(
      {.refs = {
           {.kind = RefTarget::Kind::kTopic, .id = "regex"},
           {.kind = RefTarget::Kind::kFlag, .id = "--summary"},
           {.kind = RefTarget::Kind::kPrimary, .id = "-printf"},
           {.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"},
           {.kind = RefTarget::Kind::kUrl, .id = "https://example.org/reference"},
           {.kind = RefTarget::Kind::kAnchor, .id = "section"},
       }});
  EXPECT_THAT(backend.Take(), HasSubstr("See also: --help=regex, --help=--summary, --help=-printf, find(1)"));
}

TEST_F(PlainBackendTest, RelatedCommandsRespectTheHelpWidth) {
  PlainTextBackend backend(HelpRenderContext{.width = 32});
  backend.EmitSeeAlso(
      {.refs = {
           {.kind = RefTarget::Kind::kTopic, .id = "regex"},
           {.kind = RefTarget::Kind::kFlag, .id = "--summary"},
           {.kind = RefTarget::Kind::kPrimary, .id = "-printf"},
       }});
  const std::string out = backend.Take();
  EXPECT_THAT(out, HasSubstr("See also:"));
  EXPECT_THAT(out, HasSubstr("--help=--summary"));
  for (const std::string_view line : absl::StrSplit(out, '\n')) {
    EXPECT_THAT(line.size(), Le(32));
  }
}

TEST_F(PlainBackendTest, NarrowPolicyTableRetainsEveryLabeledValue) {
  PlainTextBackend backend(HelpRenderContext{.width = 40});
  backend.EmitTable({
      .header = {"Operation", "Ordinary path", "Output path"},
      .cells =
          {
              {"Overwrite", "file-writing, file-overwrite", "output-file-writing, output-file-overwrite"},
              {"Delete", "file-deletion", "output-file-deletion"},
          },
  });
  const std::string out = backend.Take();
  EXPECT_THAT(out, HasSubstr("Operation: Overwrite"));
  EXPECT_THAT(out, HasSubstr("Ordinary path:"));
  EXPECT_THAT(out, HasSubstr("Output path:"));
  EXPECT_THAT(out, HasSubstr("file-writing"));
  EXPECT_THAT(out, HasSubstr("file-overwrite"));
  EXPECT_THAT(out, HasSubstr("output-file-writing"));
  EXPECT_THAT(out, HasSubstr("output-file-overwrite"));
  EXPECT_THAT(out, HasSubstr("Operation: Delete"));
  EXPECT_THAT(out, HasSubstr("output-file-deletion"));
  for (const std::string_view line : absl::StrSplit(out, '\n')) {
    EXPECT_THAT(line.size(), Le(40));
  }
}

TEST_F(PlainBackendTest, CompleteReferencePointersUseTheFormatPresentation) {
  PlainTextBackend backend;
  backend.EmitSeeAlso({
      .refs = {{.kind = RefTarget::Kind::kTopic, .id = "regex", .label = "Regex matching"}},
      .in_document = true,
  });
  EXPECT_THAT(backend.Take(), IsEmpty());
}

}  // namespace
}  // namespace xff::cli
