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

#ifndef XFF_REGISTRY_REGISTRY_H_
#define XFF_REGISTRY_REGISTRY_H_

#include <string_view>

#include "absl/types/span.h"
#include "mbo/types/optional_ref.h"
#include "xff/registry/descriptor.h"

namespace xff::registry {

// Looks up the descriptor for a command-line expression token (e.g. "-name",
// "-type", "-o", "!"). Returns empty if the token is not a known
// predicate / action / operator.
mbo::types::OptionalRef<const Descriptor> Lookup(std::string_view name, Mode mode = Mode::kXff);

// All descriptors, in registry (source) order. The single enumeration point for
// the help system, generated --help, `xff help`, and the planned man-page / .md
// documentation generators, so they never drift from the parser's vocabulary.
absl::Span<const Descriptor> All();

}  // namespace xff::registry

#endif  // XFF_REGISTRY_REGISTRY_H_
