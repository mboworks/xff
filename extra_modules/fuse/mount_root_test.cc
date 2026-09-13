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

#include "xff/fuse/mount_root.h"

#include <unistd.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::fuse {
namespace {

namespace stdfs = ::std::filesystem;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::UnorderedElementsAre;

struct MountRootTest : ::testing::Test {
  void SetUp() override {
    base_ = absl::StrCat(
        ::testing::TempDir(), "/mount-root-test-", ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code error;
    stdfs::remove_all(base_, error);
    stdfs::create_directories(base_, error);
    options_ = MountRootOptions{.base_override = base_};
  }

  void TearDown() override {
    std::error_code error;
    stdfs::remove_all(base_, error);
  }

  std::string base_;
  MountRootOptions options_;
};

TEST_F(MountRootTest, CreateMakesThePerRunTreeAndTheDestructorRemovesIt) {
  std::string path;
  {
    MBO_ASSERT_OK_AND_ASSIGN(const MountRoot root, MountRoot::Create(options_));
    path = std::string(root.path());
    EXPECT_THAT(path, AllOf(HasSubstr(absl::StrCat(base_, "/xff/")), HasSubstr(absl::StrCat("/", ::getpid(), "-"))));
    EXPECT_THAT(stdfs::is_directory(path), IsTrue());
  }
  EXPECT_THAT(stdfs::exists(path), IsFalse());
  // The shared `<base>/xff/` level survives the run: it is every run's parent, never one run's own.
  EXPECT_THAT(stdfs::is_directory(absl::StrCat(base_, "/xff")), IsTrue());
}

TEST_F(MountRootTest, CreateUsesTheXdgRuntimeDirectoryByDefault) {
  // Read and restore the inherited value before asserting on Create(), so even a failure cannot
  // leak the test's process-wide environment change into later cases.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* const inherited = std::getenv("XDG_RUNTIME_DIR");
  const bool had_original = inherited != nullptr;
  const std::string original = inherited == nullptr ? "" : inherited;
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ASSERT_THAT(::setenv("XDG_RUNTIME_DIR", base_.c_str(), /*overwrite=*/1), 0);
  absl::StatusOr<MountRoot> created = MountRoot::Create();
  int restore_result;
  if (had_original) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    restore_result = ::setenv("XDG_RUNTIME_DIR", original.c_str(), /*overwrite=*/1);
  } else {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    restore_result = ::unsetenv("XDG_RUNTIME_DIR");
  }
  ASSERT_THAT(restore_result, 0);

  MBO_ASSERT_OK_AND_ASSIGN(const MountRoot root, std::move(created));
  EXPECT_THAT(root.path(), HasSubstr(absl::StrCat(base_, "/xff/")));
}

TEST_F(MountRootTest, EmptyXdgRuntimeDirectoryFallsBackToTheSystemTemporaryDirectory) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* const inherited = std::getenv("XDG_RUNTIME_DIR");
  const bool had_original = inherited != nullptr;
  const std::string original = inherited == nullptr ? "" : inherited;
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ASSERT_THAT(::setenv("XDG_RUNTIME_DIR", "", /*overwrite=*/1), 0);
  absl::StatusOr<MountRoot> created = MountRoot::Create();
  const int restore_result = had_original
                                 // NOLINTNEXTLINE(concurrency-mt-unsafe)
                                 ? ::setenv("XDG_RUNTIME_DIR", original.c_str(), /*overwrite=*/1)
                                 // NOLINTNEXTLINE(concurrency-mt-unsafe)
                                 : ::unsetenv("XDG_RUNTIME_DIR");
  ASSERT_THAT(restore_result, 0);

  MBO_ASSERT_OK_AND_ASSIGN(const MountRoot root, std::move(created));
  EXPECT_THAT(root.path(), Not(HasSubstr(base_)));
}

TEST_F(MountRootTest, CreateReportsWhenTheSharedBaseCannotBeCreated) {
  const std::string shared_base = absl::StrCat(base_, "/xff");
  std::ofstream blocker(shared_base);
  blocker << "not a directory";
  blocker.close();
  EXPECT_THAT(
      MountRoot::Create(options_), StatusIs(absl::StatusCode::kAlreadyExists, HasSubstr("cannot create directories")));
}

TEST_F(MountRootTest, MountPointsUseTheBasenameAndDisambiguateDuplicates) {
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot root, MountRoot::Create(options_));
  MBO_ASSERT_OK_AND_ASSIGN(const std::string first, root.MountPointFor("/data/box.tgz"));
  MBO_ASSERT_OK_AND_ASSIGN(const std::string second, root.MountPointFor("/other/box.tgz"));
  EXPECT_THAT(first, EndsWith("/box.tgz"));
  EXPECT_THAT(second, EndsWith("/box.tgz.1"));
  EXPECT_THAT(stdfs::is_directory(first), IsTrue());
  EXPECT_THAT(stdfs::is_directory(second), IsTrue());
}

TEST_F(MountRootTest, MountPointAcceptsAContainerWithoutAParentPath) {
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot root, MountRoot::Create(options_));
  MBO_ASSERT_OK_AND_ASSIGN(const std::string point, root.MountPointFor("box.tgz"));
  EXPECT_THAT(point, EndsWith("/box.tgz"));
}

