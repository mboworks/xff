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

#include "xff/cli/markdown_backend.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::WithDropIndent;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::HasSubstr;

Inline Text(std::string text) {
  return {.style = Inline::Style::kText, .text = std::move(text)};
}

Inline Code(std::string text) {
  return {.style = Inline::Style::kCode, .text = std::move(text)};
}

Inline Strong(std::string text) {
  return {.style = Inline::Style::kStrong, .text = std::move(text)};
}

Inline Ref(std::string label, RefTarget target) {
  return {.style = Inline::Style::kRef, .text = std::move(label), .target = std::move(target)};
}

// ---- MarkdownRefLink ----

struct MarkdownRefLinkTest : ::testing::Test {};

TEST_F(MarkdownRefLinkTest, UrlBecomesALink) {
  EXPECT_THAT(
      MarkdownRefLink({.kind = RefTarget::Kind::kUrl, .id = "https://helly25.com"}, "helly25"),
      Eq("[helly25](https://helly25.com)"));
}

TEST_F(MarkdownRefLinkTest, InDocumentTargetsLinkToAnAnchorSlug) {
  EXPECT_THAT(MarkdownRefLink({.kind = RefTarget::Kind::kTopic, .id = "fields"}, ""), Eq("[fields](#topic-fields)"));
  EXPECT_THAT(
      MarkdownRefLink({.kind = RefTarget::Kind::kFlag, .id = "--summary"}, ""), Eq("[--summary](#flag-summary)"));
  EXPECT_THAT(
      MarkdownRefLink({.kind = RefTarget::Kind::kPrimary, .id = "-printf"}, "the printf action"),
      Eq("[the printf action](#primary-printf)"));
}

TEST_F(MarkdownRefLinkTest, ManPageIsPlainNameSection) {
  EXPECT_THAT(MarkdownRefLink({.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}, ""), Eq("find(1)"));
}

TEST_F(MarkdownRefLinkTest, ManPageKeepsExplicitLabel) {
  EXPECT_THAT(
      MarkdownRefLink({.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}, "find manual"),
      Eq("find manual"));
}

// ---- RenderInlinesMarkdown ----

struct RenderInlinesMarkdownTest : ::testing::Test {};

TEST_F(RenderInlinesMarkdownTest, MapsEmphasisToMarkdown) {
  EXPECT_THAT(
      RenderInlinesMarkdown({Text("run "), Code("xff"), Text(" and "), Strong("stop")}), Eq("run `xff` and **stop**"));
}

TEST_F(RenderInlinesMarkdownTest, RefRendersAsALink) {
  EXPECT_THAT(
      RenderInlinesMarkdown({Text("see "), Ref("", {.kind = RefTarget::Kind::kTopic, .id = "fields"})}),
      Eq("see [fields](#topic-fields)"));
}

// ---- RenderDocument over MarkdownBackend ----

struct MarkdownBackendTest : ::testing::Test {};

TEST_F(MarkdownBackendTest, RendersAWholeDocumentAsMarkdown) {
  const Document doc{
      .name = "xff",
      .tagline = "eXtended File Find",
      .usage = "[path...] [expr]",
      .sections =
          {
              Section{
                  .title = "OPTIONS",
                  .children =
                      {
                          Content{
                              .node = Entry{.term = "--summary", .summary = {Text("group + aggregate")}, .xff = true},
                          },
                          Content{.node = Rows{.rows = {{.term = "%p", .description = {Text("path")}}}}},
                          Content{.node = Example{.text = "xff . -type f", .lang = "sh"}},
                          Content{
                              .node =
                                  SeeAlso{
                                      .refs = {{.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}},
                                      .note = {Text("the classic.")},
                                  },
                          },
                      },
              },
          },
  };

  MarkdownBackend backend;
  RenderDocument(doc, backend);
  EXPECT_THAT(backend.Take(), WithDropIndent(EqualsText(R"out(
      # xff

      eXtended File Find.

      **Usage:** `xff [path...] [expr]`

      ## OPTIONS
      - `--summary` - group + aggregate _(xff)_

      - `%p` - path

      ```sh
      xff . -type f
      ```

      find(1)

      the classic.
      )out")));
}

TEST_F(MarkdownBackendTest, LongDocumentGetsLinkedSectionContents) {
  const Document doc{
      .name = "xff",
      .sections =
          {
              Section{.title = "Description"},
              Section{.title = "Command structure"},
              Section{.title = "Options"},
              Section{.title = "Exit status"},
          },
  };

  MarkdownBackend backend;
  RenderDocument(doc, backend);
  const std::string out = backend.Take();
  EXPECT_THAT(
      out, HasSubstr(
               "## Contents\n\n"
               "- [Description](#description)\n"
               "- [Command structure](#command-structure)\n"
               "- [Options](#options)\n"
               "- [Exit status](#exit-status)\n"));
}

TEST_F(MarkdownBackendTest, SeeAlsoUsesCopyableHelpCommands) {
  MarkdownBackend backend;
  backend.EmitSeeAlso({
      .refs =
          {
              {.kind = RefTarget::Kind::kTopic, .id = "regex"},
              {.kind = RefTarget::Kind::kFlag, .id = "--summary"},
              {.kind = RefTarget::Kind::kPrimary, .id = "-printf"},
              {.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"},
              {.kind = RefTarget::Kind::kUrl, .id = "https://example.org/reference"},
              {.kind = RefTarget::Kind::kAnchor, .id = "section"},
          },
  });
  EXPECT_THAT(
      backend.Take(), AllOf(
                          HasSubstr("[--help=regex](#topic-regex)"), HasSubstr("[--help=--summary](#flag-summary)"),
                          HasSubstr("[--help=-printf](#primary-printf)"), HasSubstr("find(1)")));
}

TEST_F(MarkdownBackendTest, ReferenceAnchorsMatchTheirLinks) {
  MarkdownBackend backend;
  backend.BeginSection({.title = "Comparing trees", .anchor = "compare"});
  backend.BeginEntry({.term = "--summary[=GROUP]", .anchor = "flag---summary"});
  backend.EndEntry({});
  backend.BeginEntry({.term = "!", .anchor = "primary-!"});
  backend.EndEntry({});
  EXPECT_THAT(
      backend.Take(),
      AllOf(HasSubstr("id=\"compare\""), HasSubstr("id=\"flag-summary\""), HasSubstr("id=\"primary\"")));
  EXPECT_THAT(MarkdownRefLink({.kind = RefTarget::Kind::kPrimary, .id = "!"}, ""), Eq("[!](#primary)"));
}

TEST_F(MarkdownBackendTest, CompleteReferencePointersUseTheFormatPresentation) {
  MarkdownBackend backend;
  backend.EmitSeeAlso({
      .refs = {{.kind = RefTarget::Kind::kTopic, .id = "regex", .label = "Regex matching"}},
      .in_document = true,
  });
  EXPECT_THAT(backend.Take(), HasSubstr("[Regex matching](#topic-regex)"));
}

}  // namespace
}  // namespace xff::cli
