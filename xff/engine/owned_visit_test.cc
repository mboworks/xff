// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/owned_visit.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/engine/walk.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

using ::mbo::testing::EqualsText;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Ref;

struct OwnedVisitTest : ::testing::Test {
  const vfs::Metadata metadata;
};

TEST_F(OwnedVisitTest, PreservesOverlappingAndIndependentTextAfterSourceDestruction) {
  struct Text {
    std::string_view path;
    std::string_view name;
    std::string_view root;
  };

  static constexpr std::array kCases = std::to_array<Text>({
      {.path = "", .name = "", .root = ""},
      {.path = "./leaf", .name = "leaf", .root = "."},
      {.path = "leaf", .name = "leaf", .root = "."},
      {.path = "/root/leaf", .name = "other", .root = "/elsewhere"},
      {.path = "/root//leaf/", .name = "leaf", .root = "/root/"},
      {.path = "archive.zip!/nested/leaf", .name = "leaf", .root = "archive.zip"},
      {.path = "prefix-name", .name = "name", .root = "prefix"},
      {.path = "p", .name = "longer-name", .root = "longer-root"},
  });
  for (const auto& expected : kCases) {
    const OwnedVisit owned = [&] {
      const std::string path(expected.path);
      const std::string name(expected.name);
      const std::string root(expected.root);
      return OwnedVisit(Visit{.path = path, .name = name, .root = root, .metadata = metadata});
    }();
    EXPECT_THAT(owned.Path(), EqualsText(expected.path));
    EXPECT_THAT(owned.Name(), EqualsText(expected.name));
    EXPECT_THAT(owned.Root(), EqualsText(expected.root));
    EXPECT_THAT(owned.LogicalTextBytes(), Eq(expected.path.size() + expected.name.size() + expected.root.size()));
  }
}

TEST_F(OwnedVisitTest, PreservesCountedSlicesAndEmbeddedNullBytes) {
  const std::string bytes("ignored/root/a\0b/ignored", 24);
  const std::string_view path = std::string_view(bytes).substr(7, 9);
  const std::string_view name = path.substr(5);
  const std::string_view root = path.substr(0, 4);
  const OwnedVisit owned(Visit{.path = path, .name = name, .root = root, .metadata = metadata});
  EXPECT_THAT(owned.Path(), Eq(path));
  EXPECT_THAT(owned.Name(), Eq(name));
  EXPECT_THAT(owned.Root(), Eq(root));
  EXPECT_THAT(owned.LogicalTextBytes(), Eq(path.size() + name.size() + root.size()));
  const std::string_view independent("x\0y", 3);
  const OwnedVisit fallback(Visit{.path = "a", .name = independent, .root = independent, .metadata = metadata});
  EXPECT_THAT(fallback.Name(), Eq(independent));
  EXPECT_THAT(fallback.Root(), Eq(independent));
}

TEST_F(OwnedVisitTest, CopiesAndMovesRebuildViewsForShortAndLongStrings) {
  for (const std::size_t length : {1, 16, 256}) {
    const std::string root(length, 'r');
    const std::string name(length, 'n');
    std::string path = root;
    path += '/';
    path += name;
    OwnedVisit original(Visit{.path = path, .name = name, .root = root, .metadata = metadata});
    const OwnedVisit copy(original);
    std::vector<OwnedVisit> relocated;
    relocated.push_back(std::move(original));
    for (std::size_t index = 0; index < 100; ++index) {
      relocated.emplace_back(copy);
    }
    for (const auto& entry : relocated) {
      const Visit visit = entry.AsVisit();
      EXPECT_THAT(visit.path, EqualsText(path));
      EXPECT_THAT(visit.name, EqualsText(name));
      EXPECT_THAT(visit.root, EqualsText(root));
    }
    // Moving intentionally leaves observable empty slices.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_THAT(original.Path(), IsEmpty());
    EXPECT_THAT(original.Name(), IsEmpty());
    EXPECT_THAT(original.Root(), IsEmpty());
    EXPECT_THAT(original.LogicalTextBytes(), Eq(0));
    EXPECT_THAT(copy.Path(), EqualsText(path));
  }
}

TEST_F(OwnedVisitTest, SnapshotOwnsMetadataAndRetainsFilesystemLifetime) {
  auto owner = std::make_shared<vfs::LocalFs>();
  const std::weak_ptr<const vfs::FileSystem> weak = owner;
  const OwnedVisit owned(
      Visit{
          .path = "./leaf",
          .name = "leaf",
          .root = ".",
          .depth = 3,
          .metadata = {.type = vfs::FileType::kRegular, .size = 42},
          .fs = *owner,
          .fs_owner = owner,
          .root_index = 7,
      });
  owner.reset();
  EXPECT_THAT(weak.expired(), IsFalse());
  const Visit view = owned.AsVisit();
  EXPECT_THAT(view.depth, Eq(3));
  EXPECT_THAT(view.root_index, Eq(7));
  EXPECT_THAT(view.metadata.type, Eq(vfs::FileType::kRegular));
  EXPECT_THAT(view.metadata.size, Eq(42));
  ASSERT_THAT(static_cast<bool>(view.fs), IsTrue());
  EXPECT_THAT(*view.fs, Ref(*owned.AsVisit().fs));
  EXPECT_THAT(view.fs_owner, Eq(owned.AsVisit().fs_owner));
}

TEST_F(OwnedVisitTest, DefaultSnapshotAndMovedFromObserverAreEmpty) {
  const OwnedVisit empty;
  EXPECT_THAT(empty.Path(), IsEmpty());
  EXPECT_THAT(empty.Name(), IsEmpty());
  EXPECT_THAT(empty.Root(), IsEmpty());
  EXPECT_THAT(static_cast<bool>(empty.AsVisit().fs), IsFalse());
  const vfs::LocalFs fs;
  OwnedVisit observed(Visit{.path = "x", .metadata = metadata, .fs = fs});
  const OwnedVisit moved(std::move(observed));
  EXPECT_THAT(moved.Path(), EqualsText("x"));
  // The move contract explicitly disengages the old observer.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  EXPECT_THAT(static_cast<bool>(observed.AsVisit().fs), IsFalse());
  // Path is also explicitly empty after the move.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  EXPECT_THAT(observed.Path(), IsEmpty());
}

TEST_F(OwnedVisitTest, FilesystemOwnerSurvivesCopiesAndMovesUntilTheLastSnapshotDies) {
  std::weak_ptr<const vfs::FileSystem> weak;
  {
    auto owner = std::make_shared<vfs::LocalFs>();
    weak = owner;
    OwnedVisit original(Visit{.path = "archive!member", .metadata = metadata, .fs = *owner, .fs_owner = owner});
    const OwnedVisit copy(original);
    const OwnedVisit moved(std::move(original));
    owner.reset();
    EXPECT_THAT(weak.expired(), IsFalse());
    EXPECT_THAT(copy.Path(), EqualsText("archive!member"));
    EXPECT_THAT(moved.Path(), EqualsText("archive!member"));
  }
  EXPECT_THAT(weak.expired(), IsTrue());
}

}  // namespace
}  // namespace xff::engine
