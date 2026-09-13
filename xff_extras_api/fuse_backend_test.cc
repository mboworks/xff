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

#include "xff/fuse/fuse_backend.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::fuse {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::NotNull;

class StubFileSystem final : public vfs::FileSystem {
 public:
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override { return std::vector<vfs::Entry>(); }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override { return vfs::Metadata(); }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read only"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("not a link");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("stub"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return std::string("content"); }
};

class StubMount final : public Mount {
 public:
  std::string_view MountPoint() const override { return "/tmp/mounted"; }

  std::string PathFor(std::string_view member) const override {
    return std::string("/tmp/mounted/") + std::string(member);
  }
};

struct FuseBackendTest : ::testing::Test {
  void TearDown() override { RegisterMountFactory(MountFactory()); }
};

// Both states in their forced order: this binary links no registration TU, so the slot answers
// false (the lean binary's answer) until the registrar - what @xff_fuse's registration TU declares
// at file scope - flips it.
TEST_F(FuseBackendTest, UnregisteredIsTheLeanAnswerAndRegistrationFlipsIt) {
  ASSERT_THAT(MountSupportAvailable(), IsFalse());
  const MountSupportRegistrar registrar{};
  EXPECT_THAT(MountSupportAvailable(), IsTrue());
}

TEST_F(FuseBackendTest, ANullFilesystemIsRejectedBeforeFactoryLookup) {
  EXPECT_THAT(
      MountContainer(nullptr, "box.tar"), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("null filesystem")));
}

TEST_F(FuseBackendTest, WithNoFactoryMountingIsUnavailable) {
  EXPECT_THAT(
      MountContainer(std::make_shared<StubFileSystem>(), "box.tar"),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("no FUSE mount support")));
}

TEST_F(FuseBackendTest, ARegisteredFactoryReceivesOwnershipAndContainerName) {
  std::shared_ptr<const vfs::FileSystem> seen_fs;
  std::string seen_container;
  RegisterMountFactory(
      [&seen_fs, &seen_container](
          std::shared_ptr<const vfs::FileSystem> fs, std::string_view container, const vfs::MutationPolicy&) {
        seen_fs = std::move(fs);
        seen_container = container;
        return absl::StatusOr<std::unique_ptr<Mount>>(std::make_unique<StubMount>());
      });
  const std::shared_ptr<StubFileSystem> fs = std::make_shared<StubFileSystem>();

  MBO_ASSERT_OK_AND_ASSIGN(const std::unique_ptr<Mount> mount, MountContainer(fs, "box.tar"));
  EXPECT_THAT(seen_fs.get(), fs.get());
  EXPECT_THAT(seen_container, "box.tar");
  EXPECT_THAT(mount.get(), NotNull());
  EXPECT_THAT(mount->MountPoint(), "/tmp/mounted");
  EXPECT_THAT(mount->PathFor("dir/file.txt"), "/tmp/mounted/dir/file.txt");
}

TEST_F(FuseBackendTest, BlockedWritingAndDryRunNeverCallTheMountFactory) {
  RegisterMountFactory(
      // Match MountFactory's owning signature even though this rejection sentinel never uses the owner.
      // NOLINTNEXTLINE(performance-unnecessary-value-param)
      [](std::shared_ptr<const vfs::FileSystem>, std::string_view,
         const vfs::MutationPolicy&) -> absl::StatusOr<std::unique_ptr<Mount>> {
        ADD_FAILURE() << "blocked mount reached factory";
        return absl::InternalError("unexpected mount");
      });
  const auto fs = std::make_shared<StubFileSystem>();
  EXPECT_THAT(MountContainer(fs, "box.tar", {.block_writing = true}), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(MountContainer(fs, "box.tar", {.dry_run = true}), StatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace xff::fuse
