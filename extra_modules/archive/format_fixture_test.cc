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

// Coverage for the PACKAGE formats we support for free (test_data/README.md; fixtures written by
// tools/make_archive_fixtures.py).
//
// Almost every "package format" is a zip or a tar underneath, so libarchive reads it and xff already
// dives into it - which means this was shipped behaviour with no test. Each case below pins one
// wrapper shape, and one fixture stands for its whole family: a jar covers war/ear, apk/aab, vsix,
// xpi, docx/odt and nupkg, since only the extension and the manifest names differ.
//
// The last group is the open "prefixed payload" question (CRX3, JMOD, self-extracting archives, and
// by extension AppImage / PyInstaller). These tests do not assume an answer, they RECORD it: whether
// libarchive finds a payload that does not start at byte 0, in both the absolute-offset shape a real
// SFX has and the appended-verbatim shape CRX and JMOD actually use.

#include <array>
#include <cstdlib>
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

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Contains;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::SizeIs;

// The file every fixture carries, so one assertion works across formats.
constexpr std::string_view kCommon = "data/readme.txt";
constexpr std::string_view kCommonContent = "This is the xff archive fixture.\nfindable-needle\n";

// Every fixture tools/make_archive_fixtures.py writes. Named here so a case can assert something of
// the whole set rather than repeating the list.
constexpr std::array kAllFixtures = std::to_array<std::string_view>({
    "example.crx",
    "example.deb",
    "example.gem",
    "example.jar",
    "example.jmod",
    "example.rpm",
    "example.whl",
    "npm-example.tgz",
    "sfx-example.zip",
});

struct FormatFixtureTest : ::testing::Test {
  // As in phar_fixture_test: the BUILD file hands over one fixture's runfiles path, because this test
  // lives in an external bazel module whose canonical repo directory name is not ours to hard-code.
  static std::string Fixture(std::string_view name) {
    // Bazel's own environment, read once in a single-threaded test; getenv is safe here.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* const anchor = std::getenv("XFF_ARCHIVE_FIXTURE_ANCHOR");
    EXPECT_THAT(anchor, NotNull()) << "the BUILD file must set XFF_ARCHIVE_FIXTURE_ANCHOR";
    const std::string_view anchor_path = anchor != nullptr ? anchor : "";
    const std::string_view::size_type slash = anchor_path.rfind('/');
    const std::string_view directory = slash == std::string_view::npos ? "." : anchor_path.substr(0, slash);
    return absl::StrCat(directory, "/", name);
  }
};

TEST_F(FormatFixtureTest, ZipBasedPackagesReadAsArchives) {
  // A jar and a wheel are zips, so this one case is the whole zip-wrapper family. Member names are
  // reported exactly as stored - no format-specific rewriting - which is what lets `-path` and
  // `{relpath}` work on them unchanged.
  EXPECT_THAT(
      ListMembersOfFile(Fixture("example.jar")),
      IsOkAndHolds(Contains(Field("path", &Member::path, "META-INF/MANIFEST.MF"))));
  EXPECT_THAT(ReadMemberOfFile(Fixture("example.jar"), kCommon), IsOkAndHolds(kCommonContent));
  EXPECT_THAT(
      ListMembersOfFile(Fixture("example.whl")),
      IsOkAndHolds(Contains(Field("path", &Member::path, "pkg-1.0.dist-info/METADATA"))));
  EXPECT_THAT(ReadMemberOfFile(Fixture("example.whl"), kCommon), IsOkAndHolds(kCommonContent));
}

TEST_F(FormatFixtureTest, AGzipFilteredTarReadsWithNoFilterSpelledOut) {
  // The npm / Cargo `.crate` / OCI-layer shape. The caller never names tar or gzip: detection is by
  // content, which is what lets one --archive flag cover every packaging in this file.
  EXPECT_THAT(
      ReadMemberOfFile(Fixture("npm-example.tgz"), absl::StrCat("package/", kCommon)), IsOkAndHolds(kCommonContent));
}

