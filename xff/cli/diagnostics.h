// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_CLI_DIAGNOSTICS_H_
#define XFF_CLI_DIAGNOSTICS_H_

#include <string>
#include <string_view>

#include "absl/status/status.h"

namespace xff::cli {

// Advisory text only: callers retain the original failure and never reparse a correction.
// Empty when no close registered CLI spelling is available. Includes its final newline.
std::string UnknownGlobalHint(std::string_view token);

// Suggest registered help selectors (including config-only flags); never resolve automatically.
std::string UnknownHelpHint(std::string_view selector);

// Consumes parser provenance, never scans arbitrary argv or matches diagnostic prose.
std::string ParseErrorHint(const absl::Status& status);

}  // namespace xff::cli

#endif  // XFF_CLI_DIAGNOSTICS_H_
