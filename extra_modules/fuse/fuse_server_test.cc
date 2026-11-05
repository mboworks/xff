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

#include "xff/fuse/fuse_server.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/fuse/fuse_loader.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

// The process environment handed to a spawned child. It is C's own global, declared here the way
// xff/exec does: required on macOS (no <unistd.h> declaration), merely redundant on Linux. The
// NOLINTs cover that redundancy and the shape POSIX defines and we cannot change.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-redundant-declaration)
extern "C" char** environ;

namespace xff::fuse {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

// A fixed three-entry tree the way an opened container presents one:
//   <root>/hello.txt   regular, "hello, mount\n"
//   <root>/sub/        directory
//   <root>/sub/a.bin   regular, "abc"
//   <root>/link        symlink -> "hello.txt"
struct FakeFileSystem : vfs::FileSystem {
  struct Failures {
    bool child_stat;
    bool root_readdir;
    bool content;
    bool readlink;
  };

  FakeFileSystem() = default;

  explicit FakeFileSystem(Failures failures) : failures_(failures) {}

  // Members are spelled the way the archive VFS spells them - `container!member`, NOT
  // `container/member`. A fake that joined with a slash would let a server that assumes local-path
  // syntax pass while failing on every real container.
  static constexpr std::string_view kRoot = "/container.zip";
  static constexpr std::string_view kSep = "!";
  static constexpr std::string_view kHello = "hello, mount\n";

  static vfs::Metadata MetaFor(vfs::FileType type, std::uint64_t size) {
    return {
        .type = type,
        .source = vfs::Source::kArchiveMember,
        .size = size,
        .mode = 0644,
        .mtime = absl::FromUnixSeconds(1'700'000'000),
    };
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    if (failures_.root_readdir && dir == kRoot) {
      return absl::PermissionDeniedError("root listing denied");
    }
    if (dir == kRoot) {
      return std::vector<vfs::Entry>{
          {.path = absl::StrCat(kRoot, kSep, "hello.txt"), .name = "hello.txt", .type = vfs::FileType::kRegular},
          {.path = absl::StrCat(kRoot, kSep, "sub"), .name = "sub", .type = vfs::FileType::kDirectory},
          {.path = absl::StrCat(kRoot, kSep, "link"), .name = "link", .type = vfs::FileType::kSymlink},
      };
    }
    if (dir == absl::StrCat(kRoot, kSep, "sub")) {
      return std::vector<vfs::Entry>{
          {.path = absl::StrCat(kRoot, kSep, "sub/a.bin"), .name = "a.bin", .type = vfs::FileType::kRegular},
      };
    }
    return absl::NotFoundError(absl::StrCat("no directory '", dir, "'"));
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    if (path == kRoot || path == absl::StrCat(kRoot, kSep, "sub")) {
      return MetaFor(vfs::FileType::kDirectory, 0);
    }
    if (path == absl::StrCat(kRoot, kSep, "hello.txt")) {
      if (failures_.child_stat) {
        return absl::PermissionDeniedError("member stat denied");
      }
      return MetaFor(vfs::FileType::kRegular, kHello.size());
    }
    if (path == absl::StrCat(kRoot, kSep, "sub/a.bin")) {
      return MetaFor(vfs::FileType::kRegular, 3);
    }
    if (path == absl::StrCat(kRoot, kSep, "link")) {
      return MetaFor(vfs::FileType::kSymlink, 9);
    }
    return absl::NotFoundError(absl::StrCat("no entry '", path, "'"));
  }

  absl::Status Remove(std::string_view /*path*/) const override { return absl::UnimplementedError("read-only"); }

  bool Access(std::string_view /*path*/, vfs::AccessMode mode) const override { return mode == vfs::AccessMode::kRead; }

  absl::StatusOr<std::string> ReadLink(std::string_view path) const override {
    if (path == absl::StrCat(kRoot, kSep, "link")) {
      if (failures_.readlink) {
        return absl::InvalidArgumentError("broken symlink metadata");
      }
      return std::string("hello.txt");
    }
    return absl::InvalidArgumentError("not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view /*path*/) const override { return std::string("fake"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view /*path*/) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    if (path == absl::StrCat(kRoot, kSep, "hello.txt")) {
      if (failures_.content) {
        return absl::PermissionDeniedError("member content denied");
      }
      return std::string(kHello);
    }
    if (path == absl::StrCat(kRoot, kSep, "sub/a.bin")) {
      return std::string("abc");
    }
    return absl::NotFoundError(absl::StrCat("no content '", path, "'"));
  }

 private:
  Failures failures_{};
};

// One mount per test (a mount is milliseconds), so each test states one behaviour and no test
// depends on an earlier one's reads.
struct FuseServerTest : ::testing::Test {
  std::shared_ptr<const FakeFileSystem> fs = std::make_shared<FakeFileSystem>();
  std::string mount_point;
  std::unique_ptr<FuseServer> server;

