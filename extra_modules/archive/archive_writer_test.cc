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

#include "xff/archive/archive_writer.h"

#include <archive.h>
#include <archive_entry.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_reader.h"

namespace xff::archive {
namespace {

namespace stdfs = ::std::filesystem;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::UnorderedElementsAre;

struct FileSpec {
  std::string path;
  std::string content;
};

struct ArchiveWriterTest : ::testing::Test {
  // Writes a real archive to disk with libarchive's write API, so the test needs no committed binary
  // fixture and no external tool. `gzip` adds the gz filter, which is what proves the rewrite carries
  // the container's compression over rather than expanding it.
  static std::string WriteArchive(const std::vector<FileSpec>& files, std::string_view name, bool gzip = false) {
    // ::testing::TempDir() is exactly this test's old hand-rolled TEST_TMPDIR-or-/tmp, minus the
    // std::getenv that clang-tidy flags as thread-unsafe.
    const std::string path = absl::StrCat(::testing::TempDir(), "/", name);
    struct ::archive* out = ::archive_write_new();
    EXPECT_THAT(out != nullptr, IsTrue());
    ::archive_write_set_format_pax_restricted(out);
    if (gzip) {
      ::archive_write_add_filter_gzip(out);
    }
    ::archive_write_open_filename(out, path.c_str());
    for (const FileSpec& file : files) {
      struct ::archive_entry* entry = ::archive_entry_new();
      ::archive_entry_set_pathname(entry, file.path.c_str());
      ::archive_entry_set_size(entry, static_cast<std::int64_t>(file.content.size()));
      ::archive_entry_set_filetype(entry, AE_IFREG);
      ::archive_entry_set_perm(entry, 0644);
      ::archive_write_header(out, entry);
      ::archive_write_data(out, file.content.data(), file.content.size());
      ::archive_entry_free(entry);
    }
    ::archive_write_close(out);
    ::archive_write_free(out);
    return path;
  }

  static std::vector<std::string> MemberNames(const std::string& path) {
    std::vector<std::string> names;
    const absl::StatusOr<std::vector<Member>> members = ListMembersOfFile(path);
    EXPECT_THAT(members, IsOk());
    if (members.ok()) {
      for (const Member& member : *members) {
        names.push_back(member.path);
      }
    }
    return names;
  }

  static std::string Bytes(const std::string& path) {
    const std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
  }

