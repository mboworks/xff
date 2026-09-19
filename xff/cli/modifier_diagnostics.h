// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_CLI_MODIFIER_DIAGNOSTICS_H_
#define XFF_CLI_MODIFIER_DIAGNOSTICS_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/config/config.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"

namespace xff::cli {

// Nonfatal explanations for effective CLI modifiers with no structural consumer.
// Config defaults and superseded requests are quiet. Does not evaluate expressions.
absl::StatusOr<std::string> InactiveModifierNotes(
    const parser::Command& effective,
    const std::vector<config::ResolvedFlag>& application,
    registry::Style style = registry::Style::kXff);

}  // namespace xff::cli

#endif  // XFF_CLI_MODIFIER_DIAGNOSTICS_H_
