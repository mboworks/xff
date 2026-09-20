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

#include "xff/archive/archive_fs.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "archive.h"
#include "archive_entry.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::archive {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::Range;
using ::testing::SizeIs;
using ::testing::TestParamInfo;
using ::testing::UnorderedElementsAre;

// Writes a tar whose directories are DELIBERATELY not stored: only `dir/sub/deep.txt` and
// `top.txt`. A real tar stream often looks exactly like this, and it is what makes synthesized
// parents load-bearing - without them a walk finds nothing to descend into.
// A path in the test's own temp dir, so nothing leaks between runs or between tests.
std::string TempPath(std::string_view name) {
  return absl::StrCat(::testing::TempDir(), "/", name);  // TEST_TMPDIR or /tmp, without the getenv
}

std::string WriteFixtureTar() {
  const std::string path = TempPath("archive_fs_fixture.tar");
  // `::archive` on purpose: unqualified `archive` resolves to THIS namespace (xff::archive).
  ::archive* out = archive_write_new();
  archive_write_set_format_pax_restricted(out);
  archive_write_open_filename(out, path.c_str());

  const auto add_file = [out](const char* name, std::string_view contents, mode_t mode) {
    ::archive_entry* entry = archive_entry_new();
    archive_entry_set_pathname(entry, name);
    archive_entry_set_size(entry, static_cast<int64_t>(contents.size()));
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, mode);
    archive_entry_set_mtime(entry, 1'700'000'000, 0);
    archive_write_header(out, entry);
    archive_write_data(out, contents.data(), contents.size());
    archive_entry_free(entry);
  };
  add_file("top.txt", "top", 0644);
  add_file("./prefixed.txt", "prefix", 0644);
  add_file("dir/sub/deep.txt", "deep!", 0600);

  // A symlink member, so ReadLink has something real to resolve.
  ::archive_entry* link = archive_entry_new();
  archive_entry_set_pathname(link, "dir/link");
  archive_entry_set_filetype(link, AE_IFLNK);
  archive_entry_set_symlink(link, "sub/deep.txt");
  archive_entry_set_perm(link, 0777);
  archive_write_header(out, link);
  archive_entry_free(link);

  archive_write_close(out);
  archive_write_free(out);
  return path;
}

std::string TruncatedPhar() {
  std::string phar = "<?php __HALT_COMPILER(); ?>\n";
  phar.append("\x13\0\0\0", 4);  // Declares a 19-byte manifest.
  phar.append("\0\0\0\0", 4);    // No members.
  phar.append("\x11\x10", 2);    // API version 1.1.1.
  phar.append(12, '\0');         // Flags and two empty length-prefixed fields: only 18 bytes.
  return phar;
}

struct ArchiveFsTest : ::testing::Test {
  // Built once for the whole suite (WriteFixtureTar touches the disk); the function-local static
  // owns the path for the rest of the process without a leaked allocation.
  static const std::string& Tar() {
    static const std::string tar = WriteFixtureTar();
    return tar;
  }

  // Every test opens its own filesystem: Open() indexes eagerly, so this is cheap and keeps the
  // tests independent.
  static ArchiveFileSystem Fs(MemberPathOptions options = {}) {
    absl::StatusOr<ArchiveFileSystem> fs = ArchiveFileSystem::Open(Tar(), options);
    CHECK_OK(fs) << "opening the fixture tar must succeed";
    return std::move(*fs);
  }
};