TEST_F(MountRootTest, MountPointReportsANameCollision) {
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot root, MountRoot::Create(options_));
  const std::string collision = absl::StrCat(root.path(), "/box.tgz");
  std::ofstream blocker(collision);
  blocker << "not a directory";
  blocker.close();
  EXPECT_THAT(
      root.MountPointFor("box.tgz"),
      StatusIs(absl::StatusCode::kAlreadyExists, HasSubstr("cannot create directories")));
}

TEST_F(MountRootTest, AMovedFromRootOwnsNothing) {
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot root, MountRoot::Create(options_));
  const std::string path(root.path());
  {
    const MountRoot taken = std::move(root);
    EXPECT_THAT(taken.path(), path);
  }
  // `taken` removed the tree; the moved-from `root` destructing later must not touch anything.
  EXPECT_THAT(stdfs::exists(path), IsFalse());
}

TEST_F(MountRootTest, MoveAssignmentTransfersOwnership) {
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot source, MountRoot::Create(options_));
  const std::string source_path(source.path());
  MountRoot destination = std::move(source);
  const MountRootOptions other_options{.base_override = absl::StrCat(base_, "/other")};
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot replacement, MountRoot::Create(other_options));
  const std::string replaced_path(replacement.path());
  replacement = std::move(destination);
  EXPECT_THAT(replacement.path(), source_path);
  EXPECT_THAT(stdfs::exists(replaced_path), IsFalse());
}

TEST_F(MountRootTest, MoveAssignmentIntoAMovedFromRootTransfersOwnership) {
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot source, MountRoot::Create(options_));
  MountRoot destination = std::move(source);
  const std::string path(destination.path());
  source = std::move(destination);
  EXPECT_THAT(source.path(), path);
}

TEST_F(MountRootTest, SelfMoveAssignmentPreservesOwnership) {
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot root, MountRoot::Create(options_));
  const std::string path(root.path());
  MountRoot* const same = &root;
  root = std::move(*same);
  EXPECT_THAT(root.path(), path);
  EXPECT_THAT(stdfs::is_directory(path), IsTrue());
}

TEST_F(MountRootTest, StaleRootsFindsDeadPidsAndLeavesTheLivingAndTheForeign) {
  MBO_ASSERT_OK_AND_ASSIGN(const MountRoot live, MountRoot::Create(options_));
  // A pid can never be alive beyond the kernel's pid space; 999999999 is comfortably dead. A
  // non-numeric sibling is somebody else's directory and must never be considered ours.
  const std::string dead = absl::StrCat(base_, "/xff/999999999");
  const std::string foreign = absl::StrCat(base_, "/xff/not-a-pid");
  const std::string non_positive = absl::StrCat(base_, "/xff/0");
  std::error_code error;
  stdfs::create_directories(dead, error);
  stdfs::create_directories(foreign, error);
  stdfs::create_directories(non_positive, error);
  const std::ofstream foreign_file(absl::StrCat(base_, "/xff/123-not-a-directory"));
  EXPECT_THAT(StaleRoots(options_), UnorderedElementsAre(dead));
}

TEST_F(MountRootTest, SweepUnmountsEachMountPointThenRemovesTheRoot) {
  const std::string dead = absl::StrCat(base_, "/xff/999999999");
  std::error_code error;
  stdfs::create_directories(absl::StrCat(dead, "/box.tgz"), error);
  stdfs::create_directories(absl::StrCat(dead, "/other.zip"), error);
  std::vector<std::string> unmounted;
  const std::size_t removed =
      SweepStaleRoots([&unmounted](std::string_view point) { unmounted.emplace_back(point); }, options_);
  EXPECT_THAT(removed, 1U);
  EXPECT_THAT(unmounted, UnorderedElementsAre(absl::StrCat(dead, "/box.tgz"), absl::StrCat(dead, "/other.zip")));
  EXPECT_THAT(stdfs::exists(dead), IsFalse());
}

TEST_F(MountRootTest, SweepingNothingIsANoOp) {
  EXPECT_THAT(StaleRoots(options_), IsEmpty());
  EXPECT_THAT(SweepStaleRoots([](std::string_view) {}, options_), 0U);
}

TEST_F(MountRootTest, InvalidBasenamesNeverCreateMountPoints) {
  MBO_ASSERT_OK_AND_ASSIGN(auto root, MountRoot::Create(options_));
  const std::vector<std::string_view> paths = {"", "box/", ".", "box/.."};
  for (const std::string_view path : paths) {
    EXPECT_THAT(root.MountPointFor(path), StatusIs(absl::StatusCode::kInvalidArgument));
  }
  EXPECT_THAT(stdfs::is_empty(root.path()), IsTrue());
}

TEST_F(MountRootTest, DeletionPolicyPreventsStaleRootUnmountAndCleanup) {
  const std::string dead = absl::StrCat(base_, "/xff/999999999/box.tar");
  ASSERT_THAT(stdfs::create_directories(dead), IsTrue());
  options_.mutations.block_deletion = true;
  EXPECT_THAT(SweepStaleRoots([](std::string_view) { ADD_FAILURE() << "must not unmount"; }, options_), 0);
  EXPECT_THAT(stdfs::exists(dead), IsTrue());
  options_.mutations.block_deletion = false;
  options_.mutations.dry_run = true;
  EXPECT_THAT(SweepStaleRoots([](std::string_view) { ADD_FAILURE() << "must not unmount"; }, options_), 0);
  EXPECT_THAT(stdfs::exists(dead), IsTrue());
}

}  // namespace
}  // namespace xff::fuse
