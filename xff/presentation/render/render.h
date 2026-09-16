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

#ifndef XFF_PRESENTATION_RENDER_RENDER_H_
#define XFF_PRESENTATION_RENDER_RENDER_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/btree_map.h"
#include "xff/presentation/format/format.h"

namespace xff::render {

// Output record format for matched paths. kPlain/kNul mirror find's
// -print/-print0; kJsonl is xff's modern one-object-per-line stream; kCsv/kTsv are
// streaming tabular (a one-time header via Header(), then one field row per match);
// kAligned/kMarkdown are buffered tabular (column widths need every row, so the whole
// table renders once via RenderTable): aligned space-padded columns, or a Markdown table.
// kTree renders the matched paths as a directory tree (via the Tree builder), buffered.
enum class Format { kPlain, kNul, kJsonl, kCsv, kTsv, kAligned, kMarkdown, kTree };

// How path bytes are emitted (xff `--path-encoding`). kRaw writes the bytes
// verbatim (find-compatible default). kEscape C-escapes the backslash and control
// characters (`\n`, `\t`, `\r`, else `\xNN`) so a newline or control byte in a
// filename cannot corrupt the line-oriented kPlain stream. It applies only to
// kPlain: kNul stays raw by design (the NUL is the separator) and kJsonl always
// JSON-escapes regardless.
enum class PathEncoding { kRaw, kEscape };

// Formats matched paths into output records. Stateless aside from the format +
// encoding selectors; cheap to copy.
class Renderer {
 public:
  explicit Renderer(Format format, PathEncoding encoding = PathEncoding::kRaw) : format_(format), encoding_(encoding) {}

  // Returns the output record for `path`, terminator included:
  //   kPlain -> "path\n" (path C-escaped when encoding is kEscape),
  //   kNul   -> "path\0" (always raw), kJsonl -> {"path":"<JSON-escaped>"}\n,
  //   kCsv   -> a CSV field + "\n" (RFC-4180: quoted when it holds , " CR or LF),
  //   kTsv   -> a tab-escaped field + "\n" (\t \n \r \\ escaped).
  // `color` is an ANSI SGR parameter (e.g. "1;34"); when non-empty it wraps the
  // kPlain path in `\e[<color>m...\e[0m` (--color). Ignored for kNul (the NUL is the
  // separator) and the machine formats (kJsonl / kCsv / kTsv), which stay uncolored.
  std::string Record(std::string_view path, std::string_view color = {}) const;

  // The one-time header row for the tabular formats (kCsv / kTsv): the column names
  // terminated by a newline (currently the single default column `path`); empty for
  // kPlain / kNul / kJsonl. The driver emits it once before the records, unless
  // --no-header.
  std::string Header() const;

 private:
  Format format_;
  PathEncoding encoding_;
};

// Streams a buffered tabular table -- kAligned (columns padded to their widest cell,
// space-separated, with a dashed underline under the header) or kMarkdown (a GitHub Markdown
// table: `| a | b |` rows, a `| --- | --- |` rule, cells with `|` and newlines escaped) --
// with a bounded buffer so a huge result set need not be held whole. It buffers up to
// `window` rows to size the columns, then emits the header + rule + those rows at the locked
// widths and streams every later row at those widths; a wider later cell just overflows its
// column (like -ls past its --buffer window) so no row is ever dropped. `window == kAll`
// buffers everything and emits it all on Flush() (full alignment; the aligned/md default);
// `window == 0` does not buffer (each row streams as it arrives, columns growing to fit, so
// rows need not align across the run). `with_header`
// false (from --no-header) drops the header and its rule. Feed each match via Add() during
// the walk, then call Flush() once after it. A non-buffered `format` makes every call "".
class TableStream {
 public:
  static constexpr std::size_t kAll = static_cast<std::size_t>(-1);

  // `byte_budget` (0 = none) flushes the window early once the buffered cell bytes reach it,
  // so a --buffer memory cap bounds the table regardless of row count.
  // Alignment defaults to left for unspecified columns; Markdown rules carry right markers.
  TableStream(
      Format format,
      std::vector<std::string> header,
      bool with_header,
      std::size_t window,
      std::size_t byte_budget = 0,
      std::vector<format::Align> alignments = {});

