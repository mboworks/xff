// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/archive/archive_extension.h"

#include <array>
#include <cstdint>
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

namespace xff::archive {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::_;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Optional;
using ::testing::UnorderedElementsAre;

constexpr std::array kFakePackFormats = std::to_array<std::string_view>({"tar.fake"});

struct ArchiveExtensionTest : ::testing::Test {};

TEST_F(ArchiveExtensionTest, RegistryOwnsRoutingVocabularyAndDeterministicReplacement) {
  std::string packed;
  RegisterCompressionExtension({
      .name = "fake",
      .suffixes = {".tar.fake", ".fake"},
      .read_formats = {{.name = "file", .suffixes = {".fake"}, .detail = "fake compressed file"}},
      .pack_formats = {"tar.fake"},
      .pack_vocabulary =
          {{.name = "strength", .value_syntax = "N", .formats = kFakePackFormats, .detail = "fake strength"}},
      .decoder = [](std::string_view label, std::optional<std::string_view> bytes,
                    std::uint64_t) { return absl::StatusOr<std::string>(std::string(bytes.value_or(label))); },
      .packer =
          [&packed](std::string_view path, const std::vector<PackFile>&, const PackOptions&) {
            packed = path;
            return absl::OkStatus();
          },
  });

  const auto extension = CompressionExtensionFor("BUNDLE.TAR.FAKE");
  ASSERT_THAT(extension, Optional(_));
  EXPECT_THAT(absl::StrCat(*extension), Eq("fake"));
  EXPECT_THAT(CompressionExtensionStem("dir/notes.fake"), Eq(std::optional<std::string>("notes")));
  EXPECT_THAT(CompressionExtensionReadFormats(), Contains(Field("name", &ReadFormatInfo::name, "file")));
  EXPECT_THAT(CompressionExtensionPackFormats(), Contains("tar.fake"));
  EXPECT_THAT(
      CompressionExtensionPackVocabulary(), Contains(Field("formats", &PackOptionInfo::formats, Contains("tar.fake"))));
  EXPECT_THAT(DecodeCompressionExtension("x.fake", "payload"), IsOkAndHolds(Eq("payload")));
  EXPECT_THAT(PackCompressionExtension("out.tar.fake", {}, {}), IsOk());
  EXPECT_THAT(packed, Eq("out.tar.fake"));

  RegisterCompressionExtension({
      .name = "fake",
      .suffixes = {".newfake"},
      .decoder = [](std::string_view, std::optional<std::string_view>,
                    std::uint64_t) { return absl::StatusOr<std::string>("replacement"); },
  });
  EXPECT_THAT(CompressionExtensionFor("x.fake"), Eq(std::nullopt));
  EXPECT_THAT(DecodeCompressionExtension("x.newfake", std::nullopt), IsOkAndHolds(Eq("replacement")));
}

TEST_F(ArchiveExtensionTest, UnknownCompressionOwnerIsAnArgumentError) {
  EXPECT_THAT(DecodeCompressionExtension("plain.txt", std::nullopt), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(PackCompressionExtension("plain.txt", {}, {}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(CompressionExtensionStem("dir/plain.txt"), Eq(std::nullopt));
}

TEST_F(ArchiveExtensionTest, LongestCaseInsensitiveSuffixAndFormatWin) {
  std::string packed;
  RegisterCompressionExtension({
      .name = "nested",
      .suffixes = {".tar.fake", ".fake"},
      .read_formats = {{.name = "nested", .suffixes = {".fake", ".tar.fake"}, .detail = "nested fake"}},
      .pack_formats = {"fake", "tar.fake"},
      .decoder = [](std::string_view, std::optional<std::string_view>,
                    std::uint64_t) { return absl::StatusOr<std::string>("decoded"); },
      .packer =
          [&packed](std::string_view path, const std::vector<PackFile>&, const PackOptions&) {
            packed = path;
            return absl::OkStatus();
          },
  });

  EXPECT_THAT(CompressionExtensionFor(".fake"), Eq(std::nullopt));
  EXPECT_THAT(CompressionExtensionFor("NAME.TAR.FAKE"), Optional(Field("name", &CompressionExtension::name, "nested")));
  EXPECT_THAT(CompressionExtensionStem("dir/NAME.TAR.FAKE"), Eq(std::optional<std::string>("NAME")));
  EXPECT_THAT(CompressionExtensionStem("NAME.FAKE"), Eq(std::optional<std::string>("NAME")));
  EXPECT_THAT(CompressionExtensionPackFormatFor("OUT.TAR.FAKE"), Eq("tar.fake"));
  EXPECT_THAT(CompressionExtensionPackFormatFor("OUT.FAKE"), Eq("fake"));
  EXPECT_THAT(CompressionExtensionPackFormatFor("OUT.TXT"), Eq(""));
  EXPECT_THAT(PackCompressionExtension("OUT.TAR.FAKE", {}, {}), IsOk());
  EXPECT_THAT(packed, Eq("OUT.TAR.FAKE"));
  EXPECT_THAT(CompressionExtensionPackFormats(), Contains("tar.fake"));
  EXPECT_THAT(
      CompressionExtensionReadFormats(),
      Contains(AllOf(
          Field("name", &ReadFormatInfo::name, "nested"),
          Field("suffixes", &ReadFormatInfo::suffixes, UnorderedElementsAre(".fake", ".tar.fake")))));
}

TEST_F(ArchiveExtensionTest, SourceDecoderReplacementRetainsFallbackAndErrors) {
  const auto input = vfs::MemoryReadSource("input");
  RegisterSourceDecoder("test-source", [](const vfs::SharedReadSource&) -> absl::StatusOr<vfs::SharedReadSource> {
    return absl::InvalidArgumentError("not mine");
  });
  EXPECT_THAT(DecodeSource(input), IsOkAndHolds(Eq(input)));
  RegisterSourceDecoder("test-source", [](const vfs::SharedReadSource&) -> absl::StatusOr<vfs::SharedReadSource> {
    return absl::DataLossError("broken source");
  });
  EXPECT_THAT(DecodeSource(input), StatusIs(absl::StatusCode::kDataLoss));
  RegisterSourceDecoder("test-source", [](vfs::SharedReadSource source) { return source; });
  EXPECT_THAT(DecodeSource(input), IsOkAndHolds(Eq(input)));
  RegisterCompressionExtension({.name = "stream-only", .suffixes = {".stream-only"}});
  EXPECT_THAT(
      DecodeCompressionExtension("input.stream-only", std::nullopt), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::archive
