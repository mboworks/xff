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

// POSIX (opendir/lstat/st_atim/d_type/...) and statx() are hidden by glibc
// under the strict `-std=c++23` we build with; request them explicitly. No
// effect on macOS, whose headers expose POSIX regardless of language mode.
#if defined(__linux__) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE 1
#endif

#include "xff/vfs/local_fs.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mbo/status/status_macros.h"
#include "xff/vfs/mutations.h"
#if defined(__APPLE__)
# include <sys/mount.h>  // statfs + f_fstypename (BSD/macOS report the name directly)
# include <sys/param.h>
#else
# include <sys/vfs.h>  // statfs + f_type magic (Linux reports a number, not a name)
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::vfs {
namespace {

FileType TypeFromMode(mode_t mode) {
  if (S_ISREG(mode)) {
    return FileType::kRegular;
  }
  if (S_ISDIR(mode)) {
    return FileType::kDirectory;
  }
  if (S_ISLNK(mode)) {
    return FileType::kSymlink;
  }
  if (S_ISBLK(mode)) {
    return FileType::kBlockDevice;
  }
  if (S_ISCHR(mode)) {
    return FileType::kCharDevice;
  }
  if (S_ISFIFO(mode)) {
    return FileType::kFifo;
  }
  if (S_ISSOCK(mode)) {
    return FileType::kSocket;
  }
  return FileType::kUnknown;
}

FileType TypeFromDirent(unsigned char d_type) {
  switch (d_type) {
    case DT_BLK: return FileType::kBlockDevice;
    case DT_CHR: return FileType::kCharDevice;
    case DT_DIR: return FileType::kDirectory;
    case DT_FIFO: return FileType::kFifo;
    case DT_LNK: return FileType::kSymlink;
    case DT_REG: return FileType::kRegular;
    case DT_SOCK: return FileType::kSocket;
    default: return FileType::kUnknown;
  }
}

std::string JoinPath(std::string_view dir, std::string_view name) {
  if (dir.empty()) {
    return std::string(name);
  }
  if (dir.back() == '/') {
    return absl::StrCat(dir, name);
  }
  return absl::StrCat(dir, "/", name);
}

// Birth/creation time, where the platform + filesystem record it.
std::optional<absl::Time> BirthTime(
    [[maybe_unused]] const struct stat& st,
    [[maybe_unused]] const std::string& path,
    [[maybe_unused]] bool follow_symlinks) {
#if defined(__APPLE__)
  return absl::TimeFromTimespec(st.st_birthtimespec);
#elif defined(__linux__) && defined(STATX_BTIME)
  // Zero-initialized on purpose, for MemorySanitizer. `statx` has no MSan interceptor (unlike
  // `stat` / `lstat` / `fstat`), so the sanitizer never learns that the kernel filled `stx` in
  // and every read below - starting with `stx.stx_mask` - is reported as use-of-uninitialized.
  // Pre-initializing gives the shadow a defined state; the kernel then overwrites the bytes with
  // the real values, so the result is unchanged and only the false report goes away.
  struct statx stx = {};
  const int flags = follow_symlinks ? 0 : AT_SYMLINK_NOFOLLOW;
  if (::statx(AT_FDCWD, path.c_str(), flags, STATX_BTIME, &stx) == 0 && (stx.stx_mask & STATX_BTIME) != 0U) {
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(stx.stx_btime.tv_sec);
    ts.tv_nsec = static_cast<long>(stx.stx_btime.tv_nsec);  // NOLINT(google-runtime-int)
    return absl::TimeFromTimespec(ts);
  }
  return std::nullopt;
#else
  return std::nullopt;
#endif
}

Metadata MetadataFromStat(const struct stat& st, std::optional<absl::Time> btime) {
  Metadata md;
  md.type = TypeFromMode(st.st_mode);
  md.size = static_cast<std::uint64_t>(st.st_size);
  md.blocks = static_cast<std::uint64_t>(st.st_blocks);  // 512-byte blocks, for -ls
  md.mode = static_cast<std::uint32_t>(st.st_mode);
  md.nlink = static_cast<std::uint64_t>(st.st_nlink);
  md.uid = static_cast<std::uint32_t>(st.st_uid);
  md.gid = static_cast<std::uint32_t>(st.st_gid);
  md.ino = static_cast<std::uint64_t>(st.st_ino);
  md.dev = static_cast<std::uint64_t>(st.st_dev);
#if defined(__APPLE__)
  md.atime = absl::TimeFromTimespec(st.st_atimespec);
  md.mtime = absl::TimeFromTimespec(st.st_mtimespec);
  md.ctime = absl::TimeFromTimespec(st.st_ctimespec);
#else
  md.atime = absl::TimeFromTimespec(st.st_atim);
  md.mtime = absl::TimeFromTimespec(st.st_mtim);
  md.ctime = absl::TimeFromTimespec(st.st_ctim);
#endif
  md.btime = btime;
  return md;
}

#if !defined(__APPLE__)
struct FsMagic {
  std::uint64_t magic;
  std::string_view name;
};

// A stable subset of the kernel's filesystem magic numbers mapped to the names
// GNU find reports (Linux `statfs` yields a number, not a name like BSD/macOS).
// These constants are kernel ABI (see `linux/magic.h`) and inlined so we do not
// depend on a given kernel header exposing every one. An unrecognised magic
// falls back to a hex string, so `-fstype` simply will not match a name we do
// not know -- which is the correct outcome rather than a wrong match.
constexpr auto kFsMagics = std::to_array<FsMagic>({
    {.magic = 0x0000'6969, .name = "nfs"},     {.magic = 0x0000'9660, .name = "iso9660"},
    {.magic = 0x0000'9FA0, .name = "proc"},    {.magic = 0x0000'4D44, .name = "msdos"},
    {.magic = 0x0000'1CD1, .name = "devpts"},  {.magic = 0x0000'EF53, .name = "ext2/ext3"},
    {.magic = 0x0027'E0EB, .name = "cgroup"},  {.magic = 0x0102'1994, .name = "tmpfs"},
    {.magic = 0x5846'5342, .name = "xfs"},     {.magic = 0x6265'6572, .name = "sysfs"},
    {.magic = 0x6367'7270, .name = "cgroup2"}, {.magic = 0x6462'6720, .name = "debugfs"},
    {.magic = 0x6573'7546, .name = "fuse"},    {.magic = 0x7371'7368, .name = "squashfs"},
    {.magic = 0x794C'7630, .name = "overlay"}, {.magic = 0x8584'58F6, .name = "ramfs"},
    {.magic = 0x9123'683E, .name = "btrfs"},   {.magic = 0xCAFE'4A11, .name = "bpf"},
    {.magic = 0xF2F5'2010, .name = "f2fs"},
});
#endif

}  // namespace

absl::StatusOr<std::vector<Entry>> LocalFs::ReadDir(std::string_view dir) const {
  const std::string dir_str(dir);
  DIR* dirp = ::opendir(dir_str.c_str());
  if (dirp == nullptr) {
    return absl::ErrnoToStatus(errno, absl::StrCat("opendir('", dir, "')"));
  }

  std::vector<Entry> entries;
  errno = 0;
  // Thread-safe here: each directory is opened and iterated by exactly one worker, so this DIR* is
  // never shared across threads. readdir is only unsafe for threads sharing a stream (readdir_r, the
  // per-stream alternative, is deprecated).
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  for (const struct dirent* de = ::readdir(dirp); de != nullptr; de = ::readdir(dirp)) {
    // d_name is a fixed C char[] field; the array-to-pointer decay is inherent to reading it.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    const std::string_view name(de->d_name);
    if (name == "." || name == "..") {
      errno = 0;
      continue;
    }
    Entry entry;
    entry.path = JoinPath(dir, name);
    entry.name = std::string(name);
    entry.type = TypeFromDirent(de->d_type);
    entry.source = Source::kLocalFs;
    entry.read_only = false;
    entries.push_back(std::move(entry));
    errno = 0;
  }
  const int read_errno = errno;
  ::closedir(dirp);
  if (read_errno != 0) {
    return absl::ErrnoToStatus(read_errno, absl::StrCat("readdir('", dir, "')"));
  }
  return entries;
}

absl::StatusOr<Metadata> LocalFs::Stat(std::string_view path, bool follow_symlinks) const {
  const std::string path_str(path);
  struct stat st{};
  // XFF_HOST_IO: LocalFs implements the VFS metadata operation at the POSIX boundary.
  const int rc = follow_symlinks ? ::stat(path_str.c_str(), &st) : ::lstat(path_str.c_str(), &st);
  if (rc != 0) {
    return absl::ErrnoToStatus(errno, absl::StrCat(follow_symlinks ? "stat('" : "lstat('", path, "')"));
  }
  return MetadataFromStat(st, BirthTime(st, path_str, follow_symlinks));
}

absl::Status LocalFs::Remove(std::string_view path) const {
  return RemoveHostEntry(path, {});
}

absl::StatusOr<std::unique_ptr<OutputFile>> LocalFs::OpenOutput(std::string_view path, bool exclusive) const {
  return OpenHostOutput(path, exclusive, {});
}

absl::Status LocalFs::WriteContent(std::string_view path, std::string_view content) const {
  MBO_ASSIGN_OR_RETURN(const auto output, OpenOutput(path, false));
  return output->Write(content);
}

bool LocalFs::Access(std::string_view path, AccessMode mode) const {
  const int flag = mode == AccessMode::kRead ? R_OK : (mode == AccessMode::kWrite ? W_OK : X_OK);
  // XFF_HOST_IO: LocalFs implements the VFS access probe at the POSIX boundary.
  return ::access(std::string(path).c_str(), flag) == 0;
}

absl::StatusOr<std::string> LocalFs::ReadLink(std::string_view path) const {
  const std::string path_str(path);
  std::string buffer(1'024, '\0');
  for (;;) {
    const ssize_t len = ::readlink(path_str.c_str(), buffer.data(), buffer.size());
    if (len < 0) {
      return absl::ErrnoToStatus(errno, absl::StrCat("readlink('", path, "')"));
    }
    if (static_cast<std::string::size_type>(len) < buffer.size()) {
      buffer.resize(static_cast<std::string::size_type>(len));
      return buffer;
    }
    buffer.resize(buffer.size() * 2);  // target may have been truncated; grow and retry
  }
}

absl::StatusOr<std::string> LocalFs::FsType(std::string_view path) const {
  const std::string path_str(path);
  struct statfs sfs{};
  if (::statfs(path_str.c_str(), &sfs) != 0) {
    return absl::ErrnoToStatus(errno, absl::StrCat("statfs('", path, "')"));
  }
#if defined(__APPLE__)
  // f_fstypename is a fixed C char[] field; the array-to-pointer decay is inherent to reading it.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
  return std::string(sfs.f_fstypename);  // BSD/macOS report the type name directly
#else
  const std::uint64_t magic = static_cast<std::uint64_t>(sfs.f_type) & 0xFFFF'FFFFULL;
  for (const FsMagic& entry : kFsMagics) {
    if (entry.magic == magic) {
      return std::string(entry.name);
    }
  }
  return absl::StrCat("0x", absl::Hex(magic));  // unknown magic: a non-matching sentinel
#endif
}

absl::StatusOr<bool> LocalFs::IsCaseSensitive(std::string_view path) const {
#if defined(_PC_CASE_SENSITIVE)
  // macOS / BSD expose the volume's case rule directly: pathconf returns 1 for a
  // case-sensitive volume and 0 for a case-folding one. A -1 return is either an
  // error (errno set) or an indeterminate answer (errno left at 0); both fall back
  // to the conservative case-sensitive default (error propagated; indeterminate
  // reported as case-sensitive), which is always find-faithful.
  const std::string path_str(path);
  errno = 0;
  const long rc = ::pathconf(path_str.c_str(), _PC_CASE_SENSITIVE);  // NOLINT(google-runtime-int)
  if (rc < 0) {
    if (errno != 0) {
      return absl::ErrnoToStatus(errno, absl::StrCat("pathconf(_PC_CASE_SENSITIVE, '", path, "')"));
    }
    return true;  // indeterminate -> conservative byte-exact
  }
  return rc != 0;
#else
  // Linux / glibc has no _PC_CASE_SENSITIVE. ext4 / xfs / btrfs are case-sensitive
  // by default; per-directory case folding (ext4 +F / statx STATX_ATTR_CASEFOLD) is
  // a future refinement. Report case-sensitive, which is also the safe default and
  // matches find's byte-exact matching.
  (void)path;
  return true;
#endif
}

absl::StatusOr<std::string> LocalFs::ReadContent(std::string_view path) const {
  const std::string path_str(path);
  // O_CLOEXEC so the fd is not leaked into -exec'd children; open() is variadic by declaration.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int fd = ::open(path_str.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, absl::StrCat("open('", path, "')"));
  }
  std::string content;
  std::array<char, std::size_t{64} * 1'024> buffer{};
  for (;;) {
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0) {
      const int read_errno = errno;
      ::close(fd);
      return absl::ErrnoToStatus(read_errno, absl::StrCat("read('", path, "')"));
    }
    if (count == 0) {
      break;  // end of file
    }
    content.append(buffer.data(), static_cast<std::string::size_type>(count));
  }
  ::close(fd);
  return content;
}

absl::StatusOr<std::string> LocalFs::ReadContentRange(std::string_view path, std::uint64_t offset, std::size_t length)
    const {
  const std::string path_str(path);
  // O_CLOEXEC so the fd is not leaked into -exec'd children; open() is variadic by declaration.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int fd = ::open(path_str.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, absl::StrCat("open('", path, "')"));
  }
  if (length == 0) {
    ::close(fd);
    return std::string();
  }
  if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
    ::close(fd);
    return absl::OutOfRangeError(absl::StrCat("read offset is not representable for '", path, "': ", offset));
  }

  std::string content(length, '\0');
  std::size_t total = 0;
  while (total < length) {
    const auto max_offset = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
    if (total > max_offset - offset) {
      break;
    }
    const std::size_t chunk = std::min(length - total, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
    const ssize_t count =
        ::pread(fd, std::span<char>(content).subspan(total).data(), chunk, static_cast<off_t>(offset + total));
    if (count < 0) {
      if (errno == EINTR) {  // LCOV_EXCL_BR_LINE: requires interrupting the individual pread syscall.
        continue;            // LCOV_EXCL_LINE
      }
      const int read_errno = errno;
      ::close(fd);
      return absl::ErrnoToStatus(read_errno, absl::StrCat("pread('", path, "')"));
    }
    if (count == 0) {
      break;
    }
    total += static_cast<std::size_t>(count);
  }
  ::close(fd);
  content.resize(total);
  return content;
}

}  // namespace xff::vfs
