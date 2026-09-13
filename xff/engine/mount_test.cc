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

#include "xff/engine/mount.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::Optional;

// This binary links no fuse extra, which is the LEAN build every one of these tests describes: the
// mount seam answers "not built in", so a mount never happens and the caller extracts. The mounted
// path itself is the extra's own test (@xff_fuse), where a mount can actually be made.
struct StubFileSystem : vfs::FileSystem {
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view /*dir*/) const override {
    return std::vector<vfs::Entry>{};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view /*path*/, bool /*follow_symlinks*/) const override {
    return vfs::Metadata{.type = vfs::FileType::kRegular};
  }

  absl::Status Remove(std::string_view /*path*/) const override { return absl::UnimplementedError("read-only"); }

  bool Access(std::string_view /*path*/, vfs::AccessMode /*mode*/) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view /*path*/) const override {
    return absl::InvalidArgumentError("not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view /*path*/) const override { return std::string("fake"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view /*path*/) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view /*path*/) const override {
    return absl::NotFoundError("no content");
  }
};

// A mount that mounts nothing: the lifetime test needs a Mount to exist, not a kernel to serve it.
struct FakeMount final : fuse::Mount {
  explicit FakeMount(std::string_view container) : mount_point(absl::StrCat("/fake/", container)) {}

  [[nodiscard]] std::string_view MountPoint() const override { return mount_point; }

  [[nodiscard]] std::string PathFor(std::string_view member) const override {
    return absl::StrCat(mount_point, "/", member);
  }

  std::string mount_point;
};

struct MountedContainersTest : ::testing::Test {
  std::shared_ptr<const vfs::FileSystem> fs = std::make_shared<StubFileSystem>();
};

TEST_F(MountedContainersTest, DisarmedAnswersNothingAndExplainsNothing) {
  MountedContainers mounts;
  EXPECT_THAT(mounts.Armed(), IsFalse());
  EXPECT_THAT(mounts.PathFor(fs, "a.tar!hello.txt"), Eq(std::nullopt));
  // Disarmed is not a degrade: nothing was asked for, so there is nothing to report.
  EXPECT_THAT(mounts.DegradeReason(), Eq(""));
}

TEST_F(MountedContainersTest, APathThatNamesNoMemberIsNotMountedAndIsNotAFailure) {
  MountedContainers mounts(/*armed=*/true);
  EXPECT_THAT(mounts.Armed(), IsTrue());
  // An ordinary file: the walk hands these to the same call, and a mount would be meaningless.
  EXPECT_THAT(mounts.PathFor(fs, "/tmp/plain.txt"), Eq(std::nullopt));
  EXPECT_THAT(mounts.DegradeReason(), Eq(""));
}

TEST_F(MountedContainersTest, WithoutTheExtraAMemberDegradesWithOneReason) {
  MountedContainers mounts(/*armed=*/true);
  EXPECT_THAT(mounts.PathFor(fs, "a.tar!hello.txt"), Eq(std::nullopt));
  const std::string first(mounts.DegradeReason());
  EXPECT_THAT(first, Not(Eq("")));

  // A second member, and a member of a SECOND container, must not append another sentence: the run
  // reports the reason once, however many members it visits.
  EXPECT_THAT(mounts.PathFor(fs, "a.tar!other.txt"), Eq(std::nullopt));
  EXPECT_THAT(mounts.PathFor(fs, "b.zip!x"), Eq(std::nullopt));
  EXPECT_THAT(std::string(mounts.DegradeReason()), Eq(first));
}

// The regression test for the bug that cost this epic a day. A mount serves its filesystem from
// the FUSE thread for the whole run, while the walk owns the container's reader only for the dive
// that opened it. When the factory took `const vfs::FileSystem&`, that was a use-after-free the
// type system could not see: it surfaced as an intermittent ECONNABORTED on one architecture, and
// only ThreadSanitizer eventually named it. The factory now takes shared ownership, so this test
// asserts the property directly - drop every caller-side reference and the reader is STILL alive.
TEST_F(MountedContainersTest, AMountKeepsTheReaderAliveAfterTheCallerDropsIt) {
  std::weak_ptr<const vfs::FileSystem> observer;
  std::shared_ptr<const vfs::FileSystem> captured;
  fuse::RegisterMountFactory(
      [&captured](
          std::shared_ptr<const vfs::FileSystem> mounted, std::string_view container,
          const vfs::MutationPolicy&) -> absl::StatusOr<std::unique_ptr<fuse::Mount>> {
        captured = std::move(mounted);  // what a real Mount does by storing it
        return std::make_unique<FakeMount>(std::string(container));
      });

  {
    const std::shared_ptr<const vfs::FileSystem> reader = std::make_shared<StubFileSystem>();
    observer = reader;
    MountedContainers mounts(/*armed=*/true);
    EXPECT_THAT(mounts.PathFor(reader, "a.tar!hello.txt"), Optional(Eq("/fake/a.tar/hello.txt")));
    // The dive ends here: the walk's own share goes away while the mount lives on.
  }

  EXPECT_THAT(observer.expired(), IsFalse()) << "the mount must hold the reader it serves";
  captured.reset();
  EXPECT_THAT(observer.expired(), IsTrue()) << "and must be the last owner, so nothing leaks";
  fuse::RegisterMountFactory(nullptr);
}

}  // namespace
}  // namespace xff::engine
