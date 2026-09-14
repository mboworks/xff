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

#include "xff/cli/roff_backend.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "xff/cli/help_model.h"

namespace xff::cli {

std::string RoffEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool at_line_start = true;
  for (const char chr : text) {
    // A leading `.` or `'` in the first column is a roff control line; guard it.
    if (at_line_start && (chr == '.' || chr == '\'')) {
      out += "\\&";
    }
    at_line_start = chr == '\n';
    if (chr == '\\') {
      out += "\\\\";
    } else if (chr == '-') {
      out += "\\-";  // option dashes stay real hyphen-minus (copy-pasteable)
    } else {
      out += chr;
    }
  }
  return out;
}

void RoffBackend::EmitInline(const Inlines& runs) {
  for (const Inline& run : runs) {
    switch (run.style) {
      case Inline::Style::kText: absl::StrAppend(&out_, RoffEscape(run.text)); break;
      case Inline::Style::kCode:
      case Inline::Style::kStrong: absl::StrAppend(&out_, "\\fB", RoffEscape(run.text), "\\fR"); break;
      case Inline::Style::kEmphasis: absl::StrAppend(&out_, "\\fI", RoffEscape(run.text), "\\fR"); break;
      case Inline::Style::kRef: absl::StrAppend(&out_, RoffEscape(run.text)); break;
    }
  }
}

void RoffBackend::Preamble(const Document& doc) {
  absl::StrAppendFormat(&out_, ".TH %s 1 \"\" \"%s\" \"User Commands\"\n", RoffEscape(doc.name), RoffEscape(doc.name));
  absl::StrAppend(&out_, ".SH NAME\n", RoffEscape(doc.name), " \\- ", RoffEscape(doc.tagline), "\n");
  absl::StrAppend(&out_, ".SH SYNOPSIS\n.B ", RoffEscape(doc.name), "\n", RoffEscape(doc.usage), "\n");
  para_ = false;
}

void RoffBackend::BeginSection(const Section& section) {
  // Man convention: top-level headings upper-cased (the model carries natural case).
  absl::StrAppendFormat(&out_, ".SH %s\n", RoffEscape(absl::AsciiStrToUpper(section.title)));
  para_ = false;
}

void RoffBackend::BeginSubsection(const Subsection& subsection) {
  if (subsection.title.empty()) {
    return;  // a title-less subsection only groups/indents in plain text; no heading here
  }
  absl::StrAppendFormat(&out_, ".SS %s\n", RoffEscape(subsection.title));
  para_ = false;
}

void RoffBackend::BeginEntry(const Entry& entry) {
  absl::StrAppend(&out_, ".TP\n.B ", RoffEscape(entry.term), "\n");
  EmitInline(entry.summary);
  std::string tag;
  if (!entry.tags.empty()) {
    tag = absl::StrCat(" (", absl::StrJoin(entry.tags, ", "), ")");
  } else if (entry.xff) {
    tag = " (xff extension)";
  }
  absl::StrAppend(&out_, tag, "\n");
  para_ = false;
}

void RoffBackend::EmitProse(const Prose& prose) {
  if (para_) {
    absl::StrAppend(&out_, ".PP\n");
  }
  EmitInline(prose.runs);
  absl::StrAppend(&out_, "\n");
  para_ = true;
}

void RoffBackend::EmitExample(const Example& example) {
  absl::StrAppend(&out_, ".PP\n.nf\n", RoffEscape(example.text));
  if (example.text.empty() || example.text.back() != '\n') {
    absl::StrAppend(&out_, "\n");
  }
  absl::StrAppend(&out_, ".fi\n");
  para_ = false;
}

void RoffBackend::EmitBullets(const Bullets& bullets) {
  for (const Inlines& item : bullets.items) {
    absl::StrAppend(&out_, ".IP \\(bu 3\n");
    EmitInline(item);
    absl::StrAppend(&out_, "\n");
  }
  para_ = false;
}

void RoffBackend::EmitRows(const Rows& rows) {
  for (const Row& row : rows.rows) {
    absl::StrAppend(&out_, ".TP\n.B ", RoffEscape(row.term), "\n");
    EmitInline(row.description);
    absl::StrAppend(&out_, "\n");
  }
  para_ = false;
}

void RoffBackend::EmitTable(const Table& table) {
  // Width-aligned columns in a no-fill block; man pages have the width for the whole line, so no
  // wrapping - the plain backend is the width-constrained rendering.
  std::vector<std::size_t> widths(table.header.size(), 0);
  for (std::size_t i = 0; i < table.header.size(); ++i) {
    widths[i] = table.header[i].size();
  }
  for (const std::vector<std::string>& row : table.cells) {
    for (std::size_t i = 0; i + 1 < row.size() && i < widths.size(); ++i) {
      widths[i] = std::max(widths[i], row[i].size());
    }
  }
  absl::StrAppend(&out_, ".nf\n");
  const auto emit_row = [&](const std::vector<std::string>& row) {
    std::string line;
    for (std::size_t i = 0; i < row.size(); ++i) {
      absl::StrAppend(&line, row[i]);
      if (i + 1 < row.size()) {
        absl::StrAppend(&line, std::string(widths[i] + 2 - row[i].size(), ' '));
      }
    }
    absl::StrAppend(&out_, RoffEscape(line), "\n");
  };
  emit_row(table.header);
  for (const std::vector<std::string>& row : table.cells) {
    emit_row(row);
  }
  absl::StrAppend(&out_, ".fi\n");
  para_ = false;
}

void RoffBackend::EmitSeeAlso(const SeeAlso& see_also) {
  if (!see_also.refs.empty() && see_also.refs.front().kind != RefTarget::Kind::kManPage) {
    absl::StrAppend(&out_, ".PP\nSee also:\n");
  }
  for (std::size_t i = 0; i < see_also.refs.size(); ++i) {
    const RefTarget& ref = see_also.refs[i];
    if (ref.kind == RefTarget::Kind::kManPage) {
      absl::StrAppendFormat(
          &out_, ".BR %s (%s)%s\n", RoffEscape(ref.id), RoffEscape(ref.section),
          i + 1 < see_also.refs.size() ? "," : "");
    } else {
      absl::StrAppendFormat(
          &out_, ".B %s%s\n", RoffEscape(HelpReferenceLabel(ref)), i + 1 < see_also.refs.size() ? "," : "");
    }
  }
  if (!see_also.note.empty()) {
    absl::StrAppend(&out_, ".PP\n");
    EmitInline(see_also.note);
    absl::StrAppend(&out_, "\n");
  }
  para_ = false;
}

std::string RoffBackend::Take() {
  return std::move(out_);
}

}  // namespace xff::cli
