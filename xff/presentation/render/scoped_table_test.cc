// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/presentation/render/scoped_table.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/presentation/format/text_width.h"

namespace xff::render {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::WithDropIndent;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Le;
using ::testing::Not;
using ::testing::StartsWith;

struct ScopedTableTest : ::testing::Test {
  const std::vector<std::string> scopes{"left-only", "right-only", "different", "identical"};
  const std::vector<ScopedTableRow> rows{
      {
          .label = "wide",
          .metrics =
              {{{"1", "25.00%", "1  B", "25.00%"}},
               {{"-", "-", "-", "-"}},
               {{"0", "0.00%", "0  B", "0.00%"}},
               {{"3", "75.00%", "3  B", "75.00%"}}},
      },
      {.label = "total", .metrics = std::vector<std::array<std::string, 4>>(4, {"4", "100.00%", "4  B", "100.00%"})}};
};

TEST_F(ScopedTableTest, ExactLayoutsAtStandardWidths) {
  for (const auto width : std::to_array<std::size_t>({80, 120})) {
    EXPECT_THAT(RenderScopedTable(scopes, rows, width, true), WithDropIndent(EqualsText(R"out(
        Group
        Scope         Count  % count  Size   % size
        wide
          left-only       1   25.00%  1  B   25.00%
          right-only      -        -     -        -
          different       0    0.00%  0  B    0.00%
          identical       3   75.00%  3  B   75.00%
        total
          left-only       4  100.00%  4  B  100.00%
          right-only      4  100.00%  4  B  100.00%
          different       4  100.00%  4  B  100.00%
          identical       4  100.00%  4  B  100.00%
        )out")));
  }
  EXPECT_THAT(RenderScopedTable(scopes, rows, 160, true), WithDropIndent(EqualsText(R"out(
        Group  left-only                      right-only                     different                      identical
               Count  % count  Size   % size  Count  % count  Size   % size  Count  % count  Size   % size  Count  % count  Size   % size
        wide       1   25.00%  1  B   25.00%      -        -     -        -      0    0.00%  0  B    0.00%      3   75.00%  3  B   75.00%
        total      4  100.00%  4  B  100.00%      4  100.00%  4  B  100.00%      4  100.00%  4  B  100.00%      4  100.00%  4  B  100.00%
        )out")));
}

TEST_F(ScopedTableTest, NarrowWidthsKeepOneTableWithAllScopeValues) {
  for (const auto width : std::to_array<std::size_t>({80, 120})) {
    const std::string output = RenderScopedTable(scopes, rows, width, true);
    EXPECT_THAT(output, StartsWith("Group\nScope"));
    EXPECT_THAT(output, HasSubstr("\nwide\n  left-only"));
    EXPECT_THAT(output, HasSubstr("\ntotal\n  left-only"));
    EXPECT_THAT(output, HasSubstr("  right-only"));
    EXPECT_THAT(output, HasSubstr("  different"));
    EXPECT_THAT(output, HasSubstr("  identical"));
    EXPECT_THAT(output, HasSubstr("75.00%"));
    for (const std::string_view line : absl::StrSplit(output, '\n')) {
      EXPECT_THAT(format::TextColumns(line), Le(width)) << line;
    }
  }
}

TEST_F(ScopedTableTest, WideLayoutGroupsScopeHeadersAndKeepsNumericColumns) {
  const std::string output = RenderScopedTable(scopes, rows, 160, true);
  EXPECT_THAT(output, Not(StartsWith("Group\n")));
  EXPECT_THAT(output, StartsWith("Group  left-only"));
  EXPECT_THAT(output, HasSubstr("\nwide"));
  EXPECT_THAT(output, Not(HasSubstr("left-only count")));
  for (const std::string_view line : absl::StrSplit(output, '\n')) {
    EXPECT_THAT(format::TextColumns(line), Le(160)) << line;
  }
  EXPECT_THAT(RenderScopedTable(scopes, rows, 0, true), Eq(output));
}

TEST_F(ScopedTableTest, EscapesControlsWithoutLosingNumericCellsAtTinyWidths) {
  auto escaped_rows = rows;
  escaped_rows.front().label = "a\nb\t\\c";
  const std::string output = RenderScopedTable(scopes, escaped_rows, 80, false);
  EXPECT_THAT(output, StartsWith(R"(a\nb\t\\c)"));
  EXPECT_THAT(output, Not(HasSubstr("a\nb")));
  EXPECT_THAT(output, Not(HasSubstr("\t")));
  const std::string tiny = RenderScopedTable(scopes, rows, 1, false);
  EXPECT_THAT(tiny, HasSubstr("100.00%"));
  EXPECT_THAT(tiny, HasSubstr("25.00%"));
  EXPECT_THAT(RenderScopedTable(scopes, {}, 80, false), IsEmpty());
}

TEST_F(ScopedTableTest, HeaderlessNarrowLayoutPreservesGroupAndScopeIdentities) {
  const std::string output = RenderScopedTable(scopes, rows, 80, false);
  EXPECT_THAT(output, StartsWith("wide\n  left-only"));
  EXPECT_THAT(output, Not(HasSubstr("Count")));
  EXPECT_THAT(output, HasSubstr("\ntotal\n  left-only"));
  EXPECT_THAT(RenderScopedTable({}, rows, 80, true), IsEmpty());
}

// Unicode-specific wrapping tests use dedicated labels.
struct UnicodeScopedTableTest : ScopedTableTest {};

TEST_F(UnicodeScopedTableTest, LongUnicodeLabelsWrapAndRetainAllContent) {
  auto long_rows = rows;
  // Fullwidth Latin A (two columns) plus e with combining acute (one column).
  std::string label;
  for (int i = 0; i < 60; ++i) {
    label += "\uff21e\u0301";
  }
  long_rows.front().label = label;
  for (const auto width : std::to_array<std::size_t>({80, 120, 160})) {
    const std::string output = RenderScopedTable(scopes, long_rows, width, false);
    std::string restored = output.substr(0, output.find("\n  left-only"));
    std::erase(restored, '\n');
    EXPECT_THAT(restored, Eq(label));
    for (const std::string_view line : absl::StrSplit(output, '\n')) {
      EXPECT_THAT(format::TextColumns(line), Le(width)) << line;
      EXPECT_THAT(line, Not(StartsWith("\u0301")));
    }
  }
}

}  // namespace
}  // namespace xff::render