  // Mounts the fake tree, or marks the test skipped and leaves `server` null. Callers must return
  // when `server` is null: gtest's SKIP/FATAL only unwind the TEST body, never a helper.
  // `name` keeps each test on its own mount point; unused when the MSan branch below skips.
  void MountOrSkip(
      [[maybe_unused]] std::string_view name,
      [[maybe_unused]] std::string_view root = FakeFileSystem::kRoot) {
#if defined(MEMORY_SANITIZER)
    // MSan false-positives on anything it did not instrument, and these tests exist to call
    // through the dlopened SYSTEM libfuse3 - every byte it writes reads as uninitialized.
    // ASan/TSan tolerate uninstrumented libraries and keep running this path.
    GTEST_SKIP() << "MSan cannot model the uninstrumented system libfuse3";
#else
    if (!FuseAvailable()) {
# if defined(XFF_FUSE_TESTS_REQUIRED)
      FAIL() << "fuse3 must be loadable here: " << FuseLoader::Instance().Error();
# else
      GTEST_SKIP() << "no fuse3 on this machine: " << FuseLoader::Instance().Error();
# endif
    }
    // A plain directory is all a mount point is; MountRoot (which normally provides it) has its
    // own test and stays out of these.
    mount_point = (std::filesystem::path(::testing::TempDir()) / absl::StrCat("xff-fuse-", name)).string();
    std::filesystem::remove_all(mount_point);
    std::filesystem::create_directories(mount_point);
    absl::StatusOr<std::unique_ptr<FuseServer>> mounted = FuseServer::Mount(fs, std::string(root), mount_point);
    if (!mounted.ok()) {
# if defined(XFF_FUSE_TESTS_REQUIRED)
      FAIL() << mounted.status();
# else
      GTEST_SKIP() << "fuse3 present but mounting not permitted here: " << mounted.status();
# endif
    }
    server = *std::move(mounted);
#endif
  }

