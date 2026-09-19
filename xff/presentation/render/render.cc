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

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "nlohmann/json.hpp"

namespace xff::render {
namespace {

// Appends `path` to `out` as a JSON string body (without the surrounding
// quotes), escaping per RFC 8259: quote, backslash, and the control characters
// U+0000..U+001F (the common ones by name, the rest as \u00XX).
void AppendJsonEscaped(std::string_view path, std::string& out) {
  static constexpr std::string_view kHex = "0123456789abcdef";
  for (const char ch : path) {
    const auto byte = static_cast<unsigned char>(ch);
    switch (ch) {
      case '"': out.append("\\\""); break;
      case '\\': out.append("\\\\"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      default:
        if (byte < 0x20) {
          out.append("\\u00");
          out.push_back(kHex[static_cast<unsigned>(byte) >> 4U]);
          out.push_back(kHex[static_cast<unsigned>(byte) & 0x0FU]);
        } else {
          out.push_back(ch);
        }
    }
  }
}

// Appends `path` to `out`, C-escaping the backslash and control characters so a
// newline or control byte in a filename cannot corrupt line-oriented output: `\\`,
// `\n`, `\t`, `\r`, and any other byte < 0x20 or 0x7F (DEL) as `\xNN`. Printable
// ASCII and high (UTF-8) bytes pass through verbatim.
void AppendCEscaped(std::string_view path, std::string& out) {
  static constexpr std::string_view kHex = "0123456789ABCDEF";
  for (const char ch : path) {
    const auto byte = static_cast<unsigned char>(ch);
    switch (ch) {
      case '\\': out.append("\\\\"); break;
      case '\n': out.append("\\n"); break;
      case '\t': out.append("\\t"); break;
      case '\r': out.append("\\r"); break;
      default:
        if (byte < 0x20 || byte == 0x7F) {
          out.append("\\x");
          out.push_back(kHex[static_cast<unsigned>(byte) >> 4U]);
          out.push_back(kHex[static_cast<unsigned>(byte) & 0x0FU]);
        } else {
          out.push_back(ch);
        }
    }
  }
}

// Appends `field` to `out` as one RFC-4180 CSV field: quoted only when it contains a
// comma, double-quote, CR, or LF, with each interior `"` doubled. A path never holds a
// NUL or '/'-embedded control that would break this, so no other escaping is needed.
void AppendCsvField(std::string_view field, std::string& out) {
  if (field.find_first_of(",\"\r\n") == std::string_view::npos) {
    out.append(field);
    return;
  }
  out.push_back('"');
  for (const char ch : field) {
    if (ch == '"') {
      out.push_back('"');  // double the interior quote
    }
    out.push_back(ch);
  }
  out.push_back('"');
}

// Appends `field` to `out` as one TSV field: a TSV field cannot hold a literal tab or
// newline, so escape tab / newline / CR / backslash as `\t` / `\n` / `\r` / `\\`.
void AppendTsvField(std::string_view field, std::string& out) {
  for (const char ch : field) {
    switch (ch) {
      case '\\': out.append("\\\\"); break;
      case '\t': out.append("\\t"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      default: out.push_back(ch);
    }
  }
}

// Preserve the display spelling through Markdown's own backslash processing.
std::string MarkdownCell(std::string_view field) {
  std::string out;
  for (const char ch : field) {
    if (ch == '\\' || ch == '|') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

}  // namespace

std::string EscapeDisplayText(std::string_view text) {
  std::string out;
  AppendCEscaped(text, out);
  return out;
}

std::string JsonQuote(std::string_view text) {
  std::string result = "\"";
  AppendJsonEscaped(text, result);
  result.push_back('"');
  return result;
}

std::string JsonValue(std::string_view bytes) {
  std::string quoted = JsonQuote(bytes);
  // Reuse the JSON parser's UTF-8 validation; quoting already handles the JSON syntax.
  const bool ascii = std::ranges::all_of(bytes, [](char byte) { return static_cast<unsigned char>(byte) < 0x80; });
  if (ascii || nlohmann::json::accept(quoted)) {
    return quoted;
  }
  return absl::StrCat(R"({"encoding":"base64","data":")", absl::Base64Escape(bytes), R"("})");
}

std::string Renderer::Record(std::string_view path, std::string_view color) const {
  switch (format_) {
    case Format::kAligned:
    case Format::kMarkdown:
    case Format::kTree:
      // Buffered formats: the whole output renders once (RenderTable / Tree), so a single
      // record has no standalone encoding here. Emit the raw path line as a defensive fallback
      // (the walk driver routes these formats through their builder, never Record).
      return absl::StrCat(path, "\n");
    case Format::kCsv: {
      std::string record;
      AppendCsvField(path, record);
      record.push_back('\n');
      return record;
    }
    case Format::kJsonl: return absl::StrCat("{\"path\":", JsonValue(path), "}\n");
    case Format::kNul: {
      std::string record(path);
      record.push_back('\0');
      return record;
    }
    case Format::kPlain: {
      std::string body;  // the path bytes (raw or C-escaped), before color + newline
      if (encoding_ == PathEncoding::kEscape) {
        AppendCEscaped(path, body);
      } else {
        body = std::string(path);
      }
      if (color.empty()) {
        return absl::StrCat(body, "\n");
      }
      return absl::StrCat("\x1b[", color, "m", body, "\x1b[0m\n");
    }
    case Format::kTsv: {
      std::string record;
      AppendTsvField(path, record);
      record.push_back('\n');
      return record;
    }
  }
  return absl::StrCat(path, "\n");  // unreachable: every Format returns above
}

std::string EncodeTabularRow(Format format, const std::vector<std::string>& cells) {
  switch (format) {
    case Format::kCsv:
    case Format::kTsv: {
      std::string out;
      bool first = true;
      for (const std::string& cell : cells) {
        if (!first) {
          out.push_back(format == Format::kTsv ? '\t' : ',');
        }
        first = false;
        if (format == Format::kTsv) {
          AppendTsvField(cell, out);
        } else {
          AppendCsvField(cell, out);
        }
      }
      out.push_back('\n');
      return out;
    }
    case Format::kAligned:
    case Format::kMarkdown:
    case Format::kTree:
    case Format::kJsonl:
    case Format::kNul:
    case Format::kPlain: return "";  // streaming per-row is csv/tsv only (buffered ones use their builder)
  }
  return "";  // unreachable: every Format handled above
}

std::string Renderer::Header() const {
  switch (format_) {
    case Format::kCsv: {
      std::string header;
      AppendCsvField("path", header);  // slice 1: the single default column
      header.push_back('\n');
      return header;
    }
    case Format::kTsv: {
      std::string header;
      AppendTsvField("path", header);
      header.push_back('\n');
      return header;
    }
    case Format::kAligned:
    case Format::kMarkdown:
    case Format::kTree:
    case Format::kJsonl:
    case Format::kNul:
    case Format::kPlain: return "";  // buffered formats emit their header inside their builder
  }
  return "";  // unreachable: every Format handled above
}

TableStream::TableStream(
    Format format,
    std::vector<std::string> header,
    bool with_header,
    std::size_t window,
    std::size_t byte_budget,
    std::vector<format::Align> alignments)
    : md_(format == Format::kMarkdown),
      columns_(format == Format::kAligned || format == Format::kMarkdown ? header.size() : 0),
      with_header_(with_header),
      window_(window),
      byte_budget_(byte_budget),
      widths_(columns_, md_ ? 3 : 0),
      alignments_(std::move(alignments)),
      buffering_(window != 0) {
  alignments_.resize(columns_, format::Align::kLeft);
  header_.reserve(columns_);
  for (std::size_t col = 0; col < columns_; ++col) {
    const std::string display = EscapeDisplayText(header[col]);
    header_.push_back(md_ ? MarkdownCell(display) : display);
    if (with_header_) {
      widths_[col] = std::max(widths_[col], header_[col].size());
    }
  }
}

std::string TableStream::Row(const std::vector<std::string>& cells) const {
  std::string out;
  for (std::size_t col = 0; col < columns_; ++col) {
    if (md_) {
      out.append(col == 0 ? "| " : " | ");
    }
    const std::string& cell = cells[col];
    const bool right = alignments_[col] == format::Align::kRight;
    if (right) {
      out.append(widths_[col] - cell.size(), ' ');
    }
    out.append(cell);
    // Pad to the column width, except the last cell of an aligned row (no trailing space).
    // widths_ always covers every cell already emitted, so the subtraction never underflows.
    if (!right && (md_ || col + 1 < columns_)) {
      out.append(widths_[col] - cell.size(), ' ');
    }
    if (md_ && col + 1 == columns_) {
      out.append(" |");
    } else if (!md_ && col + 1 < columns_) {
      out.append("  ");  // two-space column gap
    }
  }
  out.push_back('\n');
  return out;
}

std::string TableStream::HeaderAndRule() {
  if (header_done_ || !with_header_) {
    return "";
  }
  header_done_ = true;
  std::string out = Row(header_);
  // The rule under the header: `| --- | --- |` for md, a dashed underline for aligned.
  for (std::size_t col = 0; col < columns_; ++col) {
    if (md_) {
      out.append(col == 0 ? "| " : " | ");
    }
    const bool right = md_ && alignments_[col] == format::Align::kRight;
    out.append(widths_[col] - (right ? 1 : 0), '-');
    if (right) {
      out.push_back(':');
    }
    if (md_ && col + 1 == columns_) {
      out.append(" |");
    } else if (!md_ && col + 1 < columns_) {
      out.append("  ");
    }
  }
  out.push_back('\n');
  return out;
}

std::string TableStream::Add(const std::vector<std::string>& cells) {
  return AddImpl(cells, false);
}

std::string TableStream::AddDisplay(const std::vector<std::string>& cells) {
  return AddImpl(cells, true);
}

std::string TableStream::AddImpl(const std::vector<std::string>& cells, bool display_text) {
  if (columns_ == 0) {
    return "";  // not a buffered tabular format
  }
  std::vector<std::string> row;
  row.reserve(columns_);
  for (std::size_t col = 0; col < columns_; ++col) {
    const std::string_view cell = col < cells.size() ? std::string_view(cells[col]) : std::string_view();
    const std::string display = display_text ? std::string(cell) : EscapeDisplayText(cell);
    row.push_back(md_ ? MarkdownCell(display) : display);
  }
  for (std::size_t col = 0; col < columns_; ++col) {
    widths_[col] = std::max(widths_[col], row[col].size());
  }
  if (buffering_) {
    for (const std::string& cell : row) {
      buffered_bytes_ += cell.size();
    }
    buffer_.push_back(std::move(row));
    if ((window_ != kAll && buffer_.size() >= window_) || (byte_budget_ != 0 && buffered_bytes_ >= byte_budget_)) {
      // The window is full (by row count or the byte budget): lock the widths, flush the
      // header + rule + buffered rows, then stream. A later wider cell grows its column for
      // that row only (earlier rows keep the locked width), so no row is dropped -- the same
      // graceful skew as -ls past its window.
      buffering_ = false;
      std::string out = HeaderAndRule();
      for (const std::vector<std::string>& buffered : buffer_) {
        out += Row(buffered);
      }
      buffer_.clear();
      return out;
    }
    return "";
  }
  // Streaming (window == 0, or already past the initial window): emit at the current widths.
  return HeaderAndRule() + Row(row);
}

std::string TableStream::Flush() {
  if (columns_ == 0 || flushed_) {
    return "";
  }
  flushed_ = true;
  if (buffering_) {
    // The window was never reached (or window == kAll): full alignment of everything buffered.
    buffering_ = false;
    std::string out = HeaderAndRule();
    for (const std::vector<std::string>& buffered : buffer_) {
      out += Row(buffered);
    }
    buffer_.clear();
    return out;
  }
  // Streaming already emitted every row; emit the header alone if nothing ever streamed.
  return HeaderAndRule();
}

std::string RenderTable(
    Format format,
    const std::vector<std::string>& header,
    const std::vector<std::vector<std::string>>& rows,
    bool with_header) {
  TableStream stream(format, header, with_header, TableStream::kAll);
  std::string out;
  for (const std::vector<std::string>& row : rows) {
    out += stream.Add(row);  // window == kAll buffers every row, so each Add returns ""
  }
  out += stream.Flush();
  return out;
}

void Tree::Add(std::string_view path) {
  std::reference_wrapper<Node> node = root_;
  const auto descend = [&node](std::string_view name) {
    std::unique_ptr<Node>& child = node.get().children[std::string(name)];  // create-or-descend
    if (child == nullptr) {
      child = std::make_unique<Node>();
    }
    node = *child;  // shared prefixes reuse the same node
  };
  if (!path.empty() && path.front() == '/') {
    descend("/");  // an absolute path roots at "/"
  }
  for (const std::string_view part : absl::StrSplit(path, '/', absl::SkipEmpty())) {
    descend(part);
  }
}

// Deliberate recursion: the tree renderer descends into each node's children by design.
// NOLINTNEXTLINE(misc-no-recursion)
void Tree::RenderChildren(const Node& node, std::string_view prefix, std::string& out) const {
  // Box-drawing connectors when unicode_: tee (U+251C), elbow (U+2514), and vertical (U+2502)
  // with horizontals (U+2500), as literal UTF-8; else the ASCII forms.
  const std::string_view tee = unicode_ ? "\u251c\u2500\u2500 " : "|-- ";
  const std::string_view elbow = unicode_ ? "\u2514\u2500\u2500 " : "`-- ";
  const std::string_view vertical = unicode_ ? "\u2502   " : "|   ";
  static constexpr std::string_view kGap = "    ";
  std::size_t index = 0;
  const std::size_t count = node.children.size();
  for (const auto& [name, child] : node.children) {
    const bool last = ++index == count;
    out.append(prefix);
    out.append(last ? elbow : tee);
    AppendCEscaped(name, out);
    out.push_back('\n');
    RenderChildren(*child, absl::StrCat(prefix, last ? kGap : vertical), out);
  }
}

std::string Tree::Render() const {
  // Each top-level node is a bare root line (no connector); its descendants indent under it.
  std::string out;
  for (const auto& [name, child] : root_.children) {
    AppendCEscaped(name, out);
    out.push_back('\n');
    RenderChildren(*child, "", out);
  }
  return out;
}

}  // namespace xff::render
