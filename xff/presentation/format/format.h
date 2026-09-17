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

#ifndef XFF_PRESENTATION_FORMAT_FORMAT_H_
#define XFF_PRESENTATION_FORMAT_FORMAT_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Shared number/text formatting for xff's human-facing output (the --summary
// table today; -ls column alignment and more later). The single place that knows
// how a count or a byte size is rendered, so every report groups digits and pads
// columns the same way. Size-unit rendering (--human) is layered on in a follow-up.
namespace xff::format {

// Renders `value` in decimal. When `group_sep` is non-zero the digits are grouped
// in threes from the right with that separator (e.g. Int(1234567, ',') ==
// "1,234,567"); the default (`\0`) leaves them ungrouped ("1234567").
std::string Int(std::uint64_t value, char group_sep = '\0');

// Which powers/suffixes Size() uses: kIec is binary (1024) with IEC suffixes
// (B/KiB/MiB/GiB/...), matching xff's -size units; kSi is decimal (1000) with SI
// suffixes (B/kB/MB/GB/...), like `ls --si` and disk-vendor sizes.
enum class SizeUnits { kIec, kSi };

// Renders `bytes` as a human-readable size (du -h / ls -h style): exact bytes under
// one unit ("56 B"), otherwise the value scaled to the largest fitting unit with one
// decimal and a space before the suffix ("5.6 MiB", "5.9 MB"). `units` picks the
// binary (kIec) or decimal (kSi) scale.
std::string Size(std::uint64_t bytes, SizeUnits units);

// A human size split into a right-alignable number and a left-alignable unit suffix,
// for a column-aligned report (--summary). See SizeColumns.
struct SizeParts {
  std::string number;
  std::string suffix;
};

// Like Size, but with the UNIT right-aligned in the width of this system's prefixed suffixes
// (2 for SI's kB / MB, 3 for IEC's KiB / MiB), so a bare byte count gets the extra space its
// missing prefix letter would occupy: "991  B" lines its `B` up under the `B` of "1.2 kB". For a
// size COLUMN that is right-aligned as a whole (`-ls`), where Size alone would align the last
// letter but leave the numbers ragged. Size stays the scalar spelling, for prose and fields.
std::string SizeAligned(std::uint64_t bytes, SizeUnits units);

// Like Size, but split for two-column alignment. `number` carries a fixed-width fraction
// area -- `fraction_digits` decimals for a scaled unit ("12.34"), or, for an exact byte
// count, the integer with that area blanked ("12   ") -- so right-aligning the number
// column lines every decimal point up (and blanks it for bytes) while the suffixes, in a
// left-aligned column, all start at one place. So a caller pads: PadLeft(number, w) + " "
// + suffix. `fraction_digits == 0` drops the point (both scaled and bytes are integers).
SizeParts SizeColumns(std::uint64_t bytes, SizeUnits units, unsigned fraction_digits);

// Right-justifies `text` in a field of `width` columns by prepending spaces (no
// truncation: a longer `text` is returned unchanged). For aligning a numeric
// column to its widest cell.
std::string PadLeft(std::string_view text, std::size_t width);

// Left-justifies `text` in a field of `width` columns by appending spaces (no
// truncation). For a left-aligned label column.
std::string PadRight(std::string_view text, std::size_t width);

// Per-column alignment for a Table cell.
enum class Align { kLeft, kRight };

// Accumulates rows of already-formatted cells and tracks the maximum width of each
// column as rows are added, so the whole table can be rendered aligned to its
// widest cell per column. This is the shared "column context" for xff's aligned
// reports (--summary now; -ls and other tabular output next) -- callers format each
// cell (via Int/human sizes/etc.), AddRow them, then Render once.
class Table {
 public:
  // One Align per column; every AddRow must supply exactly this many cells.
  explicit Table(std::vector<Align> alignments);

  // Appends a row of pre-formatted cells, widening each column to fit. Extra cells
  // beyond the column count are ignored; missing ones render empty.
  void AddRow(std::vector<std::string> cells);

