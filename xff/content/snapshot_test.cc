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
using ::testing::ElementsAre;
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

TEST_F(SnapshotTest, EmptySnapshotsRemainLazyAcrossCopyMoveAndInvalidation) {
  Snapshot copied(snapshot);
  Snapshot moved(std::move(copied));
  snapshot.Invalidate();
  snapshot.Invalidate();
  moved.Invalidate();
  const auto derived = std::move(moved).TakeDerived({.lines = true}, fs, "file");
  EXPECT_THAT(snapshot.Loaded(), IsFalse());
  EXPECT_THAT(derived.Loaded(), IsFalse());
  EXPECT_THAT(fs.reads, Eq(0));
  EXPECT_THAT(derived.Lines(fs, "file"), Optional(2));
  EXPECT_THAT(fs.reads, Eq(1));
}

TEST_F(SnapshotTest, AssignmentReplacesCachedContentAndDerivedValues) {
  EXPECT_THAT(snapshot.Lines(fs, "file"), Optional(2));
  const auto digest = snapshot.Digest(fs, "file", {.algo = "sha256"});
  Snapshot copied(snapshot);
  snapshot = Snapshot{};
  EXPECT_THAT(snapshot.Loaded(), IsFalse());
  fs.bytes = "replacement";
  EXPECT_THAT(snapshot.Lines(fs, "file"), Optional(1));
  snapshot = copied;
  EXPECT_THAT(snapshot.Lines(fs, "file"), Optional(2));
  EXPECT_THAT(snapshot.Digest(fs, "file", {.algo = "sha256"}), Eq(digest));
  copied.Invalidate();
  EXPECT_THAT(snapshot.Lines(fs, "file"), Optional(2));
  snapshot = std::move(copied);
  EXPECT_THAT(snapshot.Loaded(), IsFalse());
  EXPECT_THAT(snapshot.Lines(fs, "file"), Optional(1));
  EXPECT_THAT(fs.reads, Eq(3));
}

TEST_F(SnapshotTest, EmptyDerivationTransfersNoCachesOrBytes) {
  EXPECT_THAT(snapshot.Lines(fs, "file"), Optional(2));
  const auto derived = std::move(snapshot).TakeDerived({}, fs, "file");
  EXPECT_THAT(derived.Loaded(), IsFalse());
  EXPECT_THAT(fs.reads, Eq(1));
  fs.bytes = "replacement";
  EXPECT_THAT(derived.Lines(fs, "file"), Optional(1));
  EXPECT_THAT(fs.reads, Eq(2));
}

TEST_F(SnapshotTest, DerivedTransferRetainsRequestedValuesWithoutRetainingBytes) {
  EXPECT_THAT(snapshot.Read(fs, "file"), IsOkAndHolds(EqualsText(fs.bytes)));
  const auto digest = hash::HashData("md5", fs.bytes).value_or("unsupported");
  Derivation request{.lines = true, .digests = {{.algorithm = "md5"}}};
  request.Include({.digests = {{.algorithm = "md5"}}});
  EXPECT_THAT(request.digests, ElementsAre(DigestRequest{.algorithm = "md5"}));
  auto derived = std::move(snapshot).TakeDerived(request, fs, "file");
  EXPECT_THAT(derived.Loaded(), IsFalse());
  fs.bytes = "replacement";
  EXPECT_THAT(derived.Lines(fs, "file"), Optional(2));
  EXPECT_THAT(derived.Digest(fs, "file", {.algo = "md5"}), Optional(EqualsText(digest)));
  EXPECT_THAT(fs.reads, Eq(1));
  // Unrequested byte consumers still read lazily; no old raw content crossed the boundary.
  EXPECT_THAT(derived.Read(fs, "file"), IsOkAndHolds(EqualsText("replacement")));
  EXPECT_THAT(fs.reads, Eq(2));
  derived.Invalidate();
  EXPECT_THAT(derived.Lines(fs, "file"), Optional(1));
  EXPECT_THAT(fs.reads, Eq(3));
}

