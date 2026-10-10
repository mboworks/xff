// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Fork the measured pipeline only after exec has discarded Python's address space.
// stdin/stderr and the output pipe are supplied by the caller; stdout carries JSON.
// This adapter does not open or mutate host files and does not launch a shell.

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "mbo/status/status_macros.h"

namespace {

using Command = std::vector<std::string>;
using Pipe = std::array<int, 2>;

// POSIX signal handlers require shared signal-safe state.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t interrupted = 0;

void Interrupt(int /*signal*/) {
  interrupted = 1;
}

struct Request final {
  int output_fd = -1;
  std::string cwd;
  std::vector<Command> commands;
};

absl::StatusOr<Request> ParseRequest(const std::vector<std::string>& arguments) {
  Request request;
  if (arguments.size() < 4 || !absl::SimpleAtoi(arguments[0], &request.output_fd) || request.output_fd <= 2) {
    return absl::InvalidArgumentError("expected output pipe fd, cwd, and length-prefixed commands");
  }
  request.cwd = arguments[1];
  std::size_t next = 2;
  while (next < arguments.size()) {
    std::size_t count = 0;
    const std::string& count_argument = arguments[next++];
    if (!absl::SimpleAtoi(count_argument, &count) || count == 0 || count > arguments.size() - next) {
      return absl::InvalidArgumentError("invalid command argument count");
    }
    Command command;
    command.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      command.push_back(arguments[next++]);
    }
    if (command.front().empty() || request.commands.size() >= 16) {
      return absl::InvalidArgumentError("expected nonempty executable and at most 16 commands");
    }
    request.commands.push_back(std::move(command));
  }
  return request;
}

absl::Status LastError(const std::string_view operation) {
  return absl::ErrnoToStatus(errno, operation);
}

// XFF_HOST_IO: creates only an anonymous pipeline, never opens a host file.
absl::StatusOr<Pipe> MakePipe() {
  Pipe pipe_fds{};
  // macOS has no pipe2; this single-threaded launcher closes unused endpoints before exec.
  // NOLINTNEXTLINE(android-cloexec-pipe)
  if (::pipe(pipe_fds.data()) != 0) {
    return LastError("pipe");
  }
  // A caller may have closed stdin. Keep pipeline endpoints out of the stdio
  // slots so dup2/ClosePipes cannot mistake a new pipe for inherited input.
  for (int& fd : pipe_fds) {
    if (fd <= STDERR_FILENO) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): fcntl is the POSIX descriptor-duplication ABI.
      const int replacement = ::fcntl(fd, F_DUPFD, STDERR_FILENO + 1);
      if (replacement < 0) {
        absl::Status status = LastError("duplicate pipe descriptor");
        for (const int owned : pipe_fds) {
          ::close(owned);
        }
        return status;
      }
      ::close(fd);
      fd = replacement;
    }
  }
  return pipe_fds;
}

struct ChildUsage final {
  int exit_code = 0;
  double user_seconds = 0;
  double system_seconds = 0;
  std::int64_t rss_bytes = 0;
};

absl::StatusOr<ChildUsage> WaitChild(pid_t pid) {
  int status = 0;
  rusage usage{};
  pid_t result = -1;
  for (;;) {
    if (interrupted != 0) {
      return absl::CancelledError("pipeline wait interrupted");
    }
    result = ::wait4(pid, &status, 0, &usage);
    if (result >= 0) {
      break;
    }
    if (errno != EINTR) {
      return LastError("wait4");
    }
  }
  if (!WIFEXITED(status) && !WIFSIGNALED(status)) {
    return absl::InternalError("unexpected child wait status");
  }
  constexpr double kMicrosPerSecond = 1'000'000;
#ifdef __APPLE__
  constexpr std::int64_t kRssScale = 1;
#else
  constexpr std::int64_t kRssScale = 1'024;
#endif
  return ChildUsage{
      .exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status),
      .user_seconds =
          static_cast<double>(usage.ru_utime.tv_sec) + (static_cast<double>(usage.ru_utime.tv_usec) / kMicrosPerSecond),
      .system_seconds =
          static_cast<double>(usage.ru_stime.tv_sec) + (static_cast<double>(usage.ru_stime.tv_usec) / kMicrosPerSecond),
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the POSIX ru_maxrss ABI aliases a Linux union member.
      .rss_bytes = usage.ru_maxrss * kRssScale,
  };
}

class Pipeline final {
 public:
  Pipeline() = default;
  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;
  Pipeline(Pipeline&&) = delete;
  Pipeline& operator=(Pipeline&&) = delete;

