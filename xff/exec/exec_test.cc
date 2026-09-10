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

#include "xff/exec/exec.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::exec {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

// /bin/sh is used as a portable, always-present command so these tests do not
// depend on PATH lookup or any particular utility being installed.
struct ExecTest : ::testing::Test {};

TEST_F(ExecTest, ReturnsTrueWhenChildExitsZero) {
  EXPECT_THAT(Execute({"/bin/sh", "-c", "exit 0"}, "ignored"), IsTrue());
}

TEST_F(ExecTest, ReturnsFalseWhenChildExitsNonzero) {
  EXPECT_THAT(Execute({"/bin/sh", "-c", "exit 7"}, "ignored"), IsFalse());
}

TEST_F(ExecTest, SubstitutesBracePlaceholderWithPath) {
  // {} is replaced by the path everywhere it appears in a token.
  EXPECT_THAT(Execute({"/bin/sh", "-c", "test \"{}\" = /a/b"}, "/a/b"), IsTrue());
  EXPECT_THAT(Execute({"/bin/sh", "-c", "test \"{}\" = /a/b"}, "/zz"), IsFalse());
}

TEST_F(ExecTest, EmptyCommandIsFalse) {
  EXPECT_THAT(Execute({}, "x"), IsFalse());
}

TEST_F(ExecTest, ExecuteArgsSpawnsVerbatimWithoutSubstitution) {
  EXPECT_THAT(ExecuteArgs({"/bin/sh", "-c", "exit 0"}), IsTrue());
  EXPECT_THAT(ExecuteArgs({"/bin/sh", "-c", "exit 7"}), IsFalse());
  EXPECT_THAT(ExecuteArgs({}), IsFalse());  // empty argv
  // No "{}" substitution: the literal braces reach the child unchanged, so both
  // sides of the comparison are the literal string "{}".
  EXPECT_THAT(ExecuteArgs({"/bin/sh", "-c", "test '{}' = '{}'"}), IsTrue());
}

TEST_F(ExecTest, CaptureOutputReturnsChildStdout) {
  // Raw stdout, no trailing newline added.
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "printf 'hello world'"}), Optional(Eq("hello world")));
}

TEST_F(ExecTest, CaptureOutputCapturesStdoutEvenOnNonzeroExit) {
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "printf partial; exit 3"}), Optional(Eq("partial")));
}

TEST_F(ExecTest, CaptureOutputEmptyArgsOrSpawnFailureIsNullopt) {
  EXPECT_THAT(CaptureOutput({}), Eq(std::nullopt));
  EXPECT_THAT(CaptureOutput({"/nonexistent/xff/zzz"}), Eq(std::nullopt));  // spawn fails
}

TEST_F(ExecTest, CaptureOutputDrainsMoreThanPipeBuffer) {
  // 100 KB exceeds the pipe buffer; the child blocks on write until we read, so
  // this would deadlock if we reaped before draining.
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "head -c 100000 /dev/zero | tr '\\0' x"}), Optional(SizeIs(100'000U)));
}

TEST_F(ExecTest, ExecuteInDirSetsChildWorkingDirectory) {
  // The child runs with its cwd set to `dir`: chdir to "/" -> pwd -P is "/". "/" is
  // always present and accessible, including under the test sandbox.
  EXPECT_THAT(ExecuteInDir({"/bin/sh", "-c", "test \"$(pwd -P)\" = /"}, "/", "ignored"), IsTrue());
}

TEST_F(ExecTest, ExecuteInDirSubstitutesBraceWithName) {
  // {} is replaced by `name` (the caller's "./<basename>"), independent of the cwd.
  EXPECT_THAT(ExecuteInDir({"/bin/sh", "-c", "test \"{}\" = ./f.txt"}, "/", "./f.txt"), IsTrue());
  EXPECT_THAT(ExecuteInDir({"/bin/sh", "-c", "test \"{}\" = ./f.txt"}, "/", "./other"), IsFalse());
  EXPECT_THAT(ExecuteInDir({}, "/", "x"), IsFalse());  // empty command
}

TEST_F(ExecTest, ExecuteArgsInDirSpawnsVerbatimInDir) {
  EXPECT_THAT(ExecuteArgsInDir({"/bin/sh", "-c", "test \"$(pwd -P)\" = /"}, "/"), IsTrue());
  EXPECT_THAT(ExecuteArgsInDir({}, "/"), IsFalse());  // empty argv
  // No "{}" substitution: the literal braces reach the child unchanged.
  EXPECT_THAT(ExecuteArgsInDir({"/bin/sh", "-c", "test '{}' = '{}'"}, "/"), IsTrue());
}

TEST_F(ExecTest, BatchNoOpAndFailureCases) {
  EXPECT_THAT(ExecuteBatch({}, {}), true);
  EXPECT_THAT(ExecuteBatch({"{}"}, {}), true);
  EXPECT_THAT(ExecuteBatch({"{}"}, {"item"}), false);
  EXPECT_THAT(ExecuteBatch({"/nonexistent/xff/zzz", "{}"}, {"item"}), false);
  EXPECT_THAT(ExecuteBatchInDir({"/bin/sh", "-c", "exit 0", "{}"}, {"./item"}, "/nonexistent/xff/dir"), false);
}

TEST_F(ExecTest, CaptureOutputFailsWhenWorkingDirectoryDoesNotExist) {
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "printf unreachable"}, "/nonexistent/xff/dir"), Eq(std::nullopt));
}