TEST_F(SnapshotTest, UnrequestedCachesAreNotTransferredAndBinaryLineFailuresRemainCached) {
  EXPECT_THAT(
      snapshot.Digest(fs, "file", {.algo = "sha256"}),
      Optional(EqualsText(hash::HashData("sha256", fs.bytes).value_or("unsupported"))));
  const auto derived = std::move(snapshot).TakeDerived({.digests = {{.algorithm = "md5"}}}, fs, "file");
  EXPECT_THAT(derived.Loaded(), IsFalse());
  fs.bytes = "replacement";
  EXPECT_THAT(
      derived.Digest(fs, "file", {.algo = "sha256"}),
      Optional(EqualsText(hash::HashData("sha256", fs.bytes).value_or("unsupported"))));
  EXPECT_THAT(fs.reads, Eq(2));
  Snapshot binary;
  fs.bytes = std::string("one\0two", 7);
  EXPECT_THAT(binary.Read(fs, "file"), IsOkAndHolds(EqualsText(fs.bytes)));
  const auto binary_derived = std::move(binary).TakeDerived({.lines = true}, fs, "file");
  EXPECT_THAT(binary_derived.Loaded(), IsFalse());
  fs.bytes = "now text";
  EXPECT_THAT(binary_derived.Lines(fs, "file"), Eq(std::nullopt));
  EXPECT_THAT(fs.reads, Eq(3));
}

TEST_F(SnapshotTest, DerivationWithoutLoadedBytesDoesNotReadAndCachedFailuresStayFailures) {
  const auto unloaded = std::move(snapshot).TakeDerived({.lines = true}, fs, "file");
  EXPECT_THAT(unloaded.Loaded(), IsFalse());
  EXPECT_THAT(fs.reads, Eq(0));
  EXPECT_THAT(unloaded.Lines(fs, "file"), Optional(2));
  EXPECT_THAT(fs.reads, Eq(1));
  Snapshot failing;
  fs.fail = true;
  EXPECT_THAT(failing.Read(fs, "file"), StatusIs(absl::StatusCode::kPermissionDenied));
  const auto failed = std::move(failing).TakeDerived({.lines = true, .digests = {{.algorithm = "md5"}}}, fs, "file");
  fs.fail = false;
  EXPECT_THAT(failed.Loaded(), IsFalse());
  EXPECT_THAT(failed.Lines(fs, "file"), Eq(std::nullopt));
  EXPECT_THAT(failed.Digest(fs, "file", {.algo = "md5"}), Eq(std::nullopt));
  EXPECT_THAT(fs.reads, Eq(2));
}

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
  const auto moved = std::move(snapshot);
  EXPECT_THAT(moved.Read(fs, "file"), IsOkAndHolds(EqualsText(fs.bytes)));
  EXPECT_THAT(moved.Lines(fs, "file"), Eq(std::nullopt));
  EXPECT_THAT(fs.reads, Eq(1));
}

TEST_F(SnapshotTest, DigestViewsBorrowStableCacheStorageAcrossOtherDigestInsertions) {
  const auto first = snapshot.DigestView(fs, "file", {.algo = "sha256"});
  const auto expected = hash::HashData("sha256", fs.bytes).value_or("unsupported");
  ASSERT_THAT(first, Optional(EqualsText(expected)));
  EXPECT_THAT(
      snapshot.DigestView(fs, "file", {.algo = "md5"}),
      Optional(EqualsText(hash::HashData("md5", fs.bytes).value_or("unsupported"))));
  EXPECT_THAT(
      snapshot.DigestView(fs, "file", {.algo = "sha256", .encoding = hash::Encoding::kBase64}),
      Optional(EqualsText(hash::HashData("sha256", fs.bytes, hash::Encoding::kBase64).value_or("unsupported"))));
  const auto repeated = snapshot.DigestView(fs, "file", {.algo = "sha256"});
  EXPECT_THAT(first, Optional(EqualsText(expected)));
  EXPECT_THAT(repeated, Optional(EqualsText(expected)));
  EXPECT_THAT(first.value_or("").begin(), Eq(repeated.value_or("").begin()));
  EXPECT_THAT(fs.reads, Eq(1));
}

