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

#include "xff/engine/run.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/env/env.h"
#include "xff/parser/parser.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

namespace fs = ::std::filesystem;
using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::MatchesRegex;
using ::testing::Ne;
using ::testing::Not;
using ::testing::PrintToString;
using ::testing::UnorderedElementsAre;

// Fixture tree:
//   <root>/a.txt
//   <root>/b.md
//   <root>/sub/c.txt
struct RunTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir())
            / (std::string("xff_run_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code ec;
    fs::remove_all(root_, ec);
    ASSERT_TRUE(fs::create_directories(root_ / "sub"));
    { std::ofstream(root_ / "a.txt") << "a"; }
    { std::ofstream(root_ / "b.md") << "b"; }
    { std::ofstream(root_ / "sub" / "c.txt") << "c"; }
  }

  void TearDown() override {
    env::ClearForTesting();
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  std::string Path(std::string_view child) const { return (root_ / child).string(); }

  // Parses `<root> <expr...>`, runs it, and returns the emitted records with a
  // single trailing terminator ('\n' or '\0') stripped.
  std::vector<std::string> RunExpr(const std::vector<std::string>& expr) {
    std::vector<std::string> argv = {root_.string()};
    argv.insert(argv.end(), expr.begin(), expr.end());
    const auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> records;
    last_errors_ = RunFind(
                       *command, fs_,
                       [&](std::string_view record) {
                         std::string text(record);
                         if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
                           text.pop_back();
                         }
                         records.push_back(std::move(text));
                       },
                       [](std::string_view, absl::Status) {})
                       .errors;
    return records;
  }

  // Like RunExpr, but takes the whole argv (so leading globals such as --summary
  // can come before the root), returning the emitted records, terminator stripped.
  std::vector<std::string> RunArgvRecords(const std::vector<std::string>& argv) {
    const auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> records;
    if (!command.ok()) {
      return records;
    }
    last_errors_ = RunFind(
                       *command, fs_,
                       [&](std::string_view record) {
                         std::string text(record);
                         if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
                           text.pop_back();
                         }
                         records.push_back(std::move(text));
                       },
                       [](std::string_view, absl::Status) {})
                       .errors;
    return records;
  }

  // Runs the bare root under `style` to exercise the mode-scoped traversal
  // defaults (RunFind's `style`), returning records with the terminator stripped.
  std::vector<std::string> RunStyled(registry::Style style) const {
    const auto command = parser::Parse({root_.string()});
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> records;
    if (!command.ok()) {
      return records;
    }
    RunFind(
        *command, fs_,
        [&](std::string_view record) {
          std::string text(record);
          if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
            text.pop_back();
          }
          records.push_back(std::move(text));
        },
        [](std::string_view, absl::Status) {}, style);
    return records;
  }

  vfs::LocalFs fs_;
  fs::path root_;
  int last_errors_ = 0;
};

