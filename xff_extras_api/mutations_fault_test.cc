// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/vfs/mutations.h"

namespace xff::vfs::fault_test {
namespace {
enum class Operation { kNone, kOpen, kOpenAt, kDuplicate, kStat, kStatAt, kMkdir, kRename, kStream, kRead };

struct Failure {
  Operation operation = Operation::kNone;
  int error = EIO;
  int skip = 0;
  bool persistent = false;
};

Failure& State() {
  static thread_local Failure current;
  return current;
}

bool Fail(Operation operation) {
  auto& failure = State();
  if (failure.operation != operation) {
    return false;
  }
  if (failure.skip > 0) {
    --failure.skip;
    return false;
  }
  errno = failure.error;
  if (!failure.persistent) {
    failure.operation = Operation::kNone;
  }
  return true;
}

struct Injection {
  explicit Injection(Failure value) { State() = value; }

  Injection(const Injection&) = delete;
  Injection& operator=(const Injection&) = delete;
  Injection(Injection&&) = delete;
  Injection& operator=(Injection&&) = delete;

  ~Injection() { State() = {}; }
};

template<typename Function>
auto Inject(Failure value, Function&& function) {
  const Injection injection(value);
  return std::forward<Function>(function)();
}
}  // namespace
}  // namespace xff::vfs::fault_test

// Link wrapping is confined to this test executable. Production exposes no fault-injection switch.
// The wrappers also compile on macOS, where the tests skip because Apple's linker has no --wrap.
// Reserved symbols and the token-pasting alias are the linker ABI, not application identifiers.
// NOLINTBEGIN(readability-identifier-naming,cppcoreguidelines-pro-type-vararg,hicpp-vararg,bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,cppcoreguidelines-macro-usage)
#if defined(__linux__)
# define XFF_REAL(name) __real_##name
extern "C" {
decltype(::open) __real_open;
decltype(::openat) __real_openat;
decltype(::fcntl) __real_fcntl;
decltype(::fstat) __real_fstat;
decltype(::fstatat) __real_fstatat;
decltype(::mkdirat) __real_mkdirat;
decltype(::renameat) __real_renameat;
decltype(::fdopendir) __real_fdopendir;
decltype(::readdir) __real_readdir;
}
#else
# define XFF_REAL(name) ::name
#endif

// POSIX ABI names, pointer parameters, and varargs are required by linker wrapping.
using ::xff::vfs::fault_test::Fail;
using ::xff::vfs::fault_test::Operation;

// Linux va_list is an array; the required va_* macros decay it at the POSIX ABI boundary.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
// XFF_ABI_POINTER: exact POSIX open wrapper ABI.
extern "C" int __wrap_open(const char* path, int flags, ...) {
  if (Fail(Operation::kOpen)) {
    return -1;
  }
  if ((static_cast<unsigned>(flags) & static_cast<unsigned>(O_CREAT)) == 0) {
    return XFF_REAL(open)(path, flags);
  }
  va_list args;
  va_start(args, flags);
  const mode_t mode = va_arg(args, int);
  va_end(args);
  return XFF_REAL(open)(path, flags, mode);
}

// XFF_ABI_POINTER: exact POSIX openat wrapper ABI.
extern "C" int __wrap_openat(int parent, const char* path, int flags, ...) {
  if (Fail(Operation::kOpenAt)) {
    return -1;
  }
  if ((static_cast<unsigned>(flags) & static_cast<unsigned>(O_CREAT)) == 0) {
    return XFF_REAL(openat)(parent, path, flags);
  }
  va_list args;
  va_start(args, flags);
  const mode_t mode = va_arg(args, int);
  va_end(args);
  return XFF_REAL(openat)(parent, path, flags, mode);
}

extern "C" int __wrap_fcntl(int fd, int command, ...) {
  if (command == F_DUPFD_CLOEXEC && Fail(Operation::kDuplicate)) {
    return -1;
  }
  if (command == F_GETFD || command == F_GETFL || command == F_GETOWN) {
    return XFF_REAL(fcntl)(fd, command);
  }
  va_list args;
  va_start(args, command);
  if (command == F_GETLK || command == F_SETLK || command == F_SETLKW) {
    // XFF_ABI_POINTER: POSIX fcntl lock commands require a borrowed flock argument.
    auto* lock = va_arg(args, struct flock*);
    va_end(args);
    return XFF_REAL(fcntl)(fd, command, lock);
  }
  const int argument = va_arg(args, int);
  va_end(args);
  return XFF_REAL(fcntl)(fd, command, argument);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)

