// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_CLI_CONFIG_VALIDATION_H_
#define XFF_CLI_CONFIG_VALIDATION_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/config/config.h"
#include "xff/parser/ast.h"

namespace xff::cli {

// Reports overriding global settings that occur more than once in one logical config section.
// Positive/negative forms are one setting; aliases and valued forms use their canonical global
// identity. Accumulating settings, expression primaries, separate selectors, and separate files
// remain independent. Structurally invalid config controls are rejected separately by policy.
std::vector<std::string> ConfigOverrideNotices(const config::ConfigInputs& inputs);

struct SystemConfigValidation final {
  config::SystemConfig config;
  std::vector<std::string> diagnostics;
  std::vector<std::string> disabled_configs;
  absl::Status selected_configs_status;
};

// Validates system globals independently and named sections atomically. Invalid global lines are
// diagnosed and omitted. One invalid line disables its complete named section; disablement then
// propagates through --config references. Selecting a disabled section is a hard error.
SystemConfigValidation ValidateSystemConfig(
    config::SystemConfig config,
    const std::vector<std::string>& selected_configs,
    std::string_view path = "/etc/xff.ini");

// Applies global options in resolution order and ANDs the config expression with the CLI expression.
// Literal primary operands retain their role; they cannot become global options.
absl::StatusOr<parser::Command> ApplyResolvedConfig(
    parser::Command command,
    const std::vector<config::ResolvedFlag>& resolved);

}  // namespace xff::cli

#endif  // XFF_CLI_CONFIG_VALIDATION_H_
