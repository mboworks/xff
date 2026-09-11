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

// Asserts that linking this extra makes the CORE able to open containers - by asking the seam, never
// the class. A test that called ArchiveFileSystem::Open directly would keep passing if the registrar
// were dropped from the link, which is precisely the regression worth guarding: `--archive` would go
// back to "not built into this binary" with nothing failing to build.

#include "xff/archive/archive_register.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_backend.h"
#include "xff/archive/archive_extension.h"
#include "xff/archive/archive_fs.h"

namespace xff::archive {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;

struct ArchiveRegisterTest : ::testing::Test {};

TEST_F(ArchiveRegisterTest, LinkingTheExtraGivesTheCoreArchiveSupport) {
  EXPECT_THAT(ContainerSupportAvailable(), IsTrue());
}

TEST_F(ArchiveRegisterTest, TheSeamReachesTheRealReaderAndKeepsItsErrors) {
  // A path that exists but is not an archive must come back InvalidArgument (the walk then treats it
  // as an ordinary file). Reaching the real reader is the point: with no registrar the seam would
  // answer Unimplemented instead.
  EXPECT_THAT(OpenContainer("/etc/hosts"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveRegisterTest, TheDiveGateIsDerivedFromTheDeclaredReadFormats) {
  // ONE extension SOT: the gate is built from the registration, so every declared suffix dives (by
  // its last dotted component, which is how compounds like `.tar.gz` gate) and an undeclared name
  // does not. There is no second list left to drift.
  const std::vector<ReadFormatInfo> formats = ContainerReadFormats();
  ASSERT_THAT(formats, Not(IsEmpty()));
  for (const ReadFormatInfo& format : formats) {
    for (const std::string& suffix : format.suffixes) {
      EXPECT_THAT(LooksLikeContainerName(absl::StrCat("x", suffix)), IsTrue()) << format.name << " declares " << suffix;
    }
  }
  EXPECT_THAT(LooksLikeContainerName("notes.txt"), IsFalse());
  EXPECT_THAT(LooksLikeContainerName("Makefile"), IsFalse());
}

TEST_F(ArchiveRegisterTest, LinkingTheExtraGivesTheCoreContainerCreation) {
  EXPECT_THAT(ContainerPackingAvailable(), IsTrue());
  // The formats travel with the packer, so the CLI's pre-walk check sees the real set.
  EXPECT_THAT(ContainerPackFormats(), Contains("tar.gz"));
}

TEST_F(ArchiveRegisterTest, TheOptionVocabularyReachesTheCoreThroughTheSeam) {
  // What the CLI checks a `--pack-option` name against, and what `--help=archive` renders. Reading it
  // from the seam is the point: a name added to the writer must reach both without a second list.
  EXPECT_THAT(
      ContainerPackVocabulary(),
      Contains(AllOf(
          Field("name", &PackOptionInfo::name, "level"), Field("formats", &PackOptionInfo::formats, Contains("zip")))));
}

TEST_F(ArchiveRegisterTest, TheSeamReachesTheRealWriterAndKeepsItsErrors) {
  // An output name carrying no writable format is InvalidArgument; with no registrar the seam would
  // answer Unimplemented instead, which is the regression this guards.
  EXPECT_THAT(PackContainer("/tmp/xff-register.rar", {}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveRegisterTest, TheWriterRefusesToInvalidateAnArchiveBasedPharSignature) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): Bazel's immutable test environment.
  const char* const fixture = std::getenv("XFF_ARCHIVE_REGISTER_PHAR");
  ASSERT_THAT(fixture, Not(Eq(nullptr)));
  EXPECT_THAT(
      RemoveContainerMembers(fixture, {"data/readme.txt"}),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("signature")));
}

TEST_F(ArchiveRegisterTest, RefreshMergesExtensionVocabularyAndRoutesItsPacker) {
  std::string packed;
  RegisterCompressionExtension({
      .name = "test-extension",
      .suffixes = {".extra"},
      .read_formats =
          {
              {.name = "file", .suffixes = {".extra"}, .detail = "test-compressed file"},
              {.name = "file", .suffixes = {".extra-quiet"}},
              {.name = "extra-container", .suffixes = {".tar.extra"}},
          },
      .pack_formats = {"tar.extra"},
      .pack_vocabulary = {{.name = "strength", .value_syntax = "N", .detail = "test strength"}},
      .decoder = [](std::string_view, std::optional<std::string_view>,
                    std::uint64_t) { return absl::StatusOr<std::string>("decoded"); },
      .packer =
          [&packed](std::string_view path, const std::vector<PackFile>&, const PackOptions&) {
            packed = path;
            return absl::OkStatus();
          },
  });
  RegisterArchiveBackend();

  EXPECT_THAT(
      ContainerReadFormats(), AllOf(
                                  Contains(AllOf(
                                      Field("name", &ReadFormatInfo::name, "file"),
                                      Field("suffixes", &ReadFormatInfo::suffixes, Contains(".extra")),
                                      Field("detail", &ReadFormatInfo::detail, HasSubstr("test-compressed file")))),
                                  Contains(Field("name", &ReadFormatInfo::name, "extra-container"))));
  EXPECT_THAT(ContainerPackFormats(), Contains("tar.extra"));
  EXPECT_THAT(ContainerPackVocabulary(), Contains(Field("name", &PackOptionInfo::name, "strength")));
  EXPECT_THAT(PackContainer("out.tar.extra", {}), IsOk());
  EXPECT_THAT(packed, Eq("out.tar.extra"));

  const std::filesystem::path input = std::filesystem::path(::testing::TempDir()) / "xff-register.extra";
  std::ofstream(input, std::ios::binary) << "ignored";
  EXPECT_THAT(OpenContainer(input.string()), IsOk());
  EXPECT_THAT(ArchiveFileSystem::OpenBytes("nested.extra", "ignored"), IsOk());
}

}  // namespace
}  // namespace xff::archive
