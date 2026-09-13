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

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/mutations.h"

namespace xff::vfs {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::SizeIs;

// A minimal READ-ONLY backend, the shape an extra (archive diving; remote later) implements. Writing
// one here is the point of the test: this module exists so a backend can be implemented WITHOUT
// depending on the xff core, so the seam has to be implementable using nothing but this module. If a
// future edit adds a pure virtual, or moves a type the signatures need, this file stops compiling -
// which is exactly the regression we want, since the core would otherwise be the only implementer.
class ReadOnlyFakeFs : public FileSystem {
 public:
  absl::StatusOr<std::vector<Entry>> ReadDir(std::string_view dir) const override {
    if (dir != "/box") {
      return absl::NotFoundError("no such directory");
    }
    Entry entry;
    entry.path = "/box/member.txt";
    entry.name = "member.txt";
    entry.read_only = true;  // a virtual entry: actions must refuse it
    return std::vector<Entry>{entry};
  }

  absl::StatusOr<Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    if (path == "/box") {
      return Metadata{.type = FileType::kDirectory};
    }
    if (path != "/box/member.txt") {
      return absl::NotFoundError("no such path");
    }
    Metadata metadata;
    metadata.size = 7;
    return metadata;
  }

  // Read-only source: mutation is an error rather than a silent no-op.
  absl::Status Remove(std::string_view /*path*/) const override {
    return absl::PermissionDeniedError("read-only backend");
  }

  bool Access(std::string_view path, AccessMode mode) const override {
    return path == "/box/member.txt" && mode == AccessMode::kRead;
  }

  absl::StatusOr<std::string> ReadLink(std::string_view /*path*/) const override {
    return absl::InvalidArgumentError("not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view /*path*/) const override { return std::string("fake"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view /*path*/) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    if (path != "/box/member.txt") {
      return absl::NotFoundError("no such path");
    }
    return std::string("content");
  }
};

struct VfsSeamTest : ::testing::Test {};

TEST_F(VfsSeamTest, AnExtraCanImplementTheInterfaceUsingOnlyThisModule) {
  const ReadOnlyFakeFs fs;
  const FileSystem& seam = fs;  // usable through the abstract interface, which is how the engine holds it
  EXPECT_THAT(seam.ReadDir("/box"), IsOkAndHolds(SizeIs(1)));
  EXPECT_THAT(seam.ReadContent("/box/member.txt"), IsOkAndHolds("content"));
  EXPECT_THAT(seam.ReadContentRange("/box/member.txt", 2, 3), IsOkAndHolds("nte"));
  EXPECT_THAT(seam.ReadContentRange("/box/member.txt", 7, 3), IsOkAndHolds(""));
  EXPECT_THAT(seam.ReadContentRange("/box/member.txt", 0, 0), IsOkAndHolds(""));
  EXPECT_THAT(seam.ReadContentRange("/missing", 0, 1), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(seam.FsType("/box"), IsOkAndHolds("fake"));
  EXPECT_THAT(seam.IsCaseSensitive("/box"), IsOkAndHolds(true));
}

TEST_F(VfsSeamTest, ReadOnlyBackendNeverFallsBackToHostOutput) {
  const ReadOnlyFakeFs fs;
  EXPECT_THAT(fs.OpenOutput("/box/member.txt", false), StatusIs(absl::StatusCode::kUnimplemented));
}

TEST_F(VfsSeamTest, ControlledOperationsRetainBackendRestrictionsAndRejectHostScopes) {
  const ReadOnlyFakeFs fs;
  EXPECT_THAT(fs.SupportsDirectoryPolicies(), IsFalse());
  EXPECT_THAT(fs.RemoveControlled("/missing", {}), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(fs.RemoveControlled("/box/member.txt", {}), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(
      fs.RemoveControlled("/box", {.block_directory_deletion = true}), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(fs.RemoveControlled("/box", {}), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(
      fs.RemoveControlled("/box/member.txt", {.block_deletion = true}), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(fs.OpenControlledOutput("/box/member.txt", false, {}), StatusIs(absl::StatusCode::kUnimplemented));
  EXPECT_THAT(
      fs.OpenControlledOutput("/box/member.txt", false, {.block_writing = true}),
      StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(
      fs.OpenControlledOutput("/box/member.txt", false, {.block_overwrite = true}),
      StatusIs(absl::StatusCode::kUnimplemented));
  EXPECT_THAT(fs.OpenControlledOutput("/box/member.txt", true, {}), StatusIs(absl::StatusCode::kUnimplemented));
  ASSERT_OK_AND_ASSIGN(const auto directories, DirectoryPolicy::Create({}));
  EXPECT_THAT(
      fs.RemoveControlled("/box/member.txt", {.directories = directories}),
      StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(
      fs.OpenControlledOutput("/box/member.txt", false, {.directories = directories}),
      StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(VfsSeamTest, PerPathFailuresAreStatusesSoTheWalkCanContinue) {
  // The contract is that a per-path failure is REPORTED, not thrown or fatal: the engine keeps
  // traversing and folds it into the exit code.
  const ReadOnlyFakeFs fs;
  EXPECT_THAT(fs.ReadDir("/missing"), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(fs.Stat("/missing", /*follow_symlinks=*/false), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(fs.ReadLink("/box/member.txt"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(VfsSeamTest, AReadOnlyBackendRefusesRemovalRatherThanSilentlyIgnoringIt) {
  // Virtual entries are read-only; -delete must get an error it can report, never a no-op that
  // looks like success.
  const ReadOnlyFakeFs fs;
  EXPECT_THAT(fs.Remove("/box/member.txt"), StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(VfsSeamTest, AReadOnlyBackendRefusesWritingByDefault) {
  const ReadOnlyFakeFs fs;
  EXPECT_THAT(fs.WriteContent("/box/member.txt", "content"), StatusIs(absl::StatusCode::kUnimplemented));
}

TEST_F(VfsSeamTest, EntryAndMetadataDefaultToTheRealFilesystemCase) {
  // Defaults matter: a backend fills in only what it knows, so an unset field must mean "ordinary
  // real-filesystem entry" - in particular read_only false, so nothing accidentally treats a real
  // file as a virtual one.
  const Entry entry;
  EXPECT_THAT(entry.read_only, IsFalse());
  EXPECT_THAT(entry.path, "");
  const Metadata metadata;
  EXPECT_THAT(metadata.size, 0);
  EXPECT_THAT(metadata.btime, Eq(std::nullopt));  // birth time is optional, absent by default
}

TEST_F(VfsSeamTest, AccessDistinguishesTheModes) {
  const ReadOnlyFakeFs fs;
  EXPECT_THAT(fs.Access("/box/member.txt", AccessMode::kRead), IsTrue());
  EXPECT_THAT(fs.Access("/box/member.txt", AccessMode::kWrite), IsFalse());
  EXPECT_THAT(fs.Access("/nope", AccessMode::kRead), IsFalse());
}

TEST_F(VfsSeamTest, ReadDirReportsVirtualEntriesAsReadOnly) {
  const ReadOnlyFakeFs fs;
  EXPECT_THAT(fs.ReadDir("/box"), IsOkAndHolds(ElementsAre(Field("read_only", &Entry::read_only, IsTrue()))));
}

}  // namespace
}  // namespace xff::vfs
