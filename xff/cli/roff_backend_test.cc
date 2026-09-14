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

#include "xff/cli/roff_backend.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::StartsWith;

Inline Text(std::string text) {
  return {.style = Inline::Style::kText, .text = std::move(text)};
}

Inline Code(std::string text) {
  return {.style = Inline::Style::kCode, .text = std::move(text)};
}

Inline Styled(Inline::Style style, std::string text) {
  return {.style = style, .text = std::move(text)};
}

struct RoffEscapeTest : ::testing::Test {};

TEST_F(RoffEscapeTest, EscapesHyphensBackslashesAndLeadingControls) {
  EXPECT_THAT(RoffEscape("--summary"), Eq(R"(\-\-summary)"));
  EXPECT_THAT(RoffEscape(R"(a\b)"), Eq(R"(a\\b)"));
  EXPECT_THAT(RoffEscape(".hidden"), Eq(R"(\&.hidden)"));   // leading dot guarded
  EXPECT_THAT(RoffEscape("plain text"), Eq("plain text"));  // nothing to escape
}

struct RoffBackendTest : ::testing::Test {};

TEST_F(RoffBackendTest, RendersManPageStructure) {
  const Document doc{
      .name = "xff",
      .tagline = "eXtended File Find",
      .usage = "xff [path]",
      .sections =
          {
              Section{
                  .title = "Options",  // natural case in the model ...
                  .children =
                      {
                          Content{
                              .node =
                                  Entry{
                                      .term = "--summary",
                                      .summary = {Text("group + "), Code("aggregate")},
                                      .xff = true}},
                      },
              },
              Section{
                  .title = "See also",
                  .children =
                      {
                          Content{
                              .node =
                                  SeeAlso{.refs = {{.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}}}},
                      },
              },
          },
  };

  RoffBackend backend;
  RenderDocument(doc, backend);
  const std::string out = backend.Take();
  // roff control directives are only valid in the first column, so each check anchors
  // to a line start: `.TH` opens the file, the rest are preceded by a newline.
  EXPECT_THAT(out, StartsWith(".TH xff 1"));
  EXPECT_THAT(out, HasSubstr("\n.SH NAME\nxff \\- eXtended File Find\n"));
  EXPECT_THAT(out, HasSubstr("\n.SH OPTIONS\n"));  // ... upper-cased for roff
  EXPECT_THAT(out, HasSubstr("\n.TP\n.B \\-\\-summary\n"));
  EXPECT_THAT(out, HasSubstr("group + \\fBaggregate\\fR (xff extension)"));
  EXPECT_THAT(out, HasSubstr("\n.BR find (1)\n"));
}

TEST_F(RoffBackendTest, RendersEveryBlockAndInlineBoundary) {
  RoffBackend backend;
  backend.Preamble({.name = "xff", .tagline = "find files", .usage = "xff [path]"});
  backend.BeginSection({.title = "details"});
  backend.BeginSubsection({});  // an empty title is a grouping node, not a roff heading
  backend.BeginSubsection({.title = "Rendering"});
  backend.BeginEntry({
      .term = "--mode",
      .summary =
          {
              Styled(Inline::Style::kStrong, "strong"),
              Styled(Inline::Style::kText, " "),
              Styled(Inline::Style::kEmphasis, "emphasis"),
              Styled(Inline::Style::kText, " "),
              Styled(Inline::Style::kRef, "reference"),
          },
      .tags = {"global", "xff"},
  });
  backend.BeginEntry({.term = "plain", .summary = {Text("no classification")}});
  backend.EmitProse({.runs = {Text("first paragraph")}});
  backend.EmitProse({.runs = {Text("second paragraph")}});
  backend.EmitExample({.text = "printf example"});
  backend.EmitExample({.text = "already terminated\n"});
  backend.EmitExample({});
  backend.EmitBullets({.items = {{Text("one")}, {Code("two")}}});
  backend.EmitRows({.rows = {{.term = "row", .description = {Text("description")}}}});
  backend.EmitTable({.header = {"Name", "Meaning"}, .cells = {{"short", "first"}, {"longer", "second"}}});
  backend.EmitSeeAlso({
      .refs =
          {
              {.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"},
              {.kind = RefTarget::Kind::kManPage, .id = "grep", .section = "1"},
          },
      .note = {Text("related tools")},
  });

  const std::string out = backend.Take();
  EXPECT_THAT(
      out, AllOf(
               HasSubstr(".SS Rendering\n"), HasSubstr("strong\\fR \\fIemphasis\\fR reference (global, xff)"),
               HasSubstr("first paragraph\n.PP\nsecond paragraph\n"), HasSubstr(".nf\nprintf example\n.fi\n"),
               HasSubstr("already terminated\n.fi\n"), HasSubstr(".IP \\(bu 3\n"),
               HasSubstr("Name    Meaning\nshort   first\nlonger  second\n"),
               HasSubstr(".BR find (1),\n.BR grep (1)\n"), HasSubstr("related tools\n")));
}

TEST_F(RoffBackendTest, SeeAlsoUsesCopyableHelpCommands) {
  RoffBackend backend;
  backend.EmitSeeAlso(
      {.refs = {
           {.kind = RefTarget::Kind::kTopic, .id = "regex"},
           {.kind = RefTarget::Kind::kFlag, .id = "--summary"},
           {.kind = RefTarget::Kind::kPrimary, .id = "-printf"},
           {.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"},
           {.kind = RefTarget::Kind::kUrl, .id = "https://example.org/reference"},
           {.kind = RefTarget::Kind::kAnchor, .id = "section"},
       }});
  EXPECT_THAT(
      backend.Take(),
      AllOf(HasSubstr("See also:"), HasSubstr("help=regex"), HasSubstr("help="), HasSubstr(".BR find (1)")));
}

TEST_F(RoffBackendTest, CompleteReferencePointersUseTheFormatPresentation) {
  RoffBackend backend;
  backend.EmitSeeAlso({
      .refs = {{.kind = RefTarget::Kind::kTopic, .id = "regex", .label = "Regex matching"}},
      .in_document = true,
  });
  EXPECT_THAT(backend.Take(), HasSubstr(".B Regex matching"));
}

}  // namespace
}  // namespace xff::cli
