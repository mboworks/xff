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

#ifndef XFF_CLI_HELP_BACKEND_H_
#define XFF_CLI_HELP_BACKEND_H_

#include <cstddef>
#include <string>
#include <utility>

#include "xff/cli/help_model.h"

// The render seam for the help model (#154 slice C): a backend consumes the
// output-independent help_model tree and emits one concrete format (plain text,
// ANSI color, Markdown, roff, HTML). `RenderDocument` is the single walker that
// drives a backend over a Document - so a new backend is the only thing a new
// output format needs, and no format re-implements the traversal.
//
// Unlike the older DocRenderer walk, a backend receives the model's already-parsed
// Inlines (styles + cross-reference targets) rather than raw backtick strings, so
// highlighting and cross-referencing are first-class and cannot be lost in a
// re-parse. See docs/design-help-model.md.
namespace xff::cli {

// A copyable help command for internal references, or the conventional external locator.
[[nodiscard]] std::string HelpReferenceLabel(const RefTarget& target);

// The render context: presentation meta resolved once at the CLI boundary (from the
// flags + terminal) and held by the backend base as a constant, so every backend and
// the RenderDocument walk see the same settings and refer to them as needed. The
// help model (Document) stays pure content; how it is presented lives here.
struct HelpRenderContext {
  std::size_t width = 0;  // wrap column for flowing text; 0 = do not wrap
  bool color = false;     // emit ANSI color (resolved from --color at the boundary)
};

// Emits one output format from the help model. RenderDocument() calls these in
// document order; container nodes (section, subsection, entry) are bracketed by
// Begin/End so a backend can open and close nesting. The default End* hooks do
// nothing, so a flat backend only overrides what it needs. Inline runs are handed
// over whole (as Inlines) so each backend maps Style / RefTarget to its own form.
//
// The base holds the HelpRenderContext (width / color / ...); every backend reads it
// via Context(). It is set at construction and constant thereafter.
class HelpBackend {
 public:
  HelpBackend() = default;

  // std::move is a no-op while HelpRenderContext is trivially copyable, but keeps the
  // sink-parameter idiom for when it gains a non-trivial field (e.g. a color palette).
  // NOLINTNEXTLINE(*-move-const-arg)
  explicit HelpBackend(HelpRenderContext context) : context_(std::move(context)) {}

  virtual ~HelpBackend() = default;
  HelpBackend(const HelpBackend&) = delete;
  HelpBackend& operator=(const HelpBackend&) = delete;
  HelpBackend(HelpBackend&&) = delete;
  HelpBackend& operator=(HelpBackend&&) = delete;

  // Program name, one-line tagline, and usage synopsis.
  virtual void Preamble(const Document& doc) = 0;

  // A top-level section and its close.
  virtual void BeginSection(const Section& section) = 0;

  virtual void EndSection(const Section& /*section*/) {}

  // A nested subsection and its close.
  virtual void BeginSubsection(const Subsection& subsection) = 0;

  virtual void EndSubsection(const Subsection& /*subsection*/) {}

  // A definition entry (its detail blocks are walked between Begin and End).
  virtual void BeginEntry(const Entry& entry) = 0;

  virtual void EndEntry(const Entry& /*entry*/) {}

  // Leaf blocks. Named Emit* so the method names do not collide with the model's
  // node type names (Prose, Example, ...) they take by reference.
  virtual void EmitProse(const Prose& prose) = 0;
  virtual void EmitExample(const Example& example) = 0;
  virtual void EmitBullets(const Bullets& bullets) = 0;
  virtual void EmitRows(const Rows& rows) = 0;

  virtual void EmitTable(const Table& table) = 0;
  virtual void EmitSeeAlso(const SeeAlso& see_also) = 0;

  // The accumulated document.
  [[nodiscard]] virtual std::string Take() = 0;

 protected:
  // The render context, constant for the backend's lifetime; every backend reads it.
  [[nodiscard]] const HelpRenderContext& Context() const { return context_; }

 private:
  const HelpRenderContext context_;
};

// Walks `doc` in document order and drives `backend`, recursing into subsection
// children and entry detail blocks. The one traversal every backend shares.
void RenderDocument(const Document& doc, HelpBackend& backend);

}  // namespace xff::cli

#endif  // XFF_CLI_HELP_BACKEND_H_
