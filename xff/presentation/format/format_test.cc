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

#include "xff/presentation/format/format.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"

namespace xff::format {
namespace {

using ::mbo::testing::EqualsText;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Optional;

struct FormatTest : ::testing::Test {};

TEST_F(FormatTest, IntUngroupedByDefault) {
  EXPECT_THAT(Int(0), "0");
  EXPECT_THAT(Int(1'234'567), "1234567");
}

TEST_F(FormatTest, IntGroupsInThreesFromTheRight) {
  EXPECT_THAT(Int(0, ','), "0");
  EXPECT_THAT(Int(12, ','), "12");
  EXPECT_THAT(Int(123, ','), "123");
  EXPECT_THAT(Int(1'234, ','), "1,234");
  EXPECT_THAT(Int(12'345, ','), "12,345");
  EXPECT_THAT(Int(123'456, ','), "123,456");
  EXPECT_THAT(Int(1'234'567, ','), "1,234,567");
  EXPECT_THAT(Int(1'000'000'000, ','), "1,000,000,000");
}

TEST_F(FormatTest, SizeIecUsesBinaryUnits) {
  EXPECT_THAT(Size(0, SizeUnits::kIec), "0 B");
  EXPECT_THAT(Size(56, SizeUnits::kIec), "56 B");  // below one unit: exact bytes
  EXPECT_THAT(Size(1'023, SizeUnits::kIec), "1023 B");
  EXPECT_THAT(Size(1'024, SizeUnits::kIec), "1.0 KiB");
  EXPECT_THAT(Size(1'536, SizeUnits::kIec), "1.5 KiB");  // 1024 + 512
  EXPECT_THAT(Size(5'872'025, SizeUnits::kIec), "5.6 MiB");
  EXPECT_THAT(Size(1'024ULL * 1'024 * 1'024, SizeUnits::kIec), "1.0 GiB");
}

TEST_F(FormatTest, SizeSiUsesDecimalUnits) {
  EXPECT_THAT(Size(999, SizeUnits::kSi), "999 B");
  EXPECT_THAT(Size(1'000, SizeUnits::kSi), "1.0 kB");
  EXPECT_THAT(Size(5'872'025, SizeUnits::kSi), "5.9 MB");
  EXPECT_THAT(Size(2'000'000'000, SizeUnits::kSi), "2.0 GB");
}

TEST_F(FormatTest, SizeAlignedPadsTheBareByteUnitIntoThePrefixColumn) {
  // The point of the extra space: in a right-aligned column, `B` has to land under the `B` of
  // `kB`, which it cannot do while the prefix letter is simply absent.
  EXPECT_THAT(SizeAligned(991, SizeUnits::kSi), Eq("991  B"));
  EXPECT_THAT(SizeAligned(1'200, SizeUnits::kSi), Eq("1.2 kB"));
  EXPECT_THAT(SizeAligned(2'500'000, SizeUnits::kSi), Eq("2.5 MB"));
  // IEC prefixes are a letter wider, so the byte row pads by two.
  EXPECT_THAT(SizeAligned(991, SizeUnits::kIec), Eq("991   B"));
  EXPECT_THAT(SizeAligned(2'048, SizeUnits::kIec), Eq("2.0 KiB"));
}

TEST_F(FormatTest, SizeAlignedKeepsTheUnitsRightAlignedAcrossRows) {
  // The property that matters is a column one: every rendering ends its unit in the same place,
  // so right-aligning the whole cell aligns the numbers AND the unit.
  static constexpr auto kSizes = std::to_array<std::uint64_t>({0, 1, 999, 1'000, 1'048'576, 9'999'999'999});
  for (const std::uint64_t bytes : kSizes) {
    const std::string rendered = SizeAligned(bytes, SizeUnits::kSi);
    EXPECT_THAT(rendered, EndsWith("B")) << bytes;
    // number + space + a 2-wide unit field: the unit starts a fixed distance from the end.
    EXPECT_THAT(rendered.substr(rendered.size() - 3, 1), Eq(" ")) << rendered;
  }
}

TEST_F(FormatTest, SizeColumnsSplitsNumberAndSuffixWithFixedFraction) {
  // Scaled units get exactly `fraction_digits` decimals; the suffix comes back separately
  // for a left-aligned column.
  const SizeParts kib = SizeColumns(1'536, SizeUnits::kIec, 2);
  EXPECT_THAT(kib.number, "1.50");
  EXPECT_THAT(kib.suffix, "KiB");
  const SizeParts mb = SizeColumns(5'872'025, SizeUnits::kSi, 2);
  EXPECT_THAT(mb.number, "5.87");
  EXPECT_THAT(mb.suffix, "MB");
}

TEST_F(FormatTest, SizeColumnsBlanksTheFractionAreaForExactBytes) {
  // Exact bytes render as the integer; the fraction columns (point + digits) become
  // spaces so a right-aligned number column still lines the (absent) point up.
  const SizeParts parts = SizeColumns(56, SizeUnits::kIec, 2);
  EXPECT_THAT(parts.number, "56   ");  // "56" + three blanks standing in for ".DD"
  EXPECT_THAT(parts.suffix, "B");
  // A one-digit byte and a one-digit scaled value share a number width, so right-aligning
  // the column lines the point (present or blanked) up.
  EXPECT_THAT(SizeColumns(5, SizeUnits::kIec, 2).number, "5   ");
  EXPECT_THAT(SizeColumns(1'024, SizeUnits::kIec, 2).number, "1.00");
}

TEST_F(FormatTest, SizeColumnsZeroPrecisionDropsThePoint) {
  EXPECT_THAT(SizeColumns(5'872'025, SizeUnits::kIec, 0).number, "6");  // rounded, no point
  EXPECT_THAT(SizeColumns(56, SizeUnits::kIec, 0).number, "56");        // no fraction area to blank
}

TEST_F(FormatTest, PadLeftRightJustifies) {
  EXPECT_THAT(PadLeft("42", 5), "   42");
  EXPECT_THAT(PadLeft("42", 2), "42");        // already wide enough
  EXPECT_THAT(PadLeft("12345", 3), "12345");  // never truncates
}

TEST_F(FormatTest, PadRightLeftJustifies) {
  EXPECT_THAT(PadRight("ab", 5), "ab   ");
  EXPECT_THAT(PadRight("ab", 2), "ab");
  EXPECT_THAT(PadRight("abcde", 3), "abcde");
}

TEST_F(FormatTest, TableAlignsColumnsToTheirWidestCell) {
  Table table({Align::kLeft, Align::kRight, Align::kRight});
  table.AddRow({"md", "1", "56"});
  table.AddRow({"txt", "2", "1,239"});
  table.AddRow({"total", "3", "1,295"});
  // Label left-padded to 5 ("total"); count right-aligned to 1; size right to 5.
  // Two-space gaps; the right-most (right-aligned) column leaves no trailing space.
  EXPECT_THAT(table.Render(), EqualsText("md     1     56\ntxt    2  1,239\ntotal  3  1,295\n"));
  EXPECT_THAT(table.RowCount(), 3U);
}

TEST_F(FormatTest, TableLeftColumnHasNoTrailingWhitespaceWhenLast) {
  Table table({Align::kRight, Align::kLeft});
  table.AddRow({"1", "a"});
  table.AddRow({"200", "bb"});
  // Right column is last + left-aligned, so it is emitted without trailing padding.
  EXPECT_THAT(table.Render(), EqualsText("  1  a\n200  bb\n"));
}

TEST_F(FormatTest, ColumnBufferAllAlignsAcrossEveryRow) {
  ColumnBuffer buf({Align::kLeft, Align::kRight}, {0, 0}, ColumnBuffer::kAll);
  EXPECT_THAT(buf.Add({"a", "1"}), "");     // buffered, nothing emitted yet
  EXPECT_THAT(buf.Add({"bb", "200"}), "");  // buffered
  // Flush aligns to the widest cells: col0 width 2, col1 width 3 (two-space gap).
  EXPECT_THAT(buf.Flush(), EqualsText("a     1\nbb  200\n"));
  EXPECT_THAT(buf.Flush(), "");  // idempotent
}

TEST_F(FormatTest, ColumnBufferWindowFlushesThenStreamsAndGrows) {
  ColumnBuffer buf({Align::kLeft, Align::kRight}, {0, 0}, /*window=*/2);
  EXPECT_THAT(buf.Add({"a", "1"}), "");  // still buffering (1 < window)
  // window full -> flush both at widths 2/2
  EXPECT_THAT(buf.Add({"bb", "20"}), EqualsText("a    1\nbb  20\n"));
  // Streaming now: a wider row grows the columns for itself (the flushed rows are out).
  EXPECT_THAT(buf.Add({"ccc", "300"}), EqualsText("ccc  300\n"));
  EXPECT_THAT(buf.Flush(), "");
}

TEST_F(FormatTest, ColumnBufferOffStreamsEachRowAtMinWidths) {
  ColumnBuffer buf({Align::kLeft, Align::kRight}, {3, 4}, /*window=*/0);
  // No buffering: each row emitted immediately, padded to the fixed minimum widths.
  EXPECT_THAT(buf.Add({"a", "1"}), EqualsText("a       1\n"));
  EXPECT_THAT(buf.Add({"bb", "200"}), EqualsText("bb    200\n"));
  EXPECT_THAT(buf.Flush(), "");
}

TEST_F(FormatTest, ParseBufferWindowKeywords) {
  EXPECT_THAT(ParseBufferWindow("auto"), Optional(Eq(std::size_t{100})));
  EXPECT_THAT(ParseBufferWindow("off"), Optional(Eq(std::size_t{0})));
  EXPECT_THAT(ParseBufferWindow("all"), Optional(Eq(ColumnBuffer::kAll)));
}

TEST_F(FormatTest, ParseBufferWindowBareInteger) {
  EXPECT_THAT(ParseBufferWindow("0"), Optional(Eq(std::size_t{0})));
  EXPECT_THAT(ParseBufferWindow("250"), Optional(Eq(std::size_t{250})));
}

TEST_F(FormatTest, ParseBufferWindowScalesTheDecimalSiMultiplier) {
  EXPECT_THAT(ParseBufferWindow("10k"), Optional(Eq(std::size_t{10'000})));
  EXPECT_THAT(ParseBufferWindow("10K"), Optional(Eq(std::size_t{10'000})));  // case-insensitive
  EXPECT_THAT(ParseBufferWindow("2M"), Optional(Eq(std::size_t{2'000'000})));
  EXPECT_THAT(ParseBufferWindow("1G"), Optional(Eq(std::size_t{1'000'000'000})));
}

TEST_F(FormatTest, ParseBufferWindowRejectsByteBudgetsAndGarbage) {
  // A byte-unit form (a trailing B) is a memory budget, not a row window -- handled elsewhere.
  EXPECT_THAT(ParseBufferWindow("10MB"), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow("10MiB"), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow("10B"), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow("garbage"), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow(""), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow("M"), Eq(std::nullopt));  // multiplier with no number
}

TEST_F(FormatTest, ScaledBufferCountsRejectOverflow) {
  EXPECT_THAT(ParseBufferWindow("18446744073709551615T"), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow("18446744073709552k"), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow("-1"), Eq(std::nullopt));
}

TEST_F(FormatTest, ParseByteBudgetDecimalAndBinaryUnits) {
  EXPECT_THAT(ParseByteBudget("10B"), Optional(Eq(std::size_t{10})));
  EXPECT_THAT(ParseByteBudget("10KB"), Optional(Eq(std::size_t{10'000})));        // SI 10^3
  EXPECT_THAT(ParseByteBudget("10MB"), Optional(Eq(std::size_t{10'000'000})));    // SI 10^6
  EXPECT_THAT(ParseByteBudget("2GB"), Optional(Eq(std::size_t{2'000'000'000})));  // SI 10^9
  EXPECT_THAT(ParseByteBudget("10KiB"), Optional(Eq(std::size_t{10'240})));       // IEC 2^10
  EXPECT_THAT(ParseByteBudget("10MiB"), Optional(Eq(std::size_t{10'485'760})));   // IEC 2^20
  EXPECT_THAT(ParseByteBudget("10mb"), Optional(Eq(std::size_t{10'000'000})));    // case-insensitive
  EXPECT_THAT(ParseByteBudget("1PB"), Optional(Eq(std::size_t{1'000'000'000'000'000})));
  EXPECT_THAT(ParseByteBudget("1PiB"), Optional(Eq(std::size_t{1ULL << 50U})));
  EXPECT_THAT(ParseByteBudget("1EB"), Optional(Eq(std::size_t{1'000'000'000'000'000'000})));
  EXPECT_THAT(ParseByteBudget("1EiB"), Optional(Eq(std::size_t{1ULL << 60U})));
}

TEST_F(FormatTest, ParseByteBudgetRejectsRowFormsAndGarbage) {
  EXPECT_THAT(ParseByteBudget("10M"), Eq(std::nullopt));  // a row count (no trailing B)
  EXPECT_THAT(ParseByteBudget("100"), Eq(std::nullopt));
  EXPECT_THAT(ParseByteBudget("all"), Eq(std::nullopt));
  EXPECT_THAT(ParseByteBudget("10iB"), Eq(std::nullopt));  // IEC 'i' with no scale letter
  EXPECT_THAT(ParseByteBudget("B"), Eq(std::nullopt));     // no number
  EXPECT_THAT(ParseByteBudget(""), Eq(std::nullopt));
  EXPECT_THAT(ParseByteBudget("19EB"), Eq(std::nullopt));
  EXPECT_THAT(ParseByteBudget("16EiB"), Eq(std::nullopt));
}

TEST_F(FormatTest, ColumnBufferFlushesOnTheByteBudget) {
  // window kAll (no row cap) plus a 6-byte budget: the buffer keeps accumulating until the
  // buffered cell bytes reach the budget, then flushes the aligned block and streams the rest.
  ColumnBuffer buf({Align::kLeft, Align::kLeft}, {0, 0}, ColumnBuffer::kAll, /*byte_budget=*/6);
  EXPECT_THAT(buf.Add({"a", "b"}), IsEmpty());        // 2 bytes buffered, under budget
  const std::string flushed = buf.Add({"cc", "dd"});  // +4 = 6 >= budget -> flush both rows
  EXPECT_THAT(flushed, Not(IsEmpty()));
  EXPECT_THAT(flushed, HasSubstr("a"));
  EXPECT_THAT(flushed, HasSubstr("cc"));
  EXPECT_THAT(buf.Flush(), IsEmpty());  // everything already emitted
}

}  // namespace
}  // namespace xff::format
