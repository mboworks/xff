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

#ifndef XFF_CLI_MANPAGE_H_
#define XFF_CLI_MANPAGE_H_

#include <string>
#include <string_view>

namespace xff::cli {

// Renders the xff(1) man page (roff/troff) on demand from the same single sources of
// truth the parser and `--help` use -- cli::Globals() for options and
// registry::All() for the expression vocabulary -- so the page can never drift from
// the binary. `program_name` is the basename used to invoke the binary, so an
// installed `xff_full --man` page consistently documents xff_full while a
// symlink named `xff` documents xff. Pipe to `man -l -` or install under
// .../man/man1/<program_name>.1.
std::string ManPage(std::string_view program_name = "xff");

}  // namespace xff::cli

#endif  // XFF_CLI_MANPAGE_H_
