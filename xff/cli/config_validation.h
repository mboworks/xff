// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_CLI_CONFIG_VALIDATION_H_
#define XFF_CLI_CONFIG_VALIDATION_H_

#include <string>
#include <vector>

#include "xff/config/config.h"

namespace xff::cli {

// Reports overriding global settings that occur more than once in one logical config section.
// Positive/negative forms are one setting; aliases and valued forms use their canonical global
// identity. Accumulating settings, expression primaries, separate selectors, and separate files
// remain independent. Structurally invalid config controls are rejected separately by policy.
std::vector<std::string> ConfigOverrideNotices(const config::ConfigInputs& inputs);

}  // namespace xff::cli

#endif  // XFF_CLI_CONFIG_VALIDATION_H_
