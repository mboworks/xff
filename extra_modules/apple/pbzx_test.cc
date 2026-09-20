// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/apple/pbzx.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_fs.h"
#include "xff/archive/archive_reader.h"
#include "xff/vfs/read_source.h"

namespace xff::apple {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

std::string Number(std::uint64_t number) {
  std::string result(8, '\0');
  for (char& byte : result | std::views::reverse) {
    byte = static_cast<char>(number & 255U);
    number >>= 8U;
  }
  return result;
}

struct PbzxTest : ::testing::Test {
  static std::string Frame(std::string_view data) {
    return "pbzx" + Number(16ULL * 1'024 * 1'024) + Number(data.size()) + Number(data.size()) + std::string(data);
  }

  static absl::StatusOr<std::string> Decode(std::string bytes) {
    MBO_ASSIGN_OR_RETURN(auto source, DecodePbzx(vfs::MemoryReadSource(std::move(bytes))));
    return vfs::ReadSourceBytes(*source, 1'024UZ * 1'024);
  }

  static std::string Fixture(std::string_view name) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): XFF_ABI_POINTER: Bazel fixes this test environment before startup.
    const char* value = std::getenv("XFF_APPLE_FIXTURE");
    const std::string anchor = value == nullptr ? "" : value;
    return anchor.substr(0, anchor.rfind('/') + 1) + std::string(name);
  }
};

class FragmentStream final : public vfs::ReadStream {
 public:
  explicit FragmentStream(std::unique_ptr<vfs::ReadStream> stream) : stream_(std::move(stream)) {}

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    return stream_->Read(std::min(max_bytes, std::size_t{1}));
  }

 private:
  std::unique_ptr<vfs::ReadStream> stream_;
};

class FragmentSource final : public vfs::ReadSource {
 public:
  explicit FragmentSource(vfs::SharedReadSource source) : source_(std::move(source)) {}

  absl::StatusOr<std::unique_ptr<vfs::ReadStream>> Open() const override {
    MBO_ASSIGN_OR_RETURN(auto stream, source_->Open());
    return std::make_unique<FragmentStream>(std::move(stream));
  }

 private:
  vfs::SharedReadSource source_;
};

