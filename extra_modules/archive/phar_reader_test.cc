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

#include "xff/archive/phar_reader.h"

#include <zlib.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_reader.h"

namespace xff::archive {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;

// PHP's own compression flags, so the test names the same bits the format does.
constexpr std::uint32_t kCompressedGz = 0x00001000;

// What may sit between `__HALT_COMPILER();` and the manifest, and what may not.
//
// Every case here was handed to PHP as raw bytes to see whether PHP itself opens the container, and
// the results match php-src's rule exactly (ext/phar/phar.c, `phar_parse_pharfile`): the close tag is
// consumed ONLY as the three-byte sequence `" ?>"` or `"\n?>"` - one single space or one newline -
// followed by at most one line ending. Hence a bare `?>`, a tab, two spaces, a trailing space, a lone
// line ending and `\r\n?>` are all rejected, while the empty tail is the legal minimal spelling. A `\r`
// after the marker is only accepted with its `\n`, mirroring PHP's "if we have an \r we require an \n
// as well".
struct StubTail {
  std::string_view tail;
  bool readable;
};

constexpr std::array kStubTails = std::to_array<StubTail>({
    {.tail = "", .readable = true},
    {.tail = " ?>", .readable = true},
    {.tail = " ?>\n", .readable = true},
    {.tail = " ?>\r\n", .readable = true},
    {.tail = "\n?>", .readable = true},
    {.tail = "\n?>\n", .readable = true},
    {.tail = "\n?>\r\n", .readable = true},
    {.tail = "?>", .readable = false},
    {.tail = "?>\n", .readable = false},
    {.tail = "\t?>\n", .readable = false},
    {.tail = "  ?>\n", .readable = false},
    {.tail = " ?> \n", .readable = false},
    {.tail = "\r\n?>", .readable = false},
    // A `\r` that no `\n` follows: PHP calls this a truncated manifest, so it must not read.
    {.tail = " ?>\r", .readable = false},
    {.tail = "\n?>\r", .readable = false},
    {.tail = " ?>\r\r\n", .readable = false},
    {.tail = "\n", .readable = false},
    {.tail = "\r\n", .readable = false},
    {.tail = " ", .readable = false},
});

// One member to place into a generated phar. `name` is stored verbatim, so a directory is spelled
// with the trailing slash the format uses.
struct PharSpec {
  std::string name;
  std::string content;
  std::uint32_t flags = 0644;
  // The member's UNCOMPRESSED length. Equal to `content.size()` unless the member is compressed, where
  // `content` is the stored (compressed) stream and this is what it must inflate to - the manifest
  // carries both, and the reader uses this one as the output length.
  std::optional<std::size_t> uncompressed_size;
};

struct PharReaderTest : ::testing::Test {
  static void AppendUint32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>(value >> 8U & 0xFFU));
    out.push_back(static_cast<char>(value >> 16U & 0xFFU));
    out.push_back(static_cast<char>(value >> 24U & 0xFFU));
  }

  static void AppendLengthPrefixed(std::string& out, std::string_view value) {
    AppendUint32(out, static_cast<std::uint32_t>(value.size()));
    out.append(value);
  }

  // Raw deflate (windowBits = -15: no zlib or gzip wrapper), the encoding PHP stores a compressed phar
  // member as. Compressing here rather than committing a blob keeps the fixture readable and makes the
  // round trip - this test deflates, the reader inflates - the actual assertion.
  static std::string RawDeflate(std::string_view content) {
    ::z_stream stream{};
    if (::deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
      return {};
    }
    // The output buffer is ours, so it lives in zlib's own element type (see phar_reader.cc); only
    // the foreign input still needs the char/uchar cast plus a const_cast for the non-const next_in.
    std::vector<::Bytef> out(content.size() + 64);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-const-cast)
    stream.next_in = reinterpret_cast<::Bytef*>(const_cast<char*>(content.data()));
    stream.avail_in = static_cast<::uInt>(content.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<::uInt>(out.size());
    const int status = ::deflate(&stream, Z_FINISH);
    const std::size_t produced = out.size() - stream.avail_out;
    ::deflateEnd(&stream);
    if (status != Z_STREAM_END) {
      return {};
    }
    out.resize(produced);
    return {out.begin(), out.end()};
  }

  // Builds a native phar by hand. There is no phar writer to lean on (that is the point of the
  // reader), so the test IS the format specification: stub, 4-byte manifest length, manifest,
  // then the member data in manifest order.
  static std::string MakePhar(
      const std::vector<PharSpec>& members,
      std::string_view stub = "<?php __HALT_COMPILER(); ?>\n",
      std::string_view alias = "",
      std::uint32_t mtime = 1'700'000'000) {
    std::string manifest;
    AppendUint32(manifest, static_cast<std::uint32_t>(members.size()));
    manifest.push_back('\x11');  // API version 1.1.1, nibble-packed big-endian
    manifest.push_back('\x10');
    AppendUint32(manifest, 0);  // global flags
    AppendLengthPrefixed(manifest, alias);
    AppendLengthPrefixed(manifest, "");  // container metadata
    std::string data;
    for (const PharSpec& member : members) {
      AppendLengthPrefixed(manifest, member.name);
      AppendUint32(manifest, static_cast<std::uint32_t>(member.uncompressed_size.value_or(member.content.size())));
      AppendUint32(manifest, mtime);
      AppendUint32(manifest, static_cast<std::uint32_t>(member.content.size()));  // stored size
      AppendUint32(manifest, 0);                                                  // CRC32, unchecked here
      AppendUint32(manifest, member.flags);
      AppendLengthPrefixed(manifest, "");  // member metadata
      data.append(member.content);
    }
    std::string phar(stub);
    AppendUint32(phar, static_cast<std::uint32_t>(manifest.size()));
    phar.append(manifest);
    phar.append(data);
    return phar;
  }

  // The same bytes on disk, for the file-based entry points.
  static std::string WritePhar(const std::vector<PharSpec>& members, std::string_view name) {
    const std::string path = absl::StrCat(::testing::TempDir(), "/", name);
    const std::string bytes = MakePhar(members);
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  static std::string WriteBytes(std::string_view bytes, std::string_view name) {
    std::string path = absl::StrCat(::testing::TempDir(), "/", name);
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
  }
};

