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

#include "xff/archive/archive_reader.h"

#include <archive.h>
#include <archive_entry.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_reader_internal.h"
#include "xff/license/notice.h"

namespace xff::archive {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::Optional;
using ::testing::SizeIs;

// One file to place into a generated archive.
struct FileSpec {
  std::string path;
  std::string content;
};

struct ArchiveReaderTest : ::testing::Test {
  // Builds an archive in memory with libarchive's WRITE API, so the test needs no committed binary
  // fixture and exercises the same library end to end. `format` is a libarchive write-format setter
  // ("pax_restricted" tar here); `gzip` adds the gz filter, proving filter detection on read.
  static std::string MakeArchive(const std::vector<FileSpec>& files, bool gzip = false) {
    struct ::archive* out = ::archive_write_new();
    EXPECT_THAT(out != nullptr, IsTrue());
    ::archive_write_set_format_pax_restricted(out);
    if (gzip) {
      ::archive_write_add_filter_gzip(out);
    }
    std::string buffer(std::size_t{256} * 1'024, '\0');
    std::size_t used = 0;
    ::archive_write_open_memory(out, buffer.data(), buffer.size(), &used);
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
    buffer.resize(used);
    return buffer;
  }

  // The same bytes on disk: ReadMemberOfFile / ListMembersOfFile stream from a path, so a file is
  // needed - built from MakeArchive so both entry points see byte-identical input.
  static std::string WriteArchive(const std::vector<FileSpec>& files, std::string_view name) {
    const std::string path = absl::StrCat(::testing::TempDir(), "/", name);
    const std::string bytes = MakeArchive(files);
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  // A real gzip stream on disk, written through libarchive so the test needs no external tool.
  static std::string WriteGzip(std::string_view name, std::string_view content) {
    const std::string path = absl::StrCat(::testing::TempDir(), "/", name);
    struct ::archive* out = ::archive_write_new();
    ::archive_write_set_format_raw(out);
    ::archive_write_add_filter_gzip(out);
    ::archive_write_open_filename(out, path.c_str());
    struct ::archive_entry* entry = ::archive_entry_new();
    ::archive_entry_set_pathname(entry, "data");
    ::archive_entry_set_size(entry, static_cast<std::int64_t>(content.size()));
    ::archive_entry_set_filetype(entry, AE_IFREG);
    ::archive_write_header(out, entry);
    ::archive_write_data(out, content.data(), content.size());
    ::archive_entry_free(entry);
    ::archive_write_close(out);
    ::archive_write_free(out);
    return path;
  }

  static std::string WritePlain(std::string_view name, std::string_view content) {
    const std::string path = absl::StrCat(::testing::TempDir(), "/", name);
    std::ofstream(path, std::ios::binary).write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
  }

  // A tar carrying an explicit DIRECTORY member, for the "no content to read" case.
  static std::string WriteArchiveWithDirectory(std::string_view name) {
    const std::string path = absl::StrCat(::testing::TempDir(), "/", name);
    struct ::archive* out = ::archive_write_new();
    ::archive_write_set_format_pax_restricted(out);
    ::archive_write_open_filename(out, path.c_str());
    struct ::archive_entry* dir = ::archive_entry_new();
    ::archive_entry_set_pathname(dir, "dir");
    ::archive_entry_set_filetype(dir, AE_IFDIR);
    ::archive_entry_set_perm(dir, 0755);
    ::archive_write_header(out, dir);
    ::archive_entry_free(dir);
    ::archive_write_close(out);
    ::archive_write_free(out);
    return path;
  }
};

bool RejectFilters(struct ::archive&) {
  return false;
}

TEST_F(ArchiveReaderTest, ListsTarMembersWithTheirPathsAndSizes) {
  const std::string tar = MakeArchive({{.path = "dir/a.txt", .content = "abc"}, {.path = "b.bin", .content = "xyzw"}});
  EXPECT_THAT(
      ListMembers(tar), IsOkAndHolds(ElementsAre(
                            AllOf(Field("path", &Member::path, "dir/a.txt"), Field("size", &Member::size, 3)),
                            AllOf(Field("path", &Member::path, "b.bin"), Field("size", &Member::size, 4)))));
}

TEST_F(ArchiveReaderTest, DetectsTheCompressionFilterFromContent) {
  // A gzip-filtered tar reads identically: the caller never names the format or the filter, which
  // is what lets one --archive flag cover tar / tgz / zip / ... uniformly.
  const std::string tgz = MakeArchive({{.path = "only.txt", .content = "hello"}}, /*gzip=*/true);
  EXPECT_THAT(
      ListMembers(tgz),
      IsOkAndHolds(ElementsAre(AllOf(Field("path", &Member::path, "only.txt"), Field("size", &Member::size, 5)))));
}

TEST_F(ArchiveReaderTest, ReportsMemberMetadata) {
  const std::string tar = MakeArchive({{.path = "m.txt", .content = "data"}});
  const auto members = ListMembers(tar);
  ASSERT_THAT(members, IsOkAndHolds(SizeIs(1)));
  EXPECT_THAT(members->front().mode, 0644);
  EXPECT_THAT(members->front().is_directory, IsFalse());
  EXPECT_THAT(members->front().is_symlink, IsFalse());
}

TEST_F(ArchiveReaderTest, PlainDataIsNotAnArchive) {
  // "Not an archive" must stay distinguishable from "broken archive": the walk treats the first as
  // an ordinary file and only reports the second.
  EXPECT_THAT(ListMembers("just some text, definitely not an archive"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, FilterRegistrationFailuresAreReported) {
  EXPECT_THAT(
      internal::ListMembersWithFilterEnabler("not empty", RejectFilters),
      StatusIs(absl::StatusCode::kResourceExhausted, HasSubstr("allocate")));
  EXPECT_THAT(
      internal::ReadCompressedSingleFileWithFilterEnabler("missing.gz", 0, RejectFilters),
      StatusIs(absl::StatusCode::kUnavailable, HasSubstr("compression filters")));
}

TEST_F(ArchiveReaderTest, BaseArchiveExtraDoesNotAbsorbBrotliFromLibarchive) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe) Bazel sets this immutable fixture path before the test.
  const char* const fixture = std::getenv("XFF_ARCHIVE_BROTLI_FIXTURE");
  ASSERT_THAT(fixture, Not(nullptr));
  EXPECT_THAT(ListMembersOfFile(fixture), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, EmptyInputIsNotAnArchive) {
  EXPECT_THAT(ListMembers(""), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, TextThatMtreeWouldClaimIsStillNotAnArchive) {
  // mtree is a plain-text format with no magic, so `archive_read_support_format_all` accepts
  // ordinary text as an "archive" whose members are named after its words. That made `xff notes.txt`
  // (the xff family dives a named root by default) invent a member, so the reader enables its
  // formats explicitly and leaves mtree out. Each line below is text a walk really meets.
  constexpr std::array kTextThatIsNotAnArchive = std::to_array<std::string_view>({
      "needle\n",
      "hello world\n",
      "./relative/path/looking/line\n",
      // Even the real mtree preamble stays a text file to us.
      "#mtree\n./notes type=file\n",
  });
  for (const std::string_view text : kTextThatIsNotAnArchive) {
    EXPECT_THAT(ListMembers(text), StatusIs(absl::StatusCode::kInvalidArgument)) << text;
  }
}

TEST_F(ArchiveReaderTest, TheCompressionSuffixNamesTheSingleMemberInside) {
  // `notes.txt.gz` holds `notes.txt` - the same name gzip -d restores, and the only name recoverable
  // from a bare compressed stream, which carries no member list at all.
  EXPECT_THAT(CompressionSuffixStripped("notes.txt.gz"), Optional(std::string("notes.txt")));
  EXPECT_THAT(CompressionSuffixStripped("dump.sql.bz2"), Optional(std::string("dump.sql")));
  EXPECT_THAT(CompressionSuffixStripped("data.XZ"), Optional(std::string("data")));  // case folds
  EXPECT_THAT(CompressionSuffixStripped("source.tar.lz"), Optional(std::string("source.tar")));
  EXPECT_THAT(CompressionSuffixStripped("trace.lz4"), Optional(std::string("trace")));
  EXPECT_THAT(CompressionSuffixStripped("legacy.Z"), Optional(std::string("legacy")));
  // Not single-file compression: a tar shorthand (`.tgz`) is an archive libarchive reads by itself and
  // is deliberately absent from the table, a name that is ONLY a suffix is a dotfile, and everything
  // else has nothing to strip.
  constexpr std::array kNotSingleFileCompressed = std::to_array<std::string_view>({
      "a.tgz",
      "unsupported.br",
      ".gz",
      "notes.txt",
      "Makefile",
  });
  for (const std::string_view name : kNotSingleFileCompressed) {
    EXPECT_THAT(CompressionSuffixStripped(name), Eq(std::nullopt)) << name;
  }
}

TEST_F(ArchiveReaderTest, ACompressedSingleFileReadsWholeAndTextNamedGzDoesNot) {
  // The narrow use of libarchive's `raw` format: it bids on ANYTHING, so it is registered only for a
  // file whose NAME claims compression, and then the read is confirmed by a real filter having applied.
  // A text file called `notes.gz` therefore stays a text file rather than becoming a one-member archive.
  const std::string path = WriteGzip("single.txt.gz", "findable-needle\n");
  EXPECT_THAT(ReadCompressedSingleFile(path), IsOkAndHolds("findable-needle\n"));
  const std::string liar = WritePlain("liar.gz", "just text, no gzip header\n");
  EXPECT_THAT(ReadCompressedSingleFile(liar), StatusIs(absl::StatusCode::kInvalidArgument));
  const std::string unnamed = WriteGzip("no-suffix", "compressed but unnamed\n");
  EXPECT_THAT(ReadCompressedSingleFile(unnamed), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, ACompressedSingleFileHonoursTheByteLimit) {
  const std::string path = WriteGzip("big.txt.gz", "0123456789");
  EXPECT_THAT(ReadCompressedSingleFile(path, 4), StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST_F(ArchiveReaderTest, RegistersTheExtraAndLibraryLicenseNotices) {
  const auto has_component = [](std::string_view name, std::string_view spdx) {
    return Contains(
        AllOf(Field("component", &license::Notice::component, name), Field("spdx", &license::Notice::spdx, spdx)));
  };
  const auto notices = license::Notices();
  EXPECT_THAT(notices, has_component("xff archive extra (@xff_archive)", "Apache-2.0"));
  EXPECT_THAT(notices, has_component("libarchive", "BSD-2-Clause"));
  EXPECT_THAT(notices, has_component("bzip2", "bzip2-1.0.6"));
  EXPECT_THAT(notices, has_component("zlib", "Zlib"));
  EXPECT_THAT(notices, has_component("liblzma (XZ Utils)", "LicenseRef-Public-Domain"));
  EXPECT_THAT(notices, has_component("LZ4 library", "BSD-2-Clause"));
  EXPECT_THAT(notices, has_component("Zstandard", "BSD-3-Clause"));
  EXPECT_THAT(notices, Not(Contains(Field("component", &license::Notice::component, "Mbed TLS"))));

  EXPECT_THAT(license::LicenseBodyFor("BSD-2-Clause"), Not(IsEmpty()));
  EXPECT_THAT(license::LicenseBodyFor("BSD-3-Clause"), Not(IsEmpty()));
  EXPECT_THAT(license::LicenseBodyFor("bzip2-1.0.6"), Not(IsEmpty()));
  EXPECT_THAT(license::LicenseBodyFor("Zlib"), Not(IsEmpty()));
  EXPECT_THAT(license::LicenseBodyFor("LicenseRef-Public-Domain"), Not(IsEmpty()));
}

// ReadMemberOfFile: the entry point the content predicates need. Each error state is distinct on
// purpose, so a caller can tell "no such member" from "member has no content" from "bomb guard".
TEST_F(ArchiveReaderTest, ReadMemberOfFileReturnsTheMemberContent) {
  const std::string tar = WriteArchive({{.path = "hello.txt", .content = "hello\n"}}, "read.tar");
  EXPECT_THAT(ReadMemberOfFile(tar, "hello.txt"), IsOkAndHolds("hello\n"));
}

TEST_F(ArchiveReaderTest, ReadMemberOfFileIgnoresALeadingDotSlashOnEitherSide) {
  // Tar streams write both spellings for the same member, so neither side may be authoritative.
  const std::string tar = WriteArchive({{.path = "hello.txt", .content = "hello\n"}}, "dotslash.tar");
  EXPECT_THAT(ReadMemberOfFile(tar, "./hello.txt"), IsOkAndHolds("hello\n"));
}

TEST_F(ArchiveReaderTest, ReadMemberOfFileReportsAMissingMemberAsNotFound) {
  const std::string tar = WriteArchive({{.path = "hello.txt", .content = "hello\n"}}, "missing.tar");
  EXPECT_THAT(ReadMemberOfFile(tar, "nope.txt"), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(ArchiveReaderTest, ReadMemberOfFileRefusesSomethingWithNoContent) {
  // A directory member: FailedPrecondition, not an empty string - a content predicate could not
  // distinguish an empty string here from a genuinely empty file.
  const std::string tar = WriteArchiveWithDirectory("dir.tar");
  EXPECT_THAT(ReadMemberOfFile(tar, "dir"), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(ArchiveReaderTest, ReadMemberOfFileRejectsANonArchive) {
  EXPECT_THAT(ReadMemberOfFile("/etc/hosts", "anything"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, ReadMemberOfFileEnforcesTheByteLimit) {
  // The bomb guard: a small header can promise a huge expansion, so the LOOP bounds the read rather
  // than trusting the declared size.
  const std::string tar = WriteArchive({{.path = "hello.txt", .content = "hello\n"}}, "limit.tar");
  EXPECT_THAT(ReadMemberOfFile(tar, "hello.txt", /*max_bytes=*/2), StatusIs(absl::StatusCode::kResourceExhausted));
  // The limit is inclusive: content exactly at the limit is fine.
  EXPECT_THAT(ReadMemberOfFile(tar, "hello.txt", /*max_bytes=*/6), IsOkAndHolds("hello\n"));
}

class FailingReadStream final : public vfs::ReadStream {
 public:
  explicit FailingReadStream(std::string prefix) : prefix_(std::move(prefix)) {}

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    if (prefix_.empty()) {
      return absl::DataLossError("injected source failure");
    }
    auto result = prefix_.substr(0, max_bytes);
    prefix_.erase(0, result.size());
    return result;
  }

 private:
  std::string prefix_;
};

class FailingReadSource final : public vfs::ReadSource {
 public:
  explicit FailingReadSource(std::string prefix = {}) : prefix_(std::move(prefix)) {}

  absl::StatusOr<std::unique_ptr<vfs::ReadStream>> Open() const override {
    return std::make_unique<FailingReadStream>(prefix_);
  }

 private:
  std::string prefix_;
};

TEST_F(ArchiveReaderTest, SourceSessionsPreserveOpenAndCallbackErrors) {
  EXPECT_THAT(ListMembersOfSource(vfs::HostReadSource("")), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(
      ListMembersOfSource(std::make_shared<FailingReadSource>()),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("injected source failure")));
  EXPECT_THAT(ListMembersOfSource(vfs::MemoryReadSource("xar!bad")), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(ListMembersOfSource(vfs::MemoryReadSource("")), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(MemberReadSource(vfs::HostReadSource(""), "member")->Open(), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(ArchiveReaderTest, MemberStreamsValidateNamesTypesAndOwnIndependentCursors) {
  const auto source =
      vfs::MemoryReadSource(MakeArchive({{.path = "first", .content = "abc"}, {.path = "second", .content = "def"}}));
  EXPECT_THAT(MemberReadSource(source, "missing")->Open(), StatusIs(absl::StatusCode::kNotFound));
  MBO_ASSERT_OK_AND_ASSIGN(auto stream, MemberReadSource(source, "second")->Open());
  EXPECT_THAT(stream->Read(0), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(stream->Read(2), IsOkAndHolds(Eq("de")));
  EXPECT_THAT(stream->Read(100), IsOkAndHolds(Eq("f")));
  EXPECT_THAT(stream->Read(1), IsOkAndHolds(IsEmpty()));
  const auto directory = vfs::HostReadSource(WriteArchiveWithDirectory("stream-dir.tar"));
  EXPECT_THAT(MemberReadSource(directory, "dir")->Open(), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(ArchiveReaderTest, MemberStreamsReportTruncationAndReadErrorsAfterOpen) {
  const auto tar = MakeArchive({{.path = "large", .content = std::string(128UZ * 1'024, 'a')}});
  const auto broken = std::make_shared<FailingReadSource>(tar.substr(0, 64UZ * 1'024));
  MBO_ASSERT_OK_AND_ASSIGN(auto stream, MemberReadSource(broken, "large")->Open());
  EXPECT_THAT(stream->Read(64UZ * 1'024), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(MemberReadSource(broken, "missing")->Open(), StatusIs(absl::StatusCode::kDataLoss));
  const auto truncated = vfs::MemoryReadSource(tar.substr(0, 64UZ * 1'024));
  MBO_ASSERT_OK_AND_ASSIGN(auto short_stream, MemberReadSource(truncated, "large")->Open());
  EXPECT_THAT(short_stream->Read(64UZ * 1'024), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(MemberReadSource(truncated, "missing")->Open(), StatusIs(absl::StatusCode::kDataLoss));
}

}  // namespace
}  // namespace xff::archive
