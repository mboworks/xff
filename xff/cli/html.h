// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_CLI_HTML_H_
#define XFF_CLI_HTML_H_

#include <string>

namespace xff::cli {

// Renders the complete published reference as a standalone HTML5 document from
// the same semantic model as `--help`, `--man`, and `--help=full --help-format=markdown`.
[[nodiscard]] std::string HtmlReference();

}  // namespace xff::cli

#endif  // XFF_CLI_HTML_H_
