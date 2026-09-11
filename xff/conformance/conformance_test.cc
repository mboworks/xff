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

// This suite asserts xff's OWN resulting offering against a fixed fixture, NOT
// conformance to a system find. xff is the union of POSIX/GNU/BSD with its own
// chosen resolutions and extensions, so the right question is "does xff produce the
// output xff specifies", not "does it match whatever find is installed" (which
// varies by platform and skips where a flag is flavor-specific). The expected sets
// below are therefore hardcoded and deterministic.
//
// To stay platform-independent the fixture controls everything the predicates read:
// file contents (sizes), modes (chmod), and the regular files' mtimes (pinned to
// distinct far-past instants, so directories/the symlink -- left at their ~now
// creation time -- are always strictly newer). Cases whose result is inherently
// filesystem-dependent and cannot be pinned on a real FS (birth time, which not
// every FS records; inode-change time, which has no set API) are covered by the
// engine unit test instead, not here.

#include <grp.h>     // getgrgid()/struct group for the -group arg
#include <pwd.h>     // getpwuid()/struct passwd for the -user arg
#include <unistd.h>  // geteuid()/getegid() for the -uid/-gid and -user/-group args

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/local_fs.h"

namespace xff {
namespace {

using ::testing::IsFalse;
using ::testing::IsTrue;
namespace fs = ::std::filesystem;
using ::mbo::testing::IsOk;
using ::testing::ElementsAreArray;

// Fixture tree (relative to <root>):
//   a.txt      1 byte,  mode 0644, mtime pinned oldest
//   b.md       2 bytes, mode 0640, mtime pinned
//   sub/       directory
//   sub/c.txt  3 bytes, mode 0600, mtime pinned
//   link       symlink -> a.txt (mtime ~now)
//   empty.txt  0 bytes, mode 0664, mtime pinned newest of the files
//   emptydir/  empty directory
struct ConformanceTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir())
            / (std::string("xff_conf_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code ec;
    fs::remove_all(root_, ec);
    ASSERT_THAT(fs::create_directories(root_ / "sub"), IsTrue());
    fs::create_directory(root_ / "emptydir", ec);
    { std::ofstream(root_ / "a.txt") << "a"; }
    { std::ofstream(root_ / "b.md") << "bb"; }
    { std::ofstream(root_ / "sub" / "c.txt") << "ccc"; }
    { std::ofstream(root_ / "empty.txt"); }  // 0 bytes
    fs::create_symlink("a.txt", root_ / "link");
    // Known modes so -perm is meaningful and FS-independent.
    fs::permissions(root_ / "a.txt", static_cast<fs::perms>(0644));
    fs::permissions(root_ / "b.md", static_cast<fs::perms>(0640));
    fs::permissions(root_ / "sub" / "c.txt", static_cast<fs::perms>(0600));
    fs::permissions(root_ / "empty.txt", static_cast<fs::perms>(0664));
    // Pin the regular files' mtimes to distinct far-past instants (a < b < c <
    // empty, all well before now), so -newer*/-newermt are deterministic and the
    // directories + symlink (still at ~now) are strictly the newest entries.
    const auto now = fs::file_time_type::clock::now();
    PinMtime("a.txt", now - std::chrono::hours(4'000));
    PinMtime("b.md", now - std::chrono::hours(3'000));
    PinMtime("sub/c.txt", now - std::chrono::hours(2'000));
    PinMtime("empty.txt", now - std::chrono::hours(1'000));
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  void PinMtime(std::string_view rel, fs::file_time_type when) const {
    std::error_code ec;
    fs::last_write_time(root_ / rel, when, ec);
    ASSERT_THAT(ec, IsFalse()) << "pin mtime " << rel << ": " << ec.message();
  }

  // The full path for a fixture entry; "" is the root itself.
  std::string Rel(std::string_view rel) const { return rel.empty() ? root_.string() : (root_ / rel).string(); }

  // Maps fixture-relative names to full paths, sorted, for ElementsAreArray.
  std::vector<std::string> Paths(const std::vector<std::string>& rels) const {
    std::vector<std::string> out;
    out.reserve(rels.size());
    for (const std::string& rel : rels) {
      out.push_back(Rel(rel));
    }
    std::sort(out.begin(), out.end());
    return out;
  }

  // Runs xff over `<globals> <root> <expr>` and returns the emitted records, sorted,
  // with a single trailing terminator stripped.
  std::vector<std::string> Xff(const std::vector<std::string>& globals, const std::vector<std::string>& expr) const {
    std::vector<std::string> argv = globals;
    argv.push_back(root_.string());
    argv.insert(argv.end(), expr.begin(), expr.end());
    auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> lines;
    if (command.ok()) {
      parser::BindMatchers(
          *command, parser::GrammarFromGlobals(command->globals),
          parser::ResolveCaseMode(command->globals, registry::Style::kXff));
      engine::RunFind(
          *command, fs_,
          [&](std::string_view record) {
            std::string text(record);
            if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
              text.pop_back();
            }
            lines.push_back(std::move(text));
          },
          [](std::string_view, absl::Status) {});
    }
    std::sort(lines.begin(), lines.end());
    return lines;
  }

  // Asserts xff's output for `expr` is exactly the given fixture entries.
  void ExpectXff(const std::vector<std::string>& expr, const std::vector<std::string>& expected_rels) const {
    ExpectXff({}, expr, expected_rels);
  }

  void ExpectXff(
      const std::vector<std::string>& globals,
      const std::vector<std::string>& expr,
      const std::vector<std::string>& expected_rels) const {
    EXPECT_THAT(Xff(globals, expr), ElementsAreArray(Paths(expected_rels)));
  }

  // The whole tree (every entry, "" being the root).
  static std::vector<std::string> All() {
    return {"", "a.txt", "b.md", "sub", "sub/c.txt", "link", "empty.txt", "emptydir"};
  }

  vfs::LocalFs fs_;
  fs::path root_;
};

TEST_F(ConformanceTest, PrintAll) {
  ExpectXff({}, All());
}

TEST_F(ConformanceTest, ExplicitPrint) {
  ExpectXff({"-print"}, All());
}

TEST_F(ConformanceTest, TypeFile) {
  ExpectXff({"-type", "f"}, {"a.txt", "b.md", "sub/c.txt", "empty.txt"});
}

TEST_F(ConformanceTest, TypeDirectory) {
  ExpectXff({"-type", "d"}, {"", "sub", "emptydir"});
}

TEST_F(ConformanceTest, TypeSymlink) {
  ExpectXff({"-type", "l"}, {"link"});
}

TEST_F(ConformanceTest, LnameMatchesSymlinkTarget) {
  ExpectXff({"-lname", "a.txt"}, {"link"});
}

TEST_F(ConformanceTest, LnameGlobMatchesSymlinkTarget) {
  ExpectXff({"-lname", "*.txt"}, {"link"});
}

TEST_F(ConformanceTest, IlnameFoldsCase) {
  ExpectXff({"-ilname", "A.TXT"}, {"link"});
}

// -xtype follows the symlink: link -> a.txt (a regular file), so -xtype f includes
// link while -type f does not; -xtype l matches only broken links (none here).
TEST_F(ConformanceTest, XtypeFileFollowsLink) {
  ExpectXff({"-xtype", "f"}, {"a.txt", "b.md", "sub/c.txt", "empty.txt", "link"});
}

TEST_F(ConformanceTest, XtypeDirectory) {
  ExpectXff({"-xtype", "d"}, {"", "sub", "emptydir"});
}

TEST_F(ConformanceTest, XtypeSymlinkOnlyBroken) {
  ExpectXff({"-xtype", "l"}, {});
}

TEST_F(ConformanceTest, NameGlob) {
  ExpectXff({"-name", "*.txt"}, {"a.txt", "sub/c.txt", "empty.txt"});
}

TEST_F(ConformanceTest, NotTypeDirectory) {
  ExpectXff({"!", "-type", "d"}, {"a.txt", "b.md", "sub/c.txt", "link", "empty.txt"});
}

TEST_F(ConformanceTest, OrExpression) {
  ExpectXff({"-name", "*.txt", "-o", "-type", "d"}, {"", "a.txt", "sub", "sub/c.txt", "empty.txt", "emptydir"});
}

TEST_F(ConformanceTest, PathGlob) {
  ExpectXff({"-path", "*/sub/*"}, {"sub/c.txt"});
}

TEST_F(ConformanceTest, WholenameGlob) {
  ExpectXff({"-wholename", "*/sub/*"}, {"sub/c.txt"});
}

TEST_F(ConformanceTest, IwholenameFoldsCase) {
  ExpectXff({"-iwholename", "*/SUB/*"}, {"sub/c.txt"});
}

TEST_F(ConformanceTest, AndChain) {
  ExpectXff({"-type", "f", "-name", "*.md"}, {"b.md"});
}

// -size cases are scoped to -type f: the regular-file sizes are fixed by the
// fixture (1/2/3/0 bytes), whereas directory and symlink sizes are filesystem-
// dependent. The rounding/unit arithmetic is covered by the engine unit test.
TEST_F(ConformanceTest, SizeFileExactByte) {
  ExpectXff({"-type", "f", "-size", "1c"}, {"a.txt"});
}

TEST_F(ConformanceTest, SizeFileGreaterByte) {
  ExpectXff({"-type", "f", "-size", "+1c"}, {"b.md", "sub/c.txt"});
}

TEST_F(ConformanceTest, SizeFileUnderThreeBytes) {
  ExpectXff({"-type", "f", "-size", "-3c"}, {"a.txt", "b.md", "empty.txt"});
}

TEST_F(ConformanceTest, PermExact) {
  ExpectXff({"-perm", "644"}, {"a.txt"});
}

TEST_F(ConformanceTest, PermExactOther) {
  ExpectXff({"-perm", "600"}, {"sub/c.txt"});
}

TEST_F(ConformanceTest, PermAllBitsOwnerWrite) {
  ExpectXff({"-perm", "-200"}, All());  // every entry is owner-writable
}

TEST_F(ConformanceTest, PermAllBitsGroupAndOtherRead) {
  // 0044 = group-read and other-read. a.txt(644)/empty(664) have both; the dirs
  // and symlink (0755/0777) have both; b.md(640) lacks other-read, c.txt(600) lacks
  // both.
  ExpectXff({"-perm", "-044"}, {"", "a.txt", "sub", "link", "empty.txt", "emptydir"});
}

TEST_F(ConformanceTest, MaxDepthOne) {
  ExpectXff({"-maxdepth", "1"}, {"", "a.txt", "b.md", "sub", "link", "empty.txt", "emptydir"});
}

TEST_F(ConformanceTest, MaxDepthTwo) {
  ExpectXff({"-maxdepth", "2"}, All());
}

TEST_F(ConformanceTest, MinDepthOne) {
  ExpectXff({"-mindepth", "1"}, {"a.txt", "b.md", "sub", "sub/c.txt", "link", "empty.txt", "emptydir"});
}

TEST_F(ConformanceTest, MaxDepthWithType) {
  ExpectXff({"-maxdepth", "1", "-type", "f"}, {"a.txt", "b.md", "empty.txt"});
}

TEST_F(ConformanceTest, Empty) {
  ExpectXff({"-empty"}, {"empty.txt", "emptydir"});
}

TEST_F(ConformanceTest, LinksOne) {
  ExpectXff({"-links", "1"}, {"a.txt", "b.md", "sub/c.txt", "link", "empty.txt"});
}

TEST_F(ConformanceTest, LinksMoreThanOne) {
  ExpectXff({"-links", "+1"}, {"", "sub", "emptydir"});  // directories have nlink >= 2
}

TEST_F(ConformanceTest, SamefileMatchesHardLink) {
  std::error_code ec;
  fs::create_hard_link(root_ / "a.txt", root_ / "a-hardlink", ec);
  if (ec) {
    GTEST_SKIP() << "hard links unsupported on this filesystem";
  }
  ExpectXff({"-samefile", (root_ / "a.txt").string()}, {"a.txt", "a-hardlink"});
}

// b.md's mtime is pinned to the middle, so -newer/-newermm select every entry
// strictly newer: the later-pinned files plus the ~now directories and symlink.
TEST_F(ConformanceTest, NewerThanReferenceFile) {
  ExpectXff({"-newer", (root_ / "b.md").string()}, {"", "sub", "sub/c.txt", "link", "empty.txt", "emptydir"});
}

TEST_F(ConformanceTest, NewerMtimeVsRefMtime) {
  ExpectXff({"-newermm", (root_ / "b.md").string()}, {"", "sub", "sub/c.txt", "link", "empty.txt", "emptydir"});
}

// -newermt with the "yesterday" day word: the files are pinned far in the past, so
// only the ~now directories and symlink are newer than yesterday.
TEST_F(ConformanceTest, NewerMtimeVsYesterdayKeyword) {
  ExpectXff({"-newermt", "yesterday"}, {"", "sub", "link", "emptydir"});
}

TEST_F(ConformanceTest, UidMatchesCurrentUser) {
  ExpectXff({"-uid", std::to_string(::geteuid())}, All());  // every entry is owned by the test user
}

TEST_F(ConformanceTest, GidMatchesCurrentGroup) {
  ExpectXff({"-gid", std::to_string(::getegid())}, All());
}

TEST_F(ConformanceTest, UserMatchesCurrentUser) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded CLI/test path
  const struct passwd* const pw = ::getpwuid(::geteuid());
  if (pw == nullptr) {
    GTEST_SKIP() << "no passwd entry for euid";
  }
  ExpectXff({"-user", pw->pw_name}, All());
}

TEST_F(ConformanceTest, GroupMatchesCurrentGroup) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded CLI/test path
  const struct group* const gr = ::getgrgid(::getegid());
  if (gr == nullptr) {
    GTEST_SKIP() << "no group entry for egid";
  }
  ExpectXff({"-group", gr->gr_name}, All());
}

TEST_F(ConformanceTest, ParenGrouping) {
  ExpectXff(
      {"(", "-type", "f", "-o", "-type", "d", ")"}, {"", "a.txt", "b.md", "sub", "sub/c.txt", "empty.txt", "emptydir"});
}

TEST_F(ConformanceTest, CommaListImplicitPrint) {
  // The comma's value is its right operand, so the implicit -print fires for the
  // *.md match only.
  ExpectXff({"-name", "*.txt", ",", "-name", "*.md"}, {"b.md"});
}

TEST_F(ConformanceTest, CommaWithExplicitAction) {
  // -print (right operand) is an always-true action, so every entry prints.
  ExpectXff({"-type", "f", ",", "-print"}, All());
}

TEST_F(ConformanceTest, PruneSkipsDirectory) {
  // `-name sub -prune -o -print`: sub matches and is pruned (not printed, subtree
  // skipped); everything else prints.
  ExpectXff({"-name", "sub", "-prune", "-o", "-print"}, {"", "a.txt", "b.md", "link", "empty.txt", "emptydir"});
}

TEST_F(ConformanceTest, DepthSameSet) {
  ExpectXff({"-depth"}, All());  // post-order changes visit order only; the set is unchanged
}

TEST_F(ConformanceTest, DepthWithTypeFile) {
  ExpectXff({"-depth", "-type", "f"}, {"a.txt", "b.md", "sub/c.txt", "empty.txt"});
}

TEST_F(ConformanceTest, XdevSameSetOnSingleDevice) {
  ExpectXff({"-xdev"}, All());  // single-device fixture: -xdev prunes nothing
}

TEST_F(ConformanceTest, FollowAllTypeFileIncludesSymlink) {
  ExpectXff({"-L"}, {"-type", "f"}, {"a.txt", "b.md", "sub/c.txt", "empty.txt", "link"});
}

TEST_F(ConformanceTest, FollowAllLeavesNoSymlinkType) {
  ExpectXff({"-L"}, {"-type", "l"}, {});  // under -L the symlink resolves to its target
}

TEST_F(ConformanceTest, PhysicalKeepsSymlinkType) {
  ExpectXff({"-P"}, {"-type", "l"}, {"link"});
}

TEST_F(ConformanceTest, RegexWholePathTxt) {
  ExpectXff({"-regex", ".*\\.txt"}, {"a.txt", "sub/c.txt", "empty.txt"});
}

TEST_F(ConformanceTest, IRegexWholePathTxt) {
  ExpectXff({"-iregex", ".*\\.TXT"}, {"a.txt", "sub/c.txt", "empty.txt"});
}

// -printf is scoped to -type f so %p/%s/%f cover only the fixed-size regular files
// (directory sizes and the root basename are otherwise environment-dependent).
TEST_F(ConformanceTest, PrintfPathAndSize) {
  EXPECT_THAT(
      Xff({}, {"-type", "f", "-printf", "%p %s\\n"}),
      ElementsAreArray(
          std::vector<std::string>{
              absl::StrCat(Rel("a.txt"), " 1"), absl::StrCat(Rel("b.md"), " 2"), absl::StrCat(Rel("empty.txt"), " 0"),
              absl::StrCat(Rel("sub/c.txt"), " 3")}));
}

TEST_F(ConformanceTest, PrintfNameTypeDepth) {
  EXPECT_THAT(
      Xff({}, {"-type", "f", "-printf", "%f %y %d\\n"}),
      ElementsAreArray(std::vector<std::string>{"a.txt f 1", "b.md f 1", "c.txt f 2", "empty.txt f 1"}));
}

}  // namespace
}  // namespace xff
