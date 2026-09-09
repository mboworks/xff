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

#ifndef XFF_CLI_MARKDOWN_BACKEND_H_
#define XFF_CLI_MARKDOWN_BACKEND_H_

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

// The Markdown help backend: renders the help model to GitHub-flavored Markdown.
// Inline emphasis maps to Markdown (`code`, _italic_, **bold**); an entry is a
// bullet whose term is backtick-wrapped (so its `=NAME` / `[..]` / `|` stay
// literal) with an _(xff)_ tag for extensions, and its detail prose an indented
// continuation of that bullet. Driving it over BuildReference() produces
// `xff --help=full:markdown` and the committed XFF.md reference.
namespace xff::cli {

// A HelpBackend that accumulates Markdown. Construct, RenderDocument() into it, Take().
class MarkdownBackend final : public HelpBackend {
 public:
  void Preamble(const Document& doc) override;
  void BeginSection(const Section& section) override;
  void BeginSubsection(const Subsection& subsection) override;
  void BeginEntry(const Entry& entry) override;
  void EndEntry(const Entry& entry) override;
  void EmitProse(const Prose& prose) override;
  void EmitExample(const Example& example) override;
  void EmitBullets(const Bullets& bullets) override;
  void EmitRows(const Rows& rows) override;

  void EmitTable(const Table& table) override;
  void EmitSeeAlso(const SeeAlso& see_also) override;
  [[nodiscard]] std::string Take() override;

 private:
  std::string out_;
  std::size_t preamble_end_ = 0;
  std::vector<std::pair<std::string, std::string>> section_links_;
  bool emit_contents_ = false;
  bool in_entry_ = false;  // detail prose renders as an indented continuation of the entry bullet
};

// Renders inline runs to Markdown: literal text passes through, code / emphasis /
// strong map to their Markdown markup, and a cross-reference to a link. Exposed for
// reuse / testing.
[[nodiscard]] std::string RenderInlinesMarkdown(const Inlines& runs);

// The Markdown form of an inline cross-reference: `[label](url)` for a URL,
// `[label](#slug)` for an in-document target, and `label` (e.g. `find(1)`) for a man
// page. Exposed for testing.
[[nodiscard]] std::string MarkdownRefLink(const RefTarget& target, std::string_view label);

}  // namespace xff::cli

#endif  // XFF_CLI_MARKDOWN_BACKEND_H_
