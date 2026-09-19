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

#include "xff/cli/plain_backend.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "xff/cli/help_model.h"
#include "xff/cli/wrap.h"

namespace xff::cli {
namespace {

// The help color palette (ANSI SGR). WrapText measures visible width (skipping these
// escapes), so colored words wrap by their on-screen size; still, applied per line /
// per whole-line element so a reset never straddles a newline.
constexpr std::string_view kReset = "\x1b[0m";
constexpr std::string_view kHeading = "\x1b[1m";   // section / subsection headings: bold
constexpr std::string_view kName = "\x1b[1;36m";   // flag / primary names (entry terms): bold cyan
constexpr std::string_view kValue = "\x1b[36m";    // value-table values / items: cyan
constexpr std::string_view kExample = "\x1b[32m";  // verbatim example / command code: green

// Wraps `text` in `code` (+ reset) when `color`, else returns it unchanged. Resolving
// auto vs always (and NO_COLOR) happens at the CLI boundary; here `color` is the decision.
std::string Sgr(std::string_view text, std::string_view code, bool color) {
  return color ? absl::StrCat(code, text, kReset) : std::string(text);
}

// Reconstructs the authored inline string, keeping `code` backticks - a term, a row
// description, and an entry's summary / detail pass through with their markup (as the
// hand-written plain help did), unlike free prose which drops it.
std::string RenderInlinesRaw(const Inlines& runs) {
  std::string out;
  for (const Inline& run : runs) {
    if (run.style == Inline::Style::kCode) {
      absl::StrAppend(&out, "`", run.text, "`");
    } else {
      absl::StrAppend(&out, run.text);
    }
  }
  return out;
}

// A narrow page cannot keep every policy column side by side without overflowing.
// Retain each row's identity and every column label when stacking its values.
std::string StackedTable(const Table& table, std::size_t width, std::string_view indent, bool color) {
  std::string out;
  const std::string value_indent = absl::StrCat(indent, "  ");
  for (const auto& row : table.cells) {
    if (!out.empty()) {
      absl::StrAppend(&out, "\n");
    }
    for (std::size_t index = 0; index < row.size(); ++index) {
      const auto prefix = index == 0 ? indent : std::string_view(value_indent);
      absl::StrAppend(
          &out,
          WrapText(
              absl::StrCat(Sgr(table.header.at(index), kValue, color), ": ", row.at(index)), width, prefix, prefix));
    }
  }
  return out;
}

}  // namespace

std::string PlainRefLocator(const RefTarget& target) {
  switch (target.kind) {
    case RefTarget::Kind::kTopic: return "--help=" + target.id;
    case RefTarget::Kind::kManPage: return target.id + "(" + target.section + ")";
    case RefTarget::Kind::kFlag:
    case RefTarget::Kind::kPrimary:
    case RefTarget::Kind::kUrl:
    case RefTarget::Kind::kAnchor: return target.id;
  }
  return target.id;
}

std::string RenderInlinesPlain(const Inlines& runs) {
  std::string out;
  for (const Inline& run : runs) {
    // Free prose drops emphasis markup; a ref shows its label, or its plain locator
    // when the label is empty. All other styles are literal text.
    if (run.style == Inline::Style::kRef && run.text.empty() && run.target.has_value()) {
      absl::StrAppend(&out, PlainRefLocator(*run.target));
    } else {
      absl::StrAppend(&out, run.text);
    }
  }
  return out;
}

void PlainTextBackend::Append(std::string_view text, bool allow_repeated_blank_lines) {
  for (const char ch : text) {
    if (ch == ' ' || ch == '\t') {
      pending_spaces_.push_back(ch);
      continue;
    }
    if (ch == '\n') {
      if (line_has_content_ || allow_repeated_blank_lines || (!out_.empty() && !last_line_blank_)) {
        if (line_has_content_ || allow_repeated_blank_lines) {
          out_ += pending_spaces_;
        }
        out_.push_back(ch);
        last_line_blank_ = !line_has_content_;
      }
      pending_spaces_.clear();
      line_has_content_ = false;
      continue;
    }
    out_ += pending_spaces_;
    pending_spaces_.clear();
    out_.push_back(ch);
    line_has_content_ = true;
  }
}

void PlainTextBackend::StartBlock() {
  Append("\n\n");
}

void PlainTextBackend::Preamble(const Document& doc) {
  if (doc.name.empty()) {
    return;  // a section-only render (e.g. a single help topic) carries no preamble
  }
  StartBlock();
  // The identity line: the program name reads as a name (bold cyan), the tagline plain.
  const bool color = Context().color;
  Append(
      absl::StrCat(WrapText(absl::StrCat(Sgr(doc.name, kName, color), " - ", doc.tagline), Context().width, "", "")));
  // The usage synopsis is a command template, so the whole `name synopsis` reads as code.
  Append(absl::StrCat("\nUsage: ", Sgr(absl::StrCat(doc.name, " ", doc.usage), kExample, color), "\n"));
}

void PlainTextBackend::BeginSection(const Section& section) {
  if (section.title.empty()) {
    return;  // a title-less section (e.g. a single-entry --help=NAME) carries no heading or indent
  }
  // House style: top-level headings are upper-cased (PRINTF DIRECTIVES / TIME FORMATS).
  StartBlock();
  Append(absl::StrCat(BodyIndent(), Sgr(absl::AsciiStrToUpper(section.title), kHeading, Context().color), "\n"));
  ++depth_;  // the section body indents under its heading
}

void PlainTextBackend::EndSection(const Section& section) {
  if (!section.title.empty()) {
    --depth_;
  }
}

std::string PlainTextBackend::BodyIndent() const {
  // The (count, char) constructor is intended; a braced `{count, ' '}` would list-initialize as
  // initializer_list<char> (the classic std::string gotcha), not repeat the space.
  // NOLINTNEXTLINE(modernize-return-braced-init-list)
  return std::string(static_cast<std::size_t>(2 * depth_), ' ');
}

void PlainTextBackend::BeginSubsection(const Subsection& subsection) {
  // A title-less subsection carries no heading; it just indents its children one level
  // deeper (e.g. a flag's value table nested under its "<LABEL> is one of:" line).
  if (subsection.title.empty()) {
    ++depth_;
    return;
  }
  StartBlock();
  Append(absl::StrCat(BodyIndent(), Sgr(absl::StrCat(subsection.title, ":"), kHeading, Context().color), "\n"));
  ++depth_;  // the subsection body indents under its heading
}

void PlainTextBackend::EndSubsection(const Subsection& /*subsection*/) {
  --depth_;
}

void PlainTextBackend::BeginEntry(const Entry& entry) {
  StartBlock();
  // Classification tags after the term: the explicit list when present, else the
  // bare xff marker (kept until every entry carries tags).
  std::string tag;
  if (!entry.tags.empty()) {
    tag = absl::StrCat("  (", absl::StrJoin(entry.tags, ", "), ")");
  } else if (entry.xff) {
    tag = "  (xff)";
  }
  Append(absl::StrCat(BodyIndent(), Sgr(entry.term, kName, Context().color), tag, "\n"));
  ++depth_;  // the summary + detail indent under the term
  const std::string indent = BodyIndent();
  Append(absl::StrCat(WrapText(RenderInlinesRaw(entry.summary), Context().width, indent, indent)));
  in_entry_ = true;
}

void PlainTextBackend::EndEntry(const Entry& /*entry*/) {
  in_entry_ = false;
  --depth_;
}

void PlainTextBackend::EmitProse(const Prose& prose) {
  const std::string indent = BodyIndent();
  if (in_entry_) {
    // An entry's detail line, under its term indent (keeps `code` markup, no blank line).
    Append(absl::StrCat(WrapText(RenderInlinesRaw(prose.runs), Context().width, indent, indent)));
    return;
  }
  StartBlock();
  Append(absl::StrCat(WrapText(RenderInlinesPlain(prose.runs), Context().width, indent, indent)));
}

void PlainTextBackend::EmitExample(const Example& example) {
  StartBlock();
  const std::string indent = BodyIndent();
  // Verbatim, but each line carries the current subsection body indent so a recipe's
  // command sits under its heading.
  std::string_view text = example.text;
  if (!text.empty() && text.back() == '\n') {
    text.remove_suffix(1);  // avoid a trailing indent-only line from a final newline
  }
  for (const std::string_view line : absl::StrSplit(text, '\n')) {
    Append(absl::StrCat(indent, Sgr(line, kExample, Context().color), "\n"), true);
  }
}

void PlainTextBackend::EmitBullets(const Bullets& bullets) {
  // Glued directly under its heading (no leading blank line), at the body indent.
  const std::string indent = BodyIndent();
  for (const Inlines& item : bullets.items) {
    Append(absl::StrCat(WrapText(RenderInlinesPlain(item), Context().width, indent + "- ", indent + "  ")));
  }
}

void PlainTextBackend::EmitRows(const Rows& rows) {
  if (!rows.rows.empty()) {
    StartBlock();
  }
  // The shared {term, description} layout: the body indent, then the description column
  // two spaces past the widest term - so this table aligns like the --help=printf /
  // time / size ones.
  const std::string indent = BodyIndent();
  std::vector<std::string> descriptions;
  descriptions.reserve(rows.rows.size());
  for (const Row& row : rows.rows) {
    descriptions.push_back(RenderInlinesRaw(row.description));
  }
  std::size_t term_width = 0;
  for (const Row& row : rows.rows) {
    term_width = std::max(term_width, row.term.size());
  }
  term_width += 2;  // a 2-space gap after the widest term

  // The description column hangs under the padded term column. WrapText with width==0
  // emits the verbatim aligned line (prefix + description); width>0 wraps the description
  // with its continuation lines under the same column. The term is colored as a value /
  // item; padding uses the raw term length (the color escapes are zero-width) so the
  // column still lines up, and WrapText measures visible width for the same reason.
  const std::string hang = indent + std::string(term_width, ' ');
  for (std::size_t i = 0; i < rows.rows.size(); ++i) {
    const std::string_view term = rows.rows[i].term;
    const std::string prefix =
        absl::StrCat(indent, Sgr(term, kValue, Context().color), std::string(term_width - term.size(), ' '));
    Append(absl::StrCat(WrapText(descriptions[i], Context().width, prefix, hang)));
  }
}

void PlainTextBackend::EmitTable(const Table& table) {
  // Width-aligned columns under the body indent: every column but the LAST is padded to its widest
  // cell (2-space gap), the last wraps to the page width with continuation lines hanging under its
  // own column - the model's contract that the prose-length column goes last. A dash rule under the
  // header marks it without stealing a color.
  StartBlock();
  const std::string indent = BodyIndent();
  std::vector<std::size_t> widths(table.header.size(), 0);
  for (std::size_t i = 0; i < table.header.size(); ++i) {
    widths[i] = table.header[i].size();
  }
  for (const std::vector<std::string>& row : table.cells) {
    for (std::size_t i = 0; i + 1 < row.size() && i < widths.size(); ++i) {
      widths[i] = std::max(widths[i], row[i].size());
    }
  }
  std::size_t hang_width = 0;
  for (std::size_t i = 0; i + 1 < widths.size(); ++i) {
    hang_width += widths[i] + 2;
  }
  const std::string hang = indent + std::string(hang_width, ' ');
  std::size_t last_minimum = table.header.back().size();
  for (const auto& row : table.cells) {
    for (const std::string_view word : absl::StrSplit(row.back(), absl::ByAnyChar(" \t\n"), absl::SkipEmpty())) {
      last_minimum = std::max(last_minimum, word.size());
    }
  }
  if (Context().width != 0 && hang.size() + last_minimum > Context().width) {
    Append(absl::StrCat(StackedTable(table, Context().width, indent, Context().color)));
    return;
  }
  const auto emit_row = [&](const std::vector<std::string>& row, bool colored) {
    std::string prefix = indent;
    for (std::size_t i = 0; i + 1 < row.size(); ++i) {
      const std::string_view cell = row[i];
      absl::StrAppend(
          &prefix, colored ? Sgr(cell, kValue, Context().color) : std::string(cell),
          std::string(widths[i] + 2 - cell.size(), ' '));
    }
    Append(absl::StrCat(WrapText(row.back(), Context().width, prefix, hang)));
  };
  emit_row(table.header, /*colored=*/true);
  std::vector<std::string> rule;
  rule.reserve(widths.size());
  for (const std::size_t width : widths) {
    rule.emplace_back(width, '-');
  }
  emit_row(rule, /*colored=*/false);
  for (const std::vector<std::string>& row : table.cells) {
    emit_row(row, /*colored=*/false);
  }
}

void PlainTextBackend::EmitSeeAlso(const SeeAlso& see_also) {
  if (see_also.in_document) {
    return;
  }
  StartBlock();
  const std::string indent = BodyIndent();
  std::string text;
  if (!see_also.refs.empty() && see_also.refs.front().kind != RefTarget::Kind::kManPage) {
    absl::StrAppend(&text, "See also: ");
  }
  std::string_view sep;
  for (const RefTarget& ref : see_also.refs) {
    absl::StrAppend(&text, sep, HelpReferenceLabel(ref));
    sep = ", ";
  }
  Append(absl::StrCat(WrapText(text, Context().width, indent, indent)));
  if (!see_also.note.empty()) {
    StartBlock();
    Append(absl::StrCat(WrapText(RenderInlinesPlain(see_also.note), Context().width, indent, indent)));
  }
}

std::string PlainTextBackend::Take() {
  out_ += pending_spaces_;
  pending_spaces_.clear();
  return std::move(out_);
}

}  // namespace xff::cli
