// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ENGINE_CONSUMERS_H_
#define XFF_ENGINE_CONSUMERS_H_

#include <set>

#include "absl/status/statusor.h"
#include "xff/parser/ast.h"
#include "xff/registry/consumers.h"
#include "xff/registry/descriptor.h"

namespace xff::engine {

// Uses the runtime option resolvers and typed expression metadata. No evaluation
// or filesystem access; the command must already include resolved configuration.
absl::StatusOr<std::set<registry::ModifierConsumer>> ActiveModifierConsumers(
    const parser::Command& command,
    registry::Style style = registry::Style::kXff);

}  // namespace xff::engine

#endif  // XFF_ENGINE_CONSUMERS_H_
