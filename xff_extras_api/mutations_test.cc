// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/vfs/mutations.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::vfs {
namespace {
using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct MutationsTest : ::testing::Test {
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(
        auto root,
        TemporaryDirectory::Create((std::filesystem::temp_directory_path() / "xff-mutation-test").string(), {}));
    root_ = std::move(root);
  }

  std::string Path(std::string_view name) const { return root_->Path() + "/" + std::string(name); }

  static std::string Read(const std::string& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  std::string ScopePath(std::string_view name) const {
    const auto root = std::filesystem::canonical(root_->Path());
    return name.empty() ? root.string() : (root / name).string();
  }

  std::unique_ptr<TemporaryDirectory> root_;
};

TEST_F(MutationsTest, WritingAndDryRunRefuseEveryCreationSurface) {
  const auto policies = std::to_array<MutationPolicy>({{.block_writing = true}, {.dry_run = true}});
  for (const MutationPolicy& policy : policies) {
    const auto code = policy.dry_run ? absl::StatusCode::kFailedPrecondition : absl::StatusCode::kPermissionDenied;
    EXPECT_THAT(OpenHostOutput(Path("file"), false, policy), StatusIs(code));
    EXPECT_THAT(TemporaryOutput::Create(Path("scratch"), policy), StatusIs(code));
    EXPECT_THAT(TemporaryDirectory::Create(Path("directory"), policy), StatusIs(code));
    if (policy.dry_run) {
      EXPECT_THAT(CreateHostDirectories(Path("parents/child"), policy), StatusIs(code));
    }
    EXPECT_THAT(std::filesystem::is_empty(root_->Path()), IsTrue());
  }
}

TEST_F(MutationsTest, ExclusiveOutputRetainsItsHandleAndRefusesExistingEntries) {
  const MutationPolicy policy{.block_overwrite = true};
  ASSERT_OK_AND_ASSIGN(auto output, OpenHostOutput(Path("file"), false, policy));
  EXPECT_THAT(output->Write("first"), IsOk());
  EXPECT_THAT(output->Write("second"), IsOk());
  EXPECT_THAT(OpenHostOutput(Path("file"), false, policy), StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(Read(Path("file")), Eq("firstsecond"));
  std::filesystem::create_symlink(Path("file"), Path("link"));
  std::filesystem::create_symlink(Path("missing"), Path("dangling"));
  EXPECT_THAT(OpenHostOutput(Path("link"), false, policy), StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(OpenHostOutput(Path("dangling"), false, policy), StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(std::filesystem::exists(Path("missing")), IsFalse());
  EXPECT_THAT(Read(Path("file")), Eq("firstsecond"));
}

TEST_F(MutationsTest, UnrestrictedOutputCanReplaceButDeletionRequiresItsOwnPermission) {
  ASSERT_OK_AND_ASSIGN(auto first, OpenHostOutput(Path("file"), false, {}));
  EXPECT_THAT(first->Write("original"), IsOk());
  first.reset();
  ASSERT_OK_AND_ASSIGN(auto second, OpenHostOutput(Path("file"), false, {}));
  EXPECT_THAT(second->Write("replacement"), IsOk());
  EXPECT_THAT(RemoveHostEntry(Path("file"), {.block_deletion = true}), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(RemoveHostTree(root_->Path(), {.block_deletion = true}), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(RemoveHostEntry(Path("file"), {.dry_run = true}), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(Read(Path("file")), Eq("replacement"));
  EXPECT_THAT(RemoveHostEntry(Path("file"), {}), IsOk());
}

TEST_F(MutationsTest, ArchivePublicationChecksCollisionAtPublicationTime) {
  ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(Path("archive"), {.block_overwrite = true}));
  EXPECT_THAT(output->Write("new archive"), IsOk());
  ASSERT_OK_AND_ASSIGN(auto collision, OpenHostOutput(Path("target"), true, {}));
  EXPECT_THAT(collision->Write("existing"), IsOk());
  EXPECT_THAT(output->Publish(Path("target")), StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(Read(Path("target")), Eq("existing"));
  EXPECT_THAT(output->Publish(Path("new")), IsOk());
  EXPECT_THAT(output->Write("late"), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(output->Publish(Path("another")), StatusIs(absl::StatusCode::kFailedPrecondition));
  output.reset();
  EXPECT_THAT(Read(Path("new")), Eq("new archive"));
}

TEST_F(MutationsTest, ArchivePublicationRefusesBothKindsOfSymlinkCollision) {
  ASSERT_OK_AND_ASSIGN(auto original, OpenHostOutput(Path("original"), true, {}));
  EXPECT_THAT(original->Write("original"), IsOk());
  std::filesystem::create_symlink(Path("original"), Path("link"));
  std::filesystem::create_symlink(Path("missing"), Path("dangling"));
  ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(Path("scratch"), {.block_overwrite = true}));
  EXPECT_THAT(output->Write("archive"), IsOk());
  EXPECT_THAT(output->Publish(Path("link")), StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(output->Publish(Path("dangling")), StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(Read(Path("original")), Eq("original"));
  EXPECT_THAT(std::filesystem::exists(Path("missing")), IsFalse());
}

TEST_F(MutationsTest, OwnScratchCleanupDoesNotRequireUserDeletionPermission) {
  std::string scratch;
  {
    ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(Path("scratch"), {.block_deletion = true}));
    scratch = output->Path();
    EXPECT_THAT(output->Write("archive"), IsOk());
    EXPECT_THAT(output->SetPermissions(0600), IsOk());
    EXPECT_THAT(std::filesystem::exists(scratch), IsTrue());
  }
  EXPECT_THAT(std::filesystem::exists(scratch), IsFalse());
  EXPECT_THAT(std::filesystem::is_empty(root_->Path()), IsTrue());
}

TEST_F(MutationsTest, ArchiveReplacementPublishesAndPreservesUnrelatedFiles) {
  ASSERT_OK_AND_ASSIGN(auto old, OpenHostOutput(Path("archive"), true, {}));
  EXPECT_THAT(old->Write("old"), IsOk());
  ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(Path("scratch"), {}));
  EXPECT_THAT(output->Write("new"), IsOk());
  EXPECT_THAT(output->Publish(Path("archive")), IsOk());
  EXPECT_THAT(Read(Path("archive")), Eq("new"));
}

TEST_F(MutationsTest, MissingParentsAndEntriesReportErrors) {
  EXPECT_THAT(OpenHostOutput(Path("missing/file"), false, {}), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(TemporaryOutput::Create(Path("missing/file"), {}), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(RemoveHostEntry(Path("missing"), {}), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(
      MutationPolicy{.block_overwrite = true}.Write(true),
      StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("overwrite")));
}

TEST_F(MutationsTest, DirectoryOperationsCreateAndRemoveOnlyTheRequestedTree) {
  EXPECT_THAT(CreateHostDirectories(Path("tree/nested"), {}), IsOk());
  ASSERT_OK_AND_ASSIGN(auto output, OpenHostOutput(Path("tree/nested/file"), true, {}));
  EXPECT_THAT(output->Write("owned"), IsOk());
  EXPECT_THAT(
      CreateHostDirectories(Path("tree/nested/file/child"), {}), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(RemoveHostTree(Path("tree"), {}), IsOk());
  EXPECT_THAT(std::filesystem::exists(Path("tree")), IsFalse());
  EXPECT_THAT(std::filesystem::exists(root_->Path()), IsTrue());
}

TEST_F(MutationsTest, AReadOnlyDescriptorRejectsWritingAndLeavesTheDestinationUntouched) {
  ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(Path("scratch"), {}));
  // Replace the borrowed descriptor with a read-only handle while preserving ownership.
  // POSIX open is variadic even when opening an existing file without a mode argument.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int read_only = ::open(output->Path().c_str(), O_RDONLY | O_CLOEXEC);
  ASSERT_THAT(read_only, Ge(0));
  EXPECT_THAT(::dup2(read_only, output->Fd()), Eq(output->Fd()));
  EXPECT_THAT(::close(read_only), Eq(0));
  EXPECT_THAT(output->Write("must fail"), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(Read(output->Path()), Eq(""));
}

TEST_F(MutationsTest, FlushFailureDoesNotPublishAnArchive) {
  ASSERT_OK_AND_ASSIGN(auto output, TemporaryOutput::Create(Path("scratch"), {}));
  std::array<int, 2> pipe_fds{};
#if defined(__linux__)
  ASSERT_THAT(::pipe2(pipe_fds.data(), O_CLOEXEC), Eq(0));
#else
  // macOS has no pipe2; this synchronous error-injection test never starts a child process.
  // NOLINTNEXTLINE(android-cloexec-pipe)
  ASSERT_THAT(::pipe(pipe_fds.data()), Eq(0));
#endif
  EXPECT_THAT(::dup2(pipe_fds.back(), output->Fd()), Eq(output->Fd()));
  EXPECT_THAT(::close(pipe_fds.front()), Eq(0));
  EXPECT_THAT(::close(pipe_fds.back()), Eq(0));
  EXPECT_THAT(output->Publish(Path("target")), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("flush")));
  EXPECT_THAT(std::filesystem::exists(Path("target")), IsFalse());
}

TEST_F(MutationsTest, InvalidRootsAndSymlinkRootsAreRejected) {
  EXPECT_THAT(DirectoryPolicy::Create({{.root = "relative"}}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(DirectoryPolicy::Create({{.root = "/"}}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(DirectoryPolicy::Create({{.root = ScopePath("missing")}}), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(
      DirectoryPolicy::Create({{.root = ScopePath("../escape")}}), StatusIs(absl::StatusCode::kPermissionDenied));
  std::filesystem::create_directory_symlink(ScopePath(""), ScopePath("alias"));
  EXPECT_THAT(DirectoryPolicy::Create({{.root = ScopePath("alias")}}), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(MutationsTest, PinnedRootCannotBeRedirectedByReplacingItsPathWithASymlink) {
  EXPECT_THAT(CreateHostDirectories(ScopePath("allowed"), {}), IsOk());
  EXPECT_THAT(CreateHostDirectories(ScopePath("outside"), {}), IsOk());
  ASSERT_OK_AND_ASSIGN(const auto directories, DirectoryPolicy::Create({{.root = ScopePath("allowed")}}));
  std::filesystem::rename(ScopePath("allowed"), ScopePath("retained"));
  std::filesystem::create_directory_symlink(ScopePath("outside"), ScopePath("allowed"));
  ASSERT_OK_AND_ASSIGN(
      const auto output, OpenHostOutput(ScopePath("allowed/new"), false, {.directories = directories}));
  EXPECT_THAT(output->Write("owned root"), IsOk());
  EXPECT_THAT(Read(ScopePath("retained/new")), Eq("owned root"));
  EXPECT_THAT(std::filesystem::exists(ScopePath("outside/new")), IsFalse());
  EXPECT_THAT(
      RemoveHostEntry(ScopePath("retained"), {.directories = directories}),
      StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(
      RemoveHostTree(ScopePath("retained"), {.directories = directories}),
      StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(Read(ScopePath("retained/new")), Eq("owned root"));
}

TEST_F(MutationsTest, ScopedDirectoriesAreRecursiveAndDryRunCreatesNothing) {
  ASSERT_OK_AND_ASSIGN(const auto directories, DirectoryPolicy::Create({{.root = ScopePath("")}}));
  const MutationPolicy scoped{.block_directory_creation = true, .directories = directories};
  EXPECT_THAT(CreateHostDirectories(ScopePath("nested/deeper"), scoped), IsOk());
  EXPECT_THAT(CreateHostDirectories(ScopePath("nested/deeper"), scoped), IsOk());
  const MutationPolicy preview{.dry_run = true, .directories = directories};
  EXPECT_THAT(CreateHostDirectories(ScopePath("preview"), preview), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(OpenHostOutput(ScopePath("preview"), false, preview), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(TemporaryOutput::Create(ScopePath("preview"), preview), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(RemoveHostTree(ScopePath("nested"), preview), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(std::filesystem::exists(ScopePath("preview")), IsFalse());
}

TEST_F(MutationsTest, ArchiveRestrictionsRemainMandatoryInsideAnAllowedDirectory) {
  ASSERT_OK_AND_ASSIGN(const auto directories, DirectoryPolicy::Create({{.root = ScopePath("")}}));
  const MutationPolicy blocked{.block_writing = true, .archive = true, .directories = directories};
  EXPECT_THAT(TemporaryOutput::Create(ScopePath("archive"), blocked), StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(MutationsTest, TemporaryDirectoriesRequireCreationPermissionButArchiveStagingBelongsToArchiveWriting) {
  ASSERT_OK_AND_ASSIGN(
      const auto directories,
      DirectoryPolicy::Create({{.root = ScopePath(""), .blocks = {false, false, false, true, false}}}));
  EXPECT_THAT(
      TemporaryDirectory::Create(ScopePath("scratch"), {.directories = directories}),
      StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(
      TemporaryOutput::Create(ScopePath("scratch"), {.directories = directories}),
      StatusIs(absl::StatusCode::kPermissionDenied));
  ASSERT_OK_AND_ASSIGN(
      const auto archive, TemporaryOutput::Create(ScopePath("archive"), {.archive = true, .directories = directories}));
  EXPECT_THAT(archive->Write("archive bytes"), IsOk());
  EXPECT_THAT(archive->Publish(ScopePath("result")), IsOk());
}

TEST_F(MutationsTest, ScopedWritingAllowsDescendantsButProtectsRootAndOutsidePaths) {
  ASSERT_OK_AND_ASSIGN(const auto directories, DirectoryPolicy::Create({{.root = ScopePath("")}}));
  const MutationPolicy policy{.block_writing = true, .directories = directories};
  ASSERT_OK_AND_ASSIGN(const auto output, OpenHostOutput(ScopePath("new"), false, policy));
  EXPECT_THAT(output->Write("allowed"), IsOk());
  EXPECT_THAT(Read(ScopePath("new")), Eq("allowed"));
  EXPECT_THAT(OpenHostOutput(ScopePath("") + "-sibling", false, policy), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(RemoveHostEntry(ScopePath(""), policy), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(OpenHostOutput(ScopePath(""), false, policy), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(OpenHostOutput(ScopePath("child/../new"), false, policy), StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(MutationsTest, ScopedOutputNeverOverwritesHardLinkContentOrFollowsParentLinks) {
  EXPECT_THAT(CreateHostDirectories(ScopePath("allowed"), {}), IsOk());
  ASSERT_OK_AND_ASSIGN(const auto original, OpenHostOutput(ScopePath("outside"), false, {}));
  EXPECT_THAT(original->Write("keep"), IsOk());
  std::filesystem::create_hard_link(ScopePath("outside"), ScopePath("allowed/link"));
  std::filesystem::create_directory_symlink(ScopePath(""), ScopePath("allowed/escape"));
  ASSERT_OK_AND_ASSIGN(const auto directories, DirectoryPolicy::Create({{.root = ScopePath("allowed")}}));
  const MutationPolicy policy{.block_writing = true, .directories = directories};
  ASSERT_OK_AND_ASSIGN(const auto replacement, OpenHostOutput(ScopePath("allowed/link"), false, policy));
  EXPECT_THAT(replacement->Write("replacement"), IsOk());
  EXPECT_THAT(Read(ScopePath("outside")), Eq("keep"));
  EXPECT_THAT(Read(ScopePath("allowed/link")), Eq("replacement"));
  EXPECT_THAT(
      OpenHostOutput(ScopePath("allowed/escape/outside"), false, policy).status(),
      StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(Read(ScopePath("outside")), Eq("keep"));
}

TEST_F(MutationsTest, OverlappingScopesIntersectAndOverwriteIsExclusive) {
  EXPECT_THAT(CreateHostDirectories(ScopePath("nested"), {}), IsOk());
  ASSERT_OK_AND_ASSIGN(
      const auto directories, DirectoryPolicy::Create(
                                  {{.root = ScopePath(""), .blocks = {false, true, false, false, false}},
                                   {.root = ScopePath("nested"), .blocks = {true, false, false, false, false}}}));
  const MutationPolicy policy{.directories = directories};
  ASSERT_OK_AND_ASSIGN(const auto first, OpenHostOutput(ScopePath("new"), false, policy));
  EXPECT_THAT(first->Write("keep"), IsOk());
  EXPECT_THAT(OpenHostOutput(ScopePath("new"), false, policy), StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(OpenHostOutput(ScopePath("nested/new"), false, policy), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(RemoveHostEntry(ScopePath("nested"), policy), StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(MutationsTest, RecursiveRemovalChecksFileAndDirectoryPermissionsSeparately) {
  EXPECT_THAT(CreateHostDirectories(ScopePath("tree/child"), {}), IsOk());
  ASSERT_OK_AND_ASSIGN(const auto output, OpenHostOutput(ScopePath("tree/child/keep"), false, {}));
  EXPECT_THAT(output->Write("keep"), IsOk());
  ASSERT_OK_AND_ASSIGN(
      const auto directories,
      DirectoryPolicy::Create({{.root = ScopePath(""), .blocks = {false, false, true, false, false}}}));
  const MutationPolicy policy{.directories = directories};
  EXPECT_THAT(RemoveHostTree(ScopePath("tree"), policy), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(Read(ScopePath("tree/child/keep")), Eq("keep"));
  EXPECT_THAT(RemoveHostEntry(ScopePath("tree/child/keep"), {.block_directory_deletion = true}), IsOk());
  EXPECT_THAT(
      RemoveHostEntry(ScopePath("tree/child"), {.block_directory_deletion = true}),
      StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(RemoveHostTree(ScopePath("tree"), policy), IsOk());
}

TEST_F(MutationsTest, ScratchPublicationRechecksDestinationAndOwnedCleanupStaysConfined) {
  EXPECT_THAT(CreateHostDirectories(ScopePath("allowed"), {}), IsOk());
  ASSERT_OK_AND_ASSIGN(const auto directories, DirectoryPolicy::Create({{.root = ScopePath("allowed")}}));
  const MutationPolicy policy{.block_deletion = true, .block_writing = true, .directories = directories};
  ASSERT_OK_AND_ASSIGN(auto scratch, TemporaryOutput::Create(ScopePath("allowed/scratch"), policy));
  EXPECT_THAT(scratch->Write("data"), IsOk());
  EXPECT_THAT(scratch->Publish(ScopePath("outside")), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(scratch->Publish(ScopePath("allowed/result")), IsOk());
  scratch.reset();
  EXPECT_THAT(Read(ScopePath("allowed/result")), Eq("data"));
  EXPECT_THAT(std::filesystem::exists(ScopePath("outside")), IsFalse());
}

}  // namespace
}  // namespace xff::vfs
