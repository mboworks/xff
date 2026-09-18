// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_CLI_CONFIG_VALIDATION_H_
#define XFF_CLI_CONFIG_VALIDATION_H_

#include <cstddef>
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

// Retained even when an invalid section is removed from executable configuration.
struct ConfigProfile final {
  std::string name;
  std::string path;
  std::size_t line = 0;
  config::Source source = config::Source::kUnset;
  bool globals = false;
  bool predicates = false;
  bool actions = false;
  std::vector<std::string> disabled_reasons;
};

struct ConfigFileValidation final {
  config::ConfigFile config;
  std::vector<std::string> diagnostics;
  std::vector<std::string> disabled_configs;
  std::vector<ConfigProfile> profiles;
  absl::Status status;
};

// Invalid file globals fail validation; named sections are validated atomically. Repeated declarations disable
// every occurrence in that file. Disablement propagates through --config references, and selecting
// a disabled name is a hard error. Only the permitted control locations depend on the source.
ConfigFileValidation ValidateConfigFile(
    config::ConfigFile file,
    const std::vector<std::string>& selected_configs,
    std::string_view path = "/etc/xff.ini",
    config::Source source = config::Source::kSystem);

// Lists declarations, selection, skip state, declared contribution kinds, and validation reasons.
// Availability does not arm actions: policy-gated directives are explained separately.
std::string ExplainProfiles(
    const std::vector<ConfigProfile>& profiles,
    const config::ConfigInputs& inputs,
    const std::vector<std::string>& selected);

// Validates applying explicit/composed selectors after all admitted files are available.
// Built-in styles need no declaration; primary arguments and implicit invocation names are excluded.
absl::Status ValidateConfigSelections(
    const config::ConfigInputs& inputs,
    const std::vector<config::ResolvedFlag>& resolved);

// Applies global options in resolution order and ANDs the config expression with the CLI expression.
// Literal primary operands retain their role; they cannot become global options.
absl::StatusOr<parser::Command> ApplyResolvedConfig(
    parser::Command command,
    const std::vector<config::ResolvedFlag>& resolved);

}  // namespace xff::cli

#endif  // XFF_CLI_CONFIG_VALIDATION_H_