  // XFF_HOST_IO: releases owned anonymous-pipe descriptors and reaps owned processes.
  ~Pipeline() {
    ClosePipes();
    for (const pid_t child : children_) {
      if (child > 0) {
        ::kill(child, SIGKILL);
      }
    }
    for (const pid_t child : children_) {
      if (child > 0) {
        while (::waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
      }
    }
  }

  // XFF_HOST_IO: connects caller-authorized stdio; no host handles are opened.
  absl::StatusOr<std::vector<ChildUsage>> Run(const Request& request) {
    for (std::size_t index = 1; index < request.commands.size(); ++index) {
      MBO_ASSIGN_OR_RETURN(const Pipe pipe_fds, MakePipe());
      pipes_.push_back(pipe_fds);
    }
    children_.reserve(request.commands.size());
    // The index connects commands with the preceding/following pipeline endpoints.
    for (std::size_t index = 0; index < request.commands.size(); ++index) {
      if (interrupted != 0) {
        return absl::CancelledError("pipeline launch interrupted");
      }
      const pid_t child = ::fork();
      if (child < 0) {
        return LastError("fork");
      }
      if (child == 0) {
        const int input = index == 0 ? STDIN_FILENO : pipes_[index - 1][0];
        const int output = index + 1 == request.commands.size() ? request.output_fd : pipes_[index][1];
        ExecCommand(request.commands[index], input, output, request.output_fd);
      }
      children_.push_back(child);
    }
    ClosePipes();
    ::close(request.output_fd);
    std::vector<ChildUsage> usages;
    usages.reserve(children_.size());
    for (pid_t& child : children_) {
      MBO_ASSIGN_OR_RETURN(const ChildUsage usage, WaitChild(child));
      child = -1;
      usages.push_back(usage);
    }
    return usages;
  }

 private:
  // XFF_HOST_IO: POSIX child adapter for inherited stdio and anonymous pipes, not host files.
  [[noreturn]] void ExecCommand(const Command& command, int input, int output, int output_fd) {
    if ((input != STDIN_FILENO && ::dup2(input, STDIN_FILENO) < 0) || ::dup2(output, STDOUT_FILENO) < 0) {
      std::cerr << LastError("cannot connect measured command") << '\n';
      ::_exit(127);
    }
    ClosePipes();
    ::close(output_fd);
    // XFF_ABI_POINTER: POSIX execvp requires a null-terminated char* argv; it does not mutate strings.
    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (const std::string& argument : command) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): execvp requires mutable argv but does not write it.
      argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    ::execvp(command.front().c_str(), argv.data());
    std::cerr << LastError("cannot exec " + command.front()) << '\n';
    ::_exit(127);
  }

  // XFF_HOST_IO: closes only this adapter's anonymous-pipe descriptors.
  void ClosePipes() {
    for (const Pipe& pipe_fds : pipes_) {
      ::close(pipe_fds[0]);
      ::close(pipe_fds[1]);
    }
    pipes_.clear();
  }

  std::vector<Pipe> pipes_;
  std::vector<pid_t> children_;
};

void Report(const std::vector<ChildUsage>& usages) {
  double user = 0;
  double system = 0;
  for (const ChildUsage& usage : usages) {
    user += usage.user_seconds;
    system += usage.system_seconds;
  }
  std::cout << std::setprecision(17) << "{\"user_cpu_seconds\":" << user << ",\"system_cpu_seconds\":" << system
            << ",\"rss_bytes\":[";
  bool first = true;
  for (const ChildUsage& usage : usages) {
    std::cout << (first ? "" : ",") << usage.rss_bytes;
    first = false;
  }
  std::cout << "],\"exit_codes\":[";
  first = true;
  for (const ChildUsage& usage : usages) {
    std::cout << (first ? "" : ",") << usage.exit_code;
    first = false;
  }
  std::cout << "]}\n";
}

// XFF_HOST_IO: changes only the launcher's process cwd; no host paths are mutated.
absl::Status Run(const std::vector<std::string>& arguments) {
  MBO_ASSIGN_OR_RETURN(const Request request, ParseRequest(arguments));
  if (!request.cwd.empty() && ::chdir(request.cwd.c_str()) < 0) {
    return LastError("chdir");
  }
  struct sigaction action{};
  action.sa_handler = Interrupt;
  // Darwin exposes sigemptyset as a function-like macro, so it cannot be qualified.
  sigemptyset(&action.sa_mask);
  if (::sigaction(SIGTERM, &action, nullptr) != 0 || ::sigaction(SIGINT, &action, nullptr) != 0) {
    return LastError("sigaction");
  }
  Pipeline pipeline;
  MBO_ASSIGN_OR_RETURN(const std::vector<ChildUsage> usages, pipeline.Run(request));
  Report(usages);
  return absl::OkStatus();
}

}  // namespace

// XFF_ABI_POINTER: the process entry ABI requires char** argv; internal helpers use owned strings.
int main(int argc, char** argv) {
  std::vector<std::string> arguments;
  for (int index = 1; index < argc; ++index) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): indexing the process-entry argv ABI.
    arguments.emplace_back(argv[index]);
  }
  const absl::Status status = Run(arguments);
  if (!status.ok()) {
    std::cerr << status << '\n';
    return 2;
  }
  return 0;
}