TEST_F(ArchiveFsTest, OpenRejectsSomethingThatIsNotAnArchive) {
  // A file with non-archive CONTENT: libarchive rejects it and Open passes that status through
  // unchanged, so "not an archive" stays distinguishable from "corrupt archive".
  const std::string junk = TempPath("not_an_archive.txt");
  std::ofstream(junk) << "this is plain text, not an archive at all\n";
  EXPECT_THAT(ArchiveFileSystem::Open(junk), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveFsTest, OpenPreservesCorruptionReportedByTheNativePharReader) {
  const std::string path = TempPath("truncated.phar");
  const std::string bytes = TruncatedPhar();
  std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  EXPECT_THAT(ArchiveFileSystem::Open(path), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated")));
  EXPECT_THAT(
      ArchiveFileSystem::OpenBytes("nested.phar", bytes),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated")));
}

TEST_F(ArchiveFsTest, AnEmptyFileOpensAsAnArchiveWithNoMembers) {
  // Pinning CURRENT reader behaviour, and flagging an asymmetry rather than hiding it:
  // ListMembers("") rejects empty input as InvalidArgument, but ListMembersOfFile on an empty FILE
  // opens successfully and reports no members (libarchive hits EOF immediately). Whether the file
  // variant should reject empty input the same way is a reader-level question, noted in TODO.md; the
  // backend simply presents whatever the reader returns.
  const std::string empty = TempPath("empty.tar");
  std::ofstream(empty).flush();
  MBO_ASSERT_OK_AND_ASSIGN(const ArchiveFileSystem fs, ArchiveFileSystem::Open(empty));
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<vfs::Entry> entries, fs.ReadDir(empty));
  EXPECT_THAT(entries, IsEmpty());
}

TEST_F(ArchiveFsTest, RepeatedContentReadsAreIdenticalThroughTheCache) {
  // The second read is served from the member cache rather than a fresh decompression pass; what
  // this pins is that the cached bytes ARE the member's bytes, read after read.
  MBO_ASSERT_OK_AND_ASSIGN(const ArchiveFileSystem fs, ArchiveFileSystem::Open(Tar()));
  const std::string member = absl::StrCat(Tar(), "!dir/sub/deep.txt");
  MBO_ASSERT_OK_AND_ASSIGN(const std::string first, fs.ReadContent(member));
  MBO_ASSERT_OK_AND_ASSIGN(const std::string second, fs.ReadContent(member));
  EXPECT_THAT(second, Eq(first));
  EXPECT_THAT(fs.ReadContent(member), IsOkAndHolds(Eq(first)));
}

TEST_F(ArchiveFsTest, TheContainerItselfReadsAsTheArchiveRoot) {
  // A walk enters the archive by ReadDir'ing the container path, so that must list the top level -
  // including `dir`, which the tar never stored as an entry.
  const ArchiveFileSystem fs = Fs();
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<vfs::Entry> entries, fs.ReadDir(ArchiveFsTest::Tar()));
  EXPECT_THAT(
      entries, UnorderedElementsAre(
                   Field("name", &vfs::Entry::name, "dir"), Field("name", &vfs::Entry::name, "prefixed.txt"),
                   Field("name", &vfs::Entry::name, "top.txt")));
}

TEST_F(ArchiveFsTest, ImplicitParentDirectoriesAreSynthesized) {
  // `dir` and `dir/sub` have no stored entries; without synthesis the deep member is unreachable.
  const ArchiveFileSystem fs = Fs();
  EXPECT_THAT(fs.Stat(JoinMemberPath(ArchiveFsTest::Tar(), "dir"), false), IsOk());
  EXPECT_THAT(fs.Stat(JoinMemberPath(ArchiveFsTest::Tar(), "dir/sub"), false), IsOk());
  MBO_ASSERT_OK_AND_ASSIGN(const vfs::Metadata dir, fs.Stat(JoinMemberPath(ArchiveFsTest::Tar(), "dir"), false));
  EXPECT_THAT(dir.type, vfs::FileType::kDirectory);

  MBO_ASSERT_OK_AND_ASSIGN(
      const std::vector<vfs::Entry> sub, fs.ReadDir(JoinMemberPath(ArchiveFsTest::Tar(), "dir/sub")));
  EXPECT_THAT(sub, ElementsAre(Field("name", &vfs::Entry::name, "deep.txt")));
}

TEST_F(ArchiveFsTest, EveryMemberIsReadOnlyAndTaggedAsAnArchiveMember) {
  // This pair is what makes -delete and the exec family REFUSE members instead of skipping them.
  const ArchiveFileSystem fs = Fs();
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<vfs::Entry> entries, fs.ReadDir(ArchiveFsTest::Tar()));
  for (const vfs::Entry& entry : entries) {
    EXPECT_THAT(entry.read_only, IsTrue()) << entry.path;
    EXPECT_THAT(entry.source, vfs::Source::kArchiveMember) << entry.path;
  }
}

TEST_F(ArchiveFsTest, StatReportsTheStoredSizeModeAndType) {
  const ArchiveFileSystem fs = Fs();
  MBO_ASSERT_OK_AND_ASSIGN(
      const vfs::Metadata deep, fs.Stat(JoinMemberPath(ArchiveFsTest::Tar(), "dir/sub/deep.txt"), false));
  EXPECT_THAT(deep.type, vfs::FileType::kRegular);
  EXPECT_THAT(deep.size, 5U);             // "deep!"
  EXPECT_THAT(deep.mode & 0777U, 0600U);  // as stored, not as the host would default
}

