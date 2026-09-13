// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/brotli/brotli_codec.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_backend.h"
#include "xff/archive/archive_reader.h"

namespace xff::brotli {
namespace {

namespace stdfs = ::std::filesystem;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Lt;
using ::testing::Not;
using ::testing::Optional;
using ::testing::StartsWith;
using ::testing::UnorderedElementsAre;

constexpr std::string_view kFramingSignature = "\x91\x0a\x42\x52";

void AppendVarint(std::string& output, std::uint64_t value) {
  while (true) {
    auto byte = static_cast<std::uint8_t>(value & 0x7fU);
    value >>= 7U;
    if (value != 0) {
      byte |= 0x80U;
    }
    output.push_back(static_cast<char>(byte));
    if (value == 0) {
      return;
    }
  }
}

std::string MakeFrame(
    std::string_view payload,
    std::uint64_t expected_size,
    std::optional<std::uint64_t> chunk_size = std::nullopt,
    std::uint8_t chunk_type = 2,
    std::uint8_t codec = 2,
    std::uint8_t data_flags = 0) {
  std::string body;
  body.push_back(static_cast<char>(chunk_type));
  body.push_back(static_cast<char>(codec));
  AppendVarint(body, expected_size);
  body.push_back(static_cast<char>(data_flags));
  body.append(payload);

  std::string framed(kFramingSignature);
  framed.push_back(0);
  AppendVarint(framed, chunk_size.value_or(body.size()));
  framed.append(body);
  return framed;
}

struct DecodedVarint {
  std::uint64_t value;
  std::size_t next;
};

std::optional<DecodedVarint> ReadVarint(std::string_view bytes, std::size_t cursor) {
  std::uint64_t value = 0;
  for (unsigned int index = 0; index < 9 && cursor < bytes.size(); ++index) {
    const auto byte = static_cast<std::uint8_t>(bytes[cursor++]);
    value |= static_cast<std::uint64_t>(byte & 0x7fU) << (index * 7U);
    if ((byte & 0x80U) == 0) {
      return DecodedVarint{.value = value, .next = cursor};
    }
  }
  return std::nullopt;
}

struct BrotliCodecTest : ::testing::Test {
  void SetUp() override {
    root_ = stdfs::path(::testing::TempDir())
            / ("xff-brotli-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
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
    std::ofstream output(path, std::ios::binary);
    output << content;
  }

  [[nodiscard]] static std::string Read(const stdfs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  [[nodiscard]] std::vector<archive::PackFile> Files() const {
    return {
        {.source = (root_ / "one.txt").string(), .name = "one.txt"},
        {.source = (root_ / "dir" / "two.txt").string(), .name = "dir/two.txt"},
    };
  }

  stdfs::path root_;
};

TEST_F(BrotliCodecTest, DefaultPackIsRfc9841AndRoundTripsTheTarResource) {
  const stdfs::path output = root_ / "packed.tar.br";
  ASSERT_THAT(PackTar(output.string(), Files(), {}), IsOk());
  const std::string encoded = Read(output);
  ASSERT_THAT(encoded.size(), Ge(kFramingSignature.size()));
  EXPECT_THAT(std::string_view(encoded).substr(0, kFramingSignature.size()), Eq(kFramingSignature));

  ASSERT_OK_AND_ASSIGN(const std::string tar, Decode(output.string(), std::nullopt));
  ASSERT_THAT(tar, Not(Eq("")));
  std::size_t cursor = kFramingSignature.size();
  ASSERT_THAT(cursor, Lt(encoded.size()));
  EXPECT_THAT(static_cast<std::uint8_t>(encoded[cursor++]), Eq(0));  // simple-container flags
  const std::size_t chunk_start = cursor;
  const std::optional<DecodedVarint> chunk_size = ReadVarint(encoded, cursor);
  ASSERT_THAT(chunk_size, Optional(Field("value", &DecodedVarint::value, Ge(4))));
  const DecodedVarint chunk = chunk_size.value_or(DecodedVarint{});
  cursor = chunk.next;
  const std::size_t chunk_content = cursor;
  EXPECT_THAT(chunk.value, Eq(encoded.size() - chunk_content));
  EXPECT_THAT(static_cast<std::uint8_t>(encoded[cursor++]), Eq(2));  // data chunk
  EXPECT_THAT(static_cast<std::uint8_t>(encoded[cursor++]), Eq(2));  // RFC 7932 codec
  const std::optional<DecodedVarint> uncompressed_size = ReadVarint(encoded, cursor);
  ASSERT_THAT(uncompressed_size, Optional(Field("value", &DecodedVarint::value, Eq(tar.size()))));
  cursor = uncompressed_size.value_or(DecodedVarint{}).next;
  EXPECT_THAT(static_cast<std::uint8_t>(encoded[cursor]), Eq(0));  // resource flags
  EXPECT_THAT(chunk_start, Lt(chunk_content));
  EXPECT_THAT(
      archive::ListMembers(tar),
      IsOkAndHolds(UnorderedElementsAre(
          Field("path", &archive::Member::path, "one.txt"), Field("path", &archive::Member::path, "dir/two.txt"))));
  EXPECT_THAT(archive::ReadMember(tar, "dir/two.txt"), IsOkAndHolds(Eq("second\n")));
}

TEST_F(BrotliCodecTest, RawFramingOptionWritesLegacyCompatibleRfc7932Stream) {
  const stdfs::path output = root_ / "packed.tbr";
  ASSERT_THAT(
      PackTar(
          output.string(), Files(),
          {.options = {{.name = "framing", .value = "raw"}, {.name = "level", .value = "4"}}}),
      IsOk());
  std::string encoded = Read(output);
  EXPECT_THAT(encoded, Not(StartsWith(std::string(kFramingSignature))));
  ASSERT_OK_AND_ASSIGN(const std::string tar, Decode(output.string(), encoded));
  ASSERT_THAT(tar, Not(Eq("")));
  EXPECT_THAT(archive::ReadMember(tar, "one.txt"), IsOkAndHolds(Eq("first\n")));
  encoded.push_back('x');
  EXPECT_THAT(Decode("raw-with-tail.br", encoded), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("trailing")));
}

TEST_F(BrotliCodecTest, DecoderDistinguishesInvalidTruncatedTrailingAndOversizedStreams) {
  EXPECT_THAT(Decode("empty.br", ""), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated Brotli")));
  EXPECT_THAT(Decode("bad.br", "not brotli"), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("invalid Brotli")));

  const stdfs::path output = root_ / "packed.tar.br";
  ASSERT_THAT(PackTar(output.string(), Files(), {}), IsOk());
  std::string encoded = Read(output);
  encoded.pop_back();
  EXPECT_THAT(Decode("short.tar.br", encoded), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated")));

  encoded = Read(output);
  encoded.push_back('x');
  EXPECT_THAT(Decode("trailing.tar.br", encoded), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("trailing")));
  EXPECT_THAT(
      Decode(output.string(), std::nullopt, 4), StatusIs(absl::StatusCode::kResourceExhausted, HasSubstr("4 byte")));
}

TEST_F(BrotliCodecTest, DecoderRejectsEveryUnsupportedOrMalformedRfc9841Boundary) {
  const stdfs::path output = root_ / "packed.tar.br";
  ASSERT_THAT(PackTar(output.string(), Files(), {}), IsOk());
  const std::string encoded = Read(output);
  ASSERT_OK_AND_ASSIGN(const std::string tar, Decode(output.string(), encoded));

  std::size_t cursor = kFramingSignature.size() + 1;
  const std::optional<DecodedVarint> chunk_size = ReadVarint(encoded, cursor);
  ASSERT_THAT(chunk_size, Optional(Field("value", &DecodedVarint::value, Ge(4))));
  cursor = chunk_size.value_or(DecodedVarint{}).next;
  cursor += 2;
  const std::optional<DecodedVarint> uncompressed_size = ReadVarint(encoded, cursor);
  ASSERT_THAT(uncompressed_size, Optional(Field("value", &DecodedVarint::value, Eq(tar.size()))));
  cursor = uncompressed_size.value_or(DecodedVarint{}).next;
  ++cursor;
  ASSERT_THAT(cursor, Lt(encoded.size()));
  const std::string_view payload = std::string_view(encoded).substr(cursor);

  std::string unsupported(encoded);
  unsupported[kFramingSignature.size()] = 1;
  EXPECT_THAT(
      Decode("container-flags.tar.br", unsupported),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("container features")));
  EXPECT_THAT(
      Decode("chunk-type.tar.br", MakeFrame(payload, tar.size(), std::nullopt, /*chunk_type=*/1)),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("not one Brotli")));
  EXPECT_THAT(
      Decode("codec.tar.br", MakeFrame(payload, tar.size(), std::nullopt, /*chunk_type=*/2, /*codec=*/1)),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("not one Brotli")));
  EXPECT_THAT(
      Decode(
          "data-flags.tar.br",
          MakeFrame(payload, tar.size(), std::nullopt, /*chunk_type=*/2, /*codec=*/2, /*data_flags=*/1)),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("data flags")));
  EXPECT_THAT(
      Decode("short-chunk-header.tar.br", MakeFrame(payload, tar.size(), /*chunk_size=*/0)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("chunk length")));
  EXPECT_THAT(
      Decode("wrong-size.tar.br", MakeFrame(payload, tar.size() + 1)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("size mismatch")));
  EXPECT_THAT(
      Decode("long-chunk.tar.br", MakeFrame(payload, tar.size(), payload.size() + 64)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated Brotli")));
  EXPECT_THAT(
      Decode("empty-stream.tar.br", MakeFrame("", 0)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("Brotli stream")));

  const std::string signature(kFramingSignature);
  EXPECT_THAT(
      Decode("missing-flags.tar.br", signature),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated RFC 9841 header")));
  EXPECT_THAT(
      Decode("missing-size.tar.br", signature + '\0'),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated RFC 9841 varint")));
  EXPECT_THAT(
      Decode("oversized-size.tar.br", signature + '\0' + std::string(9, static_cast<char>(0x80))),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("oversized RFC 9841 varint")));

  std::string truncated_header = signature + '\0';
  AppendVarint(truncated_header, 4);
  EXPECT_THAT(
      Decode("missing-chunk-type.tar.br", truncated_header),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated RFC 9841 header")));
  truncated_header.push_back(2);
  EXPECT_THAT(
      Decode("missing-codec.tar.br", truncated_header),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated RFC 9841 header")));
  truncated_header.push_back(2);
  EXPECT_THAT(
      Decode("missing-expected-size.tar.br", truncated_header),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated RFC 9841 varint")));
  AppendVarint(truncated_header, tar.size());
  EXPECT_THAT(
      Decode("missing-data-flags.tar.br", truncated_header),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("truncated RFC 9841 header")));
}

TEST_F(BrotliCodecTest, PackOptionsAreValidatedAndFailureLeavesNoOutput) {
  const stdfs::path output = root_ / "bad.tar.br";
  EXPECT_THAT(
      PackTar(output.string(), Files(), {.options = {{.name = "unknown", .value = "1"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("does not apply")));
  EXPECT_THAT(
      PackTar(output.string(), Files(), {.options = {{.name = "level", .value = "not-an-integer"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("0 to 11")));
  EXPECT_THAT(
      PackTar(output.string(), Files(), {.options = {{.name = "level", .value = "-1"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("0 to 11")));
  EXPECT_THAT(
      PackTar(output.string(), Files(), {.options = {{.name = "framing", .value = "mystery"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("rfc9841")));
  EXPECT_THAT(stdfs::exists(output), IsFalse());
  EXPECT_THAT(
      PackTar(output.string(), Files(), {.options = {{.name = "window", .value = "25"}}}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("10 to 24")));
  EXPECT_THAT(stdfs::exists(output), IsFalse());
}

TEST_F(BrotliCodecTest, IoFailuresKeepTheDestinationAtomic) {
  EXPECT_THAT(
      Decode((root_ / "missing.br").string(), std::nullopt),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("cannot open")));

  const stdfs::path missing_source = root_ / "missing.txt";
  const stdfs::path native_failure = root_ / "native-failure.tar.br";
  EXPECT_THAT(
      PackTar(native_failure.string(), {{.source = missing_source.string(), .name = "missing.txt"}}, {}), Not(IsOk()));
  EXPECT_THAT(stdfs::exists(native_failure), IsFalse());

  const stdfs::path encode_failure = root_ / "encode-failure.tar.br";
  stdfs::create_directory(stdfs::path(encode_failure).concat(".xff-brotli.raw"));
  EXPECT_THAT(PackTar(encode_failure.string(), Files(), {}), IsOk());
  EXPECT_THAT(stdfs::is_directory(stdfs::path(encode_failure).concat(".xff-brotli.raw")), IsTrue());

  const stdfs::path placement_failure = root_ / "placement-failure.tar.br";
  stdfs::create_directory(placement_failure);
  EXPECT_THAT(PackTar(placement_failure.string(), Files(), {}), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(stdfs::is_directory(placement_failure), IsTrue());
}

}  // namespace
}  // namespace xff::brotli
