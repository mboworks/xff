// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/content/snapshot.h"
#include "xff/hash/hash.h"
#include "xff/presentation/fields/fields.h"
#include "xff/vfs/filesystem.h"

namespace xff::fields {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Optional;

struct HashFs final : vfs::FileSystem {
  std::string bytes = "alpha\nbeta\n";
  bool fail = false;
  mutable std::size_t reads = 0;

  absl::StatusOr<std::string> ReadContent(std::string_view) const override {
    ++reads;
    return fail ? absl::StatusOr<std::string>(absl::PermissionDeniedError("unreadable")) : bytes;
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::UnimplementedError("unused");
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return absl::UnimplementedError("unused");
  }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only"); }
};

struct HashSnapshotTest : ::testing::Test {
  HashFs fs;
  content::Snapshot snapshot;
  const vfs::Metadata metadata;

  RenderContext Context(bool supplied) const {
    RenderContext result{
        .path = "virtual/file",
        .metadata = metadata,
        .fs = fs,
        .tz = absl::UTCTimeZone(),
    };
    if (supplied) {
      result.content.set_ref(snapshot);
    }
    return result;
  }
};

TEST_F(HashSnapshotTest, RepeatedHashFieldsUseOneReadWithBothLocalAndSuppliedSnapshots) {
  const auto compiled = Template::Compile("{hash}|{hash:md5/base64}|{hash}|{hash:sha256/base64}");
  const std::string sha = hash::HashData("sha256", fs.bytes).value_or("unsupported");
  const std::string expected = sha + "|" + hash::HashData("md5", fs.bytes, hash::Encoding::kBase64).value_or("") + "|"
                               + sha + "|" + hash::HashData("sha256", fs.bytes, hash::Encoding::kBase64).value_or("");
  EXPECT_THAT(compiled.Render(Context(false)), EqualsText(expected));
  EXPECT_THAT(fs.reads, Eq(1));
  EXPECT_THAT(compiled.Render(Context(true)), EqualsText(expected));
  EXPECT_THAT(compiled.Render(Context(true)), EqualsText(expected));
  EXPECT_THAT(fs.reads, Eq(2));
}

TEST_F(HashSnapshotTest, RenderedOutputOwnsItsDigestAfterTheEntrySnapshotIsInvalidated) {
  const auto compiled = Template::Compile("{hash}|{hash}");
  const std::string original = hash::HashData("sha256", fs.bytes).value_or("unsupported");
  const std::string rendered = compiled.Render(Context(true));
  fs.bytes = "replacement";
  EXPECT_THAT(compiled.Render(Context(true)), EqualsText(original + "|" + original));
  snapshot.Invalidate();
  const std::string replacement = hash::HashData("sha256", fs.bytes).value_or("unsupported");
  EXPECT_THAT(compiled.Render(Context(true)), EqualsText(replacement + "|" + replacement));
  EXPECT_THAT(rendered, EqualsText(original + "|" + original));
  EXPECT_THAT(fs.reads, Eq(2));
}

TEST_F(HashSnapshotTest, ExtractionWithoutASuppliedSnapshotRetainsTheLocalDigestUntilConsumed) {
  const auto compiled = Template::Compile(R"({hash:"m|^(.{8}).*|\1|"})");
  ASSERT_THAT(compiled.Validate(), IsOk());
  const std::string expected = hash::HashData("sha256", fs.bytes).value_or("unsupported").substr(0, 8);
  for (const bool supplied : {false, true}) {
    EXPECT_THAT(compiled.AsExtraction(Context(supplied)), Optional(ElementsAre(EqualsText(expected))));
  }
  EXPECT_THAT(fs.reads, Eq(2));
}

TEST_F(HashSnapshotTest, UnknownAlgorithmsAndReadFailuresStayEmptyUntilInvalidation) {
  const auto compiled = Template::Compile("[{hash:unknown}][{hash}][{hash}]");
  fs.fail = true;
  EXPECT_THAT(compiled.Render(Context(true)), EqualsText("[][][]"));
  fs.fail = false;
  EXPECT_THAT(compiled.Render(Context(true)), EqualsText("[][][]"));
  EXPECT_THAT(fs.reads, Eq(1));
  snapshot.Invalidate();
  const std::string expected = hash::HashData("sha256", fs.bytes).value_or("unsupported");
  EXPECT_THAT(compiled.Render(Context(true)), EqualsText("[][" + expected + "][" + expected + "]"));
  EXPECT_THAT(fs.reads, Eq(2));
}

}  // namespace
}  // namespace xff::fields