TEST_F(SnapshotTest, OwnedDigestsSurviveInvalidationWhileFreshViewsObserveReplacementBytes) {
  const auto original = hash::HashData("sha256", fs.bytes).value_or("unsupported");
  const auto owned = snapshot.Digest(fs, "file", {.algo = "sha256"});
  EXPECT_THAT(snapshot.DigestView(fs, "file", {.algo = "sha256"}), Optional(EqualsText(original)));
  fs.bytes = "replacement";
  snapshot.Invalidate();
  EXPECT_THAT(owned, Optional(EqualsText(original)));
  EXPECT_THAT(
      snapshot.DigestView(fs, "file", {.algo = "sha256"}),
      Optional(EqualsText(hash::HashData("sha256", fs.bytes).value_or("unsupported"))));
  EXPECT_THAT(fs.reads, Eq(2));
}

TEST_F(SnapshotTest, DigestViewsPreserveBinaryEmptyAndFailedReadContracts) {
  for (const auto& bytes : {std::string(), std::string("a\0b", 3)}) {
    fs.bytes = bytes;
    snapshot.Invalidate();
    for (const auto encoding : {hash::Encoding::kHex, hash::Encoding::kBase64}) {
      for (const std::string_view algorithm : {"md5", "sha256"}) {
        const hash::AlgoEncoding spec{.algo = algorithm, .encoding = encoding};
        EXPECT_THAT(
            snapshot.DigestView(fs, "file", spec),
            Optional(EqualsText(hash::HashData(algorithm, bytes, encoding).value_or("unsupported"))));
        EXPECT_THAT(
            snapshot.Digest(fs, "file", spec),
            Optional(EqualsText(snapshot.DigestView(fs, "file", spec).value_or(""))));
      }
    }
  }
  EXPECT_THAT(snapshot.DigestView(fs, "file", {.algo = "unsupported"}), Eq(std::nullopt));
  fs.fail = true;
  snapshot.Invalidate();
  EXPECT_THAT(snapshot.DigestView(fs, "file", {.algo = "sha256"}), Eq(std::nullopt));
  EXPECT_THAT(snapshot.Digest(fs, "file", {.algo = "sha256"}), Eq(std::nullopt));
  fs.fail = false;
  EXPECT_THAT(snapshot.DigestView(fs, "file", {.algo = "sha256"}), Eq(std::nullopt));
  snapshot.Invalidate();
  EXPECT_THAT(
      snapshot.DigestView(fs, "file", {.algo = "sha256"}),
      Optional(EqualsText(hash::HashData("sha256", fs.bytes).value_or("unsupported"))));
}

TEST_F(SnapshotTest, TransferredDerivedDigestsOfferViewsWithoutReadingChangedEntryContent) {
  const auto expected = hash::HashData("sha256", fs.bytes).value_or("unsupported");
  EXPECT_THAT(snapshot.DigestView(fs, "file", {.algo = "sha256"}), Optional(EqualsText(expected)));
  auto derived = std::move(snapshot).TakeDerived({.digests = {{.algorithm = "sha256"}}}, fs, "file");
  fs.bytes = "replacement";
  EXPECT_THAT(derived.DigestView(fs, "file", {.algo = "sha256"}), Optional(EqualsText(expected)));
  EXPECT_THAT(derived.Loaded(), IsFalse());
  EXPECT_THAT(fs.reads, Eq(1));
  derived.Invalidate();
  EXPECT_THAT(
      derived.DigestView(fs, "file", {.algo = "sha256"}),
      Optional(EqualsText(hash::HashData("sha256", fs.bytes).value_or("unsupported"))));
  EXPECT_THAT(fs.reads, Eq(2));
}
}  // namespace
}  // namespace xff::content