TEST_F(ArchiveFsTest, RemoveRefusesRatherThanSilentlySucceeding) {
  // Silent success would make `-delete` look like it worked on a member.
  const ArchiveFileSystem fs = Fs();
  EXPECT_THAT(
      fs.Remove(JoinMemberPath(ArchiveFsTest::Tar(), "top.txt")), StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(ArchiveFsTest, AccessIsNeverWritableAndFollowsTheStoredBits) {
  const ArchiveFileSystem fs = Fs();
  const std::string top = JoinMemberPath(ArchiveFsTest::Tar(), "top.txt");
  EXPECT_THAT(fs.Access(top, vfs::AccessMode::kRead), IsTrue());
  EXPECT_THAT(fs.Access(top, vfs::AccessMode::kWrite), IsFalse());
  // 0644 has no execute bit, so this is the stored answer rather than a blanket yes.
  EXPECT_THAT(fs.Access(top, vfs::AccessMode::kExecute), IsFalse());
  EXPECT_THAT(fs.Access(ArchiveFsTest::Tar(), vfs::AccessMode::kWrite), IsFalse());
  EXPECT_THAT(fs.Access(ArchiveFsTest::Tar(), vfs::AccessMode::kRead), IsTrue());
  EXPECT_THAT(fs.Access("/outside/archive", vfs::AccessMode::kRead), IsFalse());
  EXPECT_THAT(fs.Access(JoinMemberPath(ArchiveFsTest::Tar(), "missing"), vfs::AccessMode::kRead), IsFalse());
}

TEST_F(ArchiveFsTest, ReadLinkResolvesASymlinkMemberAndRefusesOthers) {
  const ArchiveFileSystem fs = Fs();
  EXPECT_THAT(fs.ReadLink(JoinMemberPath(ArchiveFsTest::Tar(), "dir/link")), IsOk());
  MBO_ASSERT_OK_AND_ASSIGN(const std::string target, fs.ReadLink(JoinMemberPath(ArchiveFsTest::Tar(), "dir/link")));
  EXPECT_THAT(target, "sub/deep.txt");
  EXPECT_THAT(
      fs.ReadLink(JoinMemberPath(ArchiveFsTest::Tar(), "top.txt")), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(fs.ReadLink(ArchiveFsTest::Tar()), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(fs.ReadLink(JoinMemberPath(ArchiveFsTest::Tar(), "missing")), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(ArchiveFsTest, ReadContentYieldsTheMembersOwnBytes) {
  // The read that makes -grep / -content / {hash} work on a member: the bytes come out of the
  // container, addressed by the same member path the listing printed.
  const ArchiveFileSystem fs = Fs();
  EXPECT_THAT(fs.ReadContent(JoinMemberPath(ArchiveFsTest::Tar(), "top.txt")), IsOkAndHolds("top"));
  EXPECT_THAT(fs.ReadContent(JoinMemberPath(ArchiveFsTest::Tar(), "dir/sub/deep.txt")), IsOkAndHolds("deep!"));
}

TEST_F(ArchiveFsTest, ReadContentDistinguishesItsThreeRefusals) {
  // Each answer is a different question the caller may need to tell apart: a path that is not in
  // this container at all, a member that does not exist, and one that exists with nothing to read
  // (a synthesized directory). None of them may come back as empty content, which would make a
  // content predicate silently match nothing.
  const ArchiveFileSystem fs = Fs();
  EXPECT_THAT(fs.ReadContent("/elsewhere/other.txt"), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(fs.ReadContent(JoinMemberPath(ArchiveFsTest::Tar(), "nope.txt")), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(
      fs.ReadContent(JoinMemberPath(ArchiveFsTest::Tar(), "dir/sub")), StatusIs(absl::StatusCode::kFailedPrecondition));
  // The container itself is a directory here, so it has no content of its own either.
  EXPECT_THAT(fs.ReadContent(ArchiveFsTest::Tar()), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(ArchiveFsTest, OpenBytesIndexesAContainerThatHasNoPath) {
  // A container nested in another one: its bytes come from its parent, and `container` is only the
  // label its members render under. Everything else - listing, stat, content - behaves as on disk.
  std::ifstream in(ArchiveFsTest::Tar(), std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  MBO_ASSERT_OK_AND_ASSIGN(const ArchiveFileSystem fs, ArchiveFileSystem::OpenBytes("outer.tar!nested.tar", bytes));
  EXPECT_THAT(
      fs.ReadDir("outer.tar!nested.tar"),
      IsOkAndHolds(UnorderedElementsAre(
          Field("name", &vfs::Entry::name, "dir"), Field("name", &vfs::Entry::name, "prefixed.txt"),
          Field("name", &vfs::Entry::name, "top.txt"))));
  EXPECT_THAT(fs.ReadContent("outer.tar!nested.tar!top.txt"), IsOkAndHolds("top"));
  // The nested label contains a separator itself, so resolving a member must not attribute it to
  // the outer archive by splitting at the first one.
  EXPECT_THAT(fs.ReadContent("outer.tar!other.txt"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveFsTest, FsTypeAndCaseSensitivityDescribeTheArchiveNotTheHost) {
  const ArchiveFileSystem fs = Fs();
  MBO_ASSERT_OK_AND_ASSIGN(const std::string type, fs.FsType(ArchiveFsTest::Tar()));
  EXPECT_THAT(type, "archive");
  // Stored names are bytes, so this is true even on a case-insensitive host filesystem.
  MBO_ASSERT_OK_AND_ASSIGN(const bool sensitive, fs.IsCaseSensitive(ArchiveFsTest::Tar()));
  EXPECT_THAT(sensitive, IsTrue());
}

TEST_F(ArchiveFsTest, APathOutsideThisContainerIsRejected) {
  const ArchiveFileSystem fs = Fs();
  EXPECT_THAT(fs.Stat("/etc/passwd", false), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(fs.ReadDir("other.tar!dir"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveFsTest, TheConfiguredSeparatorIsUsedForBothRenderingAndParsing) {
  // The spelling is under user control (--archive-separator), so the backend must both EMIT and
  // ACCEPT it - a path it printed has to be one it can stat back.
  const ArchiveFileSystem fs = Fs({.separator = "#"});
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<vfs::Entry> entries, fs.ReadDir(ArchiveFsTest::Tar()));
  for (const vfs::Entry& entry : entries) {
    EXPECT_THAT(entry.path, HasSubstr("#")) << entry.path;
    EXPECT_THAT(fs.Stat(entry.path, false), IsOk()) << entry.path;
  }
}

TEST_F(ArchiveFsTest, AMissingMemberIsNotFoundNotAnEmptyListing) {
  const ArchiveFileSystem fs = Fs();
  EXPECT_THAT(fs.Stat(JoinMemberPath(ArchiveFsTest::Tar(), "nope.txt"), false), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(fs.ReadDir(JoinMemberPath(ArchiveFsTest::Tar(), "nope")), StatusIs(absl::StatusCode::kNotFound));
  // A file is not a directory, and saying so beats returning nothing.
  EXPECT_THAT(
      fs.ReadDir(JoinMemberPath(ArchiveFsTest::Tar(), "top.txt")), StatusIs(absl::StatusCode::kInvalidArgument));
}

// The fixture's members, stored and synthesized alike - every one must carry an identity.
constexpr std::array kIdentifiedMembers = std::to_array<std::string_view>({
    "dir",
    "dir/sub",
    "dir/sub/deep.txt",
    "prefixed.txt",
    "top.txt",
});

TEST_F(ArchiveFsTest, EveryMemberGetsItsOwnInodeOnOneSyntheticDevice) {
  // Not cosmetic: the walk's loop detector keys on (dev, ino) and reports "filesystem loop detected"
  // the second time it meets a pair, so members all reporting {0, 0} would make the second directory
  // in any archive look like a cycle. One device per container also makes `-xdev` stop AT the
  // container, which is what a walk that started on a real filesystem should see.
  const ArchiveFileSystem fs = Fs();
  absl::flat_hash_set<std::uint64_t> inodes;
  absl::flat_hash_set<std::uint64_t> devices;
  for (const std::string_view member : kIdentifiedMembers) {
    SCOPED_TRACE(member);
    MBO_ASSERT_OK_AND_ASSIGN(
        const vfs::Metadata metadata, fs.Stat(absl::StrCat(ArchiveFsTest::Tar(), "!", member), false));
    EXPECT_THAT(metadata.ino, Ne(0U)) << member << " has no inode";
    EXPECT_THAT(inodes.insert(metadata.ino).second, IsTrue()) << member << " reuses inode " << metadata.ino;
    devices.insert(metadata.dev);
  }
  // One container is one device, and it must not collide with a real filesystem's - hence the top
  // bit, which real device numbers never carry.
  ASSERT_THAT(devices, SizeIs(1));
  EXPECT_THAT(*devices.begin() >> 63U, Eq(1U));
}

TEST_F(ArchiveFsTest, ASynthesizedParentIsIdentifiedToo) {
  // `dir` and `dir/sub` are NOT stored in the fixture tar - they are synthesized. A synthesized node
  // with no identity would defeat the loop detector just as thoroughly as a stored one.
  const ArchiveFileSystem fs = Fs();
  MBO_ASSERT_OK_AND_ASSIGN(const vfs::Metadata parent, fs.Stat(absl::StrCat(ArchiveFsTest::Tar(), "!dir"), false));
  EXPECT_THAT(parent.ino, Ne(0U));
  EXPECT_THAT(parent.nlink, Eq(1U));
}

// The COMMITTED mini container (`test_data/mini.tar`): 3.5 KiB of raw tar blocks holding
// `hello.txt` and `sub/a.bin`, no compression, so what these tests assert is what a real reader
// produces from real bytes - not what a test double was written to return.
//
// What it pins is the PATH VOCABULARY, which is a contract every consumer of the VFS depends on and
// the FUSE mount (#183) got wrong: a member is spelled `<container><separator><member>`, and that
// spelling is the only one the filesystem answers to. A mount that assembled `<container>/<member>`
// the way a local path is built would fail on every member of every real container - and a fake
// filesystem that joined with `/` would happily agree with it, which is exactly how that bug
// survived its unit test.
struct MiniContainerTest : ::testing::Test {
  static std::string Container() {
    // The BUILD file hands over a fixture's runfiles path; this module's canonical repo directory
    // name is not ours to hard-code.
    // NOLINTNEXTLINE(concurrency-mt-unsafe): bazel's environment, read once, single-threaded test
    const char* const anchor = std::getenv("XFF_ARCHIVE_FIXTURE_ANCHOR");
    CHECK_NE(anchor, nullptr) << "the BUILD file must set XFF_ARCHIVE_FIXTURE_ANCHOR";
    const std::string_view anchor_path(anchor);
    const std::string_view::size_type slash = anchor_path.rfind('/');
    const std::string_view directory = slash == std::string_view::npos ? "." : anchor_path.substr(0, slash);
    return absl::StrCat(directory, "/mini.tar");
  }
};

TEST_F(MiniContainerTest, MembersAreSpelledWithTheSeparatorNotASlash) {
  MBO_ASSERT_OK_AND_ASSIGN(const ArchiveFileSystem fs, ArchiveFileSystem::Open(Container()));
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<vfs::Entry> entries, fs.ReadDir(Container()));
  EXPECT_THAT(
      entries, UnorderedElementsAre(
                   Field("path", &vfs::Entry::path, absl::StrCat(Container(), "!hello.txt")),
                   Field("path", &vfs::Entry::path, absl::StrCat(Container(), "!sub"))));
}

TEST_F(MiniContainerTest, TheReportedPathIsTheOneTheFilesystemAnswersTo) {
  MBO_ASSERT_OK_AND_ASSIGN(const ArchiveFileSystem fs, ArchiveFileSystem::Open(Container()));
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<vfs::Entry> entries, fs.ReadDir(Container()));
  for (const vfs::Entry& entry : entries) {
    SCOPED_TRACE(entry.path);
    EXPECT_THAT(fs.Stat(entry.path, /*follow_symlinks=*/false), IsOk());
  }
  EXPECT_THAT(fs.ReadContent(absl::StrCat(Container(), "!hello.txt")), IsOkAndHolds(Eq("hello, mount\n")));
}

TEST_F(MiniContainerTest, ASlashJoinedMemberPathIsNotAMember) {
  // The mount bug in one assertion: build a child path the way a local filesystem does and the
  // container does not know it. The code is InvalidArgument rather than NotFound on purpose - the
  // path is not a member path of this container AT ALL (no separator), which is a different fault
  // from naming a member that happens to be absent.
  MBO_ASSERT_OK_AND_ASSIGN(const ArchiveFileSystem fs, ArchiveFileSystem::Open(Container()));
  EXPECT_THAT(
      fs.Stat(absl::StrCat(Container(), "/hello.txt"), /*follow_symlinks=*/false),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(MiniContainerTest, ANestedMemberKeepsItsInnerSlashesAfterTheSeparator) {
  // Only the CONTAINER boundary uses the separator; directories inside keep ordinary slashes, so a
  // mount must split once and never re-join.
  MBO_ASSERT_OK_AND_ASSIGN(const ArchiveFileSystem fs, ArchiveFileSystem::Open(Container()));
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<vfs::Entry> entries, fs.ReadDir(absl::StrCat(Container(), "!sub")));
  EXPECT_THAT(entries, ElementsAre(Field("path", &vfs::Entry::path, absl::StrCat(Container(), "!sub/a.bin"))));
  EXPECT_THAT(fs.ReadContent(absl::StrCat(Container(), "!sub/a.bin")), IsOkAndHolds(Eq("abc")));
}

// The BREADTH fixture (`test_data/many.tar.gz`): 1000 members across 10 directories, 12.5 KiB
// gzipped. Every file's content is DERIVABLE FROM ITS NAME (`d03/f0123.txt` holds
// `xff-verify 0123`), so a test asserts all thousand reads without a golden list - it proves the
// reader returns the right bytes, not merely that it returns some. mini.tar pins the path
// vocabulary; this one pins that a container of real size reads correctly end to end.
std::string ManyMembersContainer() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): bazel's environment, read once, single-threaded test
  const char* const anchor = std::getenv("XFF_ARCHIVE_FIXTURE_ANCHOR");
  CHECK_NE(anchor, nullptr) << "the BUILD file must set XFF_ARCHIVE_FIXTURE_ANCHOR";
  const std::string_view anchor_path(anchor);
  const std::string_view::size_type slash = anchor_path.rfind('/');
  const std::string_view directory = slash == std::string_view::npos ? "." : anchor_path.substr(0, slash);
  return absl::StrCat(directory, "/many.tar.gz");
}

struct ManyMemberRangesTest : ::testing::TestWithParam<int> {};

struct ManyMembersTest : ::testing::Test {};

TEST_P(ManyMemberRangesTest, EveryMemberReadsBackTheContentItsNamePredicts) {
  const std::string container = ManyMembersContainer();
  MBO_ASSERT_OK_AND_ASSIGN(const ArchiveFileSystem fs, ArchiveFileSystem::Open(container));
  int checked = 0;
  const int begin = GetParam();
  constexpr int kMembersPerRange = 100;
  for (int i = begin; i < begin + kMembersPerRange; ++i) {
    const std::string member = absl::StrFormat("d%02d/f%04d.txt", i % 10, i);
    const std::string path = absl::StrCat(container, "!", member);
    SCOPED_TRACE(path);
    EXPECT_THAT(fs.ReadContent(path), IsOkAndHolds(Eq(absl::StrFormat("xff-verify %04d\n", i))));
    ++checked;
  }
  EXPECT_THAT(checked, Eq(kMembersPerRange));
}

INSTANTIATE_TEST_SUITE_P(MemberRanges, ManyMemberRangesTest, Range(0, 1'000, 100), [](const TestParamInfo<int>& info) {
  return absl::StrFormat("Members%04dThrough%04d", info.param, info.param + 99);
});

TEST_F(ManyMembersTest, TheDirectoriesListTheirOwnHundred) {
  const std::string container = ManyMembersContainer();
  MBO_ASSERT_OK_AND_ASSIGN(const ArchiveFileSystem fs, ArchiveFileSystem::Open(container));
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<vfs::Entry> top, fs.ReadDir(container));
  EXPECT_THAT(top, SizeIs(10));
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<vfs::Entry> one, fs.ReadDir(absl::StrCat(container, "!d03")));
  EXPECT_THAT(one, SizeIs(100));
}

TEST_F(ArchiveFsTest, ContentSourcesRejectDirectoriesAndMissingMembers) {
  const auto fs = Fs();
  EXPECT_THAT(fs.ContentSource(Tar() + "!dir"), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(fs.ContentSource(Tar() + "!missing"), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(fs.ContainerCandidate("outside"), IsFalse());
  EXPECT_THAT(
      ArchiveFileSystem::OpenSource("app.pkg", vfs::MemoryReadSource("pbzx")),
      StatusIs(absl::StatusCode::kUnimplemented));
}

}  // namespace
}  // namespace xff::archive
