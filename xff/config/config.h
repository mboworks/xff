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

#ifndef XFF_CONFIG_CONFIG_H_
#define XFF_CONFIG_CONFIG_H_

#include <string>
#include <string_view>
#include <vector>

#include "xff/config/ini.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace xff::config {

// Provenance of a resolved setting: which layer contributed it. Resolution is
// last-non-unset-wins; kUnset is the "no override" sentinel and is never stored.
// There is no auto-discovered project layer (dropped 2026-07-06, Option B): config
// comes from system, user, and an explicit --xffrc=FILE only. Precedence order is
// system < user < xffrc < cli; kXffrc is a NON-ARMING tier (a named --xffrc file
// cannot self-authorize -exec/-delete; see the gate + --allow-exec).
enum class Source { kUnset, kSystem, kUser, kXffrc, kCli };

// One resolved config flag plus the layer it came from.
struct ResolvedFlag {
  std::string flag;
  Source source;
  bool is_argument = false;  // a literal primary argument, never a selector or arming directive
};

// Views of directive tokens, excluding literal primary arguments. The input strings must outlive
// the returned views. Uses registry arities, including terminated command argument runs.
std::vector<std::string_view> DirectiveTokens(const std::vector<std::string>& tokens);

// One config file consulted during discovery, recorded for --explain's source
// trace: its path, the layer it would feed, and whether it existed/was readable.
struct ConfigSource {
  std::string path;
  Source layer;
  bool found = false;
};

// One explicitly named --xffrc file. Files remain separate so resolution can
// apply each file at the command-line position where it was selected.
struct ExplicitConfig {
  std::string path;
  std::vector<RcLine> lines;
};

// The parsed layers + active selectors fed to ResolveConfig. CLI flags are NOT
// here: the caller applies them last (highest precedence) after this resolution.
struct ConfigInputs {
  SystemConfig system;                // parsed /etc/xff.ini globals + named configurations
  std::vector<RcLine> user;           // parsed user .xffrc
  std::vector<ExplicitConfig> xffrc;  // parsed --xffrc=FILE files, kept separate and in order
  std::vector<std::string> configs;   // active --config=NAME selectors (styles and/or named configs)
  bool no_config = false;             // --no-config: suppress both automatic tiers when authorized
  bool no_system_config = false;      // --no-system-config: suppress system configuration
  bool no_user_config = false;        // --no-user-config: suppress the user tier
  std::vector<ConfigSource> sources;  // every file consulted during discovery, for --explain (set by Discover)
};

// Resolves config-supplied flags using the legacy tier view, lowest precedence
// first, each tagged with its Source. Prefer ResolveConfigInOrder for execution.
// An .xffrc line contributes its flags when its
// base selector is empty/"common" or names an active --config, AND its config
// selector is empty or names an active --config. Suppressing both automatic tiers yields an empty result
// (pure CLI + built-ins). Gate the inputs first (GateConfig) so a dangerous,
// unarmed --xffrc line never reaches here.
std::vector<ResolvedFlag> ResolveConfig(const ConfigInputs& inputs);

// Produces the complete application stream. Automatic system/user defaults and
// the invocation selector apply first; command-line globals then retain their
// order. A --config selector expands newly matching user and already loaded
// explicit-file lines at that exact point. A --xffrc selector loads that file's
// currently matching lines at its exact point; later selectors may activate
// further lines from it. Each config line is emitted at most once.
std::vector<ResolvedFlag> ResolveConfigInOrder(
    const ConfigInputs& inputs,
    const std::vector<std::string>& cli_globals,
    std::string_view invocation_selector);

// The lowercase layer name for a Source: "unset"/"system"/"user"/"xffrc"/"cli".
std::string_view SourceName(Source source);

// Whether the arming flag `flag` (e.g. "--allow-exec") is active from a TRUSTED tier - the CLI
// globals, the unsectioned system configuration, or an applying user .xffrc line - but
// NOT from an --xffrc-loaded file (inputs.xffrc is deliberately excluded). This is what lets a
// named --xffrc file carry `-exec`/`-delete` yet not authorize itself: only a trusted tier arms.
bool ArmedFromTrustedTier(
    const ConfigInputs& inputs,
    const std::vector<std::string>& cli_globals,
    std::string_view flag);

// Whether `name` is one of the reserved built-in style names (find / xff / rg). Tests the base of
// a selector (so "xff:2" should be reduced to "xff" first). Those names are reserved: a config
// file may not attach behavior to a preset (see GateConfig's preset-overload rule), and argv[0]
// dispatch uses this to tell a preset invocation name from a custom alias (which selects a named
// config instead). There is no `xfd`; it is just another name (a named-config selector).
bool IsBuiltinStyle(std::string_view name);

// The active find/xff style selected by the --config stack. A --config=NAME whose
// base (the part before any ':') is "find" or "xff" picks that style; selectors
// stack, so the last style selector wins. With no style selector the default is
// the modern xff style. Custom config names (e.g. "debug") and version-pinned
// epochs ("xff:2" -> base "xff") leave the mapping unchanged. The find
// expression style makes a `find`-style run reject xff-only primaries (see
// parser::EnforceStyle); design-config.md "CLI selectors".
registry::Style ActiveStyle(const std::vector<std::string>& configs);

// The leading --config selector implied by the program name (argv[0] dispatch), from its
// basename: a built-in style name ("find"/"xff"/"rg") selects that preset; an empty name defaults
// to "xff"; ANY OTHER name (including "fd"/"xfd" - there is no magic remap) is returned verbatim as
// a NAMED-config selector (e.g. a "mytool" symlink -> "mytool"), which activates a matching
// `mytool:` config block while leaving the base style at the modern xff default (ActiveStyle
// ignores a non-style selector). main() prepends this as the lowest-precedence selector, so an
// explicit --config still stacks over it via ActiveStyle's last-wins (design-config.md "CLI
// selectors"). The returned view aliases `argv0` for a passthrough name; copy it to retain.
std::string_view DefaultStyleForProgram(std::string_view argv0);

// Renders the effective configuration for --explain: the resolved config flags
// (each prefixed by its provenance) in application order, then the CLI globals
// (provenance "cli"). Later lines override earlier ones, mirroring resolution.
std::string ExplainConfig(const std::vector<ResolvedFlag>& application);

// Renders the discovery trace for --explain: the active find/xff style, then every
// config file consulted (precedence order: system < user), each tagged with its
// layer and whether it was found. Pairs with ExplainConfig (which shows
// what each contributed); together they answer "what did xff read, and why".
std::string ExplainSources(const std::vector<ConfigSource>& sources, registry::Style style);

}  // namespace xff::config

#endif  // XFF_CONFIG_CONFIG_H_
