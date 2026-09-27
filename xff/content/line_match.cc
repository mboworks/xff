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

#include "xff/content/line_match.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "mbo/status/status_macros.h"
#include "xff/vfs/local_fs.h"

namespace xff::content {

// Grep line semantics, in one place: a hand loop rather than absl::StrSplit, so a trailing '\n'
// does not create a phantom empty final line ("a\nb\n" is two lines) and an empty file is zero
// lines, neither of which StrSplit expresses cleanly. Calls `visit(1-based number, line)` per line,
// with a trailing '\r' stripped (so CRLF matches like LF).
void VisitLines(std::string_view content, absl::FunctionRef<bool(std::size_t, std::string_view)> visit) {
  std::size_t number = 0;
  std::size_t pos = 0;
  while (pos < content.size()) {
    const std::size_t newline = content.find('\n', pos);
    const std::size_t end = newline == std::string_view::npos ? content.size() : newline;
    std::string_view line = content.substr(pos, end - pos);
    ++number;
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (!visit(number, line)) {
      break;
    }
    if (newline == std::string_view::npos) {
      break;
    }
    pos = newline + 1;
  }
}

namespace {
void ForEachLine(std::string_view content, absl::FunctionRef<void(std::size_t, std::string_view)> fn) {
  VisitLines(content, [&](std::size_t number, std::string_view line) {
    fn(number, line);
    return true;
  });
}
}  // namespace

namespace {
class StreamLines final {
 public:
  explicit StreamLines(absl::FunctionRef<bool(std::size_t, std::string_view)> visit) : visit_(visit) {}

  void Append(std::string_view block) {
    while (active_ && !block.empty()) {
      const auto end = block.find('\n');
      if (end == std::string_view::npos) {
        pending_.append(block);
        return;
      }
      const auto part = block.substr(0, end);
      if (pending_.empty()) {
        Emit(part);
      } else {
        pending_.append(part);
        Emit(pending_);
        pending_.clear();
      }
      block.remove_prefix(end + 1);
    }
  }

  void Finish() {
    if (active_ && !pending_.empty()) {
      Emit(pending_);
    }
  }

  void Discard() { pending_.clear(); }

 private:
  void Emit(std::string_view text) {
    if (text.ends_with('\r')) {
      text.remove_suffix(1);
    }
    active_ = visit_(++number_, text);
  }

  absl::FunctionRef<bool(std::size_t, std::string_view)> visit_;
  std::string pending_;
  std::size_t number_ = 0;
  bool active_ = true;
};
}  // namespace

absl::StatusOr<LineScanResult> ScanLines(
    vfs::ReadStream& stream,
    std::uint64_t binary_scan_bytes,
    absl::FunctionRef<bool(std::size_t, std::string_view)> visit) {
  LineScanResult result;
  StreamLines lines(visit);
  for (;;) {
    MBO_ASSIGN_OR_RETURN(const auto block, stream.Read(65'536));
    if (block.empty()) {
      if (!result.binary) {
        lines.Finish();
      }
      return result;
    }
    const auto sniff = static_cast<std::size_t>(std::min<std::uint64_t>(block.size(), binary_scan_bytes));
    result.binary = result.binary || std::string_view(block).substr(0, sniff).contains('\0');
    binary_scan_bytes -= sniff;
    if (result.binary) {
      lines.Discard();
    } else {
      lines.Append(block);
    }
  }
}

std::vector<LineMatch> CollectLineMatches(
    std::string_view content,
    absl::FunctionRef<bool(std::string_view line)> matches) {
  std::vector<LineMatch> result;
  ForEachLine(content, [&](std::size_t number, std::string_view line) {
    if (matches(line)) {
      result.push_back({.number = number, .text = line});
    }
  });
  return result;
}

std::vector<ContextLine> CollectLineMatchesWithContext(
    std::string_view content,
    absl::FunctionRef<bool(std::string_view line)> matches,
    std::size_t before,
    std::size_t after) {
  // Gather every line with its match flag, then select each match's [-before, +after] window.
  std::vector<ContextLine> all;
  ForEachLine(content, [&](std::size_t number, std::string_view line) {
    all.push_back({.number = number, .text = line, .is_match = matches(line), .group = 0});
  });
  const std::size_t count = all.size();
  std::vector<bool> emit(count, false);
  for (std::size_t i = 0; i < count; ++i) {
    if (!all[i].is_match) {
      continue;
    }
    const std::size_t lo = i > before ? i - before : 0;
    const std::size_t hi = after < count - i ? i + after : count - 1;  // count > 0 inside this loop
    for (std::size_t pos = lo; pos <= hi; ++pos) {
      emit[pos] = true;
    }
  }
  // Emit selected lines in order, opening a new group after each gap so a caller can separate
  // non-adjacent blocks. The first emitted line is group 0.
  std::vector<ContextLine> result;
  std::size_t group = 0;
  bool prev_emitted = false;
  bool first = true;
  for (std::size_t i = 0; i < count; ++i) {
    if (!emit[i]) {
      prev_emitted = false;
      continue;
    }
    if (!first && !prev_emitted) {
      ++group;
    }
    all[i].group = group;
    result.push_back(all[i]);
    prev_emitted = true;
    first = false;
  }
  return result;
}

std::size_t CountLines(std::string_view content) {
  std::size_t lines = 0;
  ForEachLine(content, [&lines](std::size_t, std::string_view) { ++lines; });
  return lines;
}

std::optional<std::size_t> ContentLineCount(std::string_view content) {
  if (content.substr(0, std::min(content.size(), kBinaryNulSniffBytes)).contains('\0')) {
    return std::nullopt;  // a NUL in the sniff window marks the file binary; skip it (like content search)
  }
  return CountLines(content);
}

std::optional<std::size_t> FileLineCount(std::string_view path) {
  const vfs::LocalFs fs;
  const absl::StatusOr<std::string> content = fs.ReadContent(path);
  if (!content.ok()) {
    return std::nullopt;  // unreadable / missing -> nothing to count
  }
  return ContentLineCount(*content);
}

}  // namespace xff::content
