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

#ifndef XFF_CONTENT_LINE_MATCH_H_
#define XFF_CONTENT_LINE_MATCH_H_

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"

namespace xff::content {

// How many leading bytes to sniff for a NUL when classifying a file as binary. There is no standard
// definition of a "binary file"; this is the heuristic git's buffer_is_binary() uses (its
// FIRST_FEW_BYTES = 8000) and the spirit of grep -I / ripgrep (a NUL means binary). Every xff site
// that skips or classifies binaries -- content search (-grep/-content/-rxc), line counting ({lines}),
// -diff, and the -text/-binary predicates -- shares this one value so they all agree.
inline constexpr std::size_t kBinaryNulSniffBytes = 8'000;

// One matching line within a file's content, as grep/ripgrep report it: the line's
// 1-based number and its text. Backs the `-grep` action's `{line}` / `{text}`
// fields.
struct LineMatch {
  std::size_t number = 0;  // 1-based line number
  std::string_view text;   // the line's text, trailing '\r' stripped; aliases the searched content
};

// Splits `content` into lines and returns, in order, each line for which `matches`
// is true, paired with its 1-based number. Lines end at '\n' (a trailing '\r' is
// stripped so a CRLF file matches the same as LF); the byte after a final '\n' is
// not a line, so "a\nb\n" is two lines and "" is none, matching grep. The returned
// `text` views alias `content`, which must outlive the result.
std::vector<LineMatch> CollectLineMatches(
    std::string_view content,
    absl::FunctionRef<bool(std::string_view line)> matches);

// One line selected for grep-with-context output (grep -A/-B/-C): its 1-based number, text,
// whether it is a match (vs a surrounding context line), and its group index.
struct ContextLine {
  std::size_t number = 0;
  std::string_view text;
  bool is_match = false;
  // 0-based group index; increments at each gap between emitted lines, so a caller prints a
  // group separator ("--") before every group after the first.
  std::size_t group = 0;
};

// Like CollectLineMatches, but also returns `before` lines preceding and `after` lines following
// each match (grep -B / -A; -C is before == after). Overlapping or adjacent windows merge, and a
// gap between emitted lines starts a new `group`. With before == after == 0 this returns exactly
// the match lines, each its own group (so a caller can suppress the separator when no context is
// set). Same line semantics as CollectLineMatches; the `text` views alias `content`.
std::vector<ContextLine> CollectLineMatchesWithContext(
    std::string_view content,
    absl::FunctionRef<bool(std::string_view line)> matches,
    std::size_t before,
    std::size_t after);

// The number of text lines in `content`, using the same line semantics as CollectLineMatches:
// each '\n'-terminated segment is a line, plus a trailing partial line with no final '\n'. So
// "" is 0, "a\nb\n" is 2, "a\nb" is 2, "a" is 1, "\n" is 1. Like `wc -l` but also counting an
// unterminated final line.
std::size_t CountLines(std::string_view content);

// CountLines for `content`, or nullopt when it is binary (a NUL byte in the first 8,000 bytes,
// grep/ripgrep's heuristic - the same rule content search uses to skip binaries). Split out of
// FileLineCount so a caller that ALREADY has the bytes - an archive member, read through its own
// filesystem - counts them by the identical rule instead of re-reading by path.
std::optional<std::size_t> ContentLineCount(std::string_view content);

// ContentLineCount for the regular file at `path`, or nullopt when it is unreadable as well. Reads
// the whole file, so it is expensive. The caller is responsible for restricting this to regular
// files.
std::optional<std::size_t> FileLineCount(std::string_view path);

}  // namespace xff::content

#endif  // XFF_CONTENT_LINE_MATCH_H_