TEST_F(ExecTest, DirVariantsWithEmptyOrDotDirInheritCwd) {
  // An empty or "." dir means "do not chdir" -- like ExecuteArgs/Execute.
  EXPECT_THAT(ExecuteArgsInDir({"/bin/sh", "-c", "exit 0"}, ""), IsTrue());
  EXPECT_THAT(ExecuteArgsInDir({"/bin/sh", "-c", "exit 0"}, "."), IsTrue());
}

TEST_F(ExecTest, CaptureOutputRunsChildInDirWhenDirGiven) {
  // With dir "/", the child's cwd is "/", so `pwd -P` prints "/\n" (raw, untrimmed).
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "pwd -P"}, "/"), Optional(Eq("/\n")));
}

TEST_F(ExecTest, CaptureOutputDefaultDirInheritsCwd) {
  // The default (empty) dir = no chdir: the original single-argument behavior.
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "printf hi"}), Optional(Eq("hi")));
}

TEST_F(ExecTest, ParallelExecDrainCountsNonzeroExits) {
  // Three children each `exit 1`; with a cap of 2 they are reaped as slots free up
  // and at Drain. Drain returns the total failure count across the run.
  ParallelExec pool(2);
  for (int i = 0; i < 3; ++i) {
    pool.Launch({"/bin/sh", "-c", "exit 1"}, "ignored", {});
  }
  EXPECT_THAT(pool.Drain(), Eq(3U));
}

TEST_F(ExecTest, ParallelExecDrainIsZeroWhenAllSucceed) {
  ParallelExec pool(2);
  for (int i = 0; i < 3; ++i) {
    pool.Launch({"/bin/sh", "-c", "exit 0"}, "ignored", {});
  }
  EXPECT_THAT(pool.Drain(), Eq(0U));
}

TEST_F(ExecTest, ParallelExecCountsSpawnFailures) {
  // A command that cannot be spawned counts as one failure (find reports the child
  // could not run) without aborting the rest of the run.
  ParallelExec pool(2);
  pool.Launch({"/nonexistent/xff/zzz"}, "ignored", {});
  EXPECT_THAT(pool.Drain(), Eq(1U));
}

TEST_F(ExecTest, ParallelExecSubstitutesTargetForBrace) {
  // {} is replaced by `target` in each token, like Execute: the matching target
  // exits 0, the mismatching one exits 1, so exactly one failure is tallied --
  // proving both children actually ran with the right substitution.
  ParallelExec pool(2);
  pool.Launch({"/bin/sh", "-c", "test \"{}\" = ok"}, "ok", {});
  pool.Launch({"/bin/sh", "-c", "test \"{}\" = ok"}, "no", {});
  EXPECT_THAT(pool.Drain(), Eq(1U));
}

TEST_F(ExecTest, ParallelExecRunsChildInDir) {
  // A non-empty dir sets the child cwd (find's -execdir): chdir "/" then verify.
  ParallelExec pool(2);
  pool.Launch({"/bin/sh", "-c", "test \"$(pwd -P)\" = /"}, "ignored", "/");
  EXPECT_THAT(pool.Drain(), Eq(0U));
}

TEST_F(ExecTest, ParallelExecDrainIsIdempotent) {
  ParallelExec pool(2);
  pool.Launch({"/bin/sh", "-c", "exit 1"}, "ignored", {});
  EXPECT_THAT(pool.Drain(), Eq(1U));
  EXPECT_THAT(pool.Drain(), Eq(1U));  // nothing outstanding; the tally is unchanged
}

TEST_F(ExecTest, ParallelExecRunsEveryChildBeyondCap) {
  // Six launches with a cap of 2 must all run: each appends its (short, O_APPEND-
  // atomic) target line, so the file ends with one line per launch. Exercises the
  // reap-one-when-at-cap path repeatedly.
  const std::string out = (std::filesystem::path(::testing::TempDir()) / "xff_parexec_all.lst").string();
  std::error_code ec;
  std::filesystem::remove(out, ec);
  {
    ParallelExec pool(2);
    for (int i = 0; i < 6; ++i) {
      pool.Launch({"/bin/sh", "-c", "echo {} >> '" + out + "'"}, std::to_string(i), {});
    }
    EXPECT_THAT(pool.Drain(), Eq(0U));
  }
  std::ifstream in(out, std::ios::binary);
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre("0", "1", "2", "3", "4", "5"));
  std::filesystem::remove(out, ec);
}

TEST_F(ExecTest, ParallelExecDestructorDrainsOutstandingChildren) {
  // Without an explicit Drain, the destructor still waits for every child, so a
  // forgotten Drain leaves no zombie and loses no child's work: after the pool
  // leaves scope all three lines are present.
  const std::string out = (std::filesystem::path(::testing::TempDir()) / "xff_parexec_dtor.lst").string();
  std::error_code ec;
  std::filesystem::remove(out, ec);
  {
    ParallelExec pool(4);
    for (int i = 0; i < 3; ++i) {
      pool.Launch({"/bin/sh", "-c", "echo {} >> '" + out + "'"}, std::to_string(i), {});
    }
    // No Drain(): the destructor reaps.
  }
  std::ifstream in(out, std::ios::binary);
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre("0", "1", "2"));
  std::filesystem::remove(out, ec);
}

}  // namespace
}  // namespace xff::exec
