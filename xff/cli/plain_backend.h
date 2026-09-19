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

#ifndef XFF_CLI_PLAIN_BACKEND_H_
#define XFF_CLI_PLAIN_BACKEND_H_

#include <string>
#include <string_view>
#include <utility>

#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

// The plain-text help backend: renders the help model to unadorned terminal text -
// the `--help` / topic format. Blocks are separated by a blank line; a section
// heading is upper-cased, a subsection ends in a colon, an entry is a term line
// with an indented summary, and a vocabulary table aligns on its widest term. Prose
// drops backtick markup, while a term / row description keeps it (matching the
// hand-written help). Flowing text (prose, a bullet, an entry summary / detail, a
// see-also note) word-wraps to the configured `width` (#153 / #164), each
// continuation line carrying its block's indent. Examples retain their layout; tables
// use labeled rows when fixed columns leave insufficient room for the last column.
// A `width` of 0 disables wrapping. ANSI
// color is a later slice (the color backend).
namespace xff::cli {

// A HelpBackend that accumulates plain text. Construct (optionally with a render
// context - its width drives wrapping, 0 = no wrap), RenderDocument() into it, then
// Take() the result.
class PlainTextBackend final : public HelpBackend {
 public:
  PlainTextBackend() = default;

  // std::move keeps the sink-parameter idiom; a no-op while HelpRenderContext is
  // trivially copyable (see HelpBackend), hence the lint suppression.
  // NOLINTNEXTLINE(*-move-const-arg)
  explicit PlainTextBackend(HelpRenderContext context) : HelpBackend(std::move(context)) {}

  void Preamble(const Document& doc) override;
  void BeginSection(const Section& section) override;
  void EndSection(const Section& section) override;
  void BeginSubsection(const Subsection& subsection) override;
  void EndSubsection(const Subsection& subsection) override;
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
  // All text passes through this sink; verbatim examples explicitly preserve blank lines.
  void Append(std::string_view text, bool allow_repeated_blank_lines = false);

  // Ensures a blank line between blocks without adding another when one already exists.
  void StartBlock();

  // The body indent for content at the current nesting depth (2 spaces per level), so
  // every section / subsection / entry visibly owns the content indented beneath it.
  [[nodiscard]] std::string BodyIndent() const;

  std::string out_;
  std::string pending_spaces_;
  bool line_has_content_ = false;
  bool last_line_blank_ = true;
  bool in_entry_ = false;  // an entry's detail prose renders under its (deeper) term indent
  int depth_ = 0;          // nesting depth: each Begin* heading/term increments, each End* decrements
};

// Renders inline runs to plain text: literal content with emphasis markup dropped
// and each cross-reference shown as its plain locator. Exposed for reuse / testing.
[[nodiscard]] std::string RenderInlinesPlain(const Inlines& runs);

// The plain locator a cross-reference resolves to in text output (e.g. a kManPage
// target -> "find(1)", a kTopic -> "--help=fields"). Exposed for reuse / testing.
[[nodiscard]] std::string PlainRefLocator(const RefTarget& target);

}  // namespace xff::cli

#endif  // XFF_CLI_PLAIN_BACKEND_H_
