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

#include "xff/vfs/local_fs.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"

namespace xff::vfs {
namespace {

namespace fs = ::std::filesystem;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

// Builds a small tree under a per-test temp directory:
//   <root>/file.txt   ("hello", 5 bytes)
//   <root>/sub/       (directory)
//   <root>/link       (symlink -> file.txt)
struct LocalFsTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir())
            / (std::string("xff_localfs_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code ec;
    fs::remove_all(root_, ec);
    ASSERT_THAT(fs::create_directories(root_), IsTrue());
    { std::ofstream(root_ / "file.txt") << "hello"; }
    fs::create_directory(root_ / "sub");
    fs::create_symlink("file.txt", root_ / "link");
  }

  void TearDown() override {
    if (socket_fd_ >= 0) {
      ::close(socket_fd_);
    }
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  std::string Path(std::string_view child) const { return (root_ / child).string(); }

  LocalFs local_fs_;
  fs::path root_;
  int socket_fd_ = -1;
};

TEST_F(LocalFsTest, ReadDirListsChildren) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto entries, local_fs_.ReadDir(root_.string()));
  EXPECT_THAT(
      entries,
      UnorderedElementsAre(
          Field(&Entry::name, Eq("file.txt")), Field(&Entry::name, Eq("sub")), Field(&Entry::name, Eq("link"))));
}

TEST_F(LocalFsTest, WriteContentReplacesFile) {
  EXPECT_THAT(local_fs_.WriteContent(Path("file.txt"), "updated"), IsOk());
  EXPECT_THAT(local_fs_.ReadContent(Path("file.txt")), IsOkAndHolds("updated"));
}

TEST_F(LocalFsTest, WriteContentReportsOpenFailure) {
  EXPECT_THAT(
      local_fs_.WriteContent(Path("missing/sub/file.txt"), "data"),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("cannot open")));
}

#if defined(__linux__)
TEST_F(LocalFsTest, WriteContentReportsWriteFailure) {
  EXPECT_THAT(
      local_fs_.WriteContent("/dev/full", "data"),
      StatusIs(absl::StatusCode::kResourceExhausted, HasSubstr("cannot write")));
}
#endif

TEST_F(LocalFsTest, ReadDirTagsEntriesAsWritableLocal) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto entries, local_fs_.ReadDir(root_.string()));
  for (const Entry& entry : entries) {
    EXPECT_THAT(entry.source, Source::kLocalFs);
    EXPECT_THAT(entry.read_only, IsFalse());
    EXPECT_THAT(entry.path, Path(entry.name));
  }
}

TEST_F(LocalFsTest, StatAndReadDirRecognizeSpecialFileTypes) {
  ASSERT_THAT(::mkfifo(Path("pipe").c_str(), 0600), Eq(0));
#ifdef SOCK_CLOEXEC
  socket_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
  // macOS has no SOCK_CLOEXEC; mark the private test socket immediately below.
  // NOLINTNEXTLINE(android-cloexec-socket)
  socket_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
#endif
  ASSERT_THAT(socket_fd_, Gt(-1));
#ifndef SOCK_CLOEXEC
  // POSIX fcntl requires a variadic descriptor argument.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  ASSERT_THAT(::fcntl(socket_fd_, F_SETFD, FD_CLOEXEC), Eq(0));
#endif
  struct sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  constexpr std::string_view kSocketName = "socket";
  // sun_path is the fixed C array required by sockaddr_un; copy() necessarily receives a pointer.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
  kSocketName.copy(address.sun_path, sizeof(address.sun_path) - 1);
  // open() is variadic by declaration; O_CLOEXEC prevents leaking this temporary fd to children.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int original_dir = ::open(".", O_RDONLY | O_CLOEXEC);
  ASSERT_THAT(original_dir, Gt(-1));
  ASSERT_THAT(::chdir(root_.c_str()), Eq(0));
  // POSIX bind accepts the generic sockaddr view of the concrete sockaddr_un.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const int bind_result = ::bind(socket_fd_, reinterpret_cast<const struct sockaddr*>(&address), sizeof(address));
  ASSERT_THAT(::fchdir(original_dir), Eq(0));
  ASSERT_THAT(::close(original_dir), Eq(0));
  ASSERT_THAT(bind_result, Eq(0));

  MBO_ASSERT_OK_AND_ASSIGN(const auto fifo, local_fs_.Stat(Path("pipe"), /*follow_symlinks=*/false));
  EXPECT_THAT(fifo.type, FileType::kFifo);
  MBO_ASSERT_OK_AND_ASSIGN(const auto socket, local_fs_.Stat(Path(kSocketName), /*follow_symlinks=*/false));
  EXPECT_THAT(socket.type, FileType::kSocket);
  MBO_ASSERT_OK_AND_ASSIGN(const auto device, local_fs_.Stat("/dev/null", /*follow_symlinks=*/false));
  EXPECT_THAT(device.type, FileType::kCharDevice);

  MBO_ASSERT_OK_AND_ASSIGN(const auto entries, local_fs_.ReadDir(root_.string() + "/"));
  EXPECT_THAT(
      entries, AllOf(Contains(Field(&Entry::type, FileType::kFifo)), Contains(Field(&Entry::type, FileType::kSocket))));
}

