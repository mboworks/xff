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

#ifndef XFF_CLI_MARKDOWN_H_
#define XFF_CLI_MARKDOWN_H_

#include <string>

namespace xff::cli {

// Renders a Markdown reference of the whole vocabulary from the same single sources
// of truth the parser and `--help` use -- cli::Globals() for options and
// registry::All() for the expression vocabulary -- so it cannot drift from the
// binary. Emitted by `xff --help=full --help-format=markdown`; the GitHub-renderable counterpart of the
// `--man` roff page.
std::string MarkdownReference();

}  // namespace xff::cli

#endif  // XFF_CLI_MARKDOWN_H_
