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

#include "xff/config/config.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"
#include "xff/registry/registry.h"

namespace xff::config {
namespace {

constexpr std::string_view kNoRequireSystemConfig = "--no-require-system-config";
constexpr std::string_view kNoRequireUserConfig = "--no-require-user-config";
constexpr std::string_view kAllowXffrc = "--allow-xffrc";
constexpr std::string_view kRequireSystemConfig = "--require-system-config";
constexpr std::string_view kRequireUserConfig = "--require-user-config";
constexpr std::string_view kNoAllowXffrc = "--no-allow-xffrc";

bool IsSkipPermission(std::string_view flag) {
  return flag == kNoRequireSystemConfig || flag == kNoRequireUserConfig || flag == kRequireSystemConfig
         || flag == kRequireUserConfig || flag == kAllowXffrc || flag == kNoAllowXffrc || flag == "--no-allow-exec";
}

struct ConfigEntry {
  std::string name;
  std::vector<std::string> tokens;
};

std::vector<ConfigEntry> Entries(const ConfigFile& file) {
  std::vector<ConfigEntry> entries = {{.tokens = file.globals}};
  entries.reserve(1 + file.named.size());
  for (const IniSection& section : file.named) {
    ConfigEntry entry{.name = section.name};
    for (const IniLine& line : section.lines) {
      entry.tokens.insert(entry.tokens.end(), line.tokens.begin(), line.tokens.end());
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

bool Applies(const ConfigEntry& entry, const std::vector<std::string>& configs) {
  return entry.name.empty() || absl::c_contains(configs, entry.name);
}

void AppendMatching(
    std::vector<ResolvedFlag>& out,
    const ConfigFile& file,
    const std::vector<std::string>& configs,
    Source source) {
  for (const ConfigEntry& entry : Entries(file)) {
    if (!Applies(entry, configs)) {
      continue;
    }
    for (const std::string& flag : entry.tokens) {
      if (!IsSkipPermission(flag)) {
        out.push_back(ResolvedFlag{.flag = flag, .source = source});
      }
    }
  }
}

class OrderedResolver {
 public:
  OrderedResolver(const ConfigInputs& inputs, std::string_view invocation_selector)
      : inputs_(inputs),
        selectors_{std::string(invocation_selector)},
        system_named_emitted_(inputs.system.named.size()),
        user_entries_(Entries(inputs.user)),
        user_emitted_(user_entries_.size()) {
    file_loaded_.resize(inputs.xffrc.size());
    file_emitted_.reserve(inputs.xffrc.size());
    file_entries_.reserve(inputs.xffrc.size());
    for (const ExplicitConfig& file : inputs.xffrc) {
      file_entries_.push_back(Entries(file.config));
      file_emitted_.emplace_back(file_entries_.back().size());
    }
  }

  std::vector<ResolvedFlag> Resolve(const std::vector<std::string>& cli_globals) {
    EmitSystem();
    EmitMatching();
    for (const std::string& global : cli_globals) {
      EmitFlag(global, Source::kCli);
      LoadExplicitFile(global);
    }
    return std::move(application_);
  }

 private:
  void EmitSystem() {
    if (inputs_.no_system_config) {
      return;
    }
    EmitTokens(inputs_.system.globals, Source::kSystem, 0);
  }

  void EmitMatching() {
    if (!inputs_.no_system_config) {
      for (std::size_t index = 0; index < inputs_.system.named.size(); ++index) {
        const IniSection& section = inputs_.system.named[index];
        if (system_named_emitted_[index] || !absl::c_contains(selectors_, section.name)) {
          continue;
        }
        system_named_emitted_[index] = true;
        for (const IniLine& line : section.lines) {
          EmitTokens(line.tokens, Source::kSystem, 0);
        }
      }
    }
    if (!inputs_.no_user_config && CanEmit(1)) {
      EmitLines(user_entries_, user_emitted_, Source::kUser, 1);
    }
    for (std::size_t index = 0; index < inputs_.xffrc.size(); ++index) {
      if (file_loaded_[index] && CanEmit(index + 2)) {
        EmitLines(file_entries_[index], file_emitted_[index], Source::kXffrc, index + 2);
      }
    }
  }

  bool CanEmit(std::size_t file_index) const { return !active_file_.has_value() || file_index <= *active_file_; }

  void EmitLines(
      const std::vector<ConfigEntry>& lines,
      std::vector<bool>& emitted,
      Source source,
      std::size_t file_index) {
    for (std::size_t index = 0; index < lines.size(); ++index) {
      if (emitted[index] || !Applies(lines[index], selectors_)) {
        continue;
      }
      emitted[index] = true;
      EmitTokens(lines[index].tokens, source, file_index);
    }
  }

  void EmitTokens(const std::vector<std::string>& tokens, Source source, std::size_t file_index) {
    // Expand references in place, but defer later-file refinements until this body finishes.
    const auto previous_file = std::exchange(active_file_, file_index);
    for (std::size_t pos = 0; pos < tokens.size(); ++pos) {
      const std::string& token = tokens[pos];
      if (!IsSkipPermission(token)) {
        EmitFlag(token, source);
      }
      const auto primary = registry::Lookup(token.substr(0, token.find(':')));
      if (!primary.has_value()) {
        continue;
      }
      if (primary->arity < 0) {
        while (++pos < tokens.size()) {
          application_.push_back({.flag = tokens[pos], .source = source, .is_argument = true});
          if (tokens[pos] == ";" || tokens[pos] == "+") {
            break;
          }
        }
      } else {
        for (int remaining = primary->arity; remaining > 0 && pos + 1 < tokens.size(); --remaining) {
          application_.push_back({.flag = tokens[++pos], .source = source, .is_argument = true});
        }
      }
    }
    active_file_ = previous_file;
  }

  void EmitFlag(const std::string& flag, Source source) {
    application_.push_back({.flag = flag, .source = source});
    constexpr std::string_view kConfig = "--config=";
    if (flag.starts_with(kConfig)) {
      selectors_.push_back(flag.substr(kConfig.size()));
      EmitMatching();
    }
  }

  void LoadExplicitFile(std::string_view global) {
    constexpr std::string_view kXffrc = "--xffrc=";
    if (!global.starts_with(kXffrc) || next_file_ >= inputs_.xffrc.size()) {
      return;
    }
    file_loaded_[next_file_] = true;
    EmitLines(file_entries_[next_file_], file_emitted_[next_file_], Source::kXffrc, next_file_ + 2);
    ++next_file_;
  }

  const ConfigInputs& inputs_;
  std::vector<ResolvedFlag> application_;
  std::vector<std::string> selectors_;
  std::vector<bool> system_named_emitted_;
  std::optional<std::size_t> active_file_;
  std::vector<ConfigEntry> user_entries_;
  std::vector<bool> user_emitted_;
  std::vector<std::vector<ConfigEntry>> file_entries_;
  std::vector<bool> file_loaded_;
  std::vector<std::vector<bool>> file_emitted_;
  std::size_t next_file_ = 0;
};

}  // namespace

// Primary arguments are literal data, even when they resemble selectors or config-only controls.
std::vector<std::string_view> DirectiveTokens(const std::vector<std::string>& tokens) {
  std::vector<std::string_view> result;
  for (std::size_t pos = 0; pos < tokens.size(); ++pos) {
    const std::string& token = tokens[pos];
    result.push_back(token);
    const auto primary = registry::Lookup(token.substr(0, token.find(':')));
    if (!primary.has_value()) {
      continue;
    }
    if (primary->arity < 0) {
      while (++pos < tokens.size() && tokens[pos] != ";" && tokens[pos] != "+") {}
    } else {
      pos += static_cast<std::size_t>(primary->arity);
    }
  }
  return result;
}

std::vector<ResolvedFlag> ResolveConfig(const ConfigInputs& inputs) {
  std::vector<ResolvedFlag> resolved;
  if (!inputs.no_system_config) {
    for (const std::string& flag : inputs.system.globals) {
      if (!IsSkipPermission(flag)) {
        resolved.push_back(ResolvedFlag{.flag = flag, .source = Source::kSystem});
      }
    }
    for (const IniSection& section : inputs.system.named) {
      if (!absl::c_contains(inputs.configs, section.name)) {
        continue;
      }
      for (const IniLine& line : section.lines) {
        for (const std::string& flag : line.tokens) {
          if (!IsSkipPermission(flag)) {
            resolved.push_back(ResolvedFlag{.flag = flag, .source = Source::kSystem});
          }
        }
      }
    }
  }
  if (!inputs.no_user_config) {
    AppendMatching(resolved, inputs.user, inputs.configs, Source::kUser);
  }
  for (const ExplicitConfig& file : inputs.xffrc) {
    AppendMatching(resolved, file.config, inputs.configs, Source::kXffrc);
  }
  return resolved;
}

std::vector<ResolvedFlag> ResolveConfigInOrder(
    const ConfigInputs& inputs,
    const std::vector<std::string>& cli_globals,
    std::string_view invocation_selector) {
  return OrderedResolver(inputs, invocation_selector).Resolve(cli_globals);
}

std::string_view SourceName(Source source) {
  switch (source) {
    case Source::kCli: return "cli";
    case Source::kSystem: return "system";
    case Source::kUnset: return "unset";
    case Source::kUser: return "user";
    case Source::kXffrc: return "xffrc";
  }
  return "unset";
}

bool ArmedFromTrustedTier(
    const ConfigInputs& inputs,
    const std::vector<std::string>& cli_globals,
    std::string_view flag) {
  if (flag == "--allow-exec" && absl::c_contains(DirectiveTokens(inputs.system.globals), "--no-allow-exec")) {
    return false;
  }
  // Expand selectors from automatic tiers too: a transitive named configuration may arm a file.
  // Explicit-file flags are excluded by provenance so an explicit file cannot authorize itself.
  std::vector<std::string> selectors;
  selectors.reserve(inputs.configs.size());
  for (const std::string& name : inputs.configs) {
    selectors.push_back(absl::StrCat("--config=", name));
  }
  selectors.insert(selectors.end(), cli_globals.begin(), cli_globals.end());
  ConfigInputs trusted = inputs;
  trusted.xffrc.clear();
  std::erase_if(trusted.user.named, [](const IniSection& section) { return IsBuiltinStyle(section.name); });
  return absl::c_any_of(ResolveConfigInOrder(trusted, selectors, ""), [&](const ResolvedFlag& resolved) {
    return !resolved.is_argument && resolved.source != Source::kXffrc && resolved.flag == flag;
  });
}

bool IsBuiltinStyle(std::string_view name) {
  return name == "find" || name == "rg" || name == "xff";
}

registry::Style ActiveStyle(const std::vector<std::string>& configs) {
  registry::Style style = registry::Style::kXff;  // the modern xff style is the default
  for (const std::string& name : configs) {
    std::string_view base = name;
    base = base.substr(0, base.find(':'));  // "xff:2" pins an epoch; the base picks the style
    if (base == "find") {
      style = registry::Style::kFind;
    } else if (base == "xff") {
      style = registry::Style::kXff;
    } else if (base == "rg") {
      style = registry::Style::kRg;  // the single opinionated style (gitignore, skip hidden, smart case)
    }
  }
  return style;
}

namespace {

// The active style's name for --explain output.
std::string_view StyleName(registry::Style style) {
  switch (style) {
    case registry::Style::kFind: return "find";
    case registry::Style::kRg: return "rg";
    case registry::Style::kXff: return "xff";
  }
  return "xff";
}

}  // namespace

std::string_view DefaultStyleForProgram(std::string_view argv0) {
  if (const std::string_view::size_type slash = argv0.rfind('/'); slash != std::string_view::npos) {
    argv0 = argv0.substr(slash + 1);  // basename: the last path component
  }
  if (argv0.empty()) {
    return "xff";  // no invocation name -> the modern default
  }
  // A `_full` suffix marks the extras-included twin of a program (the `xff_full` full build, and
  // more generally `<prefix>_full`); it behaves exactly as its base name, so `xff_full` -> xff,
  // `find_full` -> find, `rg_full` -> rg. Without this strip, `find_full` would not match the
  // `find` built-in selector and would silently fall through to the xff default, losing find
  // semantics. The `_full` suffix is thus reserved for the full build and never a config name.
  if (argv0.ends_with("_full")) {
    argv0.remove_suffix(std::string_view("_full").size());
    if (argv0.empty()) {
      return "xff";  // a bare `_full` invocation name -> the modern default
    }
  }
  // The invocation name is the leading --config selector, used verbatim: a built-in style name
  // (find/xff/rg) selects that preset; any other name (a `mytool` symlink to xff, and note there
  // is no `xfd`/`fd` magic - those are just names too) selects a same-named NAMED config, leaving
  // the base style at the modern xff default (ActiveStyle ignores a non-style selector). So
  // aliasing xff auto-activates the matching `mytool:` config block without a preset ever being
  // overloadable. An explicit --config still stacks (last wins).
  return argv0;
}

std::string ExplainConfig(const std::vector<ResolvedFlag>& application) {
  std::string out = "# xff effective configuration (application order; later overrides earlier)\n";
  for (const ResolvedFlag& flag : application) {
    absl::StrAppend(&out, SourceName(flag.source), "\t", flag.flag, "\n");
  }
  return out;
}

std::string ExplainSources(const std::vector<ConfigSource>& sources, registry::Style style) {
  std::string out = absl::StrCat("# xff active style: ", StyleName(style), "\n");
  absl::StrAppend(&out, "# config sources consulted (precedence order)\n");
  for (const ConfigSource& source : sources) {
    absl::StrAppend(
        &out, "source\t", SourceName(source.layer), "\t", source.found ? "found" : "absent", "\t", source.path, "\n");
  }
  return out;
}

}  // namespace xff::config
