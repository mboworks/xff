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

#include "xff/cli/help_backend.h"

#include <variant>

#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

// Walks one block list in order. Factored out so it recurses into the nested
// Blocks that Subsection and Entry carry (a section's children, an entry's details).
void RenderBlocks(const Blocks& blocks, HelpBackend& backend) {
  for (const Content& content : blocks) {
    std::visit(
        [&backend](const auto& node) {
          using Node = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<Node, Prose>) {
            backend.EmitProse(node);
          } else if constexpr (std::is_same_v<Node, Example>) {
            backend.EmitExample(node);
          } else if constexpr (std::is_same_v<Node, Bullets>) {
            backend.EmitBullets(node);
          } else if constexpr (std::is_same_v<Node, Rows>) {
            backend.EmitRows(node);
          } else if constexpr (std::is_same_v<Node, Table>) {
            backend.EmitTable(node);
          } else if constexpr (std::is_same_v<Node, SeeAlso>) {
            backend.EmitSeeAlso(node);
          } else if constexpr (std::is_same_v<Node, Entry>) {
            backend.BeginEntry(node);
            RenderBlocks(node.details, backend);
            backend.EndEntry(node);
          } else if constexpr (std::is_same_v<Node, Subsection>) {
            backend.BeginSubsection(node);
            RenderBlocks(node.children, backend);
            backend.EndSubsection(node);
          }
        },
        content.node);
  }
}

}  // namespace

std::string HelpReferenceLabel(const RefTarget& target) {
  if (!target.label.empty()) {
    return target.label;
  }
  switch (target.kind) {
    case RefTarget::Kind::kTopic:
    case RefTarget::Kind::kFlag:
    case RefTarget::Kind::kPrimary: return "--help=" + target.id;
    case RefTarget::Kind::kManPage: return target.id + "(" + target.section + ")";
    case RefTarget::Kind::kUrl:
    case RefTarget::Kind::kAnchor: return target.id;
  }
  return target.id;
}

void RenderDocument(const Document& doc, HelpBackend& backend) {
  backend.Preamble(doc);
  for (const Section& section : doc.sections) {
    backend.BeginSection(section);
    RenderBlocks(section.children, backend);
    backend.EndSection(section);
  }
}

}  // namespace xff::cli