TEST_F(PbzxTest, HeaderAndChunkParsingAcceptShortReads) {
  const auto input = std::make_shared<FragmentSource>(vfs::MemoryReadSource(Frame("fragmented")));
  MBO_ASSERT_OK_AND_ASSIGN(auto source, DecodePbzx(input));
  MBO_ASSERT_OK_AND_ASSIGN(auto stream, source->Open());
  EXPECT_THAT(stream->Read(0), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(vfs::ReadSourceBytes(*source, 100), IsOkAndHolds(EqualsText("fragmented")));
}

TEST_F(PbzxTest, FragmentedXarSourcePreservesPackageContextAndParentLifetime) {
  auto source = std::make_shared<FragmentSource>(vfs::HostReadSource(Fixture("sample.xip")));
  MBO_ASSERT_OK_AND_ASSIGN(auto outer, archive::ArchiveFileSystem::OpenSource("sample.xip", source));
  EXPECT_THAT(outer.ContainerCandidate("sample.xip!Content"), IsTrue());
  MBO_ASSERT_OK_AND_ASSIGN(auto member, outer.ContentSource("sample.xip!Content"));
  source.reset();
  MBO_ASSERT_OK_AND_ASSIGN(auto inner, archive::ArchiveFileSystem::OpenSource("payload", member));
  EXPECT_THAT(
      inner.ReadContent("payload!Applications/Example.app/Contents/info.txt"),
      IsOkAndHolds(EqualsText("portable package\n")));
}

TEST_F(PbzxTest, RawChunksAndRestartableSources) {
  MBO_ASSERT_OK_AND_ASSIGN(auto source, DecodePbzx(vfs::MemoryReadSource(Frame("abcdef"))));
  MBO_ASSERT_OK_AND_ASSIGN(auto first, source->Open());
  MBO_ASSERT_OK_AND_ASSIGN(auto second, source->Open());
  EXPECT_THAT(first->Read(2), IsOkAndHolds(EqualsText("ab")));
  EXPECT_THAT(first->Read(20), IsOkAndHolds(EqualsText("cdef")));
  EXPECT_THAT(first->Read(1), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(second->Read(3), IsOkAndHolds(EqualsText("abc")));
}

TEST_F(PbzxTest, RefusesOtherFormats) {
  EXPECT_THAT(Decode("plain"), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Decode(""), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(PbzxTest, RejectsTruncatedHeaderAndData) {
  EXPECT_THAT(Decode("pbzx"), StatusIs(absl::StatusCode::kDataLoss));
  auto bytes = Frame("data");
  bytes.pop_back();
  EXPECT_THAT(Decode(bytes), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(Decode("pbzx" + Number(16) + "short"), StatusIs(absl::StatusCode::kDataLoss));
}

TEST_F(PbzxTest, RejectsUnboundedOrInvalidLengths) {
  EXPECT_THAT(Decode("pbzx" + Number(0)), StatusIs(absl::StatusCode::kUnimplemented));
  EXPECT_THAT(
      Decode("pbzx" + Number(16) + Number(1ULL << 40U) + Number(2)), StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_THAT(Decode("pbzx" + Number(16) + Number(1) + Number(0)), StatusIs(absl::StatusCode::kDataLoss));
}

TEST_F(PbzxTest, ValidatesTerminatorAndXzData) {
  EXPECT_THAT(Decode("pbzx" + Number(16) + Number(0) + Number(0)), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(Decode("pbzx" + Number(16) + Number(0) + Number(0) + "junk"), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(Decode("pbzx" + Number(16) + Number(10) + Number(3) + "bad"), StatusIs(absl::StatusCode::kDataLoss));
}

TEST_F(PbzxTest, MixedRawXzFixtureMatchesIndependentCpio) {
  MBO_ASSERT_OK_AND_ASSIGN(auto source, DecodePbzx(vfs::HostReadSource(Fixture("payload.pbzx"))));
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto expected, vfs::ReadSourceBytes(*vfs::HostReadSource(Fixture("payload.cpio")), 4'096));
  EXPECT_THAT(vfs::ReadSourceBytes(*source, 4'096), IsOkAndHolds(EqualsText(expected)));
  EXPECT_THAT(vfs::ReadSourceBytes(*source, 4), StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST_F(PbzxTest, XarEnvelopeAndNestedPayloadStreamWithoutHostExtraction) {
  MBO_ASSERT_OK_AND_ASSIGN(auto outer, archive::ArchiveFileSystem::Open(Fixture("sample.xip")));
  const auto member = Fixture("sample.xip") + "!Content";
  EXPECT_THAT(outer.ContainerCandidate(member), IsTrue());
  MBO_ASSERT_OK_AND_ASSIGN(auto source, outer.ContentSource(member));
  MBO_ASSERT_OK_AND_ASSIGN(auto inner, archive::ArchiveFileSystem::OpenSource(member, source));
  EXPECT_THAT(
      inner.ReadContent(member + "!Applications/Example.app/Contents/info.txt"),
      IsOkAndHolds(EqualsText("portable package\n")));
}

TEST_F(PbzxTest, GenericXarNamesDoNotCreatePackageContext) {
  MBO_ASSERT_OK_AND_ASSIGN(
      auto source, archive::ArchiveFileSystem::OpenSource("ordinary.xar", vfs::HostReadSource(Fixture("plain.pkg"))));
  EXPECT_THAT(source.ContainerCandidate("ordinary.xar!Payload"), IsFalse());
  MBO_ASSERT_OK_AND_ASSIGN(
      auto package,
      archive::ArchiveFileSystem::OpenSource("APPLICATION.PKG", vfs::HostReadSource(Fixture("plain.pkg"))));
  EXPECT_THAT(package.ContainerCandidate("APPLICATION.PKG!Payload"), IsTrue());
}

TEST_F(PbzxTest, ReadsRealXarAndChecksContentDigest) {
  MBO_ASSERT_OK_AND_ASSIGN(std::string bytes, vfs::ReadSourceBytes(*vfs::HostReadSource(Fixture("sample.xar")), 4'096));
  EXPECT_THAT(
      archive::ListMembers(bytes), IsOkAndHolds(ElementsAre(Field("path", &archive::Member::path, "hello.txt"))));
  EXPECT_THAT(archive::ReadMember(bytes, "hello.txt"), IsOkAndHolds(EqualsText("hello\n")));
  bytes.back() = bytes.back() == 'X' ? 'Y' : 'X';
  EXPECT_THAT(archive::ReadMember(bytes, "hello.txt"), StatusIs(absl::StatusCode::kDataLoss));
}
}  // namespace
}  // namespace xff::apple
