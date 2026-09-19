// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/html_backend.h"

#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::HasSubstr;

Inline Text(std::string text) {
  return {.style = Inline::Style::kText, .text = std::move(text)};
}

Inline Code(std::string text) {
  return {.style = Inline::Style::kCode, .text = std::move(text)};
}

struct HtmlEscapeTest : ::testing::Test {};

TEST_F(HtmlEscapeTest, EscapesCharacterDataAndAttributes) {
  EXPECT_THAT(HtmlEscape("a&<b>\"c\""), Eq("a&amp;&lt;b&gt;\"c\""));
  EXPECT_THAT(HtmlAttributeEscape("a&<b>\"'"), Eq("a&amp;&lt;b&gt;&quot;&#39;"));
}

TEST_F(HtmlEscapeTest, SlugMatchesReferenceAnchorSpelling) {
  EXPECT_THAT(HtmlSlug("--Summary=EXT, -s"), Eq("summary-ext-s"));
  EXPECT_THAT(HtmlSlug("!"), Eq("item-21"));
}

TEST_F(HtmlEscapeTest, AutolinksBareUrlsAndLeavesTrailingPunctuationOutside) {
  EXPECT_THAT(
      RenderTextHtml("Docs: https://example.com/a_(b). Then http://example.test?q=1&x=2!"),
      Eq("Docs: <a href=\"https://example.com/a_(b)\">https://example.com/a_(b)</a>. Then "
         "<a href=\"http://example.test?q=1&amp;x=2\">http://example.test?q=1&amp;x=2</a>!"));
  EXPECT_THAT(RenderTextHtml("nothttps://example.com <unsafe>"), Eq("nothttps://example.com &lt;unsafe&gt;"));
}

struct HtmlRefLinkTest : ::testing::Test {};

TEST_F(HtmlRefLinkTest, RendersUrlInternalAndManPageTargets) {
  EXPECT_THAT(
      HtmlRefLink({.kind = RefTarget::Kind::kUrl, .id = "https://example.com/?a=1&b=2"}, "site"),
      Eq("<a href=\"https://example.com/?a=1&amp;b=2\">site</a>"));
  EXPECT_THAT(
      HtmlRefLink({.kind = RefTarget::Kind::kFlag, .id = "--summary"}, ""),
      Eq("<a href=\"#flag-summary\">--summary</a>"));
  EXPECT_THAT(
      HtmlRefLink({.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}, ""), Eq("<cite>find(1)</cite>"));
  EXPECT_THAT(
      HtmlRefLink({.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}, "manual"),
      Eq("<cite>manual</cite>"));
  EXPECT_THAT(
      HtmlRefLink({.kind = RefTarget::Kind::kTopic, .id = "fields"}, ""), Eq("<a href=\"#topic-fields\">fields</a>"));
  EXPECT_THAT(
      HtmlRefLink({.kind = RefTarget::Kind::kPrimary, .id = "-printf"}, "action"),
      Eq("<a href=\"#primary-printf\">action</a>"));
  EXPECT_THAT(
      HtmlRefLink({.kind = RefTarget::Kind::kAnchor, .id = "stable data"}, "section"),
      Eq("<a href=\"#stable-data\">section</a>"));
  EXPECT_THAT(
      HtmlRefLink({.kind = RefTarget::Kind::kUrl, .id = "https://example.com"}, ""),
      Eq("<a href=\"https://example.com\">https://example.com</a>"));
}

TEST_F(HtmlRefLinkTest, RendersEveryInlineStyleAndAnUnresolvedReferenceAsText) {
  EXPECT_THAT(
      RenderInlinesHtml({
          Text("plain "),
          Code("code"),
          {.style = Inline::Style::kEmphasis, .text = " emphasis"},
          {.style = Inline::Style::kStrong, .text = " strong"},
          {
              .style = Inline::Style::kRef,
              .text = " fields",
              .target = RefTarget{.kind = RefTarget::Kind::kTopic, .id = "fields"},
          },
          {.style = Inline::Style::kRef, .text = " unresolved"},
      }),
      Eq("plain <code>code</code><em> emphasis</em><strong> strong</strong>"
         "<a href=\"#topic-fields\"> fields</a> unresolved"));
}

struct HtmlBackendTest : ::testing::Test {};

TEST_F(HtmlBackendTest, RendersAStandaloneSemanticDocument) {
  const Document doc{
      .name = "x&f",
      .tagline = "Find <files>",
      .usage = "[path]",
      .sections = {Section{
          .title = "Options",
          .children = {Content{
              .node =
                  Subsection{
                      .title = "Output",
                      .children = {Content{
                          .node =
                              Entry{
                                  .term = "--help=full:html",
                                  .summary = {Text("render "), Code("HTML")},
                                  .details = {Content{.node = Example{.text = "x&f --help=full:html", .lang = "sh"}}},
                                  .tags = {"global", "xff"},
                              },}},
                  },}},
      }},
  };

  HtmlBackend backend;
  RenderDocument(doc, backend);
  const std::string html = backend.Take();
  EXPECT_THAT(html, HasSubstr("<!doctype html>"));
  EXPECT_THAT(html, HasSubstr("<title>x&amp;f reference</title>"));
  EXPECT_THAT(
      html, HasSubstr("<nav class=\"contents\" aria-label=\"Reference sections\">\n<strong>On this page</strong>"));
  EXPECT_THAT(html, HasSubstr("<li><a href=\"#options\">Options</a></li>"));
  EXPECT_THAT(html, HasSubstr("<section id=\"options\">\n<h2>Options</h2>"));
  EXPECT_THAT(html, HasSubstr("<section id=\"output\">\n<h3>Output</h3>"));
  EXPECT_THAT(
      html,
      HasSubstr(
          "<article class=\"entry\" id=\"help-full-html\">\n<h4><code>--help=full:html</code><span class=\"tags\">"
          "(global, xff)</span></h4>\n<p class=\"summary\">render <code>HTML</code></p>"));
  EXPECT_THAT(html, HasSubstr("<pre><code class=\"language-sh\">x&amp;f --help=full:html</code></pre>"));
  EXPECT_THAT(html, HasSubstr("</main>\n</body>\n</html>\n"));
}

