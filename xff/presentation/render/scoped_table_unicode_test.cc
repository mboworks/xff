// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/presentation/format/text_width.h"
#include "xff/presentation/render/scoped_table.h"

namespace xff::render {
namespace {
using ::testing::Eq;
using ::testing::Le;
using ::testing::Not;
using ::testing::StartsWith;

// Unicode-specific wrapping tests use dedicated labels.
struct UnicodeScopedTableTest : ::testing::Test {
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

TEST_F(UnicodeScopedTableTest, LongUnicodeLabelsWrapAndRetainAllContent) {
  auto long_rows = rows;
  // Fullwidth Latin A (two columns) plus e with combining acute (one column).
  std::string label;
  for (int i = 0; i < 60; ++i) {
    label += "Ａe\u0301";
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
