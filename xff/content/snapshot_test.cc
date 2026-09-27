// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/content/snapshot.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"

namespace xff::content {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

struct ContentFs final : vfs::FileSystem {
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

struct SnapshotTest : ::testing::Test {
  ContentFs fs;
  Snapshot snapshot;
};

TEST_F(SnapshotTest, ReusesBytesAndRequestedDerivedValuesUntilInvalidated) {
  EXPECT_THAT(snapshot.Loaded(), IsFalse());
  for (int repeat = 0; repeat < 2; ++repeat) {
    EXPECT_THAT(snapshot.Read(fs, "file"), IsOkAndHolds(EqualsText(fs.bytes)));
    EXPECT_THAT(snapshot.Lines(fs, "file"), Optional(2));
    for (const auto encoding : {hash::Encoding::kHex, hash::Encoding::kBase64}) {
      for (const std::string_view algo : {"md5", "sha256"}) {
        EXPECT_THAT(
            snapshot.Digest(fs, "file", {.algo = algo, .encoding = encoding}),
            Optional(EqualsText(hash::HashData(algo, fs.bytes, encoding).value_or("invalid"))));
      }
    }
  }
  EXPECT_THAT(fs.reads, Eq(1));
  EXPECT_THAT(snapshot.Loaded(), IsTrue());
  fs.bytes = "changed";
  snapshot.Invalidate();
  EXPECT_THAT(snapshot.Loaded(), IsFalse());
  EXPECT_THAT(snapshot.Lines(fs, "file"), Optional(1));
  EXPECT_THAT(
      snapshot.Digest(fs, "file", {.algo = "md5"}),
      Optional(EqualsText(hash::HashData("md5", fs.bytes).value_or("invalid"))));
  EXPECT_THAT(fs.reads, Eq(2));
}

TEST_F(SnapshotTest, CachesFailuresWithoutTurningThemIntoEmptyContent) {
  fs.fail = true;
  EXPECT_THAT(snapshot.Read(fs, "file"), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(snapshot.Read(fs, "file"), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(snapshot.Lines(fs, "file"), Eq(std::nullopt));
  EXPECT_THAT(snapshot.Lines(fs, "file"), Eq(std::nullopt));
  EXPECT_THAT(snapshot.Digest(fs, "file", {.algo = "sha256"}), Eq(std::nullopt));
  EXPECT_THAT(snapshot.Digest(fs, "file", {.algo = "sha256"}), Eq(std::nullopt));
  EXPECT_THAT(fs.reads, Eq(1));
  fs.fail = false;
  fs.bytes.clear();
  snapshot.Invalidate();
  EXPECT_THAT(snapshot.Lines(fs, "file"), Optional(0));
  EXPECT_THAT(snapshot.Read(fs, "file"), IsOkAndHolds(EqualsText("")));
  EXPECT_THAT(fs.reads, Eq(2));
}

TEST_F(SnapshotTest, KeepsBinaryClassificationAndMovesOwnedState) {
  fs.bytes = std::string("a\0b\n", 4);
  EXPECT_THAT(snapshot.Lines(fs, "file"), Eq(std::nullopt));
  auto moved = std::move(snapshot);
  EXPECT_THAT(moved.Read(fs, "file"), IsOkAndHolds(EqualsText(fs.bytes)));
  EXPECT_THAT(moved.Lines(fs, "file"), Eq(std::nullopt));
  EXPECT_THAT(fs.reads, Eq(1));
}
}  // namespace
}  // namespace xff::content
