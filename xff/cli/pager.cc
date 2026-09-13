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

#include "xff/cli/pager.h"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "xff/env/env.h"

namespace xff::cli {
namespace {

// Writes `text` straight to stdout - the fallback whenever paging is off or fails.
void WriteToStdout(std::string_view text) {
  std::cout << text;
}

// Pipes `text` through `command` (run via `sh -c`, so args and pipelines work), with
// `text` on the child's stdin. Returns false if the pager could not be started (the
// caller then falls back to stdout); a pager that starts and then exits early is a
// success (the user quit less), not a failure.
[[nodiscard]] bool PipeThroughPager(std::string_view text, const std::string& command) {
  std::array<int, 2> fds{};
  // macOS has no pipe2(), so the CLOEXEC flag is not available here; the child closes
  // both raw ends explicitly before exec, so nothing leaks.
  if (::pipe(fds.data()) != 0) {  // NOLINT(android-cloexec-pipe)
    return false;
  }
  const int read_fd = fds[0];
  const int write_fd = fds[1];
  const ::pid_t pid = ::fork();
  if (pid < 0) {
    ::close(read_fd);
    ::close(write_fd);
    return false;
  }
  if (pid == 0) {
    // Child: the pager reads our text as its stdin, then runs on the inherited stdout.
    ::dup2(read_fd, STDIN_FILENO);
    ::close(read_fd);
    ::close(write_fd);
    // execlp needs a C string at the exec boundary; `sh -c` handles args / pipelines.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ::execlp("sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);  // exec failed: nothing was consumed yet, so the parent still has the text
  }
  // Parent: feed the text to the pager, ignoring SIGPIPE so an early quit (a closed read
  // end) surfaces as a write error we can stop on rather than killing the process.
  ::close(read_fd);
  struct ::sigaction ignore{};
  struct ::sigaction previous{};
  ignore.sa_handler = SIG_IGN;
  ::sigaction(SIGPIPE, &ignore, &previous);
  std::size_t written = 0;
  while (written < text.size()) {
    const std::string_view remaining = text.substr(written);
    const ::ssize_t wrote = ::write(write_fd, remaining.data(), remaining.size());
    if (wrote <= 0) {
      break;  // the pager closed its input (e.g. user quit less before the end)
    }
    written += static_cast<std::size_t>(wrote);
  }
  ::close(write_fd);
  ::sigaction(SIGPIPE, &previous, nullptr);
  int status = 0;
  ::waitpid(pid, &status, 0);
  // exec failure is our only "could not start" signal; the text was not consumed, so let
  // the caller re-emit it to stdout.
  return WIFEXITED(status) == 0 || WEXITSTATUS(status) != 127;
}

}  // namespace

absl::StatusOr<PagerConfig> ResolvePager(const std::vector<std::string>& args) {
  PagerConfig pager;
  for (const std::string_view arg : args) {
    if (arg == "--no-pager" || arg == "--pager=never") {
      pager = {.when = PagerWhen::kNever};
    } else if (arg == "--pager" || arg == "--pager=always") {
      pager = {.when = PagerWhen::kAlways};
    } else if (arg == "--pager=help") {
      pager = {};
    } else if (arg == "--pager=auto") {
      pager = {.when = PagerWhen::kAuto};
    } else if (arg == "--pager=all") {
      return absl::InvalidArgumentError("invalid --pager value: all; use --pager=auto or --pager=always");
    } else if (arg.starts_with("--pager=")) {
      const std::string_view command = arg.substr(std::string_view("--pager=").size());
      if (command.empty()) {
        return absl::InvalidArgumentError("--pager requires help, auto, always, never, or a command");
      }
      pager = {.when = PagerWhen::kAlways, .command = std::string(command)};
    }
  }
  return pager;
}

PagerDecision DecidePager(const PagerConfig& pager, PagerOutput output, bool stdout_is_tty, bool suppressed) {
  if (suppressed) {
    return {};
  }
  bool page = false;
  switch (pager.when) {
    case PagerWhen::kHelp: page = output == PagerOutput::kMeta && stdout_is_tty; break;
    case PagerWhen::kAuto: page = stdout_is_tty; break;
    case PagerWhen::kAlways: page = true; break;
    case PagerWhen::kNever: page = false; break;
  }
  return {.action = page ? PagerAction::kPage : PagerAction::kDirect, .command = pager.command};
}

namespace {

// Automatic text-pager selection prefers known installed pagers with xff's flags, then consults
// the tool-specific and generic environment commands only if neither known pager exists.
// The shell performs command discovery in the child immediately before exec, avoiding a check/use
// race. `cat` is the final lossless fallback, not a pager selected while less or more is available.
std::string ResolveAutomaticTextPager() {
  return "if command -v less >/dev/null 2>&1; then exec less -FRX; "
         "elif command -v more >/dev/null 2>&1; then exec more; "
         "elif [ -n \"${XFF_PAGER:-}\" ]; then exec sh -c \"$XFF_PAGER\"; "
         "elif [ -n \"${PAGER:-}\" ]; then exec sh -c \"$PAGER\"; "
         "else exec cat; fi";
}

}  // namespace

std::string ResolvePagerCommand(PagerKind kind, std::string_view explicit_command) {
  if (!explicit_command.empty()) {
    if (kind == PagerKind::kMan) {
      return absl::StrCat(
          "if command -v mandoc >/dev/null 2>&1; then mandoc | { ", explicit_command, "; }; else exit 127; fi");
    }
    return std::string(explicit_command);
  }
  if (kind == PagerKind::kMan) {
    // $XFF_MANPAGER wins outright (empty disables), so a user can plug in any roff
    // viewer, e.g. `groff -mandoc -Tutf8 | less -R`.
    if (const std::optional<std::string> man_pager = env::Get("XFF_MANPAGER")) {
      return *man_pager;
    }
    // The built-in formats the roff with mandoc (the portable roff formatter --man's own
    // help points at) and sends it through the same automatic text-pager selection. If mandoc is
    // absent it exits 127, so EmitPaged falls back to raw roff rather than showing an empty page.
    return absl::StrCat(
        "if command -v mandoc >/dev/null 2>&1; then mandoc | { ", ResolveAutomaticTextPager(),
        "; }; else exit 127; fi");
  }
  return ResolveAutomaticTextPager();
}

void EmitPaged(std::string_view text, const PagerDecision& decision, PagerKind kind) {
  if (decision.action == PagerAction::kDirect) {
    WriteToStdout(text);
    return;
  }
  const std::string command = ResolvePagerCommand(kind, decision.command);
  if (command.empty() || !PipeThroughPager(text, command)) {
    WriteToStdout(text);
  }
}

namespace {

// SIGPIPE is ignored while a streaming pager is attached, so a quit at the first screen turns
// into failing writes we can swallow rather than a signal death mid-walk. The previous
// disposition is restored when the pager is finished.
struct ::sigaction g_previous_sigpipe;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

PagerStream::PagerStream(const PagerDecision& decision) {
  if (decision.action == PagerAction::kDirect) {
    return;
  }
  const std::string command = ResolvePagerCommand(PagerKind::kText, decision.command);
  if (command.empty()) {
    return;
  }
  std::array<int, 2> fds{};
  // macOS has no pipe2(), so no CLOEXEC here; the child closes both raw ends before exec.
  if (::pipe(fds.data()) != 0) {  // NOLINT(android-cloexec-pipe)
    return;
  }
  const int read_fd = fds[0];
  const int write_fd = fds[1];
  const ::pid_t pid = ::fork();
  if (pid < 0) {
    ::close(read_fd);
    ::close(write_fd);
    return;  // could not fork: run unpaged rather than not at all
  }
  if (pid == 0) {
    // Child: our stdout becomes its stdin; it draws on the terminal we still share.
    ::dup2(read_fd, STDIN_FILENO);
    ::close(read_fd);
    ::close(write_fd);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ::execlp("sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  // Parent: from here on, THIS process's stdout is the pipe, so every writer is paged without
  // being told - std::cout, the renderers, and any child that inherits our descriptors.
  ::close(read_fd);
  std::cout.flush();
  // A plain dup, not F_DUPFD_CLOEXEC: this fd exists to be restored onto STDOUT_FILENO, and any
  // child xff spawns while the pager is attached should inherit the paged stdout, not lose it.
  saved_stdout_ = ::dup(STDOUT_FILENO);  // NOLINT(android-cloexec-dup)
  ::dup2(write_fd, STDOUT_FILENO);
  ::close(write_fd);
  struct ::sigaction ignore{};
  ignore.sa_handler = SIG_IGN;
  ::sigaction(SIGPIPE, &ignore, &g_previous_sigpipe);
  pid_ = pid;
  active_ = true;
}

void PagerStream::Finish() {
  if (!active_) {
    return;
  }
  active_ = false;
  // Flushing before the swap is what actually delivers the tail of the listing. A failed flush
  // (the user quit the pager) is deliberately ignored, and the stream's error state cleared, so
  // anything printed after the restore still reaches the terminal.
  std::cout.flush();
  std::cout.clear();
  ::dup2(saved_stdout_, STDOUT_FILENO);  // also closes the pipe, so the pager sees EOF
  ::close(saved_stdout_);
  saved_stdout_ = -1;
  ::sigaction(SIGPIPE, &g_previous_sigpipe, nullptr);
  int status = 0;
  ::waitpid(pid_, &status, 0);  // blocks until the user quits the pager, as a pager should
  pid_ = -1;
}

PagerStream::~PagerStream() {
  Finish();
}

}  // namespace xff::cli
