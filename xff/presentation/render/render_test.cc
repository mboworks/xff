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

#include "xff/presentation/render/render.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/presentation/format/format.h"

namespace xff::render {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::WithDropIndent;

struct RenderTest : ::testing::Test {};

TEST_F(RenderTest, PlainAppendsNewline) {
  EXPECT_THAT(Renderer(Format::kPlain).Record("a/b/c"), "a/b/c\n");
}

TEST_F(RenderTest, NulAppendsNulTerminator) {
  EXPECT_THAT(Renderer(Format::kNul).Record("a/b/c"), std::string("a/b/c\0", 6));
}

TEST_F(RenderTest, JsonlEmitsOneObjectPerLine) {
  EXPECT_THAT(Renderer(Format::kJsonl).Record("a/b/c"), "{\"path\":\"a/b/c\"}\n");
}

TEST_F(RenderTest, JsonlEscapesQuotesBackslashAndControls) {
  // a "b \c <tab> d <carriage-return> e  ->  a \" b \\ c \t d \r e
  EXPECT_THAT(Renderer(Format::kJsonl).Record("a\"b\\c\td\re"), "{\"path\":\"a\\\"b\\\\c\\td\\re\"}\n");
  // A raw control byte (0x01) becomes a \u escape.
  EXPECT_THAT(Renderer(Format::kJsonl).Record(std::string("x\x01y", 3)), "{\"path\":\"x\\u0001y\"}\n");
}

TEST_F(RenderTest, PlainRawIsTheDefaultEncoding) {
  // kPlain defaults to verbatim bytes (find-compatible): a newline in the name passes
  // through, splitting the record.
  EXPECT_THAT(Renderer(Format::kPlain).Record("a\nb"), "a\nb\n");
}

TEST_F(RenderTest, PlainEscapeCEscapesBackslashAndControls) {
  // --path-encoding=escape: backslash + the common control chars become C escapes.
  EXPECT_THAT(Renderer(Format::kPlain, PathEncoding::kEscape).Record("a\nb\tc\rd\\e"), "a\\nb\\tc\\rd\\\\e\n");
  // Other control / DEL bytes use \xNN (upper-case hex); printable + high UTF-8 bytes
  // pass through verbatim.
  EXPECT_THAT(Renderer(Format::kPlain, PathEncoding::kEscape).Record(std::string("x\x01y\x7f", 4)), "x\\x01y\\x7F\n");
  EXPECT_THAT(Renderer(Format::kPlain, PathEncoding::kEscape).Record("caf\xc3\xa9"), "caf\xc3\xa9\n");
}

TEST_F(RenderTest, EscapeAppliesOnlyToPlain) {
  // kNul stays raw (the NUL is the separator); kJsonl always JSON-escapes, both
  // regardless of the path encoding.
  EXPECT_THAT(Renderer(Format::kNul, PathEncoding::kEscape).Record("a\nb"), std::string("a\nb\0", 4));
  EXPECT_THAT(Renderer(Format::kJsonl, PathEncoding::kEscape).Record("a\nb"), "{\"path\":\"a\\nb\"}\n");
}

TEST_F(RenderTest, CsvQuotesOnlyWhenNeededAndDoublesQuotes) {
  EXPECT_THAT(Renderer(Format::kCsv).Record("a/b/c"), "a/b/c\n");      // no special char -> unquoted
  EXPECT_THAT(Renderer(Format::kCsv).Record("a,b"), "\"a,b\"\n");      // comma -> quoted
  EXPECT_THAT(Renderer(Format::kCsv).Record("a\"b"), "\"a\"\"b\"\n");  // quote -> doubled + quoted
  EXPECT_THAT(Renderer(Format::kCsv).Record("a\nb"), "\"a\nb\"\n");    // newline -> quoted (kept literal)
}

TEST_F(RenderTest, TsvEscapesTabNewlineAndBackslash) {
  EXPECT_THAT(Renderer(Format::kTsv).Record("a/b/c"), "a/b/c\n");  // no special char -> verbatim
  EXPECT_THAT(Renderer(Format::kTsv).Record("a\tb\nc\rd\\e"), "a\\tb\\nc\\rd\\\\e\n");
}

TEST_F(RenderTest, OnlyTabularFormatsHaveAHeader) {
  EXPECT_THAT(Renderer(Format::kCsv).Header(), "path\n");
  EXPECT_THAT(Renderer(Format::kTsv).Header(), "path\n");
  EXPECT_THAT(Renderer(Format::kPlain).Header(), "");
  EXPECT_THAT(Renderer(Format::kNul).Header(), "");
  EXPECT_THAT(Renderer(Format::kJsonl).Header(), "");
}

TEST_F(RenderTest, EncodeTabularRowJoinsAndEncodesCells) {
  const std::vector<std::string> cells = {"a", "b,c", "d\te"};
  // CSV: comma-join; the "b,c" cell is quoted; a tab is not special in CSV, so it stays.
  EXPECT_THAT(EncodeTabularRow(Format::kCsv, cells), "a,\"b,c\",d\te\n");
  // TSV: tab-join; the interior tab in "d\te" is escaped to \t (a literal comma is fine).
  EXPECT_THAT(EncodeTabularRow(Format::kTsv, cells), "a\tb,c\td\\te\n");
  // Non-tabular formats are not rows.
  EXPECT_THAT(EncodeTabularRow(Format::kPlain, cells), "");
  EXPECT_THAT(EncodeTabularRow(Format::kJsonl, cells), "");
}

// The buffered tabular formats (kAligned / kMarkdown) render the whole table at once via
// RenderTable. EqualsText compares the multiline output line by line (unified diff on mismatch),
// per the STYLE convention.

TEST_F(RenderTest, RenderTableAlignsColumnsUnderADashedHeaderRule) {
  const std::vector<std::string> header = {"name", "size"};
  const std::vector<std::vector<std::string>> rows = {{"a.txt", "3"}, {"README", "12"}};
  EXPECT_THAT(RenderTable(Format::kAligned, header, rows), WithDropIndent(EqualsText(R"out(
      name    size
      ------  ----
      a.txt   3
      README  12
      )out")));
}

TEST_F(RenderTest, RenderTableMarkdownEmitsAGithubTableWithARule) {
  const std::vector<std::string> header = {"name", "size"};
  const std::vector<std::vector<std::string>> rows = {{"README", "12"}, {"a.txt", "3"}};
  EXPECT_THAT(RenderTable(Format::kMarkdown, header, rows), WithDropIndent(EqualsText(R"out(
      | name   | size |
      | ------ | ---- |
      | README | 12   |
      | a.txt  | 3    |
      )out")));
}

TEST_F(RenderTest, MarkdownAlignmentPadsValuesHeadersAndRulesToTheSameWidths) {
  TableStream stream(
      Format::kMarkdown, {"name", "count", "%"}, true, TableStream::kAll, 0,
      {format::Align::kLeft, format::Align::kRight, format::Align::kRight});
  EXPECT_THAT(stream.Add({"a", "3", "2%"}), "");
  EXPECT_THAT(stream.Add({"long|name", "123456", "100%"}), "");
  EXPECT_THAT(stream.Flush(), WithDropIndent(EqualsText(R"out(
      | name       |  count |    % |
      | ---------- | -----: | ---: |
      | a          |      3 |   2% |
      | long\|name | 123456 | 100% |
      )out")));
}

TEST_F(RenderTest, RightAlignmentSupportsHeaderlessAndAlignedTables) {
  TableStream markdown(
      Format::kMarkdown, {"name", "count"}, false, TableStream::kAll, 0, {format::Align::kLeft, format::Align::kRight});
  EXPECT_THAT(markdown.Add({"a", "3"}), "");
  EXPECT_THAT(markdown.Add({"long", "12"}), "");
  EXPECT_THAT(markdown.Flush(), WithDropIndent(EqualsText(R"out(
      | a    |   3 |
      | long |  12 |
      )out")));
  TableStream aligned(
      Format::kAligned, {"name", "count"}, true, TableStream::kAll, 0, {format::Align::kLeft, format::Align::kRight});
  EXPECT_THAT(aligned.Add({"a", "3"}), "");
  EXPECT_THAT(aligned.Flush(), WithDropIndent(EqualsText(R"out(
      name  count
      ----  -----
      a         3
      )out")));
}

TEST_F(RenderTest, RenderTableMarkdownFloorsColumnWidthAtThreeForTheRule) {
  EXPECT_THAT(RenderTable(Format::kMarkdown, {"x"}, {{"y"}}), WithDropIndent(EqualsText(R"out(
      | x   |
      | --- |
      | y   |
      )out")));
}

TEST_F(RenderTest, RenderTableMarkdownEscapesInteriorPipes) {
  // A literal `|` in a cell is escaped so it cannot split the column.
  EXPECT_THAT(RenderTable(Format::kMarkdown, {"name"}, {{"has|pipe.txt"}}), WithDropIndent(EqualsText(R"out(
      | name          |
      | ------------- |
      | has\|pipe.txt |
      )out")));
}

TEST_F(RenderTest, MarkdownMeasuresCellsAfterNormalizingLineBreaks) {
  EXPECT_THAT(
      RenderTable(Format::kMarkdown, {"name"}, {{"a\nb"}, {"a\r\nb"}, {"abcdef"}, {"a|b"}}),
      WithDropIndent(EqualsText(R"out(
      | name   |
      | ------ |
      | a b    |
      | a b    |
      | abcdef |
      | a\|b   |
      )out")));
}

TEST_F(RenderTest, RenderTableNoHeaderDropsTheHeaderAndRule) {
  // --no-header: only the data rows, and the widths no longer count the hidden header.
  const std::vector<std::string> header = {"name", "size"};
  const std::vector<std::vector<std::string>> rows = {{"README", "12"}, {"a.txt", "3"}};
  EXPECT_THAT(RenderTable(Format::kAligned, header, rows, /*with_header=*/false), WithDropIndent(EqualsText(R"out(
      README  12
      a.txt   3
      )out")));
}

TEST_F(RenderTest, RenderTableIsEmptyForTheStreamingAndNonTabularFormats) {
  const std::vector<std::vector<std::string>> rows = {{"a"}};
  EXPECT_THAT(RenderTable(Format::kCsv, {"path"}, rows), "");
  EXPECT_THAT(RenderTable(Format::kTsv, {"path"}, rows), "");
  EXPECT_THAT(RenderTable(Format::kPlain, {"path"}, rows), "");
}

// TableStream is the windowed engine RenderTable wraps. These lock the bounded-buffer
// behavior: buffer the window, flush aligned, then stream the rest at the locked widths.

TEST_F(RenderTest, TableStreamBuffersTheWindowThenStreamsTheRestSkewed) {
  TableStream stream(Format::kAligned, {"name", "n"}, /*with_header=*/true, /*window=*/2);
  EXPECT_THAT(stream.Add({"a", "1"}), "");  // still buffering the window
  // The second row fills the window: header + rule + both rows flush at the locked widths.
  EXPECT_THAT(stream.Add({"bb", "2"}), WithDropIndent(EqualsText(R"out(
      name  n
      ----  -
      a     1
      bb    2
      )out")));
  // Past the window a wider cell grows its own column only (graceful skew, no row dropped).
  EXPECT_THAT(stream.Add({"cccc", "3"}), EqualsText("cccc  3\n"));
  EXPECT_THAT(stream.Flush(), "");  // nothing left buffered
}

TEST_F(RenderTest, TableStreamAllBuffersUntilFlush) {
  TableStream stream(Format::kAligned, {"name", "n"}, /*with_header=*/true, TableStream::kAll);
  EXPECT_THAT(stream.Add({"a", "1"}), "");
  EXPECT_THAT(stream.Add({"cccc", "2"}), "");  // still buffering everything
  // Flush aligns the whole run at once: `cccc` widens the column for the header and all rows.
  EXPECT_THAT(stream.Flush(), WithDropIndent(EqualsText(R"out(
      name  n
      ----  -
      a     1
      cccc  2
      )out")));
  EXPECT_THAT(stream.Flush(), "");  // idempotent
}

TEST_F(RenderTest, TableStreamMarkdownStreamsPastAWindowOfOne) {
  TableStream stream(Format::kMarkdown, {"name"}, /*with_header=*/true, /*window=*/1);
  // The first row fills the window: header + rule + row flush at the locked width.
  EXPECT_THAT(
      stream.Add({"a"}), EqualsText(
                             "| name |\n"
                             "| ---- |\n"
                             "| a    |\n"));
  // A wider later cell grows its own column; still valid Markdown.
  EXPECT_THAT(stream.Add({"bbbbb"}), EqualsText("| bbbbb |\n"));
  EXPECT_THAT(stream.Flush(), "");
}

TEST_F(RenderTest, TableStreamIsEmptyForNonBufferedFormats) {
  TableStream stream(Format::kCsv, {"path"}, /*with_header=*/true, TableStream::kAll);
  EXPECT_THAT(stream.Add({"a"}), "");
  EXPECT_THAT(stream.Flush(), "");
}

TEST_F(RenderTest, TableStreamFlushesOnTheByteBudget) {
  // window kAll (no row cap) but a 4-byte budget: buffer until the buffered cell bytes reach
  // it, then flush the aligned block. --no-header keeps this to just the data rows.
  TableStream stream(Format::kAligned, {"n"}, /*with_header=*/false, TableStream::kAll, /*byte_budget=*/4);
  EXPECT_THAT(stream.Add({"ab"}), "");                      // 2 bytes, under budget
  EXPECT_THAT(stream.Add({"cd"}), EqualsText("ab\ncd\n"));  // +2 = 4 >= budget -> flush both
  EXPECT_THAT(stream.Flush(), "");                          // nothing left buffered
}

// Tree (--format=tree): splice paths into a shared-prefix structure, render depth-first with
// box-drawing connectors. Siblings are lexical; the last child gets the elbow connector.

TEST_F(RenderTest, TreeRendersUnicodeConnectorsWithCorrectLastChild) {
  Tree tree(/*unicode=*/true);
  tree.Add("root/src/main.cc");
  tree.Add("root/src/util.cc");
  tree.Add("root/README.md");  // sorts before "src"; "src" is root's last child (elbow)
  EXPECT_THAT(
      tree.Render(), EqualsText(
                         "root\n"
                         "├── README.md\n"
                         "└── src\n"
                         "    ├── main.cc\n"
                         "    └── util.cc\n"));
}

TEST_F(RenderTest, TreeRendersAsciiConnectorsWhenNotUnicode) {
  Tree tree(/*unicode=*/false);
  tree.Add("root/src/main.cc");
  tree.Add("root/src/util.cc");
  tree.Add("root/README.md");
  EXPECT_THAT(
      tree.Render(), EqualsText(
                         "root\n"
                         "|-- README.md\n"
                         "`-- src\n"
                         "    |-- main.cc\n"
                         "    `-- util.cc\n"));
}

TEST_F(RenderTest, TreeShowsAncestorsOfADeepMatch) {
  // Only the leaf was added; its ancestor directories appear as branch nodes.
  Tree tree(/*unicode=*/false);
  tree.Add("root/src/main.cc");
  EXPECT_THAT(
      tree.Render(), EqualsText(
                         "root\n"
                         "`-- src\n"
                         "    `-- main.cc\n"));
}

TEST_F(RenderTest, TreeDrawsAVerticalForNonLastBranches) {
  // `a` is not root's last child, so its subtree is prefixed with the vertical connector.
  Tree tree(/*unicode=*/true);
  tree.Add("root/a/x");
  tree.Add("root/b");
  EXPECT_THAT(
      tree.Render(), EqualsText(
                         "root\n"
                         "├── a\n"
                         "│   └── x\n"
                         "└── b\n"));
}

}  // namespace
}  // namespace xff::render