TEST_F(RunTest, NoExpressionPrintsEverything) {
  EXPECT_THAT(
      RunExpr({}), UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareUsesTheRequestedIgnorePolicyIndependentlyOnEachSide) {
  const fs::path left = root_ / "left";
  const fs::path right = root_ / "right";
  ASSERT_TRUE(fs::create_directories(left / "nested"));
  ASSERT_TRUE(fs::create_directories(right / "nested"));
  { std::ofstream(left / ".gitignore") << "left-ignored\n"; }
  { std::ofstream(right / ".gitignore") << "right-ignored\n"; }
  { std::ofstream(left / "same") << "same"; }
  { std::ofstream(right / "same") << "same"; }
  { std::ofstream(left / "changed", std::ios::binary) << std::string("a\0b", 3); }
  { std::ofstream(right / "changed", std::ios::binary) << std::string("a\0c", 3); }
  { std::ofstream(left / "left-only") << "left"; }
  { std::ofstream(right / "right-only") << "right"; }
  { std::ofstream(left / "left-ignored") << "ignored"; }
  { std::ofstream(right / "right-ignored") << "ignored"; }

  EXPECT_THAT(
      RunArgvRecords({"--compare", left.string(), right.string()}),
      ElementsAre(
          "different\t.gitignore", "different\tchanged", "left-only\tleft-ignored", "left-only\tleft-only",
          "right-only\tright-ignored", "right-only\tright-only"));
  EXPECT_THAT(last_errors_, 0);

  EXPECT_THAT(
      RunArgvRecords({"--compare", "--gitignore=on", left.string(), right.string()}),
      ElementsAre("different\t.gitignore", "different\tchanged", "left-only\tleft-only", "right-only\tright-only"));
  EXPECT_THAT(last_errors_, 0);

  EXPECT_THAT(
      RunArgvRecords({"--compare=status", "--compare-select=identical", left.string(), right.string()}),
      ElementsAre("identical\tsame"));
  EXPECT_THAT(last_errors_, 0);

  const std::vector<std::string> patch =
      RunArgvRecords({"--compare=diff", "--compare-select=different", left.string(), right.string()});
  EXPECT_THAT(patch, Contains(HasSubstr("--- a/.gitignore\n+++ b/.gitignore")));
  EXPECT_THAT(patch, Contains("Binary files a/changed and b/changed differ"));
  EXPECT_THAT(last_errors_, 0);

  const std::vector<std::string> complete_patch = RunArgvRecords({"--compare=diff", left.string(), right.string()});
  EXPECT_THAT(complete_patch, Contains(HasSubstr("--- a/left-only\n+++ /dev/null")));
  EXPECT_THAT(complete_patch, Contains(HasSubstr("--- /dev/null\n+++ b/right-only")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareRequiresTwoRootsAndAppliesTheExpressionToBoth) {
  EXPECT_THAT(RunArgvRecords({"--compare", root_.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  const fs::path left = root_ / "expression-left";
  const fs::path right = root_ / "expression-right";
  ASSERT_TRUE(fs::create_directories(left));
  ASSERT_TRUE(fs::create_directories(right));
  { std::ofstream(left / "selected.txt") << "left"; }
  { std::ofstream(right / "selected.txt") << "right"; }
  { std::ofstream(left / "excluded.md") << "left"; }
  { std::ofstream(right / "excluded.md") << "right"; }
  EXPECT_THAT(
      RunArgvRecords({"--compare", left.string(), right.string(), "-name", "*.txt"}),
      ElementsAre("different\tselected.txt"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareSelectsEveryResultKind) {
  const fs::path left = root_ / "select-left";
  const fs::path right = root_ / "select-right";
  ASSERT_TRUE(fs::create_directories(left));
  ASSERT_TRUE(fs::create_directories(right));
  { std::ofstream(left / "left") << "left"; }
  { std::ofstream(right / "right") << "right"; }
  { std::ofstream(left / "same") << "same"; }
  { std::ofstream(right / "same") << "same"; }
  { std::ofstream(left / "different") << "old"; }
  { std::ofstream(right / "different") << "newer"; }

  EXPECT_THAT(
      RunArgvRecords({"--compare", "--compare-select=all", left.string(), right.string()}),
      ElementsAre("different\tdifferent", "left-only\tleft", "right-only\tright", "identical\tsame"));
  EXPECT_THAT(
      RunArgvRecords({"--compare", "--compare-select=left-only,right-only", left.string(), right.string()}),
      ElementsAre("left-only\tleft", "right-only\tright"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareHandlesFileKindsAndTraversalOptions) {
  const fs::path left = root_ / "kinds-left";
  const fs::path right = root_ / "kinds-right";
  ASSERT_TRUE(fs::create_directories(left / ".hidden-dir"));
  ASSERT_TRUE(fs::create_directories(right / ".hidden-dir"));
  ASSERT_TRUE(fs::create_directories(left / ".git"));
  ASSERT_TRUE(fs::create_directories(right / ".git"));
  ASSERT_TRUE(fs::create_directories(left / "type-change"));
  { std::ofstream(right / "type-change") << "file"; }
  { std::ofstream(left / "size-change") << "short"; }
  { std::ofstream(right / "size-change") << "considerably longer"; }
  { std::ofstream(left / ".hidden") << "left"; }
  { std::ofstream(right / ".hidden") << "right"; }
  { std::ofstream(left / ".hidden-dir/value") << "left"; }
  { std::ofstream(right / ".hidden-dir/value") << "right"; }
  { std::ofstream(left / ".git/value") << "left"; }
  { std::ofstream(right / ".git/value") << "right"; }
  { std::ofstream(left / ".hg") << "left"; }
  { std::ofstream(right / ".hg") << "right"; }
  fs::create_symlink("target-a", left / "link");
  fs::create_symlink("target-b", right / "link");

  EXPECT_THAT(
      RunArgvRecords({"--compare", "--no-hidden", left.string(), right.string()}),
      ElementsAre("different\tlink", "different\tsize-change", "different\ttype-change"));
  EXPECT_THAT(
      RunArgvRecords({"--compare=diff", "--compare-select=different", left.string(), right.string()}),
      Contains("Files a/link and b/link differ"));
  EXPECT_THAT(
      RunArgvRecords({"--compare", "--exclude=size-change", left.string(), right.string()}),
      Not(Contains("different\tsize-change")));
  EXPECT_THAT(
      RunArgvRecords({"--compare", "--skip-vcs=hg", left.string(), right.string()}), Not(Contains("different\t.hg")));
  EXPECT_THAT(
      RunArgvRecords({"--compare", "--no-ignore", left.string(), right.string()}), Contains("different\t.git/value"));
  EXPECT_THAT(RunArgvRecords({"-u", "--compare", left.string(), right.string()}), Contains("different\t.git/value"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareRootSymlinksFollowTheRequestedTraversalMode) {
  const fs::path left_target = root_ / "symlink-left-target";
  const fs::path right_target = root_ / "symlink-right-target";
  ASSERT_TRUE(fs::create_directories(left_target));
  ASSERT_TRUE(fs::create_directories(right_target));
  { std::ofstream(left_target / "value") << "same"; }
  { std::ofstream(right_target / "value") << "same"; }
  const fs::path left = root_ / "symlink-left";
  const fs::path right = root_ / "symlink-right";
  fs::create_symlink(left_target.filename(), left);
  fs::create_symlink(right_target.filename(), right);

  EXPECT_THAT(RunArgvRecords({"--compare", left.string(), right.string()}), ElementsAre("different\t."));
  EXPECT_THAT(RunArgvRecords({"-H", "--compare", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(RunArgvRecords({"-L", "--compare", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareAppliesExplicitAndGlobalIgnoreFiles) {
  const fs::path left = root_ / "ignore-left";
  const fs::path right = root_ / "ignore-right";
  const fs::path config = root_ / "config";
  ASSERT_TRUE(fs::create_directories(left));
  ASSERT_TRUE(fs::create_directories(right));
  ASSERT_TRUE(fs::create_directories(config / "git"));
  { std::ofstream(left / "global") << "left"; }
  { std::ofstream(right / "global") << "right"; }
  { std::ofstream(left / "explicit") << "left"; }
  { std::ofstream(right / "explicit") << "right"; }
  { std::ofstream(config / "git/ignore") << "global\n"; }
  const fs::path explicit_ignore = root_ / "extra.ignore";
  { std::ofstream(explicit_ignore) << "ignore-left/explicit\nignore-right/explicit\n"; }
  env::SetForTesting("XDG_CONFIG_HOME", config.string());

  EXPECT_THAT(
      RunArgvRecords(
          {"--compare", "--gitignore=on", "--ignore-file=" + explicit_ignore.string(), left.string(), right.string()}),
      IsEmpty());
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareValidatesRootsSelectionsAndDiffOptions) {
  const fs::path left = root_ / "options-left";
  const fs::path right = root_ / "options-right";
  ASSERT_TRUE(fs::create_directories(left));
  ASSERT_TRUE(fs::create_directories(right));
  { std::ofstream(left / "value") << "old\n"; }
  { std::ofstream(right / "value") << "new\n"; }

  EXPECT_THAT(RunArgvRecords({"--compare", (root_ / "missing").string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 1);
  EXPECT_THAT(RunArgvRecords({"--compare", left.string(), (root_ / "missing").string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 1);
  EXPECT_THAT(
      RunArgvRecords({"--compare", (root_ / "a.txt").string(), right.string()}),
      ElementsAre("different\t.", "right-only\tvalue"));
  EXPECT_THAT(last_errors_, 0);
  EXPECT_THAT(RunArgvRecords({"--compare", "--compare-select=unknown", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(RunArgvRecords({"--compare", "--skip-vcs=unknown", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(
      RunArgvRecords({"--compare=diff", "--compare-select=identical", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(RunArgvRecords({"--compare=diff", "--diff-algorithm=unknown", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(RunArgvRecords({"--compare=diff", "--diff-context=invalid", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(
      RunArgvRecords({"--compare=diff", "--diff-algorithm=myers", "--diff-context=0", left.string(), right.string()}),
      Contains(HasSubstr("-old\n+new")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, FprintWritesMatchesToFileNotStdout) {
  // -fprint FILE redirects the matched paths into FILE; being an action, it also
  // suppresses the implicit -print, so stdout stays empty. The output file lives
  // outside the walked tree so it never appears in its own results.
  const std::string out = (fs::path(::testing::TempDir()) / "xff_fprint_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  EXPECT_THAT(RunExpr({"-name", "*.txt", "-fprint", out}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_TRUE(in.good());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  fs::remove(out, ec);
}

TEST_F(RunTest, FprintlnAndFprintflnWriteWithOsLineEndingToFile) {
  // xff: the file-writing forms of -println / -printfln, the counterparts of -fprint /
  // -fprintf. Each redirects into FILE (so, being an action, the implicit -print is
  // suppressed and stdout stays empty) and terminates the record with the OS line
  // ending (here "\n"). -fprintfln takes FILE then FORMAT, like -fprintf.
  const auto read_all = [](const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  const std::string ln = (fs::path(::testing::TempDir()) / "xff_fprintln.out").string();
  const std::string fln = (fs::path(::testing::TempDir()) / "xff_fprintfln.out").string();
  std::error_code ec;
  fs::remove(ln, ec);
  fs::remove(fln, ec);

  EXPECT_THAT(RunExpr({"-name", "a.txt", "-fprintln", ln}), IsEmpty());
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-fprintfln", fln, "name %f"}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);

  EXPECT_THAT(read_all(ln), Eq(Path("a.txt") + "\n"));
  EXPECT_THAT(read_all(fln), Eq(std::string("name a.txt\n")));

  fs::remove(ln, ec);
  fs::remove(fln, ec);
}

TEST_F(RunTest, PrintfPercentBraceEscapeExpandsXffFields) {
  // xff: `%{field}` in a -printf format reaches the brace field vocabulary (here
  // {relpath}); `%%` stays a literal percent, a bare `{..}` stays literal (printf formats
  // legitimately contain braces), and an unterminated `%{` is emitted literally. The whole
  // format renders as one record (it owns its terminator).
  EXPECT_THAT(
      RunExpr({"-name", "a.txt", "-printf", "rel=%{relpath} f=%f pct=%% bare={x} bad=%{oops\n"}),
      ElementsAre("rel=a.txt f=a.txt pct=% bare={x} bad=%{oops"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, DaystartFeedsTheTimeTests) {
  // Age a.txt to ~10 days ago, then select with -daystart -mtime +5 (older than
  // ~5 days, measured from today's local midnight). 10 days clears the boundary
  // with room to spare, so this exercises the daystart -> reference-instant ->
  // time-test wiring end to end without depending on the exact midnight cutoff.
  const auto ten_days_ago = fs::file_time_type::clock::now() - std::chrono::hours(24 * 10);
  std::error_code ec;
  fs::last_write_time(root_ / "a.txt", ten_days_ago, ec);
  ASSERT_FALSE(ec);
  EXPECT_THAT(RunExpr({"-daystart", "-mtime", "+5", "-name", "a.txt"}), ElementsAre(Path("a.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, TraversalSynonymsAccepted) {
  // -mount/-x (= -xdev) and -d (= -depth) are accepted; on a single-device tree
  // -xdev prunes nothing, and -d only reorders (post-order), so the set is the same.
  EXPECT_THAT(
      RunExpr({"-mount"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(
      RunExpr({"-x"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(RunExpr({"-d", "-name", "*.txt"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, IgnoreReaddirRaceAccepted) {
  // The option parses and walks normally (no races on this stable tree); the
  // ENOENT-suppression behaviour itself is covered at the walk level.
  EXPECT_THAT(
      RunExpr({"-ignore_readdir_race"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(
      RunExpr({"-noignore_readdir_race", "-name", "*.txt"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ExecPlusBatchesAllMatchesIntoOneRun) {
  // Engine-level: RunFind accumulates the matches and flushes ONE batched command
  // at end-of-walk (the exec_batches map + post-walk flush), in-process. The shell
  // appends a RUN marker per invocation plus each path, so exactly one RUN line
  // proves a single batched run (per-entry would yield two). The full binary/CLI
  // path is covered at the system level in //xff/cli:exec_test (mboworks/bashtest).
  const std::string out = (fs::path(::testing::TempDir()) / "xff_execplus_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  const std::string script = "echo RUN >> '" + out + R"('; for p in "$@"; do echo "$p" >> ')" + out + "'; done";
  RunExpr({"-name", "*.txt", "-exec", "sh", "-c", script, "_", "{}", "+"});
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_TRUE(in.good());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre("RUN", Path("a.txt"), Path("sub/c.txt")));
  fs::remove(out, ec);
}

TEST_F(RunTest, ExecdirPlusBatchesPerDirectory) {
  // *.txt are in two directories (root/a.txt, root/sub/c.txt), so -execdir ... +
  // runs ONCE PER DIRECTORY, passing the ./basename. Two dirs -> two RUN markers;
  // the items are basenames, not full paths (the cwd is each entry's directory).
  const std::string out = (fs::path(::testing::TempDir()) / "xff_execdirplus_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  const std::string script = "echo RUN >> '" + out + R"('; for p in "$@"; do echo "$p" >> ')" + out + "'; done";
  RunExpr({"-name", "*.txt", "-execdir", "sh", "-c", script, "_", "{}", "+"});
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_TRUE(in.good());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre("RUN", "RUN", "./a.txt", "./c.txt"));
  fs::remove(out, ec);
}

TEST_F(RunTest, ExecSemicolonUnderParallelJobsRunsEveryMatch) {
  // -j>1 routes the serial `-exec ... ;` action through the bounded ParallelExec
  // runner. Each of the two *.txt matches must still run exactly once; the children
  // append their path (one short, O_APPEND-atomic line apiece) so order is
  // unspecified but the set is complete -- proving no match is dropped on launch.
  const std::string out = (fs::path(::testing::TempDir()) / "xff_execpar_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  const std::string script = "echo \"$1\" >> '" + out + "'";
  RunArgvRecords({"-j2", root_.string(), "-name", "*.txt", "-exec", "sh", "-c", script, "_", "{}", ";"});
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_TRUE(in.good());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  fs::remove(out, ec);
}

TEST_F(RunTest, ExecSemicolonUnderParallelJobsLeavesExitStatusUnaffected) {
  // find's `-exec ... ;` is a predicate: a nonzero exit makes the action false but
  // does NOT raise find's exit status (unlike the `+` batch form). The parallel
  // runner preserves that -- both *.txt matches run `sh -c 'exit 1'`, yet the run
  // reports no error, identical to the synchronous -j1 path.
  RunArgvRecords({"-j2", root_.string(), "-name", "*.txt", "-exec", "sh", "-c", "exit 1", ";"});
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, JobsAllParsesAndWalksEverything) {
  // --jobs=all resolves to every detected core; the parallel walk still visits the
  // whole tree. The set is complete (order unspecified). On a 1-core host it folds
  // to -j1, which returns the same set, so the assertion holds regardless.
  EXPECT_THAT(
      RunArgvRecords({"--jobs=all", root_.string()}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, JobsAcceptsLongShortAndAllFormsAndIgnoresInvalidValues) {
  static const std::vector<std::vector<std::string>> kPrefixes = {
      {"--jobs=1"}, {"-j1"}, {"-jall"}, {"--jobs=0", "--jobs=1"}, {"--jobs=invalid", "-j1"},
  };
  for (const std::vector<std::string>& prefix : kPrefixes) {
    SCOPED_TRACE(PrintToString(prefix));
    std::vector<std::string> argv = prefix;
    argv.push_back(root_.string());
    argv.insert(argv.end(), {"-name", "*.txt"});
    EXPECT_THAT(RunArgvRecords(argv), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  }
}

TEST_F(RunTest, ModeScopedSortDefault) {
  // With no --sort, the active style picks the default: modern (kXff) sorts each
  // directory's listing, so the walk is deterministic (root, then a.txt < b.md <
  // sub as a block, then sub's contents). find leaves it unordered (same set).
  EXPECT_THAT(
      RunStyled(registry::Style::kXff),
      ElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(
      RunStyled(registry::Style::kFind),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
}

TEST_F(RunTest, SortNameVisitsSiblingsInDeterministicOrder) {
  // --sort=name orders each directory's entries by name, so the whole walk is
  // deterministic: root first, then a.txt < b.md < sub, then sub/c.txt. ElementsAre
  // (not UnorderedElementsAre) asserts the exact sequence.
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--sort", root_.string()}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, ElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
}

TEST_F(RunTest, NameGlobImplicitPrint) {
  EXPECT_THAT(RunExpr({"-name", "*.txt"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
}

TEST_F(RunTest, TypeDirectoryImplicitPrint) {
  EXPECT_THAT(RunExpr({"-type", "d"}), UnorderedElementsAre(root_.string(), Path("sub")));
}

TEST_F(RunTest, ExplicitPrintIsNotDoubled) {
  EXPECT_THAT(RunExpr({"-name", "*.txt", "-print"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
}

TEST_F(RunTest, Print0EmitsNulTerminatedRecords) {
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-print0"}), UnorderedElementsAre(Path("a.txt")));
}

TEST_F(RunTest, MaxDepthLimitsDescent) {
  // -maxdepth 1: root + its direct children, but not sub/c.txt (depth 2).
  EXPECT_THAT(
      RunExpr({"-maxdepth", "1"}), UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub")));
}

TEST_F(RunTest, LastDepthOptionWinsAcrossTheExpressionTree) {
  EXPECT_THAT(
      RunExpr({"-maxdepth", "0", "-maxdepth", "1"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub")));
  EXPECT_THAT(
      RunExpr({"-mindepth", "2", "-mindepth", "1"}),
      UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
}

TEST_F(RunTest, MinDepthSkipsRoot) {
  // -mindepth 1: everything except the root operand itself.
  EXPECT_THAT(
      RunExpr({"-mindepth", "1"}), UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
}

TEST_F(RunTest, EmptyMatchesEmptyFileAndDir) {
  std::error_code ec;
  { std::ofstream(root_ / "empty.txt"); }  // 0 bytes
  fs::create_directory(root_ / "emptydir", ec);
  // -empty: the zero-byte file and the childless directory only (a.txt/b.md/
  // sub/c.txt are non-empty; root and sub have children).
  EXPECT_THAT(RunExpr({"-empty"}), UnorderedElementsAre(Path("empty.txt"), Path("emptydir")));
}

TEST_F(RunTest, LinksOneMatchesRegularFiles) {
  // Regular files have one hard link; directories have >= 2.
  EXPECT_THAT(RunExpr({"-links", "1"}), UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
}

TEST_F(RunTest, MimeMatchesByExtensionDerivedType) {
  // Fixture: a.txt + sub/c.txt (text/plain) and b.md (text/markdown) are all text/*;
  // the directories (no extension -> octet-stream) are excluded.
  EXPECT_THAT(RunExpr({"-mime", "text/*"}), UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
}

TEST_F(RunTest, MimeVocabularyOverridesMatchingAndExposesMetadataFields) {
  const fs::path vocabulary = root_ / "mime.json";
  const fs::path binary = root_ / "b.md";
  const fs::path unspecified = root_ / "unspecified.mime-unknown";
  { const std::ofstream empty(unspecified); }
  std::ofstream(vocabulary) << R"({
    "application/x-note": {
      "description": "Note document",
      "source": "project",
      "charset": "UTF-8",
      "compressible": true,
      "extensions": ["txt"]
    },
    "application/x-binary": {"compressible": false, "extensions": ["md"]},
    "application/x-unspecified": {"extensions": ["mime-unknown"]}
  })";
  EXPECT_THAT(
      RunArgvRecords(
          {"--mime-vocabulary=" + vocabulary.string(), Path("a.txt"), binary.string(), unspecified.string(), "-printf",
           "%{mime}|%{mime-category}|%{mime-description}|%{mime-charset}|%{mime-compressible}|%{mime-source}\n"}),
      ElementsAre(
          "application/x-note|application|Note document|UTF-8|yes|project", "application/x-binary|application|||no|",
          "application/x-unspecified|application||||"));
  EXPECT_THAT(last_errors_, Eq(0));
}

TEST_F(RunTest, MissingMimeVocabularyFailsBeforeTraversal) {
  EXPECT_THAT(RunArgvRecords({"--mime-vocabulary=" + Path("absent.json"), root_.string(), "-print"}), IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
}

TEST_F(RunTest, MimeConflictPolicyControlsAmbiguousImportedVocabulary) {
  const fs::path vocabulary = root_ / "mime-conflict.json";
  std::ofstream(vocabulary) << R"({
    "application/x-first": {"extensions": ["txt"]},
    "application/x-last": {"extensions": ["txt"]}
  })";
  const std::string flag = "--mime-vocabulary=" + vocabulary.string();
  EXPECT_THAT(RunArgvRecords({flag, root_.string(), "-name", "a.txt", "-printf", "%{mime}\n"}), IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
  EXPECT_THAT(
      RunArgvRecords({flag, "--mime-conflicts=first", root_.string(), "-name", "a.txt", "-printf", "%{mime}\n"}),
      ElementsAre("application/x-first"));
  EXPECT_THAT(
      RunArgvRecords({flag, "--mime-conflicts=last", root_.string(), "-name", "a.txt", "-printf", "%{mime}\n"}),
      ElementsAre("application/x-last"));
}

TEST_F(RunTest, LangMatchesAndRendersTheLanguage) {
  { std::ofstream(root_ / "main.cc"); }
  { std::ofstream(root_ / "app.py"); }
  { std::ofstream(root_ / "Makefile"); }
  // -lang globs the language name case-insensitively (C++ from .cc; Python from .py).
  EXPECT_THAT(RunExpr({"-lang", "c++"}), ElementsAre(Path("main.cc")));
  EXPECT_THAT(RunExpr({"-lang", "python"}), ElementsAre(Path("app.py")));
  EXPECT_THAT(RunExpr({"-lang", "Makefile"}), ElementsAre(Path("Makefile")));
  // The {lang} field renders the canonical name, usable in -printf / --format.
  EXPECT_THAT(RunExpr({"-name", "main.cc", "-printf", "%{lang}\n"}), ElementsAre("C++"));
}

TEST_F(RunTest, LanguageDbOverridesMatchingAliasesAndMetadataFields) {
  { std::ofstream(root_ / "types.note"); }
  const fs::path vocabulary = root_ / "languages.json";
  std::ofstream(vocabulary) << R"({
    "NoteScript": {
      "type": "programming",
      "color": "#123456",
      "group": "Script",
      "source": "project",
      "aliases": ["ns"],
      "extensions": ["note"]
    }
  })";
  EXPECT_THAT(
      RunArgvRecords(
          {"--lang-db=" + vocabulary.string(), root_.string(), "-lang", "ns", "-printf",
           "%{lang}|%{lang-type}|%{lang-color}|%{lang-group}|%{lang-source}\n"}),
      ElementsAre("NoteScript|programming|#123456|Script|project"));
  EXPECT_THAT(last_errors_, Eq(0));
}

TEST_F(RunTest, MissingLanguageDbFailsBeforeTraversal) {
  EXPECT_THAT(RunArgvRecords({"--lang-db=" + Path("absent.json"), root_.string(), "-print"}), IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
}

TEST_F(RunTest, LanguageConflictPolicyControlsAmbiguousImportedVocabulary) {
  { std::ofstream(root_ / "types.note"); }
  const fs::path vocabulary = root_ / "language-conflict.json";
  std::ofstream(vocabulary) << R"({
    "First": {"extensions": ["note"]},
    "Last": {"extensions": ["note"]}
  })";
  const std::string flag = "--lang-db=" + vocabulary.string();
  EXPECT_THAT(RunArgvRecords({flag, root_.string(), "-name", "types.note", "-printf", "%{lang}\n"}), IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
  EXPECT_THAT(
      RunArgvRecords({flag, "--lang-conflicts=first", root_.string(), "-name", "types.note", "-printf", "%{lang}\n"}),
      ElementsAre("First"));
  EXPECT_THAT(
      RunArgvRecords({flag, "--lang-conflicts=last", root_.string(), "-name", "types.note", "-printf", "%{lang}\n"}),
      ElementsAre("Last"));
}

TEST_F(RunTest, MissingRootCountsError) {
  const std::vector<std::string> argv = {(root_ / "absent").string(), "-print"};
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse(argv));
  std::vector<std::string> records;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(errors, 1);
}

TEST_F(RunTest, PruneSkipsDirectoryDescent) {
  // `-name sub -prune -o -print`: prints everything except `sub` and its contents.
  EXPECT_THAT(
      RunExpr({"-name", "sub", "-prune", "-o", "-print"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md")));
}

TEST_F(RunTest, QuitStopsTraversal) {
  // `-quit` is an action (so no implicit -print) that stops after the first entry.
  EXPECT_THAT(RunExpr({"-quit"}), IsEmpty());
}

TEST_F(RunTest, DepthVisitsPostOrder) {
  const std::vector<std::string> out = RunExpr({"-depth"});
  // -depth lists the same set but post-order, so the root operand prints last.
  EXPECT_THAT(out, UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  ASSERT_THAT(out, Not(IsEmpty()));
  EXPECT_THAT(out.back(), root_.string());
}

TEST_F(RunTest, SymlinkLModeFollowsDirectorySymlink) {
  std::error_code ec;
  fs::create_directory_symlink(root_ / "sub", root_ / "lnk", ec);
  ASSERT_FALSE(ec);
  // `find -L <root> -name c.txt`: -L follows the directory symlink lnk -> sub, so
  // c.txt is reachable both directly (sub/c.txt) and through the link (lnk/c.txt).
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"-L", root_.string(), "-name", "c.txt"}));
  std::vector<std::string> out;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
          text.pop_back();
        }
        out.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(out, UnorderedElementsAre(Path("sub/c.txt"), Path("lnk/c.txt")));
}

TEST_F(RunTest, SymlinkHModeFollowsOnlyACommandLineRoot) {
  std::error_code ec;
  fs::create_directory_symlink(root_ / "sub", root_ / "root-link", ec);
  ASSERT_THAT(ec, Eq(std::error_code{}));
  const std::string link = Path("root-link");

  EXPECT_THAT(RunArgvRecords({"-H", link, "-name", "c.txt"}), ElementsAre(link + "/c.txt"));
  EXPECT_THAT(RunArgvRecords({"-P", link, "-name", "c.txt"}), IsEmpty());
}

TEST_F(RunTest, FormatJsonlRendersImplicitPrintAsJson) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--format=jsonl", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(Eq(std::string("{\"path\":\"") + Path("a.txt") + "\"}\n")));
}

TEST_F(RunTest, FormatNulViaDashZero) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"-0", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(Eq(Path("a.txt") + std::string("\0", 1))));
}

TEST_F(RunTest, ColorAlwaysWrapsDirectoriesButLeavesPlainFilesUncolored) {
  // --color=always forces ANSI even though the test's captured stdout is a pipe,
  // not a tty (so the default auto would stay plain). A directory gets bold blue
  // (1;34); a plain non-executable regular file is emitted with no escapes.
  EXPECT_THAT(
      RunArgvRecords({"--color=always", root_.string(), "-name", "sub"}),
      ElementsAre(absl::StrCat("\x1b[1;34m", Path("sub"), "\x1b[0m")));
  EXPECT_THAT(RunArgvRecords({"--color=always", root_.string(), "-name", "a.txt"}), ElementsAre(Path("a.txt")));
}

TEST_F(RunTest, LanguageDatabaseColorsRegularFilesInListingsAndLs) {
  const fs::path database = root_ / "languages.json";
  { std::ofstream(database) << R"({"Paint":{"color":"#3178c6","extensions":["colored"]}})"; }
  { std::ofstream(root_ / "paint.colored") << "paint"; }
  const std::string flag = absl::StrCat("--lang-db=", database.string());
  const std::string colored_path = absl::StrCat("\x1b[38;2;49;120;198m", Path("paint.colored"), "\x1b[0m");

  EXPECT_THAT(
      RunArgvRecords({"--color=always", "--color-scheme=xff", flag, root_.string(), "-name", "paint.colored"}),
      ElementsAre(colored_path));
  EXPECT_THAT(
      RunArgvRecords({"--color=always", "--color-scheme=xff", flag, root_.string(), "-name", "paint.colored", "-ls"}),
      ElementsAre(HasSubstr(colored_path)));
}

TEST_F(RunTest, TemplateRelpathIsRelativeToTheSearchRoot) {
  // {relpath} renders each entry's path relative to the search root (find %P), so the
  // walk's per-entry root wiring is exercised end-to-end.
  EXPECT_THAT(
      RunArgvRecords({"--template={relpath}", root_.string(), "-type", "f"}),
      UnorderedElementsAre("a.txt", "b.md", "sub/c.txt"));
}

TEST_F(RunTest, TemplateTargetRendersTheSymlinkTarget) {
  // {target} = the symlink's target (find %l), resolved via ReadLink at the render
  // context; empty for a non-symlink. Exercises the engine's link-target wiring e2e.
  std::error_code ec;
  fs::create_symlink("a.txt", root_ / "link.lnk", ec);
  ASSERT_FALSE(ec);
  EXPECT_THAT(RunArgvRecords({"--template={target}", root_.string(), "-name", "link.lnk"}), ElementsAre("a.txt"));
  EXPECT_THAT(RunArgvRecords({"--template=[{target}]", root_.string(), "-name", "a.txt"}), ElementsAre("[]"));
}

TEST_F(RunTest, CmpMatchesByteIdenticalContent) {
  { std::ofstream(root_ / "twin.txt") << "a"; }  // byte-identical to a.txt (content "a")
  { std::ofstream(root_ / "diff.txt") << "X"; }  // differs
  // -cmp TARGET is TRUE (same) when byte-identical; TARGET is a field template (a bare
  // path is a literal). a.txt == twin.txt, a.txt != diff.txt.
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-cmp", Path("twin.txt")}), ElementsAre(Path("a.txt")));
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-cmp", Path("diff.txt")}), IsEmpty());
  // `! -cmp` selects files that differ (the "list changed files" idiom).
  EXPECT_THAT(RunExpr({"-name", "a.txt", "!", "-cmp", Path("diff.txt")}), ElementsAre(Path("a.txt")));
  // A missing / unreadable target counts as differing (not-same -> false).
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-cmp", Path("nope.txt")}), IsEmpty());
}

TEST_F(RunTest, CmpTargetIsAPerEntryTemplate) {
  // The target is rendered per entry, so {def.NAME} / {name} build it dynamically:
  // compare each file against a same-named file under a parallel directory.
  const std::string other = (fs::path(::testing::TempDir()) / "xff_cmp_other").string();
  std::error_code ec;
  fs::remove_all(other, ec);
  ASSERT_TRUE(fs::create_directories(other));
  { std::ofstream(fs::path(other) / "a.txt") << "a"; }         // identical to <root>/a.txt
  { std::ofstream(fs::path(other) / "b.md") << "DIFFERENT"; }  // differs from <root>/b.md ("b")
  // ! -cmp '{def.OTHER}/{name}' -> files whose twin under OTHER differs (b.md; a.txt matches).
  const std::vector<std::string> changed =
      RunArgvRecords({"--define=OTHER=" + other, root_.string(), "-type", "f", "!", "-cmp", "{def.OTHER}/{name}"});
  fs::remove_all(other, ec);
  // sub/c.txt has no counterpart under OTHER (missing -> differs); b.md differs; a.txt matches.
  EXPECT_THAT(changed, UnorderedElementsAre(Path("b.md"), Path("sub/c.txt")));
}

TEST_F(RunTest, SimilarMatchesNearDuplicateTextAgainstAReference) {
  constexpr std::string_view reference =
      "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen "
      "eighteen nineteen twenty";
  { std::ofstream(root_ / "reference.txt") << reference; }
  { std::ofstream(root_ / "candidate-close.txt") << reference.substr(0, reference.rfind(' ')) << " twentyone"; }
  {
    std::ofstream(root_ / "candidate-far.txt")
        << "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen "
           "seventeen red blue green";
  }

  // One changed final word retains 15 of 17 union shingles (88%), so the default 80% admits it.
  // Four changed trailing words retain only 13 of 19 (68%), so the same matcher rejects it.
  EXPECT_THAT(
      RunExpr({"-name", "candidate-*.txt", "-similar", Path("reference.txt")}),
      ElementsAre(Path("candidate-close.txt")));
  EXPECT_THAT(
      RunExpr({"-name", "candidate-*.txt", "-similar:65%", Path("reference.txt")}),
      UnorderedElementsAre(Path("candidate-close.txt"), Path("candidate-far.txt")));
}

TEST_F(RunTest, SimilarWidthAndTokenNormalizationHaveObservableSemantics) {
  { std::ofstream(root_ / "reference.txt") << "Alpha, beta gamma delta"; }
  { std::ofstream(root_ / "same-words.txt") << "alpha BETA! gamma delta"; }
  { std::ofstream(root_ / "different-order.txt") << "alpha gamma beta delta"; }

  EXPECT_THAT(
      RunExpr({"-name", "same-words.txt", "-similar:4:100%", Path("reference.txt")}),
      ElementsAre(Path("same-words.txt")));
  EXPECT_THAT(RunExpr({"-name", "different-order.txt", "-similar:4:100%", Path("reference.txt")}), IsEmpty());
}

TEST_F(RunTest, SimilarSkipsBinaryAndMissingReferenceContent) {
  { std::ofstream(root_ / "reference.txt") << "one two three four five"; }
  {
    std::ofstream binary(root_ / "binary.txt", std::ios::binary);
    std::string content = "one two";
    content.push_back('\0');
    content.append("three four five");
    binary.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
  EXPECT_THAT(RunExpr({"-name", "binary.txt", "-similar:0%", Path("reference.txt")}), IsEmpty());
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-similar:0%", Path("missing.txt")}), IsEmpty());
}

TEST_F(RunTest, DiffPolarityIsTrueWhenEqual) {
  { std::ofstream(root_ / "twin.txt") << "a"; }   // identical to a.txt (content "a")
  { std::ofstream(root_ / "other.txt") << "X"; }  // differs
  // -diff:none is the silent matcher (TRUE = same, like -cmp); -diff is an action, so an
  // explicit -print reveals the truth. a.txt == twin, a.txt != other.
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-diff:none", Path("twin.txt"), "-print"}), ElementsAre(Path("a.txt")));
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-diff:none", Path("other.txt"), "-print"}), IsEmpty());
  // ! -diff selects files that differ from their target (the "changed files" idiom).
  EXPECT_THAT(RunExpr({"-name", "a.txt", "!", "-diff:none", Path("other.txt"), "-print"}), ElementsAre(Path("a.txt")));
  // A missing / unreadable target counts as differing (false).
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-diff:none", Path("nope.txt"), "-print"}), IsEmpty());

  // Binary inputs use byte equality rather than text diffing.
  {
    constexpr std::array kBinaryTwin = {'a', '\0', 's', 'a', 'm', 'e'};
    std::ofstream binary(root_ / "binary-twin.txt", std::ios::binary);
    binary.write(kBinaryTwin.data(), static_cast<std::streamsize>(kBinaryTwin.size()));
  }
  {
    constexpr std::array kBinaryOther = {'a', '\0', 'd', 'i', 'f', 'f', 'e', 'r', 'e', 'n', 't'};
    std::ofstream binary(root_ / "binary-other.txt", std::ios::binary);
    binary.write(kBinaryOther.data(), static_cast<std::streamsize>(kBinaryOther.size()));
  }
  EXPECT_THAT(
      RunExpr({"-name", "binary-twin.txt", "-diff:none", Path("binary-twin.txt"), "-print"}),
      ElementsAre(Path("binary-twin.txt")));
  EXPECT_THAT(RunExpr({"-name", "binary-twin.txt", "-diff:none", Path("binary-other.txt"), "-print"}), IsEmpty());
}

TEST_F(RunTest, DiffIgnoreNormalizesComparison) {
  // Two files that differ only by trailing whitespace; the normalization globals make -diff
  // treat them as equal (TRUE, so the trailing -print fires). -diff:none is the silent matcher.
  { std::ofstream(root_ / "left.txt") << "one\ntwo   \nthree\n"; }
  { std::ofstream(root_ / "right.txt") << "one\ntwo\nthree\n"; }
  const std::string right = Path("right.txt");
  // Without normalization the trailing whitespace differs -> FALSE, no print.
  EXPECT_THAT(RunExpr({"-name", "left.txt", "-diff:none", right, "-print"}), IsEmpty());
  // --diff-ignore=trail and =ws both fold the whitespace so the sides compare equal.
  EXPECT_THAT(
      RunArgvRecords({"--diff-ignore=trail", root_.string(), "-name", "left.txt", "-diff:none", right, "-print"}),
      ElementsAre(Path("left.txt")));
  EXPECT_THAT(
      RunArgvRecords({"--diff-ignore=ws", root_.string(), "-name", "left.txt", "-diff:none", right, "-print"}),
      ElementsAre(Path("left.txt")));
  // --diff-ignore-matching drops lines matching the regex before comparing (the DEBUG line here).
  { std::ofstream(root_ / "mleft.txt") << "keep\nDEBUG x\nkeep2\n"; }
  { std::ofstream(root_ / "mright.txt") << "keep\nDEBUG y\nkeep2\n"; }
  EXPECT_THAT(
      RunArgvRecords(
          {"--diff-ignore-matching=^DEBUG", root_.string(), "-name", "mleft.txt", "-diff:none", Path("mright.txt"),
           "-print"}),
      ElementsAre(Path("mleft.txt")));
  // --diff-ignore=eofnl equates a file with and one without a final newline (via mbo #234).
  { std::ofstream(root_ / "nonl.txt") << "a\nb"; }      // no final newline
  { std::ofstream(root_ / "withnl.txt") << "a\nb\n"; }  // same content, with a final newline
  EXPECT_THAT(RunExpr({"-name", "nonl.txt", "-diff:none", Path("withnl.txt"), "-print"}), IsEmpty());
  EXPECT_THAT(
      RunArgvRecords(
          {"--diff-ignore=eofnl", root_.string(), "-name", "nonl.txt", "-diff:none", Path("withnl.txt"), "-print"}),
      ElementsAre(Path("nonl.txt")));
}

TEST_F(RunTest, DiffIgnoreRejectsUnknownToken) {
  // An unknown --diff-ignore token is a pre-walk usage error (exit 2), not a silent no-op.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"--diff-ignore=bogus", root_.string(), "-name", "a.txt", "-diff", Path("b.md")}));
  std::vector<std::string> records;
  absl::Status reported;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [&](std::string_view, absl::Status status) { reported = status; });
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(reported, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("unknown --diff-ignore token 'bogus'")));
}

TEST_F(RunTest, InvalidValuedGlobalsAreRejectedBeforeTraversal) {
  struct Case {
    std::string flag;
    std::string message;
  };

  const std::array cases{
      Case{.flag = "--diff-algorithm=bogus", .message = "unknown diff algorithm 'bogus'"},
      Case{.flag = "--hash-algorithm=bogus", .message = "unknown hash algorithm 'bogus'"},
      Case{.flag = "--hash-encoding=bogus", .message = "unknown hash encoding 'bogus'"},
      Case{.flag = "--archive-aggregate=bogus", .message = "bad --archive-aggregate value 'bogus'"},
  };
  for (const Case& test : cases) {
    SCOPED_TRACE(test.flag);
    MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({test.flag, root_.string()}));
    absl::Status reported;
    const RunResult result = RunFind(
        command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status status) { reported = status; });
    EXPECT_THAT(result.errors, 2);
    EXPECT_THAT(result.any_match, IsFalse());
    EXPECT_THAT(reported, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(test.message)));
  }
}

TEST_F(RunTest, PackWithoutAnArchiveBackendIsRejectedBeforeTraversal) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--pack=" + Path("output.tar"), root_.string()}));
  absl::Status reported;
  const RunResult result =
      RunFind(command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status status) { reported = status; });
  EXPECT_THAT(result.errors, 2);
  EXPECT_THAT(result.any_match, IsFalse());
  EXPECT_THAT(reported, StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("without archive support")));
}

TEST_F(RunTest, DiffFormatAndContextGlobalsSetTheDefaults) {
  // A 7-line file with a single changed line (line 4); -diff emits the whole diff as one record.
  { std::ofstream(root_ / "one.txt") << "a\nb\nc\nd\ne\nf\ng\n"; }
  { std::ofstream(root_ / "two.txt") << "a\nb\nc\nX\ne\nf\ng\n"; }
  const std::string two = Path("two.txt");
  // Built-in default: unified with 3 lines of context (the hunk spans all 7 lines here).
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-name", "one.txt", "-diff", two}), ElementsAre(HasSubstr("@@ -1,7 +1,7 @@")));
  // --diff-context=1 narrows the unified hunk to one line of context each side.
  EXPECT_THAT(
      RunArgvRecords({"--diff-context=1", root_.string(), "-name", "one.txt", "-diff", two}),
      ElementsAre(HasSubstr("@@ -3,3 +3,3 @@")));
  // --diff-format=normal switches to the `NcN` normal format (no unified `@@` hunk header).
  EXPECT_THAT(
      RunArgvRecords({"--diff-format=normal", root_.string(), "-name", "one.txt", "-diff", two}),
      ElementsAre(AllOf(HasSubstr("4c4"), HasSubstr("< d"), HasSubstr("> X"), Not(HasSubstr("@@")))));
}

TEST_F(RunTest, ContextGlobalFeedsDiffContextWhenSymmetric) {
  { std::ofstream(root_ / "one.txt") << "a\nb\nc\nd\ne\nf\ng\n"; }
  { std::ofstream(root_ / "two.txt") << "a\nb\nc\nX\ne\nf\ng\n"; }
  const std::string two = Path("two.txt");
  // A symmetric --context=1 (grep before==after) also seeds the -diff default context.
  EXPECT_THAT(
      RunArgvRecords({"--context=1", root_.string(), "-name", "one.txt", "-diff", two}),
      ElementsAre(HasSubstr("@@ -3,3 +3,3 @@")));
  // An asymmetric --context (after != before) is grep-only; -diff falls back to its built-in 3.
  EXPECT_THAT(
      RunArgvRecords({"--context=A:1,B:0", root_.string(), "-name", "one.txt", "-diff", two}),
      ElementsAre(HasSubstr("@@ -1,7 +1,7 @@")));
  // --diff-context overrides --context for -diff regardless of order.
  EXPECT_THAT(
      RunArgvRecords({"--context=1", "--diff-context=5", root_.string(), "-name", "one.txt", "-diff", two}),
      ElementsAre(HasSubstr("@@ -1,7 +1,7 @@")));
}

TEST_F(RunTest, PerActionDiffStyleOverridesTheGlobals) {
  { std::ofstream(root_ / "one.txt") << "a\nb\nc\nd\ne\nf\ng\n"; }
  { std::ofstream(root_ / "two.txt") << "a\nb\nc\nX\ne\nf\ng\n"; }
  const std::string two = Path("two.txt");
  // -diff:c (context format) wins over --diff-format=normal; the `*** ` marker is context-diff.
  EXPECT_THAT(
      RunArgvRecords({"--diff-format=normal", root_.string(), "-name", "one.txt", "-diff:c", two}),
      ElementsAre(AllOf(HasSubstr("***"), Not(HasSubstr("4c4")))));
  // -diff:u5 (explicit context 5) wins over --diff-context=1: the hunk widens back to all 7 lines.
  EXPECT_THAT(
      RunArgvRecords({"--diff-context=1", root_.string(), "-name", "one.txt", "-diff:u5", two}),
      ElementsAre(HasSubstr("@@ -1,7 +1,7 @@")));
}

TEST_F(RunTest, DiffFormatAndContextRejectBadValues) {
  const auto run_expect_usage_error = [&](const std::vector<std::string>& argv, std::string_view message) {
    MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse(argv));
    std::vector<std::string> records;
    absl::Status reported;
    const auto [errors, any_match] = RunFind(
        command, fs_, [&](std::string_view record) { records.emplace_back(record); },
        [&](std::string_view, absl::Status status) { reported = status; });
    EXPECT_THAT(records, IsEmpty());
    EXPECT_THAT(errors, 2);
    EXPECT_THAT(reported, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(message)));
  };
  run_expect_usage_error(
      {"--diff-format=bogus", root_.string(), "-name", "a.txt", "-diff", Path("b.md")}, "unknown diff format 'bogus'");
  run_expect_usage_error(
      {"--diff-context=x", root_.string(), "-name", "a.txt", "-diff", Path("b.md")}, "bad --diff-context value 'x'");
}

TEST_F(RunTest, IgnoreFileExcludesMatchingEntriesRootedAtItsOwnDir) {
  { std::ofstream(root_ / "keep.log") << "x"; }
  { std::ofstream(root_ / "my.ignore") << "*.log\n"; }
  // --ignore-file reads the gitignore-format file and roots its patterns at the file's own
  // directory (here root_), so *.log is excluded anywhere beneath it while a.txt survives.
  const std::vector<std::string> out =
      RunArgvRecords({"--ignore-file=" + Path("my.ignore"), root_.string(), "-type", "f"});
  EXPECT_THAT(out, AllOf(Contains(Path("a.txt")), Not(Contains(Path("keep.log")))));
}

TEST_F(RunTest, IgnoreFileRootsAtItsOwnDirectoryNotTheSearchRoot) {
  { std::ofstream(root_ / "top.log") << "x"; }           // under root_, NOT under sub/
  { std::ofstream(root_ / "sub" / "deep.log") << "y"; }  // under sub/
  { std::ofstream(root_ / "sub" / "nested.ignore") << "*.log\n"; }
  // The ignore file lives in sub/, so its patterns root at sub/: sub/deep.log is excluded, but
  // the sibling top.log (outside sub/) is untouched -- the root is the file's dir, not the search
  // root, which is exactly why no separate --ignore-file-root flag is needed.
  const std::vector<std::string> out =
      RunArgvRecords({"--ignore-file=" + Path("sub/nested.ignore"), root_.string(), "-type", "f"});
  EXPECT_THAT(out, AllOf(Contains(Path("top.log")), Not(Contains(Path("sub/deep.log")))));
}

TEST_F(RunTest, HashActionPrintsDigestAndPath) {
  { std::ofstream(root_ / "abc.txt") << "abc"; }
  // -hash prints `<digest>  <path>` (the sha256sum layout); the default algorithm is sha256.
  EXPECT_THAT(
      RunExpr({"-name", "abc.txt", "-hash"}),
      ElementsAre("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  " + Path("abc.txt")));
  // -hash:ALGO[/ENCODING] selects the algorithm and hex/base64 rendering.
  EXPECT_THAT(
      RunExpr({"-name", "abc.txt", "-hash:md5"}), ElementsAre("900150983cd24fb0d6963f7d28e17f72  " + Path("abc.txt")));
  EXPECT_THAT(
      RunExpr({"-name", "abc.txt", "-hash:sha256/base64"}),
      ElementsAre("ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=  " + Path("abc.txt")));
}

TEST_F(RunTest, HashAlgorithmGlobalSetsTheDefaultForActionAndField) {
  { std::ofstream(root_ / "abc.txt") << "abc"; }
  // --hash-algorithm=md5 changes the default for a bare -hash action ...
  EXPECT_THAT(
      RunArgvRecords({"--hash-algorithm=md5", root_.string(), "-name", "abc.txt", "-hash"}),
      ElementsAre("900150983cd24fb0d6963f7d28e17f72  " + Path("abc.txt")));
  // ... and for a bare {hash} field (the %{hash} printf escape renders it).
  EXPECT_THAT(
      RunArgvRecords({"--hash-algorithm=md5", root_.string(), "-name", "abc.txt", "-printf", "%{hash}\n"}),
      ElementsAre("900150983cd24fb0d6963f7d28e17f72"));
}

TEST_F(RunTest, HashRejectsUnknownSpec) {
  // A bad -hash:ALGO[/ENCODING] spec is a pre-walk usage error (exit 2), not a silent no-op.
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({root_.string(), "-name", "a.txt", "-hash:crc32"}));
  std::vector<std::string> records;
  absl::Status reported;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [&](std::string_view, absl::Status status) { reported = status; });
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(reported, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("'-hash:crc32'")));
}

TEST_F(RunTest, ColorAutoStaysPlainWhenStdoutIsNotATty) {
  // The captured stdout here is a pipe, so auto (the default) leaves even a
  // directory uncolored; only --color=always would force escapes.
  EXPECT_THAT(RunArgvRecords({root_.string(), "-name", "sub"}), ElementsAre(Path("sub")));
}

TEST_F(RunTest, DeleteRemovesMatchedFiles) {
  RunExpr({"-name", "*.txt", "-delete"});  // -delete implies -depth, so children go first
  EXPECT_FALSE(fs::exists(root_ / "a.txt"));
  EXPECT_FALSE(fs::exists(root_ / "sub" / "c.txt"));
  EXPECT_TRUE(fs::exists(root_ / "b.md"));  // not matched
}

TEST_F(RunTest, DeleteDryRunPreviewsWithoutDeleting) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--dry-run", root_.string(), "-name", "a.txt", "-delete"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_TRUE(fs::exists(root_ / "a.txt"));                   // --dry-run: nothing deleted
  EXPECT_THAT(records, UnorderedElementsAre(Path("a.txt")));  // but previewed
}

TEST_F(RunTest, SafeRefusesDelete) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--safe", root_.string(), "-delete"}));
  const auto [errors, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, 2);
  EXPECT_TRUE(fs::exists(root_ / "a.txt"));  // refused: nothing deleted
}

TEST_F(RunTest, ExecRunsCommandPerMatch) {
  // -exec /bin/sh -c 'echo > "{}.ran"' ; creates a marker beside each matched file.
  RunExpr({"-name", "a.txt", "-exec", "/bin/sh", "-c", "echo > \"{}.ran\"", ";"});
  EXPECT_TRUE(fs::exists(root_ / "a.txt.ran"));
}

TEST_F(RunTest, SafeRefusesExec) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"--safe", root_.string(), "-exec", "/bin/sh", "-c", "echo > \"{}.ran\"", ";"}));
  const auto [errors, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, 2);
  EXPECT_FALSE(fs::exists(root_ / "a.txt.ran"));  // refused: command not run
}

TEST_F(RunTest, UnknownTimezoneIsRefusedBeforeTraversal) {
  // An unknown --timezone is a usage error refused before the walk (exit 2), like
  // the --safe guards above: reported via on_error, emitting nothing.
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--timezone=Not/AZone", root_.string()}));
  std::string err_path;
  absl::Status err_status;
  bool emitted = false;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view) { emitted = true; },
      [&](std::string_view path, absl::Status status) {
        err_path = std::string(path);
        err_status = status;
      });
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(err_path, "--timezone");
  EXPECT_THAT(err_status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(emitted) << "an invalid --timezone must not traverse";
}

TEST_F(RunTest, OversizedSizeUnitIsRefusedBeforeTraversal) {
  // -size with an over-64-bit unit (Z/Y/...) is a usage error refused before the
  // walk (exit 2), naming the limit -- not a silent per-entry no-match.
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({root_.string(), "-size", "+1Z"}));
  absl::Status err_status;
  bool emitted = false;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view) { emitted = true; },
      [&](std::string_view, absl::Status status) { err_status = status; });
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(err_status, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("largest units")));
  EXPECT_FALSE(emitted) << "a malformed -size must not traverse";
}

TEST_F(RunTest, BlockSizeRedefinesTheBareSizeUnit) {
  // A 1000-byte file is 2 blocks at the default 512 (so -size 1 misses, -size 2
  // hits) but 1 block under --block-size=4k (so -size 1 hits). Proves the global
  // redefines the bare/`b` -size unit end to end.
  { std::ofstream(root_ / "kilo.bin", std::ios::binary) << std::string(1'000, 'x'); }
  EXPECT_THAT(RunArgvRecords({root_.string(), "-name", "kilo.bin", "-size", "1"}), IsEmpty());
  EXPECT_THAT(RunArgvRecords({root_.string(), "-name", "kilo.bin", "-size", "2"}), ElementsAre(Path("kilo.bin")));
  EXPECT_THAT(
      RunArgvRecords({"--block-size=4k", root_.string(), "-name", "kilo.bin", "-size", "1"}),
      ElementsAre(Path("kilo.bin")));
}

TEST_F(RunTest, BlockSizeAcceptsExplicitDecimalAndBinaryUnits) {
  { std::ofstream(root_ / "kilo.bin", std::ios::binary) << std::string(1'001, 'x'); }
  EXPECT_THAT(
      RunArgvRecords({"--block-size=1kB", root_.string(), "-name", "kilo.bin", "-size", "2"}),
      ElementsAre(Path("kilo.bin")));
  EXPECT_THAT(
      RunArgvRecords({"--block-size=1KiB", root_.string(), "-name", "kilo.bin", "-size", "1"}),
      ElementsAre(Path("kilo.bin")));
}

TEST_F(RunTest, InvalidBlockSizeIsRefusedBeforeTraversal) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--block-size=0", root_.string()}));
  absl::Status err_status;
  bool emitted = false;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view) { emitted = true; },
      [&](std::string_view, absl::Status status) { err_status = status; });
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(err_status, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("positive")));
  EXPECT_FALSE(emitted) << "an invalid --block-size must not traverse";
}

TEST_F(RunTest, ValidTimezoneIsAcceptedAndTheRunProceeds) {
  // A valid --timezone resolves and the run proceeds normally (here it does not
  // change the result, just proving the flag is accepted end to end).
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--timezone=UTC", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  const auto [errors, any_match] = RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, 0);
  EXPECT_THAT(records, UnorderedElementsAre(Path("a.txt")));
}

TEST_F(RunTest, TimezoneAppliesToTimeFieldFormatting) {
  // --timezone reaches time-field formatting too: {mtime:%z} is the numeric zone
  // offset, so under --timezone=UTC it is "+0000" regardless of the host's zone.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--timezone=UTC", "--template={mtime:%z}", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("+0000"));
}

TEST_F(RunTest, TheZoneSuffixSwitchSpellingsWork) {
  // --time-zone-suffix documented on / off as synonyms of always / never while the shared parser
  // rejected them, so =off silently kept the default (the offset stayed). Both spellings of each
  // side must reach the same result.
  const auto run = [&](std::string_view flag) {
    const auto command = parser::Parse({std::string(flag), "--template={mtime}", root_.string(), "-name", "a.txt"});
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> records;
    RunFind(
        *command, fs_,
        [&](std::string_view record) {
          std::string text(record);
          if (!text.empty() && text.back() == '\n') {
            text.pop_back();
          }
          records.push_back(std::move(text));
        },
        [](std::string_view, absl::Status) {});
    return records.size() == 1 ? records.front() : std::string();
  };
  EXPECT_THAT(run("--time-zone-suffix=off"), Eq(run("--time-zone-suffix=never")));
  EXPECT_THAT(run("--time-zone-suffix=on"), Eq(run("--time-zone-suffix=always")));
  EXPECT_THAT(run("--time-zone-suffix=off"), Ne(run("--time-zone-suffix=on")));
}

TEST_F(RunTest, TzIsAnAliasForTimezone) {
  // --tz=ZONE is the short alias of --timezone=ZONE: under --tz=UTC the {mtime:%z}
  // numeric offset is "+0000" regardless of the host zone, just as --timezone=UTC.
  EXPECT_THAT(
      RunArgvRecords({"--tz=UTC", "--template={mtime:%z}", root_.string(), "-name", "a.txt"}),
      UnorderedElementsAre("+0000"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, FixedOffsetTimezoneAppliesToFormatting) {
  // A fixed UTC offset (+05:30) is accepted as a zone spec and reaches time-field
  // formatting: {mtime:%z} renders the zone's numeric offset, "+0530".
  EXPECT_THAT(
      RunArgvRecords({"--tz=+05:30", "--template={mtime:%z}", root_.string(), "-name", "a.txt"}),
      UnorderedElementsAre("+0530"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, AnyMatchIsTrueWhenExpressionMatches) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({root_.string(), "-name", "a.txt"}));
  const RunResult result = RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(result.any_match, IsTrue());
}

TEST_F(RunTest, AnyMatchIsFalseWhenNothingMatches) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({root_.string(), "-name", "no-such-file.zzz"}));
  const RunResult result = RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(result.any_match, IsFalse());
}

TEST_F(RunTest, AnyMatchReflectsExpressionNotEmittedOutput) {
  // any_match is the expression's truth, not output: with --implicit-print=no, a.txt
  // matches but nothing is emitted, yet any_match is still true (so --quiet on an
  // action-only expression like `-exec` still reports the match).
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--implicit-print=no", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  const RunResult result = RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(result.any_match, IsTrue());
}

TEST_F(RunTest, TimeFormatGlobalSetsTheBareTimeFieldDefault) {
  // --time-format=epoch makes a bare {mtime} render as Unix seconds (all digits,
  // no date dashes), proving the global threads through to time-field formatting.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"--time-format=epoch", "--template={mtime}", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  // epoch is all digits; a date format would carry dashes. ElementsAre folds the
  // single-match count and the content check into one matcher.
  EXPECT_THAT(records, ElementsAre(Not(HasSubstr("-"))));
}

TEST_F(RunTest, TemplateRendersImplicitPrint) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--template={name}:{type}", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("a.txt:f"));  // {name}:{type} for a regular file
}

TEST_F(RunTest, TemplateRootFieldReportsTheSearchOperand) {
  // {root} is the command-line operand a match descends from (find %H); a nested
  // match (sub/c.txt) still reports the operand, exercising run.cc's wiring of
  // Visit::root into the render context.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--template={root}|{name}", root_.string(), "-name", "c.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(root_.string() + "|c.txt"));
}

TEST_F(RunTest, ExecFieldsRendersNamedPlaceholders) {
  // --exec-fields routes -exec tokens through the field vocabulary: {path} is the
  // full path, so the marker lands beside the matched file (vs. a literal "{path}"
  // file in the cwd without the flag).
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse(
          {"--exec-fields", root_.string(), "-name", "a.txt", "-exec", "/bin/sh", "-c", "echo > \"{path}.fld\"", ";"}));
  RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_TRUE(fs::exists(root_ / "a.txt.fld"));
}

TEST_F(RunTest, ExecFieldsSubstitutesRegexCaptures) {
  // --exec-fields + a -regex match: {1}/{2} resolve to the capture groups, written
  // to a marker beside the file ({path} keeps the marker absolute for cleanup).
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--exec-fields", root_.string(), "-regex", ".*/(a)\\.(txt)", "-exec", "/bin/sh", "-c",
                               R"(printf '%s' "{1}.{2}" > "{path}.cap")", ";"}));
  RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  const fs::path marker = root_ / "a.txt.cap";
  ASSERT_TRUE(fs::exists(marker));
  std::ifstream in(marker);
  std::string content;
  std::getline(in, content);
  EXPECT_THAT(content, "a.txt");  // {1}="a", {2}="txt"
}

TEST_F(RunTest, DefinePopulatesDefNamespace) {
  // --define=NAME=VALUE surfaces as {def.NAME} in --template output (last wins).
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--define=label=old", "--define=label=new", "--template={def.label}:{name}",
                               root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("new:a.txt"));  // last --define wins
}

TEST_F(RunTest, CaptureBindsOutputForTemplate) {
  // -capture runs a command per match (with {} -> path) and binds its stdout to
  // {capture.NAME}; --template then prints it.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--template={capture.base}", root_.string(), "-name", "a.txt", "-capture:base",
                               "/bin/sh", "-c", "basename {}", ";"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("a.txt"));
}

TEST_F(RunTest, CaptureChainsPriorOutputs) {
  // A later -capture command references an earlier capture's {capture.*}.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--template={capture.b}", root_.string(), "-name", "a.txt", "-capture:a", "/bin/sh",
                               "-c", "printf X", ";", "-capture:b", "/bin/sh", "-c", "printf {capture.a}Y", ";"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("XY"));  // b = {capture.a}("X") + "Y"
}

TEST_F(RunTest, DuplicateCaptureNameIsErrorByDefault) {
  // Two -capture actions binding the same NAME, neither carrying `!` -> exit 2,
  // reported before traversal (silent clobbering would mean wrong data).
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {root_.string(), "-capture:x", "/bin/sh", "-c", "printf a", ";", "-capture:x", "/bin/sh",
                               "-c", "printf b", ";"}));
  int errors = 0;
  const auto [code, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 2);
  EXPECT_THAT(errors, 1);
}

TEST_F(RunTest, BangModifierAllowsDuplicateCaptureNameLastWins) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--template={capture.x}", root_.string(), "-name", "a.txt", "-capture:x", "/bin/sh",
                               "-c", "printf a", ";", "-capture:!x", "/bin/sh", "-c", "printf b", ";"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("b"));  // the `!` node re-binds, so the last -capture wins
}

TEST_F(RunTest, UnusedCaptureIsError) {
  // -capture:x but {capture.x} is referenced nowhere -> exit 2 before traversal.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({root_.string(), "-name", "a.txt", "-capture:x", "/bin/sh", "-c", "printf a", ";"}));
  int errors = 0;
  const auto [code, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 2);
  EXPECT_THAT(errors, 1);
}

TEST_F(RunTest, CaptureUsedByLaterExecIsNotFlagged) {
  // {capture.x} referenced in a later -exec counts as used -> no unused error.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--exec-fields", root_.string(), "-name", "a.txt", "-capture:x", "/bin/sh", "-c",
                               "printf a", ";", "-exec", "/bin/sh", "-c", "test \"{capture.x}\" = a", ";"}));
  int errors = 0;
  const auto [code, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 0);  // used by the -exec, so not flagged
  EXPECT_THAT(errors, 0);
}

TEST_F(RunTest, CaptureUsedByNegatedExecIsNotFlagged) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--exec-fields", root_.string(), "-name", "a.txt", "-capture:x", "/bin/sh", "-c",
                               "printf a", ";", "!", "-exec", "/bin/sh", "-c", "test \"{capture.x}\" = b", ";"}));
  int errors = 0;
  const auto [code, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 0);
  EXPECT_THAT(any_match, IsTrue());
  EXPECT_THAT(errors, 0);
}

TEST_F(RunTest, ImplicitPrintNoSuppressesDefaultPrint) {
  // No action, so find would print -- --implicit-print=no forces it off.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--implicit-print=no", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, IsEmpty());
}

TEST_F(RunTest, ImplicitPrintYesPrintsAlongsideAction) {
  // -exec would suppress the implicit print; --implicit-print=yes forces it on.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"--implicit-print=yes", root_.string(), "-name", "a.txt", "-exec", "/bin/sh", "-c", "true", ";"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(Path("a.txt")));
}

// The default --summary output is a right-aligned human table (grouped digits);
// these assert the stable --format=jsonl machine rows instead, so the exact counts
// are checked without depending on column padding. The aligned human rendering is
// covered end to end by cli/summary_test.sh.
TEST_F(RunTest, SummaryOverallReducesMatchesToACountAndSize) {
  // --summary suppresses the per-match print and emits one total row: a.txt and
  // sub/c.txt match (1 byte each), so 2 matches / 2 bytes.
  EXPECT_THAT(
      RunArgvRecords({"--summary", "--format=jsonl", root_.string(), "-name", "*.txt"}),
      ElementsAre(R"({"group":"total","count":2,"bytes":2})"));
}

TEST_F(RunTest, SummaryByTypeGroupsThenTotals) {
  // --summary=type over the three files (1 byte each): one "file" group, then total.
  EXPECT_THAT(
      RunArgvRecords({"--summary=type", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"group":"file","count":3,"bytes":3})", R"({"group":"total","count":3,"bytes":3})"));
}

TEST_F(RunTest, SummaryByExtensionGroupsSortedThenTotals) {
  // --summary=ext over the files: "md" (b.md) sorts before "txt" (a.txt, sub/c.txt).
  EXPECT_THAT(
      RunArgvRecords({"--summary=ext", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"group":"md","count":1,"bytes":1})", R"({"group":"txt","count":2,"bytes":2})",
          R"({"group":"total","count":3,"bytes":3})"));
}

TEST_F(RunTest, SummaryByLanguageGroupsThenTotals) {
  // --summary=lang: b.md is Markdown; a.txt and sub/c.txt have no known language ("(none)").
  // "(" sorts before letters, so the "(none)" bucket leads, then Markdown, then the total.
  EXPECT_THAT(
      RunArgvRecords({"--summary=lang", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"j({"group":"(none)","count":2,"bytes":2})j", R"j({"group":"Markdown","count":1,"bytes":1})j",
          R"j({"group":"total","count":3,"bytes":3})j"));
}

TEST_F(RunTest, SummaryHashVerificationCountsPassedAndFailedChecksInOneWalk) {
  // sha256("a"): a.txt verifies; b.md and sub/c.txt reach the same -hasheq and fail. Failed
  // predicates make the overall expression false but remain part of the verification tally.
  constexpr std::string_view kSha256A = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=type", "--summary=hash-verification", "--format=jsonl", root_.string(), "-type", "f", "-hasheq",
           std::string(kSha256A)}),
      ElementsAre(
          R"({"group":"file","count":1,"bytes":1})", R"({"group":"total","count":1,"bytes":1})",
          R"({"group":"failed","count":2,"bytes":2})", R"({"group":"verified","count":1,"bytes":1})",
          R"({"group":"total","count":3,"bytes":3})"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryHashVerificationDoesNotCountChecksSkippedByShortCircuiting) {
  constexpr std::string_view kSha256A = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", "--format=jsonl", root_.string(), "-false", "-a", "-hasheq",
           std::string(kSha256A)}),
      ElementsAre(R"({"group":"total","count":0})"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryHashVerificationRequiresExactlyOneHashCheck) {
  constexpr std::string_view kSha256A = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";
  EXPECT_THAT(RunArgvRecords({"--summary=hash-verification", root_.string(), "-type", "f"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", root_.string(), "-hasheq", std::string(kSha256A), "-o", "-hasheq",
           std::string(kSha256A)}),
      IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  // Negation changes expression truth, not the number or polarity of verification checks.
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", "--format=jsonl", root_.string(), "-name", "a.txt", "!", "-hasheq",
           "deadbeef"}),
      ElementsAre(R"({"group":"failed","count":1,"bytes":1})", R"({"group":"total","count":1,"bytes":1})"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryHashVerificationClassifiesMissingExpectationsAndNonRegularEntriesAsFailed) {
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", "--format=jsonl", root_.string(), "-name", "a.txt", "-hasheq",
           "{def.MISSING}"}),
      ElementsAre(R"({"group":"failed","count":1,"bytes":1})", R"({"group":"total","count":1,"bytes":1})"));
  EXPECT_THAT(last_errors_, 0);
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", "--format=jsonl", root_.string(), "-type", "d", "-hasheq", "deadbeef"}),
      ElementsAre(HasSubstr(R"("group":"failed","count":2)"), HasSubstr(R"("group":"total","count":2)")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryHashVerificationVerdictSurvivesDeferredReplayWithoutASecondHash) {
  { std::ofstream(root_ / "verify-00000-of-00002") << "a"; }
  { std::ofstream(root_ / "verify-00001-of-00002") << "b"; }
  constexpr std::string_view kSha256A = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";
  // The comma evaluates -hasheq before -shard-status deliberately defers both entries. Replay uses
  // its memoized prefix and the candidate's saved verdict, so each physical entry contributes once.
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", "--format=jsonl", root_.string(), "-name", "verify-*", "-a", "(", "-hasheq",
           std::string(kSha256A), ",", "-shard-status", "complete", ")"}),
      ElementsAre(
          R"({"group":"failed","count":1,"bytes":1})", R"({"group":"verified","count":1,"bytes":1})",
          R"({"group":"total","count":2,"bytes":2})"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryTopKeepsTheLargestGroupsBySize) {
  // --top=1: keep the largest group by size (txt, 2 bytes) and drop md (1 byte),
  // ordered by size; the total row still counts every matched group.
  EXPECT_THAT(
      RunArgvRecords({"--summary=ext", "--top=1", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"group":"txt","count":2,"bytes":2})", R"({"group":"total","count":3,"bytes":3})"));
}

TEST_F(RunTest, SummaryOmitsSizeWhenNothingSizeWorthyIsAggregated) {
  // Empty files -> the summary has no size dimension, so it reports counts only: no spurious
  // `0 B` column in the human table and no `bytes:0` field in the jsonl rows (#156).
  { std::ofstream(root_ / "e1.log"); }  // 0 bytes
  { std::ofstream(root_ / "e2.log"); }
  EXPECT_THAT(
      RunArgvRecords({"--summary=ext", "--format=jsonl", root_.string(), "-name", "*.log"}),
      ElementsAre(R"({"group":"log","count":2})", R"({"group":"total","count":2})"));
  // The human (default) table is count-only: no size unit column anywhere.
  EXPECT_THAT(RunArgvRecords({"--summary=ext", root_.string(), "-name", "*.log"}), Not(Contains(HasSubstr(" B"))));
}

TEST_F(RunTest, HistogramByExtensionCountsPerBucketSortedByCount) {
  // --histogram=ext: txt (a.txt, sub/c.txt) has 2, md (b.md) has 1; bars sort by count
  // descending, so txt leads. No total row (a histogram is just bars). The jsonl rows are
  // block-tagged and the per-match listing is suppressed (only histogram rows appear).
  EXPECT_THAT(
      RunArgvRecords({"--histogram=ext", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"histogram":"ext","bucket":"txt","value":2})", R"({"histogram":"ext","bucket":"md","value":1})"));
}

TEST_F(RunTest, HistogramByTypeCountsMatches) {
  EXPECT_THAT(
      RunArgvRecords({"--histogram=type", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"histogram":"type","bucket":"file","value":3})"));
}

TEST_F(RunTest, HistogramCombinesWithSummaryEmittingBothBlocks) {
  // --summary and --histogram are independent, combinable reductions fed by one walk: the
  // summary rows come first, then the histogram bars, all block-tagged in --format=jsonl.
  EXPECT_THAT(
      RunArgvRecords({"--summary=type", "--histogram=ext", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"group":"file","count":3,"bytes":3})", R"({"group":"total","count":3,"bytes":3})",
          R"({"histogram":"ext","bucket":"txt","value":2})", R"({"histogram":"ext","bucket":"md","value":1})"));
}

TEST_F(RunTest, HistogramTopKeepsTheTallestBuckets) {
  // --top=1: keep only the tallest bucket (txt, 2); a histogram has no total row.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=ext", "--top=1", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"histogram":"ext","bucket":"txt","value":2})"));
}

TEST_F(RunTest, RepeatedHistogramEmitsEachBlockInOrder) {
  // Repeatable: two --histogram flags -> two blocks, in the order given.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=type", "--histogram=ext", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"histogram":"type","bucket":"file","value":3})", R"({"histogram":"ext","bucket":"txt","value":2})",
          R"({"histogram":"ext","bucket":"md","value":1})"));
}

TEST_F(RunTest, HistogramSumOfSizePerBucket) {
  // ext:sum(size): txt has two 1-byte files (2), md has one (1); sorted by value descending.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=ext:sum(size)", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"j({"histogram":"ext:sum(size)","bucket":"txt","value":2})j",
          R"j({"histogram":"ext:sum(size)","bucket":"md","value":1})j"));
}

TEST_F(RunTest, HistogramMeanRendersFixedDecimals) {
  // type:mean(size): three 1-byte files -> mean 1.00 (default 2 decimals), a single bucket.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=type:mean(size)", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"j({"histogram":"type:mean(size)","bucket":"file","value":1.00})j"));
}

TEST_F(RunTest, HistogramMaxOfSizeSortsTiesByKey) {
  // ext:max(size): every file is 1 byte, so both buckets max at 1; equal values sort by key.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=ext:max(size)", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"j({"histogram":"ext:max(size)","bucket":"md","value":1})j",
          R"j({"histogram":"ext:max(size)","bucket":"txt","value":1})j"));
}

TEST_F(RunTest, HistogramMinOfSizeSelectsTheSmallestValuePerBucket) {
  { std::ofstream(root_ / "large.txt") << "12345"; }
  EXPECT_THAT(
      RunArgvRecords({"--histogram=ext:min(size)", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"j({"histogram":"ext:min(size)","bucket":"md","value":1})j",
          R"j({"histogram":"ext:min(size)","bucket":"txt","value":1})j"));
}

TEST_F(RunTest, HistogramLineRangesIgnoreDirectoriesAndBucketRegularFileLineCounts) {
  { std::ofstream(root_ / "a.txt") << "one\ntwo\nthree\n"; }
  { std::ofstream(root_ / "b.md") << "one\n"; }
  EXPECT_THAT(
      RunArgvRecords({"--histogram=lines", "--format=jsonl", root_.string()}),
      ElementsAre(R"({"histogram":"lines","bucket":"1-9","value":3})"));
}

TEST_F(RunTest, MissingPackOptionFileFailsBeforeTraversal) {
  EXPECT_THAT(
      RunArgvRecords({"--pack-option=@" + Path("absent-pack-options.json"), root_.string(), "-print"}), IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
}

TEST_F(RunTest, HistogramBadMeasureIsAUsageError) {
  // A numeric metric with no aggregator, an unknown aggregator, and an unknown field each fail (2).
  RunArgvRecords({"--histogram=ext:lines", root_.string(), "-type", "f"});
  EXPECT_THAT(last_errors_, 2);
  RunArgvRecords({"--histogram=ext:avg(size)", root_.string(), "-type", "f"});
  EXPECT_THAT(last_errors_, 2);
  RunArgvRecords({"--histogram=ext:sum(bogus)", root_.string(), "-type", "f"});
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, HistogramDepthRangeIsPerLevelInAscendingOrder) {
  // A numeric-range bucket draws in ascending range order (a distribution), not by height. depth:
  // root is 0 (1), a.txt/b.md/sub are 1 (3), sub/c.txt is 2 (1).
  EXPECT_THAT(
      RunArgvRecords({"--histogram=depth", "--format=jsonl", root_.string()}),
      ElementsAre(
          R"({"histogram":"depth","bucket":"0","value":1})", R"({"histogram":"depth","bucket":"1","value":3})",
          R"({"histogram":"depth","bucket":"2","value":1})"));
}

TEST_F(RunTest, HistogramSizeRangeGroupsByMagnitude) {
  // A size-range bucket groups by order of magnitude: the three 1-byte files all land in "1-9".
  EXPECT_THAT(
      RunArgvRecords({"--histogram=size", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"histogram":"size","bucket":"1-9","value":3})"));
}

TEST_F(RunTest, SummaryByMimeGroupsByMediaType) {
  // --summary=mime reuses the {mime} field: a.txt / sub/c.txt are text/plain, b.md is text/markdown.
  // Rows sort by key (text/markdown before text/plain), then the total.
  EXPECT_THAT(
      RunArgvRecords({"--summary=mime", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"group":"text/markdown","count":1,"bytes":1})", R"({"group":"text/plain","count":2,"bytes":2})",
          R"({"group":"total","count":3,"bytes":3})"));
}

TEST_F(RunTest, HistogramByMimeCountsPerMediaType) {
  // --histogram=mime: text/plain (a.txt, sub/c.txt) has 2, text/markdown (b.md) has 1; bars by count.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=mime", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"histogram":"mime","bucket":"text/plain","value":2})",
          R"({"histogram":"mime","bucket":"text/markdown","value":1})"));
}

TEST_F(RunTest, HistogramByUserGroupsUnderTheOwner) {
  // --histogram=user reuses the {user} field. Every fixture file has the same owner (the test
  // process), so there is one bucket of 3; the owner name is runtime-dependent, so assert the
  // shape (one bucket, value 3), not the name.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=user", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(MatchesRegex(R"(\{"histogram":"user","bucket":".+","value":3\})")));
}

TEST_F(RunTest, SummaryOwnerIsAnAliasOfUser) {
  // --summary=owner is the =user alias: one owner bucket of 3 (name runtime-dependent), then no
  // separate total row is emitted for a single group beyond it -- match the owner row's shape.
  EXPECT_THAT(
      RunArgvRecords({"--summary=owner", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(MatchesRegex(R"(\{"group":".+","count":3,"bytes":3\})"), R"({"group":"total","count":3,"bytes":3})"));
}

TEST_F(RunTest, HistogramByGroupGroupsUnderTheOwningGroup) {
  // --histogram=group reuses the {group} field; one owning-group bucket of 3 (name runtime-dependent).
  EXPECT_THAT(
      RunArgvRecords({"--histogram=group", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(MatchesRegex(R"(\{"histogram":"group","bucket":".+","value":3\})")));
}

TEST_F(RunTest, LsEmitsOneLinePerMatchAndSuppressesImplicitPrint) {
  // -ls is an action, so it suppresses the implicit -print: exactly one line (the
  // ls-style listing) for the match, containing its path. The exact columns are
  // umask/fs-dependent (covered deterministically in evaluate_test); here we just
  // confirm the end-to-end wiring and the print suppression.
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-ls"}), ElementsAre(HasSubstr(Path("a.txt"))));
}

// Minimal in-memory FileSystem: a root directory holding one regular file whose
// metadata carries NO birth time. The real local FS records btime on macOS/Linux,
// so it cannot reproduce the "-Btime where birth time is unrecorded" impossible
// task; this can.
class NoBtimeFs : public vfs::FileSystem {
 public:
  explicit NoBtimeFs(std::string root) : root_(std::move(root)) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    if (std::string(dir) != root_) {
      return absl::NotFoundError("NoBtimeFs: no such directory");
    }
    return std::vector<vfs::Entry>{
        vfs::Entry{.path = root_ + "/f.txt", .name = "f.txt", .type = vfs::FileType::kRegular}};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    vfs::Metadata md;
    md.type = std::string(path) == root_ ? vfs::FileType::kDirectory : vfs::FileType::kRegular;
    return md;  // btime deliberately left empty (unrecorded)
  }

  absl::Status Remove(std::string_view) const override { return absl::OkStatus(); }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("NoBtimeFs: not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("fakefs"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return std::string(); }

 private:
  std::string root_;
};

TEST_F(RunTest, BtimeOnEntryWithoutBirthtimeFailsByDefault) {
  // Impossible task: -Btime against a filesystem that does not record birth time is
  // a hard error (exit 2), reported once with a self-documenting message.
  const NoBtimeFs fs("/fake");
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"/fake", "-Btime", "1"}));
  int reports = 0;
  std::string message;
  const auto [errors, any_match] = RunFind(
      command, fs, [](std::string_view) {},
      [&](std::string_view, absl::Status status) {
        ++reports;
        message = std::string(status.message());
      });
  EXPECT_THAT(errors, Not(0));  // hard error
  EXPECT_THAT(reports, 1);      // reported once, not per entry
  EXPECT_THAT(message, HasSubstr("birth time"));
}

TEST_F(RunTest, SkipUnsupportedDowngradesImpossibleBtimeToWarnAndSkip) {
  // --skip-unsupported turns the same impossible task into a warning + skip: the
  // run still reports once (so the user knows), but it is not an error (exit 0).
  const NoBtimeFs fs("/fake");
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--skip-unsupported", "/fake", "-Btime", "1"}));
  int reports = 0;
  const auto [errors, any_match] =
      RunFind(command, fs, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++reports; });
  EXPECT_THAT(errors, 0);   // skipped -> not an error
  EXPECT_THAT(reports, 1);  // but warned once
}

// In-memory FileSystem on a case-FOLDING volume (IsCaseSensitive -> false): a root
// holding one mixed-case regular file (Foo.txt). It reports case-insensitive
// regardless of the host runner, so the FS-native -name matching / --exact tests
// below are deterministic on case-sensitive CI (ext4) too.
class CaseFoldFs : public vfs::FileSystem {
 public:
  explicit CaseFoldFs(std::string root) : root_(std::move(root)) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    if (std::string(dir) != root_) {
      return absl::NotFoundError("CaseFoldFs: no such directory");
    }
    return std::vector<vfs::Entry>{
        vfs::Entry{.path = root_ + "/Foo.txt", .name = "Foo.txt", .type = vfs::FileType::kRegular}};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    vfs::Metadata md;
    md.type = std::string(path) == root_ ? vfs::FileType::kDirectory : vfs::FileType::kRegular;
    return md;
  }

  absl::Status Remove(std::string_view) const override { return absl::OkStatus(); }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("CaseFoldFs: not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("fakefs"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return false; }  // a folding volume

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return std::string(); }

 private:
  std::string root_;
};

// Runs `-name <pattern>` over a CaseFoldFs (holding Foo.txt) in `style`, with or
// without --exact, and returns the concatenated emitted output.
std::string RunNameOnCaseFoldVolume(std::string_view pattern, std::optional<registry::Style> style, bool exact) {
  const CaseFoldFs fs("/fake");
  std::vector<std::string> args;
  if (exact) {
    args.emplace_back("--exact");
  }
  args.insert(args.end(), {"/fake", "-name", std::string(pattern)});
  const auto command = parser::Parse(args);
  EXPECT_THAT(command, IsOk());
  std::string out;
  const auto [errors, any_match] = RunFind(
      *command, fs, [&out](std::string_view record) { out += record; }, [](std::string_view, absl::Status) {}, style);
  EXPECT_THAT(errors, 0);
  return out;
}

TEST_F(RunTest, XffStyleFoldsNameOnCaseFoldingVolume) {
  // FS-native matching: the xff style matches -name the way the volume resolves
  // names, so a lower-case pattern matches the mixed-case Foo.txt on a folding FS.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", registry::Style::kXff, /*exact=*/false), HasSubstr("Foo.txt"));
}

TEST_F(RunTest, ExactOptsOutOfFsNativeFolding) {
  // --exact forces verbatim byte-exact matching even on a folding volume.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", registry::Style::kXff, /*exact=*/true), IsEmpty());
}

TEST_F(RunTest, FindStyleIsAlwaysByteExact) {
  // The find style is drop-in faithful: no FS-native folding, so a lower-case
  // pattern does not match Foo.txt regardless of the volume.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", registry::Style::kFind, /*exact=*/false), IsEmpty());
}

TEST_F(RunTest, InProcessDefaultStyleIsByteExact) {
  // std::nullopt style (the conservative in-process default) does not fold either;
  // FS-native matching is opt-in via the xff style the CLI resolves.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", std::nullopt, /*exact=*/false), IsEmpty());
}

TEST_F(RunTest, ExactCaseNameMatchesRegardlessOfFolding) {
  // The exact-case name matches in every style / with --exact -- folding only
  // widens what matches, it never stops the verbatim name from matching.
  EXPECT_THAT(RunNameOnCaseFoldVolume("Foo.txt", registry::Style::kXff, /*exact=*/false), HasSubstr("Foo.txt"));
  EXPECT_THAT(RunNameOnCaseFoldVolume("Foo.txt", registry::Style::kXff, /*exact=*/true), HasSubstr("Foo.txt"));
  EXPECT_THAT(RunNameOnCaseFoldVolume("Foo.txt", registry::Style::kFind, /*exact=*/false), HasSubstr("Foo.txt"));
}

TEST_F(RunTest, GrepEmitsPathLineTextAcrossTheWalk) {
  // -grep is an action, so it suppresses the implicit path-print and emits one
  // record per matching line, path:line:text, over the whole traversal.
  { std::ofstream(root_ / "a.txt") << "alpha\nTODO one\nbeta\nTODO two\n"; }  // overwrite the fixture's "a"
  EXPECT_THAT(
      RunExpr({"-name", "a.txt", "-grep", "TODO"}),
      ElementsAre(Path("a.txt") + ":2:TODO one", Path("a.txt") + ":4:TODO two"));
}

TEST_F(RunTest, GrepContextPrintsSurroundingLinesWithGroupSeparator) {
  { std::ofstream(root_ / "a.txt") << "1\nHIT\n3\n4\n5\nHIT\n7\n"; }
  // --context=1 (leading global): 1 line before/after each match; a match line uses ':', a context
  // line '-', and the two non-adjacent windows are divided by a "--" line -- like grep -C1.
  EXPECT_THAT(
      RunArgvRecords({"--context=1", root_.string(), "-name", "a.txt", "-grep", "HIT"}),
      ElementsAre(
          Path("a.txt") + "-1-1", Path("a.txt") + ":2:HIT", Path("a.txt") + "-3-3", "--", Path("a.txt") + "-5-5",
          Path("a.txt") + ":6:HIT", Path("a.txt") + "-7-7"));
}

TEST_F(RunTest, GrepAfterContextIsAsymmetric) {
  { std::ofstream(root_ / "a.txt") << "x\nHIT\ny\nz\n"; }
  // --after-context=1 (grep -A1): the match and one trailing line, no leading context.
  EXPECT_THAT(
      RunArgvRecords({"--after-context=1", root_.string(), "-name", "a.txt", "-grep", "HIT"}),
      ElementsAre(Path("a.txt") + ":2:HIT", Path("a.txt") + "-3-y"));
}

TEST_F(RunTest, GrepContextAcceptsEverySideSpellingAndLastValueWins) {
  { std::ofstream(root_ / "a.txt") << "zero\nHIT\ntwo\nthree\n"; }
  EXPECT_THAT(
      RunArgvRecords({"--context=A:3,a:1,B:3,b:0,C:2,c:1", root_.string(), "-name", "a.txt", "-grep", "HIT"}),
      ElementsAre(Path("a.txt") + "-1-zero", Path("a.txt") + ":2:HIT", Path("a.txt") + "-3-two"));
}

TEST_F(RunTest, GrepContextRejectsEveryMalformedValueClass) {
  static constexpr auto kCases = std::to_array<std::string_view>({
      "--context=missing-colon",
      "--context=A:not-a-number",
      "--context=D:1",
      "--before-context=bad",
      "--after-context=bad",
  });
  for (const std::string_view flag : kCases) {
    SCOPED_TRACE(flag);
    EXPECT_THAT(RunArgvRecords({std::string(flag), root_.string(), "-grep", "HIT"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
  }
}

TEST_F(RunTest, GrepRegextypeExactMatchesLiterally) {
  { std::ofstream(root_ / "a.txt") << "price 3.50\nprice 3X50\n"; }
  // --regextype=EXACT (a leading global) makes '.' a literal, so only the real 3.50.
  EXPECT_THAT(
      RunArgvRecords({"--regextype=EXACT", root_.string(), "-name", "a.txt", "-grep", "3.50"}),
      ElementsAre(Path("a.txt") + ":1:price 3.50"));
}

TEST_F(RunTest, GrepRegextypeDefaultIsRe2) {
  { std::ofstream(root_ / "a.txt") << "price 3.50\nprice 3X50\n"; }
  // Default (RE2): '.' is a wildcard, so both lines match.
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-name", "a.txt", "-grep", "3.50"}),
      ElementsAre(Path("a.txt") + ":1:price 3.50", Path("a.txt") + ":2:price 3X50"));
}

TEST_F(RunTest, UnsupportedRegextypeIsAUsageError) {
  // MATCH is reserved (#85), and PCRE2 is a build extra not linked into this (lean) test binary:
  // both are usage errors refused before the walk (exit 2), never a silent RE2 fallback.
  EXPECT_THAT(RunArgvRecords({"--regextype=MATCH", root_.string(), "-grep", "x"}), IsEmpty());
  EXPECT_THAT(last_errors_, Not(0));
  EXPECT_THAT(RunArgvRecords({"--regextype=PCRE2", root_.string(), "-grep", "x"}), IsEmpty());
  EXPECT_THAT(last_errors_, Not(0));
}

TEST_F(RunTest, GrepFormatRendersCustomTemplate) {
  // -grep:FORMAT overrides the default path:line:text with a field template.
  { std::ofstream(root_ / "a.txt") << "alpha\nTODO one\nbeta\n"; }
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-grep:{line}|{text}", "TODO"}), ElementsAre("2|TODO one"));
}

TEST_F(RunTest, GrepCountEmitsPerFileCount) {
  // --count / -c (a leading global): path:count per file with matches, not the lines.
  { std::ofstream(root_ / "a.txt") << "TODO 1\nx\nTODO 2\n"; }
  EXPECT_THAT(
      RunArgvRecords({"--count", root_.string(), "-name", "a.txt", "-grep", "TODO"}),
      ElementsAre(Path("a.txt") + ":2"));
}

TEST_F(RunTest, ShardsCollapsesEachSetToOneLineAndPassesNonShardsThrough) {
  { std::ofstream(root_ / "data-00000-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00001-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "vol.1") << ""; }  // dotnum scheme
  { std::ofstream(root_ / "vol.2") << ""; }
  // --shards (auto): each set collapses to its lowest-index representative; a.txt / b.md / sub/c.txt
  // (from SetUp) are non-shards and list unchanged.
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards"}),
      UnorderedElementsAre(
          Path("data-00000-of-00003.tfrecord"), Path("vol.1"), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsSchemeRestrictionTreatsUnselectedSchemesAsNonShards) {
  { std::ofstream(root_ / "data-00000-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00001-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "vol.1") << ""; }
  { std::ofstream(root_ / "vol.2") << ""; }
  // --shards=of collapses only the -of- set; the dotnum vol.1 / vol.2 are listed unchanged.
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards=of"}),
      UnorderedElementsAre(
          Path("data-00000-of-00003.tfrecord"), Path("vol.1"), Path("vol.2"), Path("a.txt"), Path("b.md"),
          Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsUnknownSchemeIsAUsageError) {
  EXPECT_THAT(RunArgvRecords({root_.string(), "--shards=bogus"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ShardsShowWildcardMasksTheIndex) {
  { std::ofstream(root_ / "data-00000-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00001-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003.tfrecord") << ""; }
  // The child is built with StrCat so the source carries no `??-` (a C++ trigraph).
  const std::string wildcard = absl::StrCat("data-", std::string(5, '?'), "-of-00003.tfrecord");
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards", "--shards-show=wildcard"}),
      UnorderedElementsAre(Path(wildcard), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsShowCountAppendsTheShardCount) {
  { std::ofstream(root_ / "data-00000-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00001-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003.tfrecord") << ""; }
  const std::string line = absl::StrCat("data-", std::string(5, '?'), "-of-00003.tfrecord (3 shards)");
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards", "--shards-show=count"}),
      UnorderedElementsAre(Path(line), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsAnnotateAnIncompleteSet) {
  // Shard 1 of 3 is missing: the set is flagged INCOMPLETE with its present/expected count.
  { std::ofstream(root_ / "data-00000-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003.tfrecord") << ""; }
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards"}),
      UnorderedElementsAre(
          Path("data-00000-of-00003.tfrecord (2/3 - INCOMPLETE)"), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsRenderASetContainingOnlyAnOutOfRangeFile) {
  { std::ofstream(root_ / "data-00002-of-00002") << ""; }
  EXPECT_THAT(
      RunExpr({"-type", "f", "--shards"}),
      UnorderedElementsAre(
          Path("data-00002-of-00002 (0/2 - INCOMPLETE)"), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusCompleteMatchesOnlyRepresentativesInCompleteSets) {
  { std::ofstream(root_ / "complete-00000-of-00002") << ""; }
  { std::ofstream(root_ / "complete-00001-of-00002") << ""; }
  { std::ofstream(root_ / "incomplete-00000-of-00002") << ""; }
  EXPECT_THAT(
      RunExpr({"-type", "f", "-shard-status", "complete"}),
      UnorderedElementsAre(Path("complete-00000-of-00002"), Path("complete-00001-of-00002")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusIncompleteMatchesEveryPresentRepresentativeInAnIncompleteSet) {
  { std::ofstream(root_ / "data-00000-of-00003") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003") << ""; }
  EXPECT_THAT(
      RunExpr({"-type", "f", "-shard-status", "incomplete"}),
      UnorderedElementsAre(Path("data-00000-of-00003"), Path("data-00002-of-00003")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusSuperfluousMatchesDuplicateCopiesAndOutOfRangeIndices) {
  { std::ofstream(root_ / "data-00000-of-00002.aaaaaaaa") << ""; }
  { std::ofstream(root_ / "data-00000-of-00002.bbbbbbbb") << ""; }
  { std::ofstream(root_ / "data-00001-of-00002") << ""; }
  { std::ofstream(root_ / "data-00002-of-00002") << ""; }
  EXPECT_THAT(
      RunExpr({"-type", "f", "-shard-status", "superfluous"}),
      UnorderedElementsAre(Path("data-00000-of-00002.bbbbbbbb"), Path("data-00002-of-00002")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusClassifiesOnlyTheCohortReachingThatExpressionNode) {
  { std::ofstream(root_ / "data-00000-of-00002") << ""; }
  { std::ofstream(root_ / "data-00001-of-00002") << ""; }
  // Filtering away index 1 before the result-set predicate intentionally makes
  // the remaining cohort incomplete; predicates after it do not affect the set.
  EXPECT_THAT(RunExpr({"-name", "*-00000-*", "-shard-status", "incomplete"}), ElementsAre(Path("data-00000-of-00002")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusFalseCanContinueIntoAnOrAlternative) {
  { std::ofstream(root_ / "data-00000-of-00001") << ""; }
  EXPECT_THAT(
      RunExpr({"-type", "f", "-shard-status", "incomplete", "-o", "-name", "a.txt"}), ElementsAre(Path("a.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusRejectsAnUnknownStatusBeforeWalking) {
  EXPECT_THAT(RunExpr({"-shard-status", "broken"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ShardStatusDoesNotClassifyDirectoriesWhoseNamesLookLikeShards) {
  ASSERT_THAT(fs::create_directory(root_ / "dir-00000-of-00001"), IsTrue());
  EXPECT_THAT(RunExpr({"-shard-status", "complete"}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusPreservesRepeatedVisitsThroughOverlappingRoots) {
  { std::ofstream(root_ / "data-00000-of-00001") << ""; }
  const std::string shard = Path("data-00000-of-00001");
  EXPECT_THAT(
      RunArgvRecords({root_.string(), shard, "-type", "f", "-shard-status", "complete"}), ElementsAre(shard, shard));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusUsesCustomPatterns) {
  { std::ofstream(root_ / "img_001_v3.raw") << ""; }
  { std::ofstream(root_ / "img_002_v3.raw") << ""; }
  EXPECT_THAT(
      RunArgvRecords(
          {root_.string(), "-type", "f", "-shard-status", "complete",
           R"(--shard-pattern=(?P<stem>.*)_(?P<index>\d+)_v(?P<dup>\d+)\.raw)"}),
      UnorderedElementsAre(Path("img_001_v3.raw"), Path("img_002_v3.raw")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusUsesMtimeToChooseTheRepresentative) {
  const fs::path older = root_ / "data-00000-of-00001.aaaaaaaa";
  const fs::path newer = root_ / "data-00000-of-00001.bbbbbbbb";
  { std::ofstream(older) << ""; }
  { std::ofstream(newer) << ""; }
  std::error_code ec;
  fs::last_write_time(older, fs::file_time_type::clock::now() - std::chrono::hours(1), ec);
  ASSERT_THAT(ec, Eq(std::error_code{}));
  fs::last_write_time(newer, fs::file_time_type::clock::now(), ec);
  ASSERT_THAT(ec, Eq(std::error_code{}));
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "-shard-status", "complete", "--shards-dedup=mtime"}),
      ElementsAre(Path("data-00000-of-00001.bbbbbbbb")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusHonorsTheEnabledBuiltInSchemes) {
  { std::ofstream(root_ / "data.001") << ""; }
  EXPECT_THAT(RunArgvRecords({root_.string(), "-type", "f", "-shard-status", "complete", "--shards=of"}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusUsesAllBuiltInSchemesByDefault) {
  { std::ofstream(root_ / "data.001") << ""; }
  EXPECT_THAT(RunExpr({"-type", "f", "-shard-status", "complete"}), ElementsAre(Path("data.001")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusHonorsDedupErrorWithoutCollapsedOutput) {
  { std::ofstream(root_ / "data-00000-of-00001.aaaaaaaa") << ""; }
  { std::ofstream(root_ / "data-00000-of-00001.bbbbbbbb") << ""; }
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "-shard-status", "superfluous", "--shards-dedup=error"}),
      ElementsAre(Path("data-00000-of-00001.bbbbbbbb")));
  EXPECT_THAT(last_errors_, 1);
}

TEST_F(RunTest, ShardsShowUnknownValueIsAUsageError) {
  EXPECT_THAT(RunArgvRecords({root_.string(), "--shards", "--shards-show=bogus"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ShardsDedupErrorFlagsADuplicate) {
  // Two files are the same logical shard 0 (differ only by hex tail): --shards-dedup=error fails.
  { std::ofstream(root_ / "data-00000-of-00001.aaaaaaaa") << ""; }
  { std::ofstream(root_ / "data-00000-of-00001.bbbbbbbb") << ""; }
  // The set still collapses to one line (representative), but the conflict is counted as an error
  // (one conflicting set -> one error; the CLI maps any non-zero count to a failing exit).
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards", "--shards-dedup=error"}),
      UnorderedElementsAre(Path("data-00000-of-00001.aaaaaaaa"), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 1);
}

TEST_F(RunTest, ShardsDedupUnknownValueIsAUsageError) {
  EXPECT_THAT(RunArgvRecords({root_.string(), "--shards", "--shards-dedup=bogus"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ShardPatternCustomSchemeCollapsesASet) {
  // A naming the built-ins do not recognize (`img_NNN_vX.raw`); a custom pattern groups it,
  // with the version treated as the dup (excluded from identity).
  { std::ofstream(root_ / "img_001_v3.raw") << ""; }
  { std::ofstream(root_ / "img_002_v3.raw") << ""; }
  { std::ofstream(root_ / "img_003_v3.raw") << ""; }
  EXPECT_THAT(
      RunArgvRecords(
          {root_.string(), "-type", "f", "--shards",
           R"(--shard-pattern=(?P<stem>.*)_(?P<index>\d+)_v(?P<dup>\d+)\.raw)"}),
      UnorderedElementsAre(Path("img_001_v3.raw"), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardPatternMissingRequiredGroupIsAUsageError) {
  EXPECT_THAT(RunArgvRecords({root_.string(), "--shards", R"(--shard-pattern=(?P<stem>.*)_(\d+))"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ShardsSummaryAggregatesPerLogicalSet) {
  // Three shards of one set (sizes 4+2+1) plus one non-shard file. --summary=ext in shard mode
  // counts the set once and sums its size (7 bytes), not three separate files.
  { std::ofstream(root_ / "data-000-of-003.tfrecord") << "aaaa"; }
  { std::ofstream(root_ / "data-001-of-003.tfrecord") << "bb"; }
  { std::ofstream(root_ / "data-002-of-003.tfrecord") << "c"; }
  const std::vector<std::string> rows =
      RunArgvRecords({root_.string(), "-name", "*.tfrecord", "--shards", "--summary=ext"});
  // The tfrecord row aggregates the set into one unit of 7 bytes.
  EXPECT_THAT(rows, Contains(AllOf(HasSubstr("tfrecord"), HasSubstr("7"))));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsSummaryByShardCountGroupsSets) {
  { std::ofstream(root_ / "data-000-of-002.bin") << ""; }
  { std::ofstream(root_ / "data-001-of-002.bin") << ""; }
  // --summary={shard} groups by each set's shard count; the 2-shard set lands in a "2" bucket.
  const std::vector<std::string> rows =
      RunArgvRecords({root_.string(), "-name", "*.bin", "--shards", "--summary={shard}"});
  EXPECT_THAT(rows, Contains(HasSubstr("2")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, FlavorFacetsHaveStableStorage) {
  const auto facets = FlavorFacets();
  EXPECT_THAT(FlavorFacets().data(), Eq(facets.data()));
}

}  // namespace
}  // namespace xff::engine