  // Renders every row with each cell padded to its column's width per its Align,
  // columns separated by `gap`, each row '\n'-terminated. The right-most cell is not
  // right-padded, so no line carries trailing whitespace.
  std::string Render(std::string_view gap = "  ") const;

  std::size_t RowCount() const { return rows_.size(); }

 private:
  std::vector<Align> aligns_;
  std::vector<std::size_t> widths_;
  std::vector<std::vector<std::string>> rows_;
};

// Streaming column aligner for tabular output that arrives row by row (e.g. -ls,
// which emits one line per entry during the walk). Buffers up to `window` rows to
// compute per-column widths, emits that block aligned, then streams each later row
// using those widths -- growing a column when a wider cell appears. `window == 0`
// disables buffering (every row is emitted immediately at the minimum widths, so
// rows do not align across each other); `window == kAll` buffers everything (full
// alignment, emitted on Flush). Backs -ls's --buffer=auto|off|all|N.
class ColumnBuffer {
 public:
  static constexpr std::size_t kAll = static_cast<std::size_t>(-1);

  // `aligns` and `mins` are per-column (mins are the minimum, and the fixed, widths
  // when window == 0); `window` is how many rows to buffer before the first flush.
  // `byte_budget` (0 = none) flushes the window early once the buffered cell bytes reach it,
  // so a --buffer memory cap bounds the buffer regardless of row count.
  ColumnBuffer(
      std::vector<Align> aligns,
      std::vector<std::size_t> mins,
      std::size_t window,
      std::size_t byte_budget = 0);

  // Feeds one row; returns whatever is now ready to emit (empty while still buffering
  // the initial window). Extra cells beyond the column count are ignored; missing
  // ones render empty.
  std::string Add(std::vector<std::string> cells);

  // Emits any rows still buffered (call once after the final Add). Idempotent.
  std::string Flush();

 private:
  std::vector<Align> aligns_;
  std::vector<std::size_t> widths_;  // starts at the mins; grows to the widest cell seen
  std::size_t window_;
  std::size_t byte_budget_;         // 0 = no byte cap; else flush the window at this many buffered bytes
  std::size_t buffered_bytes_ = 0;  // running total of buffered cell bytes (while buffering_)
  bool buffering_;
  std::vector<std::vector<std::string>> buffer_;
};

// Parses a --buffer=VALUE selector into a row window for the column buffers: "auto" -> 100,
// "off" -> 0, "all" -> ColumnBuffer::kAll, a bare integer -> that many rows, or an integer
// with a decimal SI multiplier suffix k/M/G/T (case-insensitive: `10k` = 10'000, `10M` = ten
// million). Returns nullopt for anything else -- a byte-budget form such as `10MB` / `10MiB`
// (a memory bound, handled elsewhere), overflow, or garbage. Option validation must reject
// a value when neither this parser nor ParseByteBudget accepts it.
// `auto` maps to 100 here; the per-format default for an absent flag is the caller's concern.
std::optional<std::size_t> ParseBufferWindow(std::string_view value);

// Parses a --buffer=VALUE memory budget into a byte count: an integer with a byte unit whose
// final letter is `B` (case-insensitive) -- bare `B` (bytes), or a scale letter k/M/G/T/P/E with a
// decimal (SI) base (`10MB` = 10 * 10^6) or an `i` before the `B` for the binary (IEC) base
// (`10MiB` = 10 * 2^20). The trailing `B` is what marks a byte budget rather than a row count
// (`10M` = ten million rows, `10MB` = ten megabytes). Returns nullopt for a non-byte value (a
// row count or keyword, or garbage such as `10iB` with no scale), so the caller treats it as a
// row window instead. Pair with ParseBufferWindow: try the window first, then the byte budget.
std::optional<std::size_t> ParseByteBudget(std::string_view value);

}  // namespace xff::format

#endif  // XFF_PRESENTATION_FORMAT_FORMAT_H_