TEST_F(LocalFsTest, ReadDirOnMissingPathIsNotFound) {
  EXPECT_THAT(local_fs_.ReadDir(Path("nope")), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(LocalFsTest, ReadDirOnRegularFileFails) {
  EXPECT_THAT(local_fs_.ReadDir(Path("file.txt")), Not(IsOk()));
}

TEST_F(LocalFsTest, StatRegularFile) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto md, local_fs_.Stat(Path("file.txt"), /*follow_symlinks=*/false));
  EXPECT_THAT(md.type, FileType::kRegular);
  EXPECT_THAT(md.size, 5U);
  EXPECT_THAT(md.nlink, Gt(0U));
  // mtime is populated (well after 2020-01-01, not a zero/epoch default).
  EXPECT_THAT(md.mtime, Gt(absl::FromUnixSeconds(1'577'836'800)));
}

TEST_F(LocalFsTest, StatDirectory) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto md, local_fs_.Stat(Path("sub"), /*follow_symlinks=*/false));
  EXPECT_THAT(md.type, FileType::kDirectory);
}

TEST_F(LocalFsTest, StatSymlinkRespectsFollow) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto link, local_fs_.Stat(Path("link"), /*follow_symlinks=*/false));
  EXPECT_THAT(link.type, FileType::kSymlink);

  MBO_ASSERT_OK_AND_ASSIGN(const auto target, local_fs_.Stat(Path("link"), /*follow_symlinks=*/true));
  EXPECT_THAT(target.type, FileType::kRegular);
  EXPECT_THAT(target.size, 5U);
}