  // The whole content of `relative` under the mount.
  std::string ReadFile(std::string_view relative) const {
    const std::ifstream file(std::filesystem::path(mount_point) / relative);
    std::stringstream content;
    content << file.rdbuf();
    return content.str();
  }
};

TEST_F(FuseServerTest, WithoutFuseMountReportsTheLoaderReason) {
  if (FuseAvailable()) {
    GTEST_SKIP() << "this machine has fuse3; the degrade path is covered where it does not";
  }
  EXPECT_THAT(
      FuseServer::Mount(fs, std::string(FakeFileSystem::kRoot), ::testing::TempDir()),  // NL
      Not(IsOk()));
}

TEST_F(FuseServerTest, NullFileSystemIsRejectedBeforeLoadingFuse) {
  EXPECT_THAT(
      FuseServer::Mount(nullptr, std::string(FakeFileSystem::kRoot), ::testing::TempDir()),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FuseServerTest, AnInvalidMountPointReportsTheMountFailure) {
#if defined(MEMORY_SANITIZER)
  GTEST_SKIP() << "MSan cannot model the uninstrumented system libfuse3";
#else
  if (!FuseAvailable()) {
    GTEST_SKIP() << "no fuse3 on this machine: " << FuseLoader::Instance().Error();
  }
  const std::string absent =
      (std::filesystem::path(::testing::TempDir()) / "xff-fuse-absent-mount-point" / "child").string();
  std::filesystem::remove_all(std::filesystem::path(absent).parent_path());
  EXPECT_THAT(
      FuseServer::Mount(fs, std::string(FakeFileSystem::kRoot), absent),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("FUSE mount failed")));
#endif
}

TEST_F(FuseServerTest, TheMountedRootListsTheContainerRoot) {
  MountOrSkip("root");
  if (server == nullptr) {
    return;
  }
  // std::string on both sides: matching a string_view SUBJECT makes gmock build its matcher on
  // the heap (the inline buffer cannot hold that impl), and the clang analyzer cannot follow that
  // refcount - it reports a leak inside gmock. Comparing strings needs no allocation at all.
  EXPECT_THAT(std::string(server->MountPoint()), Eq(mount_point));
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(mount_point)) {
    names.push_back(entry.path().filename().string());
  }
  EXPECT_THAT(names, UnorderedElementsAre("hello.txt", "sub", "link"));
}

TEST_F(FuseServerTest, AMemberReadsBackThroughTheKernel) {
  MountOrSkip("read");
  if (server == nullptr) {
    return;
  }
  EXPECT_THAT(ReadFile("hello.txt"), Eq(std::string(FakeFileSystem::kHello)));
}

TEST_F(FuseServerTest, AReadCanStartWithinTheMemberAndAtEndOfFile) {
  MountOrSkip("read-offsets");
  if (server == nullptr) {
    return;
  }
  std::ifstream file(std::filesystem::path(mount_point) / "hello.txt");
  file.seekg(7);
  std::string tail;
  std::getline(file, tail, '\0');
  EXPECT_THAT(tail, Eq("mount\n"));
  file.clear();
  file.seekg(static_cast<std::streamoff>(FakeFileSystem::kHello.size()));
  EXPECT_THAT(file.peek(), Eq(std::char_traits<char>::eof()));
}

TEST_F(FuseServerTest, MissingMembersReportNotFound) {
  MountOrSkip("missing");
  if (server == nullptr) {
    return;
  }
  std::error_code error;
  static_cast<void>(std::filesystem::status(std::filesystem::path(mount_point) / "absent", error));
  EXPECT_THAT(error.value(), Eq(ENOENT));
}

TEST_F(FuseServerTest, OpeningAMemberForWritingIsRejected) {
  MountOrSkip("read-only");
  if (server == nullptr) {
    return;
  }
  const std::ofstream file(std::filesystem::path(mount_point) / "hello.txt", std::ios::app);
  EXPECT_THAT(file.is_open(), IsFalse());
}

TEST_F(FuseServerTest, AContainerRootMayCarryTrailingSlashes) {
  MountOrSkip("trailing-root", "/container.zip///");
  if (server == nullptr) {
    return;
  }
  EXPECT_THAT(ReadFile("hello.txt"), Eq(std::string(FakeFileSystem::kHello)));
}

TEST_F(FuseServerTest, ChildStatFailuresReachTheKernel) {
  fs = std::make_shared<FakeFileSystem>(FakeFileSystem::Failures{.child_stat = true});
  MountOrSkip("stat-error");
  if (server == nullptr) {
    return;
  }
  std::error_code error;
  static_cast<void>(std::filesystem::status(std::filesystem::path(mount_point) / "hello.txt", error));
  EXPECT_THAT(error.value(), Eq(EACCES));
}

TEST_F(FuseServerTest, DirectoryReadFailuresReachTheKernel) {
  fs = std::make_shared<FakeFileSystem>(FakeFileSystem::Failures{.root_readdir = true});
  MountOrSkip("readdir-error");
  if (server == nullptr) {
    return;
  }
  std::error_code error;
  const std::filesystem::directory_iterator entries(mount_point, error);
  EXPECT_THAT(error.value(), Eq(EACCES));
}

TEST_F(FuseServerTest, ContentReadFailuresReachTheKernel) {
  fs = std::make_shared<FakeFileSystem>(FakeFileSystem::Failures{.content = true});
  MountOrSkip("content-error");
  if (server == nullptr) {
    return;
  }
  const std::ifstream file(std::filesystem::path(mount_point) / "hello.txt");
  EXPECT_THAT(file.is_open(), IsFalse());
}

TEST_F(FuseServerTest, ReadlinkFailuresReachTheKernel) {
  fs = std::make_shared<FakeFileSystem>(FakeFileSystem::Failures{.readlink = true});
  MountOrSkip("readlink-error");
  if (server == nullptr) {
    return;
  }
  std::error_code error;
  static_cast<void>(std::filesystem::read_symlink(std::filesystem::path(mount_point) / "link", error));
  EXPECT_THAT(error.value(), Eq(EINVAL));
}

TEST_F(FuseServerTest, ANestedMemberReadsBackThroughTheKernel) {
  MountOrSkip("nested");
  if (server == nullptr) {
    return;
  }
  EXPECT_THAT(ReadFile("sub/a.bin"), Eq(std::string("abc")));
}

TEST_F(FuseServerTest, ASymlinkResolvesToItsTarget) {
  MountOrSkip("symlink");
  if (server == nullptr) {
    return;
  }
  std::error_code error;
  const std::filesystem::path target =
      std::filesystem::read_symlink(std::filesystem::path(mount_point) / "link", error);
  EXPECT_THAT(error.value(), Eq(0));
  EXPECT_THAT(target.string(), Eq(std::string("hello.txt")));
}

TEST_F(FuseServerTest, AChildProcessReadsTheSameMount) {
  // The discriminator behind the ENOTCONN/ECONNABORTED failures in the CLI test: reading a mount
  // IN-PROCESS works (the cases above), while a CHILD process reading the same path fails there.
  // Serving child processes is what mounting is FOR (`-exec` gets a real path), so if that breaks
  // it breaks here, on a trivial fake filesystem, with nothing else in the picture.
  MountOrSkip("child");
  if (server == nullptr) {
    return;
  }
  // posix_spawn, not std::system: no shell, and the argv is ours (xff's own exec does the same).
  const std::string out_path = (std::filesystem::path(::testing::TempDir()) / "xff-fuse-child.out").string();
  std::string program = "cat";
  std::string target = absl::StrCat(server->MountPoint(), "/hello.txt");
  std::array<char*, 3> argv = {program.data(), target.data(), nullptr};
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  pid_t pid = 0;
  const int spawned = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  ASSERT_THAT(spawned, Eq(0));
  int wait_status = 0;
  waitpid(pid, &wait_status, 0);
  const std::ifstream produced(out_path);
  std::stringstream content;
  content << produced.rdbuf();
  EXPECT_THAT(WEXITSTATUS(wait_status), Eq(0)) << content.str();
  EXPECT_THAT(content.str(), Eq(std::string(FakeFileSystem::kHello)));
}

TEST_F(FuseServerTest, DestroyingTheServerUnmounts) {
  MountOrSkip("unmount");
  if (server == nullptr) {
    return;
  }
  server.reset();
  std::error_code error;
  EXPECT_THAT(std::filesystem::is_empty(mount_point, error), IsTrue());
}

TEST_F(FuseServerTest, CrashUnmountToleratesANonMount) {
  // Nothing is mounted at TempDir; the helper must swallow the tool's failure silently.
  CrashUnmount(::testing::TempDir());
}

}  // namespace
}  // namespace xff::fuse