TEST_F(FormatFixtureTest, AnArBasedDebianPackageReadsItsThreeMembers) {
  // A .deb is an `ar` in a fixed order. The interesting part is what the walk SEES: the payload
  // members are archives themselves, so one level in they are files, not directories.
  const auto members = ListMembersOfFile(Fixture("example.deb"));
  ASSERT_THAT(members, IsOkAndHolds(SizeIs(3)));
  EXPECT_THAT(*members, Contains(Field("path", &Member::path, "debian-binary")));
  EXPECT_THAT(*members, Contains(Field("path", &Member::path, "data.tar.gz")));
  EXPECT_THAT(ReadMemberOfFile(Fixture("example.deb"), "debian-binary"), IsOkAndHolds("2.0\n"));
}

TEST_F(FormatFixtureTest, AnRpmReadsThroughTheRpmFilterIntoItsCpioPayload) {
  // rpm is a FILTER in libarchive, not a format: the lead and headers are skipped and the cpio
  // payload is what shows up. So the member paths are the payload's, with the `./` prefix cpio
  // stores - which is exactly why the readers normalize a leading `./` when matching.
  const auto members = ListMembersOfFile(Fixture("example.rpm"));
  ASSERT_THAT(members, IsOkAndHolds(Not(IsEmpty())));
  EXPECT_THAT(*members, Contains(Field("path", &Member::path, "./usr/bin/xff-fixture")));
  EXPECT_THAT(
      ReadMemberOfFile(Fixture("example.rpm"), absl::StrCat("usr/share/doc/", kCommon)), IsOkAndHolds(kCommonContent));
}

TEST_F(FormatFixtureTest, ANestedPackageShowsOneLayerAtATime) {
  // A .gem is a tar of archives. One layer is visible for free: the inner `data.tar.gz` is a MEMBER,
  // not a directory, and reaching its contents is what --archive-depth > 1 is for. Pinned so the
  // one-layer boundary is a stated behaviour rather than an accident.
  const auto members = ListMembersOfFile(Fixture("example.gem"));
  ASSERT_THAT(members, IsOkAndHolds(SizeIs(2)));
  EXPECT_THAT(*members, Contains(Field("is_directory", &Member::is_directory, IsFalse())));
  EXPECT_THAT(*members, Contains(Field("path", &Member::path, "data.tar.gz")));
  EXPECT_THAT(ReadMemberOfFile(Fixture("example.gem"), kCommon), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(FormatFixtureTest, APayloadBehindAPrefixIsStillFound) {
  // The prefixed-payload question, answered by the library rather than assumed. A self-extracting
  // archive writes its stub first and the zip in place, so the recorded offsets are absolute;
  // libarchive's zip reader locates the end-of-central-directory by seeking from the end, so this
  // works and needs no offset-sniffing mechanism of ours.
  EXPECT_THAT(ReadMemberOfFile(Fixture("sfx-example.zip"), kCommon), IsOkAndHolds(kCommonContent));
}

TEST_F(FormatFixtureTest, AnAppendedZipBehindAFormatHeaderIsAlsoFound) {
  // CRX3 and JMOD append a plain zip verbatim, so every offset the zip records is short by the header
  // length and the reader has to derive the delta. That it works is the reason those formats need no
  // reader of their own: a `.crx` or `.jmod` is readable today, and only the file extension differs.
  EXPECT_THAT(
      ListMembersOfFile(Fixture("example.crx")), IsOkAndHolds(Contains(Field("path", &Member::path, "manifest.json"))));
  EXPECT_THAT(ReadMemberOfFile(Fixture("example.crx"), kCommon), IsOkAndHolds(kCommonContent));
  EXPECT_THAT(
      ReadMemberOfFile(Fixture("example.jmod"), absl::StrCat("classes/", kCommon)), IsOkAndHolds(kCommonContent));
}

TEST_F(FormatFixtureTest, TheFixturesAreReproducible) {
  // The generator writes fixed timestamps and no gzip mtime, so these bytes are stable: regenerating
  // without a content change produces no diff, and a diff in review therefore means something. The
  // cheap proxy for "still the bytes we generated" is that every fixture still opens.
  for (const std::string_view fixture : kAllFixtures) {
    EXPECT_THAT(ListMembersOfFile(Fixture(fixture)), IsOk()) << fixture;
  }
}

}  // namespace
}  // namespace xff::archive