  static std::string PlainFile(std::string_view name, std::string_view content) {
    const std::string path = absl::StrCat(::testing::TempDir(), "/", name);
    std::ofstream(path, std::ios::binary).write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
  }
};

TEST_F(ArchiveWriterTest, RemovingAMemberLeavesTheOthersIntact) {
  // The whole contract: the named member is gone, every other member is still there AND still holds
  // its own bytes - a rewrite that lost content would still pass a name-only check.
  const std::string path = WriteArchive(
      {{.path = "one.txt", .content = "one\n"},
       {.path = "two.txt", .content = "two\n"},
       {.path = "three.txt", .content = "3\n"}},
      "r1.tar");
  EXPECT_THAT(RemoveMembersOfFile(path, {"two.txt"}), IsOk());
  EXPECT_THAT(MemberNames(path), UnorderedElementsAre("one.txt", "three.txt"));
  EXPECT_THAT(ReadMemberOfFile(path, "one.txt"), IsOkAndHolds("one\n"));
  EXPECT_THAT(ReadMemberOfFile(path, "three.txt"), IsOkAndHolds("3\n"));
  EXPECT_THAT(ReadMemberOfFile(path, "two.txt"), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(ArchiveWriterTest, SeveralMembersGoInOneRewrite) {
  // A run deletes what its expression matched, which is a set, not one name - and rewriting the
  // container once per member would be both slow and a wider window for an interrupted write.
  const std::string path = WriteArchive(
      {{.path = "a", .content = "a"},
       {.path = "b", .content = "b"},
       {.path = "c", .content = "c"},
       {.path = "d", .content = "d"}},
      "r2.tar");
  EXPECT_THAT(RemoveMembersOfFile(path, {"a", "c"}), IsOk());
  EXPECT_THAT(MemberNames(path), UnorderedElementsAre("b", "d"));
}

TEST_F(ArchiveWriterTest, TheContainerKeepsItsCompression) {
  // A `.tar.gz` that came back as a plain tar would still list correctly, so the check is that the
  // rewritten file is STILL gzip-filtered when read back.
  const std::string path = WriteArchive(
      {{.path = "one.txt", .content = "one\n"}, {.path = "two.txt", .content = "two\n"}}, "r3.tgz",
      /*gzip=*/true);
  EXPECT_THAT(RemoveMembersOfFile(path, {"one.txt"}), IsOk());
  EXPECT_THAT(MemberNames(path), ElementsAre("two.txt"));
  // Gzip's magic, read straight off the file: the rewrite did not expand the container.
  std::ifstream in(path, std::ios::binary);
  std::string magic(2, '\0');
  in.read(magic.data(), 2);
  EXPECT_THAT(magic, std::string("\x1f\x8b", 2));
}

TEST_F(ArchiveWriterTest, AMemberThatIsNotThereIsNotFoundAndChangesNothing) {
  // Half a deletion is worse than none: a typo must leave the archive exactly as it was, so the
  // rewrite is discarded rather than renamed over the original.
  const std::string path =
      WriteArchive({{.path = "one.txt", .content = "one\n"}, {.path = "two.txt", .content = "two\n"}}, "r4.tar");
  const std::uintmax_t before = stdfs::file_size(path);
  EXPECT_THAT(
      RemoveMembersOfFile(path, {"one.txt", "nope.txt"}), StatusIs(absl::StatusCode::kNotFound, HasSubstr("nope.txt")));
  EXPECT_THAT(MemberNames(path), UnorderedElementsAre("one.txt", "two.txt"));
  EXPECT_THAT(stdfs::file_size(path), before);
  EXPECT_THAT(stdfs::exists(path + ".xff-rewrite"), IsFalse());
}

TEST_F(ArchiveWriterTest, MemberNamesMatchTheWayTheReaderMatchesThem) {
  // tar stores the same member as `dir/x` or `./dir/x` depending on how it was written, so the name
  // xff printed has to work here whichever spelling is stored.
  const std::string path =
      WriteArchive({{.path = "./dir/x", .content = "x"}, {.path = "other", .content = "o"}}, "r5.tar");
  EXPECT_THAT(RemoveMembersOfFile(path, {"dir/x"}), IsOk());
  EXPECT_THAT(MemberNames(path), ElementsAre("other"));
}

TEST_F(ArchiveWriterTest, APlainFileIsNotAnArchive) {
  // The same InvalidArgument the reader answers with, so a caller can tell "not an archive" from
  // "an archive I cannot write".
  const std::string path = PlainFile("notes.txt", "just text\n");
  EXPECT_THAT(RemoveMembersOfFile(path, {"one.txt"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveWriterTest, UnrelatedOldScratchNamesRemainUntouched) {
  const std::string path = WriteArchive(
      {{.path = "one.txt", .content = "one\n"}, {.path = "two.txt", .content = "two\n"}}, "blocked-rewrite.tar");
  const std::string rewrite_path = path + ".xff-rewrite";
  ASSERT_THAT(stdfs::create_directory(rewrite_path), IsTrue());

  EXPECT_THAT(RemoveMembersOfFile(path, {"one.txt"}), IsOk());
  EXPECT_THAT(stdfs::is_directory(rewrite_path), IsTrue());
  EXPECT_THAT(MemberNames(path), UnorderedElementsAre("two.txt"));
}

TEST_F(ArchiveWriterTest, RemovingNothingIsNotAWrite) {
  // An empty set is not an error and must not rewrite the file: `-delete` that matched nothing
  // should leave the container's bytes (and its mtime) alone.
  const std::string path = WriteArchive({{.path = "one.txt", .content = "one\n"}}, "r6.tar");
  const std::string before = Bytes(path);
  EXPECT_THAT(RemoveMembersOfFile(path, {}), IsOk());
  EXPECT_THAT(Bytes(path), before);  // byte for byte the same file, not merely an equivalent archive
}

}  // namespace
}  // namespace xff::archive
