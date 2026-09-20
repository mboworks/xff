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

#ifndef XFF_CLI_HELP_BUILD_H_
#define XFF_CLI_HELP_BUILD_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "xff/cli/help_model.h"

// Builds the complete help document from the single source of truth (#154 slice D):
// the global flags, the expression registry, and the documentation-source
// vocabularies (fields / printf / time / size / regex grammars). This is the model
// counterpart of the imperative WriteReference() walk - it produces a help_model
// Document that any HelpBackend renders (see help_backend.h), so a new output format
// is a new backend and every format shares this one build. Authored doc strings are
// parsed for their backtick / fence markup by help_parse; structure (sections,
// entries, rows) is built as typed nodes here.
namespace xff::cli {

// Assembles the reference Document from the SOT. Called once per help render.
// Whether a rendered reference describes THIS binary or the tool as a whole. The difference is one
// note: a flag whose build extra is absent normally carries "NOT built into this binary", which is
// true of the binary in hand and misleading in a published document - the reference should document
// all functionality and let each such flag's own text say it is a build extra.
enum class Audience : std::uint8_t {
  kThisBinary,  // --help / --man: what YOUR build can do
  kPublished,   // --help=full:markdown/html / published references: what xff can do
};

[[nodiscard]] Document BuildReference(Audience audience = Audience::kThisBinary);

// The compact usage page (bare `--help`): orientation, command structure, common tasks,
// and routes to focused help, the flag inventory, and the full reference.
[[nodiscard]] Document BuildUsage();

// The index topics: `list` (the topic index), `all` (every option + primary,
// summaries only), and `expressions` (the annotated primaries, no global flags).
// nullopt when NAME is not an index topic.
[[nodiscard]] std::optional<Document> IndexReference(std::string_view name);

// Just the FIELDS section as a standalone document (no preamble), for the
// `--help=fields` topic - the same content BuildReference() folds into its Fields
// section, so the topic can never drift from the full reference.
// The component names `--help=license=NAME` accepts, in Notices() order, so an unknown name can be
// answered with what IS available rather than a bare rejection.
[[nodiscard]] std::vector<std::string_view> LicenseComponentNames();

[[nodiscard]] Document FieldsReference();

// The compiled component manifest for standalone publication.
[[nodiscard]] Document NoticeReference();

// Navigation for a standalone topic (including aliases), omitted from full reference sections.
[[nodiscard]] Document TopicNavigation(std::string_view name);

// The standalone document for a sub-vocabulary `--help=TOPIC` (fields / printf / time
// / size / grammars) - the same section BuildReference() folds into the full reference,
// so the topic can never drift from it. nullopt when NAME is not such a topic.
[[nodiscard]] std::optional<Document> TopicReference(std::string_view name);

// The single-entry document for `--help=NAME` when NAME is an expression primary or
// a global flag (leading-dash convenience: `--help=sort` finds `--sort`). The entry
// is the same one BuildReference() folds into its Options / Expression sections, so
// the per-entry help can never drift from the full reference. nullopt when NAME is
// not an entry (the caller then tries the non-entry topics / an error).
[[nodiscard]] std::optional<Document> EntryReference(std::string_view name);

}  // namespace xff::cli

#endif  // XFF_CLI_HELP_BUILD_H_
