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

#ifndef XFF_ENGINE_RUN_H_
#define XFF_ENGINE_RUN_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

// One row of the flavor feature-map (--help=styles / --explain): a style-scoped behavior,
// the flag(s) that control it, and a function that yields its resolved value as a display
// string. `value({}, style)` is that style's default (an empty globals list); `value(globals,
// active)` is the current resolved value. Each facet wraps its own resolver, so the table
// and a real run read the same source and cannot drift. FlavorFacets() is the collector
// (a --feature capability would append its own facet here later).
struct FlavorFacet {
  using ValueFn = std::string (*)(const std::vector<std::string>& globals, registry::Style style);

  std::string_view behavior;
  std::string_view flag;
  ValueFn value;
};

[[nodiscard]] absl::Span<const FlavorFacet> FlavorFacets();

// Runs a parsed find command over `fs`: walks the roots in pre-order and, for
// each entry, evaluates the expression -- firing -print/-print0 actions through
// `emit`. When the expression has no action of its own (or is empty), matching
// entries get an implicit -print. Per-path failures are reported to `on_error`.
//
// Result of one complete traversal. `errors` counts per-path failures (0 == clean); `any_match`
// records whether the expression matched at least one visited entry, independently of output.
struct RunResult {
  int errors = 0;
  bool any_match = false;
};

//
// `style` selects the style-scoped traversal defaults applied when the user gives
// no `--sort` / `-j`: kXff sorts each directory (`--sort=dir`) and runs a capped
// parallel walk; kFind and kRg leave order unspecified and saturate cores.
// `std::nullopt` keeps the conservative defaults (unordered, single-threaded) and
// is what the in-process callers/tests use; the CLI passes the active style.
//
// `RunResult::any_match` is the grep-style "found anything" signal that backs `--quiet` /
// `--exit-match`. It reflects the expression's truth, not emitted output, so an action that
// suppresses the implicit -print (e.g. `-exec`) still counts as a match. It stays false on usage
// errors that stop before traversal, where match status is moot.
RunResult RunFind(
    const parser::Command& command,
    const vfs::FileSystem& fs,
    EmitFn emit,
    WalkErrorFn on_error,
    std::optional<registry::Style> style = std::nullopt);

}  // namespace xff::engine

#endif  // XFF_ENGINE_RUN_H_
