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

#ifndef XFF_CLI_HELP_MODEL_H_
#define XFF_CLI_HELP_MODEL_H_

#include <optional>
#include <string>
#include <variant>
#include <vector>

// The semantic help document model. Help is authored / built as this
// output-independent tree, and each renderer backend (plain text, ANSI color,
// Markdown, roff man page, HTML) walks it to produce its own format. The model
// carries meaning, never presentation: no width, no escaping, no format strings.
// See docs/design-help-model.md for the decision record.
namespace xff::cli {

// ========== Inline runs: highlighting + cross-references ========== //

// A cross-reference target: a SEMANTIC pointer to another documented thing, never
// a formatted link. Each backend resolves it to its own link form (HTML anchor,
// Markdown link, roff cross-ref, or plain text such as `--help=fields` / find(1)).
struct RefTarget {
  enum class Kind {
    kTopic,    // a --help=TOPIC; id is the topic name, e.g. "fields"
    kFlag,     // a global flag; id is the display, e.g. "--summary"
    kPrimary,  // an expression primary; id is the name, e.g. "-printf"
    kManPage,  // an external man page; id is the name, section is e.g. "1"
    kUrl,      // an absolute URL; id is the URL
    kAnchor,   // an explicit in-document anchor; id is the slug
  };
  Kind kind = Kind::kTopic;
  std::string id;
  std::string section;  // man section for kManPage; empty otherwise
  std::string label;    // optional display name for a reference within the same document
};

// One inline run within a text field. `style` drives per-backend highlighting;
// `text` is the literal content (and the link label when style is kRef).
struct Inline {
  enum class Style {
    kText,      // literal text, no emphasis
    kCode,      // inline code (authored as `single backticks`)
    kEmphasis,  // emphasized (italic)
    kStrong,    // strong (bold)
    kRef,       // a cross-reference; `target` is set
  };
  Style style = Style::kText;
  std::string text;
  std::optional<RefTarget> target;  // set iff style == kRef
};

using Inlines = std::vector<Inline>;

// ========== Block content (recursive) ========== //

// Forward declaration: Content is the recursive block node (defined below). The
// std::vector indirection lets Entry / Subsection hold child blocks while Content
// is still incomplete, and gives the whole variant a finite size.
struct Content;
using Blocks = std::vector<Content>;

// A prose paragraph: inline runs, free-flowed to the width by the text backends.
struct Prose {
  Inlines runs;
};

// A verbatim, preformatted block (a command, or a multi-line example such as the
// m// span diagram). Never wrapped. `lang` is an optional fence info string (e.g.
// "sh") used by the structured backends (Markdown / HTML) and ignored by the rest.
struct Example {
  std::string text;
  std::string lang;
};

// An unordered bullet list; each item is a run of inline content.
struct Bullets {
  std::vector<Inlines> items;
};

// A real multi-column table: one header row plus data rows, every cell PLAIN text (inline markup
// in cells would fight column alignment in the plain backend). Markdown renders a GFM pipe table;
// plain and roff render width-aligned columns, the LAST column wrapping to the page width - so put
// the prose-length column (extensions, notes) last.
struct Table {
  std::vector<std::string> header;
  std::vector<std::vector<std::string>> cells;
};

// A {term, description} vocabulary row (printf directive, time preset, field, ...).
struct Row {
  std::string term;
  Inlines description;
};

// An aligned list of vocabulary rows.
struct Rows {
  std::vector<Row> rows;
};

// A definition entry: a flag display or primary synopsis, its one-line summary,
// optional longer detail blocks, whether it is an xff-only extension, and a stable
// anchor for cross-referencing / indexing (auto-slugged from `term` when empty).
struct Entry {
  std::string term;
  Inlines summary;
  Blocks details;
  bool xff = false;
  // Parenthesized classification tokens shown after the term, e.g. {"global", "xff"}
  // for a flag or {"test", "find", "runs commands"} for a primary. A backend renders
  // them as "(a, b, c)". When empty, a backend falls back to the bare `xff` tag.
  std::vector<std::string> tags;
  std::string anchor;
};

// A SEE ALSO block: cross-references plus an optional trailing note.
struct SeeAlso {
  std::vector<RefTarget> refs;
  Inlines note;
  // Related entries/sections already present in this complete reference. Renderers may
  // suppress these pointers, but must never expand their targets.
  bool in_document = false;
};

// A subsection: a titled, anchored group of nested block content.
struct Subsection {
  std::string title;
  std::string anchor;
  Blocks children;
};

// The recursive block node: exactly one of the block kinds. Kept as a struct
// wrapping the variant (rather than a bare alias) so it can be forward declared
// and hold itself via Blocks (Entry.details / Subsection.children).
struct Content {
  std::variant<Prose, Example, Bullets, Rows, Table, Entry, SeeAlso, Subsection> node;
};

// ========== Top-level document ========== //

// A top-level section: a titled, anchored group of block content.
struct Section {
  std::string title;
  std::string anchor;
  Blocks children;
};

// The whole help document: preamble (program name, one-line tagline, usage
// synopsis) plus sections. Output-independent; a backend walks it to render.
struct Document {
  std::string name;
  std::string tagline;
  std::string usage;
  std::vector<Section> sections;
};

}  // namespace xff::cli

#endif  // XFF_CLI_HELP_MODEL_H_
