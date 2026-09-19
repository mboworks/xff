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

#include "xff/cli/help_width.h"

#include <sys/ioctl.h>  // ioctl, TIOCGWINSZ, struct winsize
#include <unistd.h>     // isatty, STDOUT_FILENO

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "xff/env/env.h"

namespace xff::cli {

std::optional<std::string_view> WidthFlag(std::span<const std::string> globals) {
  std::optional<std::string_view> width;
  for (const std::string& arg : globals) {
    if (arg == "--width") {
      width = "auto";
    } else if (arg.starts_with("--width=")) {
      width = std::string_view(arg).substr(8);
    }
  }
  return width;
}

absl::StatusOr<std::size_t> ResolveHelpWidth(std::optional<std::string_view> flag, std::size_t detected_cols) {
  // A positive width narrower than the minimum wraps at the minimum instead; 0 (no
  // wrap) is exempt.
  const auto clamp = [](std::size_t cols) -> std::size_t {
    return (cols != 0 && cols < kMinHelpWidth) ? kMinHelpWidth : cols;
  };
  // Explicit auto retains uncapped detection, including unknown width (0).
  const auto automatic = [&clamp, detected_cols] { return clamp(detected_cols); };
  if (!flag.has_value()) {
    return detected_cols == 0 ? kDefaultHelpWidth : std::min(automatic(), kDefaultHelpWidth);
  }
  const std::string value = absl::AsciiStrToLower(*flag);
  if (value == "auto") {
    return automatic();
  }
  if (value.starts_with("auto:")) {
    std::size_t cap = 0;
    if (!absl::SimpleAtoi(std::string_view(value).substr(5), &cap) || cap < kMinHelpWidth) {
      return absl::InvalidArgumentError("--width: auto:COLS requires an integer cap of at least 40");
    }
    return detected_cols == 0 ? cap : std::min(automatic(), cap);
  }
  if (value == "none") {
    return std::size_t{0};
  }
  std::size_t cols = 0;
  if (!absl::SimpleAtoi(value, &cols)) {
    return absl::InvalidArgumentError(
        absl::StrCat("--width: expected 'auto', 'auto:COLS', 'none', or a column count, got '", *flag, "'"));
  }
  return clamp(cols);  // an explicit --width=0 means no wrapping, same as "none"
}

std::size_t DetectTerminalWidth() {
  // $COLUMNS wins when set to a positive integer - the conventional override,
  // honored even off a tty so a caller can force a width. Read through the env cache.
  if (const std::optional<std::string> columns = env::Get("COLUMNS")) {
    std::size_t cols = 0;
    if (absl::SimpleAtoi(*columns, &cols) && cols > 0) {
      return cols;
    }
  }
  // Otherwise ask the tty for its width; ioctl / winsize are the C terminal API
  // (ioctl is variadic - the one place we must call a C vararg function).
  struct winsize ws = {};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  if (::isatty(STDOUT_FILENO) != 0 && ::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    return ws.ws_col;
  }
  return 0;
}

}  // namespace xff::cli
