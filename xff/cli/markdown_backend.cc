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

#include <cstddef>
#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

// The GitHub anchor slug for an in-document cross-reference: lower-cased, leading
// option dashes dropped, and any run of non-alphanumerics folded to a single '-'.
std::string SlugFor(const RefTarget& target) {
  std::string slug;
  bool pending_dash = false;
  const std::string id = target.kind == RefTarget::Kind::kTopic     ? "topic-" + target.id
                         : target.kind == RefTarget::Kind::kFlag    ? "flag-" + target.id
                         : target.kind == RefTarget::Kind::kPrimary ? "primary-" + target.id
                                                                    : target.id;
  for (const char chr : id) {
    if (absl::ascii_isalnum(chr)) {
      if (pending_dash && !slug.empty()) {
        slug.push_back('-');
      }
      pending_dash = false;
      slug.push_back(absl::ascii_tolower(chr));
    } else {
      pending_dash = true;
    }
  }
  return slug;
}

}  // namespace

std::string MarkdownRefLink(const RefTarget& target, std::string_view label) {
  const std::string_view text = label.empty() ? target.id : label;
  switch (target.kind) {
    case RefTarget::Kind::kUrl: return absl::StrCat("[", text, "](", target.id, ")");
    case RefTarget::Kind::kManPage:
      return label.empty() ? absl::StrCat(target.id, "(", target.section, ")") : std::string(label);
    case RefTarget::Kind::kTopic:
    case RefTarget::Kind::kFlag:
    case RefTarget::Kind::kPrimary:
    case RefTarget::Kind::kAnchor: return absl::StrCat("[", text, "](#", SlugFor(target), ")");
  }
  return std::string(text);
}

std::string RenderInlinesMarkdown(const Inlines& runs) {
  std::string out;
  for (const Inline& run : runs) {
    switch (run.style) {
      case Inline::Style::kText: absl::StrAppend(&out, run.text); break;
      case Inline::Style::kCode: absl::StrAppend(&out, "`", run.text, "`"); break;
      case Inline::Style::kEmphasis: absl::StrAppend(&out, "_", run.text, "_"); break;
      case Inline::Style::kStrong: absl::StrAppend(&out, "**", run.text, "**"); break;
      case Inline::Style::kRef:
        absl::StrAppend(&out, run.target.has_value() ? MarkdownRefLink(*run.target, run.text) : run.text);
        break;
    }
  }
  return out;
}