  // Feeds one row of already-rendered cells; returns whatever is ready to emit now (empty
  // while still buffering the initial window). Missing cells render empty; extras are ignored.
  std::string Add(const std::vector<std::string>& cells);

  // Emits any rows still buffered plus a header-only table when nothing matched (call once
  // after the final Add). Idempotent.
  std::string Flush();

 private:
  // The header + rule rows at the current widths, emitted once (nothing if --no-header).
  std::string HeaderAndRule();
  // One data row padded to the current widths.
  std::string Row(const std::vector<std::string>& cells) const;

  bool md_;
  std::size_t columns_;
  bool with_header_;
  std::size_t window_;
  std::size_t byte_budget_;          // 0 = no byte cap; else flush the window at this many buffered bytes
  std::size_t buffered_bytes_ = 0;   // running total of buffered cell bytes (while buffering_)
  std::vector<std::string> header_;  // already encoded (md-escaped) column names
  std::vector<std::size_t> widths_;
  std::vector<format::Align> alignments_;
  std::vector<std::vector<std::string>> buffer_;  // encoded rows held while still buffering
  bool buffering_;
  bool header_done_ = false;
  bool flushed_ = false;
};

// Renders a whole buffered table at once: the convenience wrapper over TableStream with
// window == kAll (full alignment). `header` is the column names, `rows` the data rows of
// already-rendered cells; `with_header` false (from --no-header) drops the header + rule.
// Holds every row (O(rows) memory); returns "" for the streaming / non-tabular formats.
std::string RenderTable(
    Format format,
    const std::vector<std::string>& header,
    const std::vector<std::vector<std::string>>& rows,
    bool with_header = true);

// Encodes one row of already-rendered cell values for a tabular format (--columns): CSV
// (each cell RFC-4180 quoted, comma-joined) or TSV (each cell tab/newline/backslash
// escaped, tab-joined), plus a trailing newline. Shared by the header row (column names)
// and each match's row. Defined only for the streaming tabular formats (kCsv / kTsv); the
// buffered kAligned / kMarkdown render via RenderTable instead, and the non-tabular kPlain /
// kNul / kJsonl are not rows -- all of those yield an empty string.
std::string EncodeTabularRow(Format format, const std::vector<std::string>& cells);

// Builds a directory tree from the matched paths and renders it (--format=tree). Each Add()
// splices a path into a shared-prefix child map, so a match implies its ancestor directories as
// branch nodes and memory is O(distinct path components), not O(rendered bytes). Render() draws
// the tree depth-first with box-drawing connectors: Unicode (the `|-- / `-- / |` glyphs' UTF-8
// forms) when `unicode`, else the ASCII forms. Siblings render lexically (the conventional tree
// order). Buffered: a node's last-child connector needs every sibling, so the whole (cheap)
// structure is built before rendering.
class Tree {
 public:
  explicit Tree(bool unicode) : unicode_(unicode) {}

  // Splices `path` (its '/'-separated components) into the tree; an absolute path roots at "/".
  void Add(std::string_view path);

  // Renders the whole tree: each top-level component is a root line, its descendants indented
  // under it. Empty (no paths added) yields "".
  std::string Render() const;

 private:
  struct Node {
    // btree_map (not std::map) keeps children lexically ordered; the value is a unique_ptr
    // because absl::btree_map needs a complete value type while Node is recursive.
    absl::btree_map<std::string, std::unique_ptr<Node>> children;
  };

  // Appends `node`'s children under `prefix`, each recursing with the connector-extended prefix.
  void RenderChildren(const Node& node, std::string_view prefix, std::string& out) const;

  bool unicode_;
  Node root_;  // the forest; root_.children are the top-level nodes (roots / first components)
};

}  // namespace xff::render

#endif  // XFF_PRESENTATION_RENDER_RENDER_H_