TEST_F(LocalFsTest, StatMissingPathIsNotFound) {
  EXPECT_THAT(local_fs_.Stat(Path("nope"), /*follow_symlinks=*/false), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(LocalFsTest, RemoveDeletesFileAndEmptyDirectory) {
  EXPECT_THAT(local_fs_.Remove(Path("file.txt")), IsOk());
  EXPECT_THAT(local_fs_.Stat(Path("file.txt"), /*follow_symlinks=*/false), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(local_fs_.Remove(Path("sub")), IsOk());  // sub is an empty directory
  EXPECT_THAT(local_fs_.Stat(Path("sub"), /*follow_symlinks=*/false), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(LocalFsTest, RemoveMissingPathErrors) {
  EXPECT_THAT(local_fs_.Remove(Path("nope")), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(LocalFsTest, ReadContentReturnsFileBytes) {
  EXPECT_THAT(local_fs_.ReadContent(Path("file.txt")), IsOkAndHolds(Eq("hello")));
}

TEST_F(LocalFsTest, ReadContentMissingPathErrors) {
  EXPECT_THAT(local_fs_.ReadContent(Path("nope")), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(LocalFsTest, ReadContentRangeReturnsOnlyTheRequestedBytes) {
  EXPECT_THAT(local_fs_.ReadContentRange(Path("file.txt"), 1, 3), IsOkAndHolds("ell"));
  EXPECT_THAT(local_fs_.ReadContentRange(Path("file.txt"), 3, 10), IsOkAndHolds("lo"));
  EXPECT_THAT(local_fs_.ReadContentRange(Path("file.txt"), 5, 10), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(local_fs_.ReadContentRange(Path("file.txt"), 0, 0), IsOkAndHolds(IsEmpty()));
}

TEST_F(LocalFsTest, ReadContentRangeReportsOpenAndOffsetErrors) {
  EXPECT_THAT(local_fs_.ReadContentRange(Path("nope"), 0, 1), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(
      local_fs_.ReadContentRange(Path("file.txt"), std::numeric_limits<std::uint64_t>::max(), 1),
      StatusIs(absl::StatusCode::kOutOfRange, HasSubstr("not representable")));
  EXPECT_THAT(local_fs_.ReadContentRange(Path("sub"), 0, 1), Not(IsOk()));
}

TEST_F(LocalFsTest, AccessChecksEachRequestedPermissionAndMissingPaths) {
  EXPECT_THAT(local_fs_.Access(Path("file.txt"), AccessMode::kRead), IsTrue());
  EXPECT_THAT(local_fs_.Access(Path("file.txt"), AccessMode::kWrite), IsTrue());
  EXPECT_THAT(local_fs_.Access(Path("file.txt"), AccessMode::kExecute), IsFalse());
  EXPECT_THAT(local_fs_.Access(Path("nope"), AccessMode::kRead), IsFalse());
}

TEST_F(LocalFsTest, ReadLinkReturnsTheStoredTarget) {
  EXPECT_THAT(local_fs_.ReadLink(Path("link")), IsOkAndHolds(Eq("file.txt")));
}

TEST_F(LocalFsTest, ReadLinkRejectsARegularFile) {
  EXPECT_THAT(local_fs_.ReadLink(Path("file.txt")), Not(IsOk()));
}

TEST_F(LocalFsTest, FsTypeReportsTheContainingFilesystemAndRejectsMissingPaths) {
  EXPECT_THAT(local_fs_.FsType(root_.string()), IsOkAndHolds(Not(Eq(""))));
  EXPECT_THAT(local_fs_.FsType(Path("nope")), StatusIs(absl::StatusCode::kNotFound));
}

#if defined(__linux__)
TEST_F(LocalFsTest, ReadLinkGrowsItsBufferForALargeTarget) {
  const std::string target(1'500, 'x');
  std::error_code error;
  fs::create_symlink(target, root_ / "long-link", error);
  ASSERT_THAT(error, Eq(std::error_code()));
  EXPECT_THAT(local_fs_.ReadLink(Path("long-link")), IsOkAndHolds(Eq(target)));
}

TEST_F(LocalFsTest, ProcfsMetadataHasNoBirthTime) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto metadata, local_fs_.Stat("/proc/self/stat", /*follow_symlinks=*/false));
  EXPECT_THAT(metadata.btime, Eq(std::nullopt));
}

TEST_F(LocalFsTest, ReadDirRecognizesCharacterDeviceEntries) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto entries, local_fs_.ReadDir("/dev"));
  EXPECT_THAT(entries, Contains(Field(&Entry::type, FileType::kCharDevice)));
}

TEST_F(LocalFsTest, FsTypeUsesHexForAnUnmappedKernelMagic) {
  using ::testing::StartsWith;
  if (!fs::exists("/dev/mqueue")) {
    GTEST_SKIP() << "/dev/mqueue is not mounted";
  }
  EXPECT_THAT(local_fs_.FsType("/dev/mqueue"), IsOkAndHolds(StartsWith("0x")));
}
#endif

TEST_F(LocalFsTest, ReadContentRejectsADirectory) {
  EXPECT_THAT(local_fs_.ReadContent(Path("sub")), Not(IsOk()));
}

TEST_F(LocalFsTest, IsCaseSensitiveProbesTheVolume) {
  // The probe must succeed on a normal directory. The value depends on the volume
  // (case-sensitive ext4 vs case-folding APFS), so it is cross-checked against the
  // volume's actual behaviour in the next test rather than hard-coded here.
  EXPECT_THAT(local_fs_.IsCaseSensitive(root_.string()), IsOk());
}

TEST_F(LocalFsTest, IsCaseSensitiveAgreesWithTheVolumeBehaviour) {
  // file.txt exists in lower case; whether the upper-case name resolves to it is
  // exactly the volume's own case rule, so the probe must report the matching
  // value: a case-folding volume resolves FILE.TXT (not sensitive), a
  // case-sensitive one does not (sensitive).
  const bool upper_resolves = fs::exists(root_ / "FILE.TXT");
  EXPECT_THAT(local_fs_.IsCaseSensitive(root_.string()), IsOkAndHolds(Eq(!upper_resolves)));
}

}  // namespace
}  // namespace xff::vfs
