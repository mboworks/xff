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

#ifndef XFF_CLI_HELP_H_
#define XFF_CLI_HELP_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "xff/registry/descriptor.h"

namespace xff::cli {

// A cookbook recipe: a task, the exact runnable command, and how it works. The single
// source for the `--help=cookbook` topic and the model's Examples section; every
// command is run end to end by //xff/examples:cookbook_test.
struct Recipe {
  std::string_view task;
  std::string_view command;
  std::string_view note;
};

// The cookbook recipes, in display order. Rendered as model nodes by BuildExamples().
[[nodiscard]] absl::Span<const Recipe> CookbookRecipes();

// The argument-shape hint shown after a primary's name: " ARG" (arity 1), " ARG ARG"
// (arity 2), " CMD... ;" (variadic), ":[!]NAME[=REGEX] CMD... ;" (a binding action), or
// "" (a flag-like primary). Read from the descriptor grammar so the model synopsis and
// the man page render identically and neither drifts from the parser.
std::string ArgHint(const registry::Descriptor& descriptor);

// One `--help=TOPIC` topic, for the generated topic index. The single source of the
// help-system map: the model's Help section (usage page + `--help=help`) renders
// HelpTopics(), so the advertised topic list cannot drift from what the CLI accepts.
struct HelpTopic {
  std::string_view name;                       // the topic keyword (--help=NAME)
  absl::Span<const std::string_view> aliases;  // alternate spellings, or empty
  std::string_view summary;                    // one-line description
  std::string_view see_also;                   // comma-separated related topic names
  bool in_full = false;                        // folded into the --help=full reference
};

// The meta-topics of the help system (help, list, expressions, fields, styles, full, ...).
[[nodiscard]] absl::Span<const HelpTopic> HelpTopics();

// One meta / doc flag for the usage page's Help section (-h/--help, --help=NAME,
// --help=TOPIC, --help=full --help-format=FORMAT, --help-full, --man, --version). These are consumed before
// parsing (not in Globals(), never looked up), so they carry their own doc SOT here
// instead of being hand-written; the model's BuildHelpSection() renders them + the topics.
struct HelpFlag {
  std::string_view display;  // e.g. "-h, --help, -help", "--man"
  std::string_view summary;  // one-line description
};

[[nodiscard]] absl::Span<const HelpFlag> HelpFlags();

}  // namespace xff::cli

#endif  // XFF_CLI_HELP_H_
