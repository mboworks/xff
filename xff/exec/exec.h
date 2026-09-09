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

#ifndef XFF_EXEC_EXEC_H_
#define XFF_EXEC_EXEC_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xff::exec {

// Runs find's `-exec command... ;` for one entry: every "{}" substring in each
// token of `command` is replaced by `path`, then the command is spawned (PATH-
// searched via posix_spawnp) and waited for. Returns true iff the child ran and
// exited 0, matching find's -exec truth value. Synchronous and serial; the `+`
// batch form and -j parallelism are layered on separately.
bool Execute(const std::vector<std::string>& command, std::string_view path);

// Runs find's `-exec command... +` batch form over `paths`: the command's final
// "{}" token is dropped and the selected paths are appended in ARG_MAX-bounded
// chunks, spawning the command once per chunk. Returns true iff every chunk ran
// and exited 0 (true for empty `paths`, which runs nothing). The caller
// guarantees the command's last token is "{}" (the parser enforces it).
bool ExecuteBatch(const std::vector<std::string>& command, const std::vector<std::string>& paths);

// Like ExecuteBatch (`-exec ... +`), but each spawned chunk runs with its working
// directory set to `dir` -- find's `-execdir ... +`, batched per directory. The
// caller passes the entries' "./<basename>" tokens as `names`.
bool ExecuteBatchInDir(
    const std::vector<std::string>& command,
    const std::vector<std::string>& names,
    std::string_view dir);

// Runs `-exec/-execdir command ;` children concurrently when xff's `-j` is
// greater than one. The same knob independently configures directory-read
// workers; the pools do not share one concurrency budget. `Launch` spawns a
// child and returns as soon as fewer than `cap` are outstanding -- reaping a
// finished one first when already at the cap -- so at most `cap` run at once;
// `Drain` waits for the rest. Because a launched child's exit status is not known
// when Launch returns, the action reports success on launch (its result does not
// gate later predicates) -- the documented parallel-mode trade-off; use `-j 1` for
// find's strict synchronous semantics.
//
// Touched only from the single-threaded walk visitor, so it holds no locks. The
// only other process-spawning path (CaptureOutput) reaps its own child before
// returning, so the unqualified `waitpid` here only ever sees these children.
class ParallelExec {
 public:
  explicit ParallelExec(std::size_t cap) : cap_(cap < 1 ? 1 : cap) {}

  ParallelExec(const ParallelExec&) = delete;
  ParallelExec& operator=(const ParallelExec&) = delete;
  ParallelExec(ParallelExec&&) = delete;
  ParallelExec& operator=(ParallelExec&&) = delete;

  ~ParallelExec() { Drain(); }  // safety net: reap anything a caller forgot to drain

  // Spawn `command` with each "{}" replaced by `target`, child cwd = `dir` (empty
  // inherits ours). Blocks only when `cap` children already run (reaps one first).
  void Launch(const std::vector<std::string>& command, std::string_view target, std::string_view dir);

  // Wait for every outstanding child; returns the total count (across the run)
  // that exited nonzero or failed to spawn. Idempotent.
  std::size_t Drain();

 private:
  void ReapOne();  // wait for one child, tallying a failure if it exited nonzero

  const std::size_t cap_;
  std::size_t outstanding_ = 0;
  std::size_t failures_ = 0;
};

// Spawns `args` verbatim (args[0] PATH-searched via posix_spawnp) and waits,
// returning true iff the child ran and exited 0. Unlike Execute it performs no
// "{}" substitution -- the caller has already produced the final argv (e.g. via
// the field vocabulary under --exec-fields). Empty `args` returns false.
bool ExecuteArgs(const std::vector<std::string>& args);

// Like Execute (find-exact "{}" -> `name` substitution) but runs the child with
// its working directory set to `dir` -- find's -execdir. The caller passes the
// entry's "./<basename>" as `name`; `dir` empty or "." inherits our directory.
// Returns true iff the child ran and exited 0.
bool ExecuteInDir(const std::vector<std::string>& command, std::string_view dir, std::string_view name);

// Like ExecuteArgs (verbatim argv, no "{}" substitution) but runs the child with
// its working directory set to `dir`. Backs -execdir under --exec-fields, where
// the caller has already rendered the argv through the field vocabulary.
bool ExecuteArgsInDir(const std::vector<std::string>& args, std::string_view dir);

// Spawns `args` verbatim, captures the child's stdout, and returns it once the
// child exits. The text is raw (no trimming) and is captured even when the child
// exits nonzero; nullopt only when `args` is empty or the spawn itself fails.
// Drains the pipe before reaping, so output larger than the pipe buffer does not
// deadlock. Backs -capture ({capture.NAME}). `dir`, when non-empty and not ".",
// sets the child's working directory before exec (find's -capturedir); the
// default inherits our directory.
std::optional<std::string> CaptureOutput(const std::vector<std::string>& args, std::string_view dir = {});

}  // namespace xff::exec

#endif  // XFF_EXEC_EXEC_H_
