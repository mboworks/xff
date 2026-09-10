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

#include "xff/archive/archive_pack.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_reader.h"

namespace xff::archive {
namespace {

using ::testing::IsFalse;
namespace stdfs = ::std::filesystem;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Lt;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

struct ArchivePackTest : ::testing::Test {
  void SetUp() override {
    root_ = stdfs::temp_directory_path()
            / ("xff-pack-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code error;
    stdfs::remove_all(root_, error);
    stdfs::create_directories(root_ / "dir", error);
    Write(root_ / "one.txt", "first\n");
    Write(root_ / "dir" / "two.txt", "second\n");
  }

  void TearDown() override {
    std::error_code error;
    stdfs::remove_all(root_, error);
  }

  static void Write(const stdfs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
  }

  // The two files above, named as a walk rooted at `root_` would name them.
  [[nodiscard]] std::vector<PackEntry> Entries() const {
    return {
        PackEntry{.source = (root_ / "one.txt").string(), .name = "one.txt"},
        PackEntry{.source = (root_ / "dir" / "two.txt").string(), .name = "dir/two.txt"},
    };
  }

  [[nodiscard]] std::string Output(std::string_view name) const { return (root_ / name).string(); }

  [[nodiscard]] static std::string Read(std::string_view path) {
    std::ifstream in(std::string(path), std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
  }

  stdfs::path root_;
};

TEST_F(ArchivePackTest, PackedFilesReadBackWithTheirNamesAndContent) {
  // The round trip is the real assertion: what we wrote is what the reader - the same one xff walks
  // archives with - finds inside.
  const std::string out = Output("packed.tar");
  ASSERT_THAT(PackFiles(out, Entries()), IsOk());
  EXPECT_THAT(
      ListMembersOfFile(out),
      IsOkAndHolds(
          UnorderedElementsAre(Field("path", &Member::path, "one.txt"), Field("path", &Member::path, "dir/two.txt"))));
  EXPECT_THAT(ReadMemberOfFile(out, "one.txt"), IsOkAndHolds(Eq("first\n")));
  EXPECT_THAT(ReadMemberOfFile(out, "dir/two.txt"), IsOkAndHolds(Eq("second\n")));
}

TEST_F(ArchivePackTest, TheOutputNameChoosesTheFormat) {
  // Each suffix produces a container the reader opens; that it reads back at all is what proves the
  // format and filter were set, since a wrong filter yields bytes libarchive cannot parse.
  // Every writable format except the liblzma-backed ones, which live in archive_pack_xz_test so
  // that target alone can be tagged `no_san` (liblzma trips UBSan's `function` check).
  static constexpr std::array kNames = std::to_array<std::string_view>({
      "packed.tar.gz",
      "packed.tar.bz2",
      "packed.tar.zst",
      "packed.tar.lz4",
      "packed.tar.Z",
      "packed.zip",
      // The single-word shortcuts GNU tar recognises, which people type: each must reach the same
      // format its long spelling does. `.tar.xz` and `.txz` live in archive_pack_xz_test (no_san).
      "packed.tgz",
      "packed.tbz2",
      "packed.tbz",
      "packed.tz2",
      "packed.tzst",
      "packed.taZ",
  });
  for (const std::string_view name : kNames) {
    const std::string out = Output(name);
    ASSERT_THAT(PackFiles(out, Entries()), IsOk()) << name;
    EXPECT_THAT(ReadMemberOfFile(out, "dir/two.txt"), IsOkAndHolds(Eq("second\n"))) << name;
  }
}

TEST_F(ArchivePackTest, AnUnknownOutputSuffixIsRefusedAndNamesWhatIsAccepted) {
  // Guessing tar for `out.rar` would produce a file whose name lies about its content.
  EXPECT_THAT(
      PackFiles(Output("packed.rar"), Entries()), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("tar.gz")));
  EXPECT_THAT(PackFiles(Output("packed"), Entries()), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchivePackTest, AMissingSourceLeavesNoHalfArchive) {
  // All or nothing: the first unreadable source aborts the pack, and the output must not exist -
  // a partial archive that looks complete is worse than no archive.
  std::vector<PackEntry> entries = Entries();
  entries.push_back(PackEntry{.source = (root_ / "absent.txt").string(), .name = "absent.txt"});
  const std::string out = Output("packed.tar");
  EXPECT_THAT(PackFiles(out, entries), StatusIs(absl::StatusCode::kNotFound, HasSubstr("absent.txt")));
  EXPECT_THAT(stdfs::exists(out), IsFalse());
}

TEST_F(ArchivePackTest, AnExistingOutputSurvivesAFailedPack) {
  // The rename-over-at-the-end contract: a failing run must not destroy what was already there.
  const std::string out = Output("packed.tar");
  Write(stdfs::path(out), "not an archive, but mine\n");
  std::vector<PackEntry> entries = Entries();
  entries.push_back(PackEntry{.source = (root_ / "absent.txt").string(), .name = "absent.txt"});
  EXPECT_THAT(PackFiles(out, entries), StatusIs(absl::StatusCode::kNotFound));
  std::ifstream in(out);
  const std::string kept((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_THAT(kept, Eq("not an archive, but mine\n"));
}

TEST_F(ArchivePackTest, DirectoriesAndSymlinksKeepTheirType) {
  std::error_code error;
  stdfs::create_symlink("one.txt", root_ / "link", error);
  ASSERT_THAT(error, IsFalse());
  const std::string out = Output("packed.tar");
  std::vector<PackEntry> entries = Entries();
  entries.push_back(PackEntry{.source = (root_ / "dir").string(), .name = "dir"});
  entries.push_back(PackEntry{.source = (root_ / "link").string(), .name = "link"});
  ASSERT_THAT(PackFiles(out, entries), IsOk());
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<Member> members, ListMembersOfFile(out));
  EXPECT_THAT(members, Contains(Field("is_directory", &Member::is_directory, true)));
  EXPECT_THAT(
      members,
      Contains(AllOf(
          Field("is_symlink", &Member::is_symlink, true), Field("link_target", &Member::link_target, "one.txt"))));
}

TEST_F(ArchivePackTest, TheOrderGivenIsTheOrderStored) {
  // Ordering is the caller's business (it honours --sort), so the writer must not reorder.
  const std::string out = Output("packed.tar");
  ASSERT_THAT(PackFiles(out, Entries()), IsOk());
  EXPECT_THAT(
      ListMembersOfFile(out),
      IsOkAndHolds(ElementsAre(Field("path", &Member::path, "one.txt"), Field("path", &Member::path, "dir/two.txt"))));
}

TEST_F(ArchivePackTest, PackingNothingStillProducesAValidEmptyArchive) {
  // An expression that matched nothing is not an error; the archive is simply empty.
  const std::string out = Output("packed.tar");
  ASSERT_THAT(PackFiles(out, {}), IsOk());
  EXPECT_THAT(ListMembersOfFile(out), IsOkAndHolds(IsEmpty()));
}

TEST_F(ArchivePackTest, TheCompressionLevelReachesTheCompressor) {
  // Proven by the only observable a level HAS: the same input compresses smaller at 9 than at 1. A
  // test that merely checked the call succeeded would pass with the option silently dropped.
  Write(root_ / "big.txt", std::string(200'000, 'a') + "\n");
  const std::vector<PackEntry> entries = {PackEntry{.source = (root_ / "big.txt").string(), .name = "big.txt"}};
  const std::string fast = Output("fast.tar.gz");
  const std::string small = Output("small.tar.gz");
  ASSERT_THAT(PackFiles(fast, entries, PackSettings{.options = {{.name = "level", .value = "1"}}}), IsOk());
  ASSERT_THAT(PackFiles(small, entries, PackSettings{.options = {{.name = "level", .value = "9"}}}), IsOk());
  EXPECT_THAT(stdfs::file_size(small), Lt(stdfs::file_size(fast)));
  // And the content survives whichever level was used.
  EXPECT_THAT(ReadMemberOfFile(small, "big.txt"), IsOkAndHolds(SizeIs(200'001)));
}

TEST_F(ArchivePackTest, CompressedAliasesAcceptTheSameOptionsAsTheirLongForms) {
  // Option applicability keys on the resolved format name. Every alias therefore needs coverage:
  // accepting the output suffix alone is insufficient if `--pack-level` or `threads` is then
  // rejected for that alias.
  static constexpr std::array kLevelAliases = std::to_array<std::string_view>({
      "alias.tgz",
      "alias.tbz2",
      "alias.tbz",
      "alias.tz2",
      "alias.tzst",
  });
  for (const std::string_view name : kLevelAliases) {
    EXPECT_THAT(PackFiles(Output(name), Entries(), PackSettings{.options = {{.name = "level", .value = "3"}}}), IsOk())
        << name;
  }
  EXPECT_THAT(
      PackFiles(Output("threaded.tzst"), Entries(), PackSettings{.options = {{.name = "threads", .value = "2"}}}),
      IsOk());
}

TEST_F(ArchivePackTest, AnUnknownOptionNameIsRefusedNamingTheVocabulary) {
  // The whole reason the names are xff's own: one that is not in the table is a usage error rather
  // than something forwarded to a library that may or may not recognise it.
  EXPECT_THAT(
      PackFiles(Output("x.tar.gz"), Entries(), PackSettings{.options = {{.name = "squish", .value = "9"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, AllOf(HasSubstr("squish"), HasSubstr("level"))));
}

TEST_F(ArchivePackTest, AnOptionIsRefusedForAFormatItDoesNotApplyTo) {
  // `zip64` is a zip idea and `timestamp` a gzip one; applying either elsewhere would be a silent
  // no-op, so the refusal names where the option does belong.
  EXPECT_THAT(
      PackFiles(Output("x.tar.gz"), Entries(), PackSettings{.options = {{.name = "zip64", .value = "yes"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, AllOf(HasSubstr("tar.gz"), HasSubstr("zip"))));
  EXPECT_THAT(
      PackFiles(Output("x.zip"), Entries(), PackSettings{.options = {{.name = "threads", .value = "2"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("tar.xz")));
}

TEST_F(ArchivePackTest, AnOptionValueOfTheWrongShapeIsRefused) {
  EXPECT_THAT(
      PackFiles(Output("x.tar.gz"), Entries(), PackSettings{.options = {{.name = "timestamp", .value = "maybe"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("yes or no")));
  EXPECT_THAT(
      PackFiles(Output("x.zip"), Entries(), PackSettings{.options = {{.name = "compression", .value = "lzma"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("store")));
  EXPECT_THAT(
      PackFiles(Output("x.tar.gz"), Entries(), PackSettings{.options = {{.name = "level", .value = "high"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("takes a number")));
}

TEST_F(ArchivePackTest, TurningTheTimestampOffMakesTwoRunsByteIdentical) {
  // The observable proof that a BOOLEAN option is translated correctly: libarchive spells a boolean
  // by presence (`!timestamp`), so a value forwarded as `timestamp=no` would silently mean "on".
  const std::string first = Output("first.tar.gz");
  const std::string second = Output("second.tar.gz");
  const PackSettings no_timestamp{.options = {{.name = "timestamp", .value = "no"}}};
  ASSERT_THAT(PackFiles(first, Entries(), no_timestamp), IsOk());
  ASSERT_THAT(PackFiles(second, Entries(), no_timestamp), IsOk());
  EXPECT_THAT(Read(first), Eq(Read(second)));
}

TEST_F(ArchivePackTest, Zip64CanBeExplicitlyEnabled) {
  const std::string out = Output("zip64.zip");
  EXPECT_THAT(PackFiles(out, Entries(), PackSettings{.options = {{.name = "zip64", .value = "yes"}}}), IsOk());
  EXPECT_THAT(ListMembersOfFile(out), IsOkAndHolds(SizeIs(2)));
}

TEST_F(ArchivePackTest, ZipStoresMembersUncompressedWhenAsked) {
  // The enum option, proven by size: `store` must leave the payload alone.
  Write(root_ / "big.txt", std::string(200'000, 'a') + "\n");
  const std::vector<PackEntry> entries = {PackEntry{.source = (root_ / "big.txt").string(), .name = "big.txt"}};
  const std::string stored = Output("stored.zip");
  const std::string deflated = Output("deflated.zip");
  ASSERT_THAT(PackFiles(stored, entries, PackSettings{.options = {{.name = "compression", .value = "store"}}}), IsOk());
  ASSERT_THAT(
      PackFiles(deflated, entries, PackSettings{.options = {{.name = "compression", .value = "deflate"}}}), IsOk());
  EXPECT_THAT(stdfs::file_size(deflated), Lt(stdfs::file_size(stored)));
  EXPECT_THAT(ReadMemberOfFile(stored, "big.txt"), IsOkAndHolds(SizeIs(200'001)));
}

TEST_F(ArchivePackTest, TheLastValueForAnOptionNameWins) {
  Write(root_ / "big.txt", std::string(200'000, 'a') + "\n");
  const std::vector<PackEntry> entries = {PackEntry{.source = (root_ / "big.txt").string(), .name = "big.txt"}};
  const std::string mixed = Output("mixed.tar.gz");
  const std::string plain = Output("plain.tar.gz");
  ASSERT_THAT(
      PackFiles(
          mixed, entries, PackSettings{.options = {{.name = "level", .value = "1"}, {.name = "level", .value = "9"}}}),
      IsOk());
  ASSERT_THAT(PackFiles(plain, entries, PackSettings{.options = {{.name = "level", .value = "9"}}}), IsOk());
  EXPECT_THAT(stdfs::file_size(mixed), Eq(stdfs::file_size(plain)));
}

TEST_F(ArchivePackTest, ARejectedOptionWritesNothing) {
  const std::string out = Output("x.tar.gz");
  EXPECT_THAT(
      PackFiles(out, Entries(), PackSettings{.options = {{.name = "squish", .value = "9"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(stdfs::exists(out), IsFalse());
}

TEST_F(ArchivePackTest, ALevelOutOfTheFormatsRangeIsRefusedNamingTheRange) {
  // libarchive answers an out-of-range level with "Undefined option", which sends the reader looking
  // for a misspelled flag; the range check has to happen here.
  EXPECT_THAT(
      PackFiles(Output("x.tar.gz"), Entries(), PackSettings{.options = {{.name = "level", .value = "99"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("accepts 0-9")));
  EXPECT_THAT(
      PackFiles(Output("x.tar.zst"), Entries(), PackSettings{.options = {{.name = "level", .value = "99"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("accepts 1-22")));
  EXPECT_THAT(
      PackFiles(Output("x.tar.gz"), Entries(), PackSettings{.options = {{.name = "level", .value = "-1"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("accepts 0-9")));
}

TEST_F(ArchivePackTest, ALevelOnAnUncompressedFormatIsAnErrorNotANoOp) {
  // A level that silently did nothing reads as a smaller archive that never arrives. `tar` is simply
  // not among the formats the option applies to, and the refusal says which ones are.
  EXPECT_THAT(
      PackFiles(Output("x.tar"), Entries(), PackSettings{.options = {{.name = "level", .value = "9"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, AllOf(HasSubstr("does not apply to tar"), HasSubstr("tar.gz"))));
  // Zip compresses, so it takes one.
  EXPECT_THAT(
      PackFiles(Output("x.zip"), Entries(), PackSettings{.options = {{.name = "level", .value = "9"}}}), IsOk());
}

TEST_F(ArchivePackTest, ThePackedMemberKeepsItsModificationTime) {
  // Without it every member unpacks stamped 1970, which breaks the make / rsync comparisons an
  // archive of source files exists for.
  const std::string out = Output("stamped.tar");
  ASSERT_THAT(PackFiles(out, Entries()), IsOk());
  EXPECT_THAT(ListMembersOfFile(out), IsOkAndHolds(Each(Field("mtime", &Member::mtime, Gt(0)))));
}

struct PackFormatsTest : ::testing::Test {};

TEST_F(PackFormatsTest, TheSingleWordShortcutsResolveLikeTheirLongSpellings) {
  // `.tgz` was the only shortcut the writer knew, so `--pack=x.txz` was refused as if it named no
  // format at all. These are the abbreviations GNU tar itself recognises.
  EXPECT_THAT(FormatFromName("a.tgz"), Eq("tgz"));
  EXPECT_THAT(FormatFromName("a.txz"), Eq("txz"));
  EXPECT_THAT(FormatFromName("a.tbz2"), Eq("tbz2"));
  EXPECT_THAT(FormatFromName("a.tbz"), Eq("tbz"));
  EXPECT_THAT(FormatFromName("a.tz2"), Eq("tz2"));
  EXPECT_THAT(FormatFromName("a.tzst"), Eq("tzst"));
  EXPECT_THAT(FormatFromName("a.taZ"), Eq("taZ"));
  EXPECT_THAT(FormatFromName("a.tlz"), Eq("tlz"));
}

TEST_F(PackFormatsTest, FormatFromNameIsCaseInsensitiveAndPrefersTheLongestSuffix) {
  EXPECT_THAT(FormatFromName("a.tar.gz"), Eq("tar.gz"));  // not "gz" and not "tar"
  EXPECT_THAT(FormatFromName("A.TAR.GZ"), Eq("tar.gz"));
  EXPECT_THAT(FormatFromName("A.TAR.Z"), Eq("tar.Z"));
  EXPECT_THAT(FormatFromName("a.tar.lz"), Eq("tar.lz"));
  EXPECT_THAT(FormatFromName("a.tar.lzma"), Eq("tar.lzma"));
  EXPECT_THAT(FormatFromName("a.tar.lz4"), Eq("tar.lz4"));
  EXPECT_THAT(FormatFromName("a.zip"), Eq("zip"));
  EXPECT_THAT(FormatFromName("a.rar"), IsEmpty());
  EXPECT_THAT(FormatFromName("plain"), IsEmpty());
}

TEST_F(PackFormatsTest, EveryListedFormatIsOneFormatFromNameAccepts) {
  // The list is the SOT for the help and the check, so a name in it must actually resolve.
  for (const std::string& format : PackFormats()) {
    EXPECT_THAT(FormatFromName(absl::StrCat("out.", format)), Eq(format)) << format;
  }
}

}  // namespace
}  // namespace xff::archive