// XFF_ABI_POINTER: exact POSIX fstat wrapper ABI.
extern "C" int __wrap_fstat(int fd, struct stat* metadata) {
  return Fail(Operation::kStat) ? -1 : XFF_REAL(fstat)(fd, metadata);
}

// XFF_ABI_POINTER: exact POSIX fstatat wrapper ABI.
extern "C" int __wrap_fstatat(int parent, const char* name, struct stat* metadata, int flags) {
  return Fail(Operation::kStatAt) ? -1 : XFF_REAL(fstatat)(parent, name, metadata, flags);
}

// XFF_ABI_POINTER: exact POSIX mkdirat wrapper ABI.
extern "C" int __wrap_mkdirat(int parent, const char* name, mode_t mode) {
  return Fail(Operation::kMkdir) ? -1 : XFF_REAL(mkdirat)(parent, name, mode);
}

// XFF_ABI_POINTER: exact POSIX renameat wrapper ABI.
extern "C" int __wrap_renameat(int source, const char* source_name, int target, const char* target_name) {
  return Fail(Operation::kRename) ? -1 : XFF_REAL(renameat)(source, source_name, target, target_name);
}

// XFF_ABI_POINTER: exact POSIX fdopendir wrapper ABI.
extern "C" DIR* __wrap_fdopendir(int fd) {
  return Fail(Operation::kStream) ? nullptr : XFF_REAL(fdopendir)(fd);
}

// XFF_ABI_POINTER: exact POSIX readdir wrapper ABI; each stream is private to one caller.
extern "C" struct dirent* __wrap_readdir(DIR* stream) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  return Fail(Operation::kRead) ? nullptr : XFF_REAL(readdir)(stream);
}

// NOLINTEND(readability-identifier-naming,cppcoreguidelines-pro-type-vararg,hicpp-vararg,bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,cppcoreguidelines-macro-usage)
#undef XFF_REAL

namespace xff::vfs {
namespace {
using fault_test::Inject;
using fault_test::Operation;
using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::StartsWith;

struct MutationFaultTest : ::testing::Test {
  void SetUp() override {
#if !defined(__linux__)
    GTEST_SKIP() << "requires the Linux linker's --wrap support";
#endif
    ASSERT_OK_AND_ASSIGN(
        root_, TemporaryDirectory::Create((std::filesystem::temp_directory_path() / "xff-fault-test").string(), {}));
    root_path_ = std::filesystem::canonical(root_->Path()).string();
    std::filesystem::create_directory(Path("scope"));
    std::filesystem::create_directory(Path("scope/tree"));
    std::ofstream(Path("scope/tree/keep")) << "keep";
    std::ofstream(Path("scope/keep")) << "keep";
    ASSERT_OK_AND_ASSIGN(directories_, DirectoryPolicy::Create({{.root = Path("scope")}}));
  }

  std::string Path(std::string_view name) const { return root_path_ + "/" + std::string(name); }

  MutationPolicy Policy() const { return {.directories = directories_}; }

  std::unique_ptr<TemporaryDirectory> root_;
  std::shared_ptr<const DirectoryPolicy> directories_;
  std::string root_path_;
};

TEST_F(MutationFaultTest, RootSetupAndResolutionReportDescriptorFailures) {
  EXPECT_THAT(
      Inject({.operation = Operation::kOpen}, [&] { return DirectoryPolicy::Create({{.root = Path("scope")}}); }),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("cannot open filesystem root")));
  EXPECT_THAT(
      Inject({.operation = Operation::kDuplicate}, [&] { return DirectoryPolicy::Create({{.root = Path("scope")}}); }),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("cannot retain policy directory")));
  EXPECT_THAT(
      Inject({.operation = Operation::kOpen}, [&] { return directories_->Resolve(Path("outside"), Policy()); }),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("cannot open filesystem root")));
  EXPECT_THAT(
      Inject({.operation = Operation::kStatAt}, [&] { return directories_->Resolve(Path("scope/keep"), Policy()); }),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("cannot inspect mutation target")));
  EXPECT_THAT(
      Inject({.operation = Operation::kStat}, [&] { return directories_->Resolve(Path("scope/tree"), Policy()); }),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("cannot inspect protected root")));
}