void MarkdownBackend::Append(std::string_view text, bool allow_repeated_blank_lines) {
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

void MarkdownBackend::Preamble(const Document& doc) {
  Append(absl::StrFormat("# %s\n\n%s.\n", doc.name, doc.tagline));
  if (!doc.usage.empty()) {
    Append(absl::StrFormat("\n**Usage:** `%s %s`\n", doc.name, doc.usage));
  }
  preamble_end_ = out_.size();
  emit_contents_ = doc.sections.size() >= 4;
}

void MarkdownBackend::BeginSection(const Section& section) {
  if (section.title.empty()) {
    return;
  }
  const std::string anchor =
      section.anchor.empty() ? SlugFor({.kind = RefTarget::Kind::kAnchor, .id = section.title}) : section.anchor;
  section_links_.emplace_back(section.title, anchor);
  if (!section.anchor.empty()) {
    Append(absl::StrCat("\n<a id=\"", anchor, "\"></a>\n"));
  }
  Append(absl::StrFormat("\n## %s\n", section.title));
}

void MarkdownBackend::BeginSubsection(const Subsection& subsection) {
  if (subsection.title.empty()) {
    return;  // a title-less subsection only groups/indents in plain text; no heading here
  }
  Append(absl::StrFormat("\n### %s\n", subsection.title));
}

void MarkdownBackend::BeginEntry(const Entry& entry) {
  if (!entry.anchor.empty()) {
    Append(absl::StrCat("\n<a id=\"", SlugFor({.kind = RefTarget::Kind::kAnchor, .id = entry.anchor}), "\"></a>\n\n"));
  }
  // A term is backtick-wrapped so its `=NAME` / `[..]` / `|` stay literal.
  std::string tag;
  if (!entry.tags.empty()) {
    tag = absl::StrCat(" _(", absl::StrJoin(entry.tags, ", "), ")_");
  } else if (entry.xff) {
    tag = " _(xff)_";
  }
  Append(absl::StrCat("- `", entry.term, "` - ", RenderInlinesMarkdown(entry.summary), tag, "\n"));
  in_entry_ = true;
}

void MarkdownBackend::EndEntry(const Entry& /*entry*/) {
  in_entry_ = false;
}

void MarkdownBackend::EmitProse(const Prose& prose) {
  if (prose.paragraph_break_before) {
    Append("\n");
  }
  if (in_entry_) {
    Append(absl::StrCat("  ", RenderInlinesMarkdown(prose.runs), "\n"));  // indented bullet continuation
  } else {
    Append(absl::StrCat("\n", RenderInlinesMarkdown(prose.runs), "\n"));
  }
}

void MarkdownBackend::EmitExample(const Example& example) {
  Append(absl::StrCat("\n```", example.lang, "\n"));
  Append(example.text, true);
  if (!example.text.empty() && example.text.back() != '\n') {
    Append("\n", true);
  }
  Append("```\n");
}

void MarkdownBackend::EmitBullets(const Bullets& bullets) {
  Append(absl::StrCat("\n"));
  for (const Inlines& item : bullets.items) {
    Append(absl::StrCat("- ", RenderInlinesMarkdown(item), "\n"));
  }
}

void MarkdownBackend::EmitRows(const Rows& rows) {
  // Inside a flag entry the rows are a value table nested under the flag's bullet: indent
  // them to match the entry's 2-space continuation prose, and close with a blank line so
  // the following prose is not absorbed as a lazy continuation of the last list item.
  const std::string prefix = in_entry_ ? "  - `" : "- `";
  Append(absl::StrCat("\n"));
  for (const Row& row : rows.rows) {
    Append(absl::StrCat(prefix, row.term, "` - ", RenderInlinesMarkdown(row.description), "\n"));
  }
  if (in_entry_) {
    Append(absl::StrCat("\n"));
  }
}

void MarkdownBackend::EmitTable(const Table& table) {
  // A GFM pipe table, VERTICALLY ALIGNED at the source level: every cell padded to its column's
  // widest member, the separator row dashed to the same width - the repo convention the
  // align-markdown-tables hook enforces on committed markdown, produced here so the generated
  // XFF.md is already in the enforced form. Cells are plain text by the model's contract, so only
  // `|` needs escaping (before widths, so padding counts what is printed).
  const auto escape = [](std::string_view cell) { return absl::StrReplaceAll(cell, {{"|", "\\|"}}); };
  std::vector<std::string> header;
  header.reserve(table.header.size());
  for (const std::string& cell : table.header) {
    header.push_back(escape(cell));
  }
  std::vector<std::vector<std::string>> rows;
  rows.reserve(table.cells.size());
  for (const std::vector<std::string>& row : table.cells) {
    std::vector<std::string> cells;
    cells.reserve(row.size());
    for (const std::string& cell : row) {
      cells.push_back(escape(cell));
    }
    rows.push_back(std::move(cells));
  }
  std::vector<std::size_t> widths(header.size(), 3);  // >= 3 so the `---` separator always fits
  for (std::size_t i = 0; i < header.size(); ++i) {
    widths[i] = std::max(widths[i], header[i].size());
  }
  for (const std::vector<std::string>& row : rows) {
    for (std::size_t i = 0; i < row.size() && i < widths.size(); ++i) {
      widths[i] = std::max(widths[i], row[i].size());
    }
  }
  const auto emit_row = [&](const std::vector<std::string>& row) {
    Append(absl::StrCat("|"));
    for (std::size_t i = 0; i < row.size(); ++i) {
      Append(absl::StrCat(" ", row[i], std::string(widths[i] - row[i].size(), ' '), " |"));
    }
    Append(absl::StrCat("\n"));
  };
  Append(absl::StrCat("\n"));
  emit_row(header);
  Append(absl::StrCat("|"));
  for (const std::size_t width : widths) {
    Append(absl::StrCat(" ", std::string(width, '-'), " |"));
  }
  Append(absl::StrCat("\n"));
  for (const std::vector<std::string>& row : rows) {
    emit_row(row);
  }
  Append(absl::StrCat("\n"));
}

void MarkdownBackend::EmitSeeAlso(const SeeAlso& see_also) {
  Inlines runs;
  if (!see_also.refs.empty() && see_also.refs.front().kind != RefTarget::Kind::kManPage) {
    runs.push_back({.text = "See also: "});
  }
  for (std::size_t i = 0; i < see_also.refs.size(); ++i) {
    const RefTarget& ref = see_also.refs[i];
    if (i != 0) {
      runs.push_back({.text = ", "});
    }
    runs.push_back({.style = Inline::Style::kRef, .text = HelpReferenceLabel(ref), .target = ref});
  }
  EmitProse({.runs = std::move(runs)});
  if (!see_also.note.empty()) {
    EmitProse({.runs = see_also.note});
  }
}

std::string MarkdownBackend::Take() {
  if (emit_contents_) {
    std::string contents = "\n## Contents\n\n";
    for (const auto& [title, anchor] : section_links_) {
      absl::StrAppend(&contents, "- [", title, "](#", anchor, ")\n");
    }
    out_.insert(preamble_end_, contents);
  }
  return std::move(out_);
}

}  // namespace xff::cli