TEST_F(PharReaderTest, ListsMembersWithTheirPathsSizesModesAndTimes) {
  const std::string phar = MakePhar({
      {.name = "bin/run.php", .content = "<?php echo 1;"},
      {.name = "lib/x.txt", .content = "xyz", .flags = 0600},
  });
  EXPECT_THAT(
      ListPharMembers(phar),
      IsOkAndHolds(ElementsAre(
          AllOf(
              Field("path", &Member::path, "bin/run.php"), Field("size", &Member::size, 13),
              Field("mode", &Member::mode, 0644), Field("mtime", &Member::mtime, 1'700'000'000)),
          AllOf(
              Field("path", &Member::path, "lib/x.txt"), Field("size", &Member::size, 3),
              Field("mode", &Member::mode, 0600)),
          AllOf(
              Field("path", &Member::path, ".phar/stub.php"), Field("size", &Member::size, 28),
              Field("mode", &Member::mode, 0444), Field("is_directory", &Member::is_directory, IsFalse())))));
}

TEST_F(PharReaderTest, NativeStubIsAReadableSyntheticFile) {
  constexpr std::string_view kStub = "#!/usr/bin/env php\n<?php echo 'boot'; __HALT_COMPILER(); ?>\n";
  const std::string phar = MakePhar({{.name = "a.txt", .content = "a"}}, kStub);

  EXPECT_THAT(ReadPharMember(phar, ".phar/stub.php"), IsOkAndHolds(kStub));
  EXPECT_THAT(ReadPharMember(phar, "./.phar/stub.php"), IsOkAndHolds(kStub));
  EXPECT_THAT(
      ReadPharMember(phar, ".phar/stub.php", kStub.size() - 1),
      StatusIs(absl::StatusCode::kResourceExhausted, HasSubstr("stub.php")));

  const std::string path = WritePhar({{.name = "a.txt", .content = "a"}}, "stub.phar");
  EXPECT_THAT(ReadPharMemberOfFile(path, ".phar/stub.php"), IsOkAndHolds("<?php __HALT_COMPILER(); ?>\n"));
}

TEST_F(PharReaderTest, StoredMemberAtStubPathWinsWithoutADuplicate) {
  const std::string phar = MakePhar({{.name = ".phar/stub.php", .content = "stored member"}});
  EXPECT_THAT(
      ListPharMembers(phar), IsOkAndHolds(ElementsAre(AllOf(
                                 Field("path", &Member::path, ".phar/stub.php"), Field("size", &Member::size, 13),
                                 Field("mode", &Member::mode, 0644)))));
  EXPECT_THAT(ReadPharMember(phar, ".phar/stub.php"), IsOkAndHolds("stored member"));
}

TEST_F(PharReaderTest, ADirectoryMemberIsTheTrailingSlashSpelling) {
  // phar has no is-directory flag bit: the trailing `/` on the stored name IS the marker, and the
  // reported path drops it so member paths compose like any other path.
  const std::string phar = MakePhar({{.name = "lib/", .content = "", .flags = 0755}});
  EXPECT_THAT(
      ListPharMembers(phar),
      IsOkAndHolds(ElementsAre(
          AllOf(
              Field("path", &Member::path, "lib"), Field("is_directory", &Member::is_directory, IsTrue()),
              Field("is_symlink", &Member::is_symlink, IsFalse())),
          Field("path", &Member::path, ".phar/stub.php"))));
}

TEST_F(PharReaderTest, AnAliasAndMetadataFieldsAreSkippedNotMisread) {
  // The alias sits between the flags and the first member, so a reader that ignores its length
  // reads garbage for every member. A non-empty alias pins that it is consumed.
  const std::string phar = MakePhar({{.name = "a.txt", .content = "a"}}, "<?php __HALT_COMPILER(); ?>\n", "app.phar");
  EXPECT_THAT(
      ListPharMembers(phar),
      IsOkAndHolds(ElementsAre(Field("path", &Member::path, "a.txt"), Field("path", &Member::path, ".phar/stub.php"))));
}

TEST_F(PharReaderTest, TheStubTailIsExactlyWhatPhpAccepts) {
  // Both directions matter. Accepting less than PHP means MISSING real phars (the `"\n?>"` spelling was
  // missed until this table existed); accepting more means claiming to read a container PHP itself
  // calls corrupt.
  for (const StubTail& stub : kStubTails) {
    const std::string phar =
        MakePhar({{.name = "a.txt", .content = "a"}}, absl::StrCat("<?php echo 'x';__HALT_COMPILER();", stub.tail));
    const auto members = ListPharMembers(phar);
    if (stub.readable) {
      EXPECT_THAT(
          members, IsOkAndHolds(ElementsAre(
                       Field("path", &Member::path, "a.txt"), Field("path", &Member::path, ".phar/stub.php"))))
          << "tail: " << absl::CEscape(stub.tail);
    } else {
      EXPECT_THAT(members, Not(IsOk())) << "tail: " << absl::CEscape(stub.tail);
    }
  }
}

TEST_F(PharReaderTest, AStubMayBeAnyPhpSourceBeforeTheToken) {
  const std::string phar = MakePhar(
      {{.name = "a.txt", .content = "a"}}, "#!/usr/bin/env php\n<?php require 'bootstrap.php'; __HALT_COMPILER(); ?>");
  EXPECT_THAT(
      ListPharMembers(phar),
      IsOkAndHolds(ElementsAre(Field("path", &Member::path, "a.txt"), Field("path", &Member::path, ".phar/stub.php"))));
}

TEST_F(PharReaderTest, PlainPhpWithoutTheHaltTokenIsNotAPhar) {
  // "Not a phar" must stay distinguishable from "corrupt phar": the walk treats the first as an
  // ordinary file and only reports the second.
  EXPECT_THAT(ListPharMembers("<?php echo 'just a script';\n"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(PharReaderTest, ATruncatedManifestIsCorruption) {
  const std::string phar = MakePhar({{.name = "a.txt", .content = "aaaa"}});
  EXPECT_THAT(
      ListPharMembers(phar.substr(0, phar.size() - 12)), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated")));
}

TEST_F(PharReaderTest, EveryTruncatedManifestPrefixIsRejected) {
  const std::string phar = MakePhar({
      {.name = "first.txt", .content = "one"},
      {.name = "second.txt", .content = "two", .flags = 0600},
  });
  constexpr std::size_t kPayloadSize = 6;
  for (std::size_t size = 0; size < phar.size() - kPayloadSize; ++size) {
    EXPECT_THAT(ListPharMembers(std::string_view(phar).substr(0, size)), Not(IsOk())) << "prefix size " << size;
  }
}

TEST_F(PharReaderTest, AMemberCountTheManifestCannotHoldMeansThisIsNotAPhar) {
  // The member count is a declared number, so it can lie - and a count the declared manifest LENGTH
  // could not possibly hold is the check that makes the halt token insufficient evidence on its own.
  // That matters concretely: a tar- or zip-based phar stores the stub as an ordinary member, so the
  // token appears inside a perfectly good tar, and committing on the token alone reported that tar as
  // a CORRUPT phar - worse than not recognising it, since the walk then reports an error instead of
  // reading the archive. So this is InvalidArgument ("not a phar"), not DataLoss.
  constexpr std::string_view kStub = "<?php __HALT_COMPILER(); ?>\n";
  std::string phar = MakePhar({{.name = "a.txt", .content = "a"}}, kStub);
  // The count is the manifest's first field, so it sits right behind the stub and the 4-byte
  // manifest length; its low byte alone is enough to inflate it.
  phar[kStub.size() + 4] = '\x09';
  EXPECT_THAT(ListPharMembers(phar), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(PharReaderTest, AStrayHaltTokenBeforeTheRealOneIsSkipped) {
  // The corollary of the check above: a stub may legitimately MENTION the token (in a string, or
  // because the file embeds another stub), so detection tries every occurrence and takes the first
  // one actually followed by a manifest.
  const std::string phar = MakePhar(
      {{.name = "a.txt", .content = "a"}},
      "<?php $marker = '__HALT_COMPILER();'; /* not the end */ __HALT_COMPILER(); ?>\n");
  EXPECT_THAT(
      ListPharMembers(phar),
      IsOkAndHolds(ElementsAre(Field("path", &Member::path, "a.txt"), Field("path", &Member::path, ".phar/stub.php"))));
}

TEST_F(PharReaderTest, ReadsMemberContentAtItsOffset) {
  // Members are concatenated in manifest order with no per-member header, so the offset of the
  // SECOND member is only right if the first member's stored size was accounted for.
  const std::string phar = MakePhar({
      {.name = "first.txt", .content = "one"},
      {.name = "second.txt", .content = "two-two"},
  });
  EXPECT_THAT(ReadPharMember(phar, "first.txt"), IsOkAndHolds("one"));
  EXPECT_THAT(ReadPharMember(phar, "second.txt"), IsOkAndHolds("two-two"));
}

TEST_F(PharReaderTest, MemberLookupIsNormalizedOnBothSides) {
  const std::string phar = MakePhar({{.name = "dir/a.txt", .content = "content"}});
  EXPECT_THAT(ReadPharMember(phar, "./dir/a.txt"), IsOkAndHolds("content"));
}

TEST_F(PharReaderTest, ReportsAMissingMemberAsNotFound) {
  const std::string phar = MakePhar({{.name = "a.txt", .content = "a"}});
  EXPECT_THAT(ReadPharMember(phar, "nope.txt"), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(PharReaderTest, ADirectoryMemberHasNoContentToRead) {
  // Found, but nothing to read - and reached through the un-slashed spelling, which is the point of
  // normalizing: a content predicate must not see this as "no such member".
  const std::string phar = MakePhar({{.name = "lib/", .content = "", .flags = 0755}});
  EXPECT_THAT(ReadPharMember(phar, "lib"), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(ReadPharMember(phar, "lib/"), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(PharReaderTest, AMemberWhoseFlagsLieAboutDeflateIsDataLossNotGarbage) {
  // The flags say deflate; the bytes are not a deflate stream. Reporting DataLoss beats handing back
  // whatever fell out: `-grep` would then silently miss a member that does contain the pattern, and a
  // hash would be computed over rubbish. The manifest's uncompressed size is also the OUTPUT length, so
  // a stream that ends early or late is caught rather than truncated silently.
  const std::string phar =
      MakePhar({{.name = "z.txt", .content = "not really deflated", .flags = std::uint32_t{0644} | kCompressedGz}});
  EXPECT_THAT(ReadPharMember(phar, "z.txt"), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("deflate")));
  // Listing it still works: the manifest carries the metadata regardless of the member encoding.
  EXPECT_THAT(
      ListPharMembers(phar),
      IsOkAndHolds(ElementsAre(Field("path", &Member::path, "z.txt"), Field("path", &Member::path, ".phar/stub.php"))));
}

TEST_F(PharReaderTest, ARealDeflatedMemberDecompresses) {
  // The positive case at reader level, with a stream this test compresses itself: raw deflate, no zlib
  // or gzip wrapper, which is what PHP writes into a phar.
  const std::string content = "findable-needle in a compressed phar member\n";
  const std::string deflated = RawDeflate(content);
  ASSERT_THAT(deflated.empty(), IsFalse());
  const std::string phar = MakePhar({{
      .name = "z.txt",
      .content = deflated,
      .flags = std::uint32_t{0644} | kCompressedGz,
      .uncompressed_size = content.size(),
  }});
  EXPECT_THAT(ReadPharMember(phar, "z.txt"), IsOkAndHolds(content));
}

TEST_F(PharReaderTest, EnforcesTheByteLimit) {
  const std::string phar = MakePhar({{.name = "a.txt", .content = "123456"}});
  EXPECT_THAT(ReadPharMember(phar, "a.txt", /*max_bytes=*/2), StatusIs(absl::StatusCode::kResourceExhausted));
  // The limit is inclusive: content exactly at the limit is fine.
  EXPECT_THAT(ReadPharMember(phar, "a.txt", /*max_bytes=*/6), IsOkAndHolds("123456"));
}

TEST_F(PharReaderTest, EnforcesTheByteLimitBeforeAllocatingDeclaredDecompressedSize) {
  const std::string phar = MakePhar({{
      .name = "z.txt",
      .content = RawDeflate("x"),
      .flags = std::uint32_t{0644} | kCompressedGz,
      .uncompressed_size = std::numeric_limits<std::uint32_t>::max(),
  }});
  EXPECT_THAT(
      ReadPharMember(phar, "z.txt", /*max_bytes=*/1UZ * 1'024 * 1'024),
      StatusIs(absl::StatusCode::kResourceExhausted, HasSubstr("byte limit")));
  const std::string path = WritePhar(
      {{
          .name = "z.txt",
          .content = RawDeflate("x"),
          .flags = std::uint32_t{0644} | kCompressedGz,
          .uncompressed_size = std::numeric_limits<std::uint32_t>::max(),
      }},
      "declared-size.phar");
  EXPECT_THAT(
      ReadPharMemberOfFile(path, "z.txt", /*max_bytes=*/1UZ * 1'024 * 1'024),
      StatusIs(absl::StatusCode::kResourceExhausted, HasSubstr("byte limit")));
}

TEST_F(PharReaderTest, ReadsAMemberWhoseDataRunsPastTheContainerAsCorruption) {
  std::string phar = MakePhar({{.name = "a.txt", .content = "0123456789"}});
  phar.resize(phar.size() - 4);
  EXPECT_THAT(ReadPharMember(phar, "a.txt"), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(ParsePharLayout(phar), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("data section is truncated")));
}

TEST_F(PharReaderTest, RejectsAnEmptyMemberNameAfterCommittingToTheManifest) {
  constexpr std::string_view kStub = "<?php __HALT_COMPILER(); ?>\n";
  std::string phar = MakePhar({{.name = "", .content = "a"}}, kStub);
  // An empty-name entry is one byte shorter than the smallest plausible entry. Add one metadata byte
  // so the up-front plausibility check commits to this as a phar and the entry parser diagnoses the
  // actual defect.
  ++phar[kStub.size()];
  constexpr std::size_t kMetadataLengthInManifest = 42;
  constexpr std::size_t kMetadataInManifest = 46;
  phar[kStub.size() + 4 + kMetadataLengthInManifest] = '\1';
  phar.insert(kStub.size() + 4 + kMetadataInManifest, 1, 'x');

  EXPECT_THAT(ListPharMembers(phar), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("empty name")));
}

TEST_F(PharReaderTest, RejectsAnUnknownMemberCompressionMethod) {
  constexpr std::uint32_t kUnknownCompression = 0x00003000;
  const std::string phar =
      MakePhar({{.name = "a.txt", .content = "a", .flags = std::uint32_t{0644} | kUnknownCompression}});
  EXPECT_THAT(
      ReadPharMember(phar, "a.txt"),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("unknown compression method")));
}

TEST_F(PharReaderTest, FileReadDetectsMemberDataTruncatedAfterItsManifest) {
  std::string phar = MakePhar({{.name = "a.txt", .content = "content"}});
  phar.resize(phar.size() - 3);
  const std::string path = WriteBytes(phar, "truncated-member.phar");
  EXPECT_THAT(ReadPharMemberOfFile(path, "a.txt"), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("past the end")));
}

TEST_F(PharReaderTest, TheFileEntryPointsSeeTheSameContainer) {
  const std::string path = WritePhar(
      {
          {.name = "bin/", .content = "", .flags = 0755},
          {.name = "bin/run.php", .content = "<?php echo 'hi';"},
      },
      "app.phar");
  EXPECT_THAT(
      ListPharMembersOfFile(path), IsOkAndHolds(ElementsAre(
                                       Field("path", &Member::path, "bin"), Field("path", &Member::path, "bin/run.php"),
                                       Field("path", &Member::path, ".phar/stub.php"))));
  EXPECT_THAT(ReadPharMemberOfFile(path, "bin/run.php"), IsOkAndHolds("<?php echo 'hi';"));
  EXPECT_THAT(ReadPharMemberOfFile(path, "bin"), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(ReadPharMemberOfFile(path, "missing"), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(PharReaderTest, AMissingFileIsNotAPhar) {
  EXPECT_THAT(
      ListPharMembersOfFile(absl::StrCat(::testing::TempDir(), "/does-not-exist.phar")),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::archive
