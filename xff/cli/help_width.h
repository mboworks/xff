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

#ifndef XFF_CLI_HELP_WIDTH_H_
#define XFF_CLI_HELP_WIDTH_H_

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

// Resolves the wrap column for plain `--help` / topic text from the `--width` flag
// and the terminal (#153 / #164). The width then drives PlainTextBackend's word-wrap
// (see wrap.h / plain_backend.h); a resolved 0 means "do not wrap".
namespace xff::cli {

// The smallest wrap width we render at: below this, help degenerates to near
// one-word-per-line, so any positive resolved width is clamped up to it (a terminal
// or an explicit --width narrower than this wraps here instead). 0 (no wrap) is
// exempt.
inline constexpr std::size_t kMinHelpWidth = 40;
inline constexpr std::size_t kDefaultHelpWidth = 110;

// Last width among parser-identified globals; child-command arguments must be excluded.
std::optional<std::string_view> WidthFlag(std::span<const std::string> globals);

// Resolves the plain-help wrap width from the `--width` flag and the terminal.
//   `flag`          the raw --width value, or nullopt when the flag is absent.
//   `detected_cols` the known terminal width, or 0 when unknown (DetectTerminalWidth).
// Values (case-insensitive): absent -> auto:110; "auto:COLS" caps detected width at
// COLS, or uses COLS when detection is unavailable. Caps below 40 are errors.
// Explicit "auto" -> detected_cols, including 0 for unknown/unlimited width.
// "none" or "0" -> 0 (no wrapping); positive integers -> fixed width, at least 40.
// Other values are errors. This pure seam is shared by help and comparison summaries.
[[nodiscard]] absl::StatusOr<std::size_t> ResolveHelpWidth(
    std::optional<std::string_view> flag,
    std::size_t detected_cols);

// The terminal width for stdout: $COLUMNS when it is a positive integer, else the
// tty's column count via ioctl when stdout is a tty, else 0 (unknown). Reads the
// environment and stdout, so it is the impure companion to ResolveHelpWidth.
[[nodiscard]] std::size_t DetectTerminalWidth();

}  // namespace xff::cli

#endif  // XFF_CLI_HELP_WIDTH_H_