TEST_F(MutationFaultTest, TraversalAndEnumerationFailuresLeaveTheTreeIntact) {
  for (const Operation operation : std::to_array<Operation>(
           {Operation::kStatAt, Operation::kOpenAt, Operation::kDuplicate, Operation::kStream, Operation::kRead})) {
    EXPECT_THAT(
        Inject({.operation = operation}, [&] { return RemoveHostTree(Path("scope/tree"), {}); }),
        StatusIs(absl::StatusCode::kUnknown));
    EXPECT_THAT(std::filesystem::exists(Path("scope/tree/keep")), IsTrue());
  }
}

TEST_F(MutationFaultTest, ScopedMetadataFailureDoesNotDeleteAnExistingEntry) {
  EXPECT_THAT(
      Inject(
          {.operation = Operation::kStatAt, .skip = 1}, [&] { return RemoveHostEntry(Path("scope/keep"), Policy()); }),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("cannot inspect scoped deletion")));
  EXPECT_THAT(std::filesystem::exists(Path("scope/keep")), IsTrue());
}

TEST_F(MutationFaultTest, ReplacementFailuresPreserveTheOldFileAndRemoveStaging) {
  EXPECT_THAT(
      Inject(
          {.operation = Operation::kOpenAt, .error = EACCES, .skip = 1},
          [&] { return OpenHostOutput(Path("scope/keep"), false, Policy()); }),
      StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("cannot create scoped replacement")));
  EXPECT_THAT(
      Inject({.operation = Operation::kRename}, [&] { return OpenHostOutput(Path("scope/keep"), false, Policy()); }),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("cannot publish scoped replacement")));
  std::string content;
  std::ifstream(Path("scope/keep")) >> content;
  EXPECT_THAT(content, Eq("keep"));
  EXPECT_THAT(std::filesystem::exists(Path("scope/keep")), IsTrue());
  for (const auto& entry : std::filesystem::directory_iterator(Path("scope"))) {
    EXPECT_THAT(entry.path().filename().string(), Not(StartsWith(".xff-output-")));
  }
}

TEST_F(MutationFaultTest, ReplacementCollisionsRetryButHaveABoundedFailure) {
  EXPECT_THAT(
      Inject(
          {.operation = Operation::kOpenAt, .error = EEXIST, .skip = 1, .persistent = true},
          [&] { return OpenHostOutput(Path("scope/keep"), false, Policy()); }),
      StatusIs(absl::StatusCode::kAlreadyExists, HasSubstr("cannot reserve scoped replacement")));
  std::string content;
  std::ifstream(Path("scope/keep")) >> content;
  EXPECT_THAT(content, Eq("keep"));
  EXPECT_THAT(
      Inject(
          {.operation = Operation::kOpenAt, .error = EEXIST, .skip = 1},
          [&] { return OpenHostOutput(Path("scope/keep"), false, Policy()); }),
      IsOk());
}

TEST_F(MutationFaultTest, ScratchCreationReportsFailuresAndRetriesCollisions) {
  EXPECT_THAT(
      Inject(
          {.operation = Operation::kMkdir, .error = EACCES},
          [&] { return TemporaryDirectory::Create(Path("scope/scratch"), Policy()); }),
      StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("cannot create scoped scratch")));
  EXPECT_THAT(
      Inject(
          {.operation = Operation::kMkdir, .error = EEXIST, .persistent = true},
          [&] { return TemporaryDirectory::Create(Path("scope/scratch"), Policy()); }),
      StatusIs(absl::StatusCode::kAlreadyExists, HasSubstr("cannot reserve scratch directory")));
  EXPECT_THAT(
      Inject(
          {.operation = Operation::kMkdir, .error = EEXIST},
          [&] { return TemporaryDirectory::Create(Path("scope/scratch"), Policy()); }),
      IsOk());
  EXPECT_THAT(
      Inject(
          {.operation = Operation::kOpenAt},
          [&] { return TemporaryDirectory::Create(Path("scope/scratch"), Policy()); }),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("cannot retain scratch directory")));
  EXPECT_THAT(
      Inject(
          {.operation = Operation::kDuplicate, .skip = 1},
          [&] { return TemporaryDirectory::Create(Path("scope/scratch"), Policy()); }),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("cannot retain scratch directory")));
}

TEST_F(MutationFaultTest, OrdinaryScratchReportsEitherRetentionFailure) {
  for (const int skip : std::to_array<int>({0, 1})) {
    EXPECT_THAT(
        Inject(
            {.operation = Operation::kOpen, .skip = skip},
            [&] { return TemporaryDirectory::Create(Path("ordinary"), {}); }),
        StatusIs(absl::StatusCode::kUnknown, HasSubstr("cannot retain scratch directory")));
  }
}
}  // namespace
}  // namespace xff::vfs
