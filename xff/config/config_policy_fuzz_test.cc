// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/config/policy.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace {

constexpr std::size_t kMaxInputBytes = 16UZ * 1'024;

void Require(bool condition) {
  if (!condition) {
    std::abort();
  }
}

void CheckRetainedLines(const xff::config::ConfigFile& file, bool require_safe) {
  if (require_safe) {
    Require(xff::config::LineSafety({.tokens = file.globals}) == xff::registry::Safety::kNone);
  }
  for (const xff::config::IniSection& section : file.named) {
    for (const xff::config::IniLine& line : section.lines) {
      Require(!xff::config::OverloadsPreset(section.name));
      if (require_safe) {
        Require(xff::config::LineSafety(line) == xff::registry::Safety::kNone);
      }
    }
  }
}

std::size_t LineCount(const xff::config::ConfigFile& file) {
  std::size_t count = file.global_lines.size();
  for (const xff::config::IniSection& section : file.named) {
    count += section.lines.size();
  }
  return count;
}

void CheckDrops(const std::vector<xff::config::Drop>& drops) {
  for (const xff::config::Drop& drop : drops) {
    Require(!xff::config::DropMessage(drop).empty());
  }
}

void CheckGate(const xff::config::ConfigInputs& inputs) {
  const xff::config::GateResult unarmed = xff::config::GateConfig(inputs, /*xffrc_armed=*/false);
  const xff::config::GateResult armed = xff::config::GateConfig(inputs, /*xffrc_armed=*/true);

  const std::vector<std::string_view> directives = xff::config::DirectiveTokens(inputs.system.globals);
  const bool prohibited = std::ranges::find(directives, "--no-allow-exec") != directives.end();
  if (prohibited) {
    Require(!xff::config::ArmedFromTrustedTier(inputs, {"--allow-exec"}, "--allow-exec"));
  }
  CheckRetainedLines(unarmed.config.user, prohibited);
  CheckRetainedLines(unarmed.config.xffrc.front().config, /*require_safe=*/true);
  CheckRetainedLines(armed.config.user, prohibited);
  CheckRetainedLines(armed.config.xffrc.front().config, prohibited);
  Require(LineCount(unarmed.config.user) == LineCount(armed.config.user));
  Require(LineCount(unarmed.config.xffrc.front().config) <= LineCount(armed.config.xffrc.front().config));
  CheckDrops(unarmed.drops);
  CheckDrops(armed.drops);

  Require(xff::config::GateConfig(unarmed.config, /*xffrc_armed=*/false).drops.empty());
  Require(xff::config::GateConfig(armed.config, /*xffrc_armed=*/true).drops.empty());

  const std::vector<xff::config::ResolvedFlag> unarmed_flags = xff::config::ResolveConfig(unarmed.config);
  const std::vector<xff::config::ResolvedFlag> armed_flags = xff::config::ResolveConfig(armed.config);
  static_cast<void>(xff::config::ExplainConfig(unarmed_flags));
  static_cast<void>(xff::config::ExplainConfig(armed_flags));

  xff::config::ConfigInputs disabled = inputs;
  disabled.no_system_config = true;
  disabled.no_user_config = true;
  for (const xff::config::ResolvedFlag& flag : xff::config::ResolveConfig(disabled)) {
    Require(flag.source == xff::config::Source::kXffrc);  // explicit files remain selected
  }
}

}  // namespace

// XFF_ABI_POINTER: rules_fuzzing requires libFuzzer's C entry-point signature.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > kMaxInputBytes) {
    return 0;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const xff::config::ConfigInputs inputs{
      .system = xff::config::ParseIni(input),
      .user = xff::config::ParseXffrc(input),
      .xffrc = {{.path = "/fuzz", .config = xff::config::ParseXffrc(input)}},
      .configs = {"xff", "fuzz"},
  };
  CheckGate(inputs);

  // An untrusted explicit file cannot authorize its own sensitive directives.
  const xff::config::ConfigInputs self_arming{
      .xffrc = {{.path = "/fuzz", .config = {.globals = {"--allow-exec"}}}},
      .configs = {"xff"},
  };
  Require(!xff::config::ArmedFromTrustedTier(self_arming, {}, "--allow-exec"));
  return 0;
}