TEST_F(HtmlBackendTest, RendersListsRowsTablesAndExplicitAnchors) {
  Document doc{.name = "xff", .sections = {Section{.title = "Data", .anchor = "stable-data"}}};
  Section& section = doc.sections.front();
  section.children.push_back(Content{.node = Bullets{.items = {{Text("one")}, {Code("two")}}}});
  section.children.push_back(Content{.node = Rows{.rows = {{.term = "%p", .description = {Text("path")}}}}});
  section.children.push_back(
      Content{.node = Table{.header = {"name", "value"}, .cells = {{"a<b", "See https://example.com/docs."}}}});

  HtmlBackend backend;
  RenderDocument(doc, backend);
  const std::string html = backend.Take();
  EXPECT_THAT(html, HasSubstr("<section id=\"stable-data\">"));
  EXPECT_THAT(html, HasSubstr("<ul>\n<li>one</li>\n<li><code>two</code></li>\n</ul>"));
  EXPECT_THAT(html, HasSubstr("<dt><code>%p</code></dt><dd>path</dd>"));
  EXPECT_THAT(
      html,
      HasSubstr(
          "<div class=\"table-wrap\" role=\"region\" tabindex=\"0\" aria-label=\"Table: name, value\">\n<table>"));
  EXPECT_THAT(
      html,
      HasSubstr("<td>a&lt;b</td><td>See <a href=\"https://example.com/docs\">https://example.com/docs</a>.</td>"));
}

TEST_F(HtmlBackendTest, NavigationUsesTheActualAnchorAfterEarlierCollisions) {
  const Document doc{
      .name = "xff",
      .sections =
          {
              Section{.title = "First", .children = {Content{.node = Subsection{.title = "Time"}}}},
              Section{.title = "Time"},
          },
  };
  HtmlBackend backend;
  RenderDocument(doc, backend);
  const std::string html = backend.Take();
  EXPECT_THAT(html, HasSubstr("<section id=\"time-1\">\n<h2>Time</h2>"));
  EXPECT_THAT(html, HasSubstr("<li><a href=\"#time-1\">Time</a></li>"));
}

TEST_F(HtmlBackendTest, RendersFallbackTagsAndSeeAlsoNotes) {
  HtmlBackend backend;
  const Entry xff_entry{.term = "-extension", .xff = true};
  const Entry plain_entry{.term = "-portable"};
  backend.BeginEntry(xff_entry);
  backend.EndEntry(xff_entry);
  backend.BeginEntry(plain_entry);
  backend.EndEntry(plain_entry);
  backend.EmitSeeAlso({
      .refs =
          {
              {.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"},
              {.kind = RefTarget::Kind::kUrl, .id = "https://example.com"},
          },
      .note = {Text("Further reading.")},
  });

  const std::string html = backend.Take();
  EXPECT_THAT(html, HasSubstr("<h4><code>-extension</code><span class=\"tags\">(xff)</span></h4>"));
  EXPECT_THAT(html, HasSubstr("<h4><code>-portable</code></h4>"));
  EXPECT_THAT(
      html, HasSubstr(
                "<p class=\"see-also\"><cite>find(1)</cite>, "
                "<a href=\"https://example.com\">https://example.com</a></p>\n"
                "<p>Further reading.</p>"));
}

TEST_F(HtmlBackendTest, SeeAlsoUsesCopyableHelpCommands) {
  HtmlBackend backend;
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
      backend.Take(),
      AllOf(
          HasSubstr("href=\"#topic-regex\">--help=regex"), HasSubstr("href=\"#flag-summary\">--help=--summary"),
          HasSubstr("href=\"#primary-printf\">--help=-printf"), HasSubstr("find(1)")));
}

TEST_F(HtmlBackendTest, ExplicitTopicsKeepTheirAnchorsAfterSubsectionCollisions) {
  const Document doc{
      .name = "xff",
      .sections =
          {
              Section{.title = "First", .children = {Content{.node = Subsection{.title = "Time"}}}},
              Section{.title = "Time formats", .anchor = "time"},
              Section{.title = "Duplicate", .anchor = "time"},
          },
  };
  HtmlBackend backend;
  RenderDocument(doc, backend);
  const std::string html = backend.Take();
  EXPECT_THAT(html, HasSubstr("<section id=\"time\">\n<h2>Time formats</h2>"));
  EXPECT_THAT(html, HasSubstr("<section id=\"time-1\">\n<h3>Time</h3>"));
  EXPECT_THAT(html, HasSubstr("<section id=\"time-2\">\n<h2>Duplicate</h2>"));
  EXPECT_THAT(html, HasSubstr("<li><a href=\"#time\">Time formats</a></li>"));
}

TEST_F(HtmlBackendTest, CompleteReferencePointersUseTheFormatPresentation) {
  HtmlBackend backend;
  backend.EmitSeeAlso({
      .refs = {{.kind = RefTarget::Kind::kTopic, .id = "regex", .label = "Regex matching"}},
      .in_document = true,
  });
  EXPECT_THAT(backend.Take(), HasSubstr("href=\"#topic-regex\">Regex matching</a>"));
}

}  // namespace
}  // namespace xff::cli
