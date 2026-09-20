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

#include "xff/archive/archive_backend.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/filesystem.h"

namespace xff::archive {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::NotNull;
using ::testing::SizeIs;

// A filesystem that answers nothing: this test is about the SLOT, not about any backend. Only
// FsType is given a value, so a test can tell the stub apart from a real one.
class StubFileSystem : public vfs::FileSystem {
 public:
  [[nodiscard]] absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view /*dir*/) const override {
    return std::vector<vfs::Entry>{};
  }

  [[nodiscard]] absl::StatusOr<vfs::Metadata> Stat(std::string_view /*path*/, bool /*follow*/) const override {
    return absl::UnimplementedError("stub");
  }

  [[nodiscard]] absl::Status Remove(std::string_view /*path*/) const override {
    return absl::UnimplementedError("stub");
  }

  [[nodiscard]] bool Access(std::string_view /*path*/, vfs::AccessMode /*mode*/) const override { return false; }

  [[nodiscard]] absl::StatusOr<std::string> ReadLink(std::string_view /*path*/) const override {
    return absl::UnimplementedError("stub");
  }

  [[nodiscard]] absl::StatusOr<std::string> FsType(std::string_view /*path*/) const override { return "stub"; }

  [[nodiscard]] absl::StatusOr<bool> IsCaseSensitive(std::string_view /*path*/) const override { return true; }

  [[nodiscard]] absl::StatusOr<std::string> ReadContent(std::string_view /*path*/) const override {
    return absl::UnimplementedError("stub");
  }
};

// The registration slot is process-wide, so a test that installs an opener has to put the slot back
// the way it found it - otherwise the case order decides what the next case sees.
struct ArchiveBackendTest : ::testing::Test {
  void TearDown() override {
    RegisterContainerOpener(ContainerOpener());
    RegisterContainerReader("test-a", ContainerOpener(), {});
    RegisterContainerReader("test-b", ContainerOpener(), {});
    RegisterContainerMemberRemover(ContainerMemberRemover());
    RegisterContainerPacker(ContainerPacker(), {}, {});
  }
};

TEST_F(ArchiveBackendTest, PackPlanRejectsNormalizedDuplicatesWithBothSources) {
  EXPECT_THAT(
      PlanPackFiles({{.source = "left/a", .name = "./dir//a"}, {.source = "right/a", .name = "dir/./a/"}}),
      StatusIs(absl::StatusCode::kAlreadyExists, AllOf(HasSubstr("dir/a"), HasSubstr("left/a"), HasSubstr("right/a"))));
}

TEST_F(ArchiveBackendTest, PackPlanFirstWinsPreservesInputOrder) {
  EXPECT_THAT(
      PlanPackFiles(
          {{.source = "z", .name = "./b"}, {.source = "x", .name = "a"}, {.source = "y", .name = "b/"}},
          PackDuplicatePolicy::kFirst),
      IsOkAndHolds(ElementsAre(
          AllOf(Field(&PackFile::source, Eq("z")), Field(&PackFile::name, Eq("b"))),
          AllOf(Field(&PackFile::source, Eq("x")), Field(&PackFile::name, Eq("a"))))));
}

TEST_F(ArchiveBackendTest, PackPlanRejectsNonDirectoryAncestorsInEitherOrder) {
  const PackFile parent{.source = "file", .name = "a"};
  const PackFile child{.source = "child", .name = "a/b"};
  const auto entry_orders = std::to_array<std::vector<PackFile>>({
      {parent, child},
      {child, parent},
  });
  for (const auto& files : entry_orders) {
    EXPECT_THAT(
        PlanPackFiles(files), StatusIs(absl::StatusCode::kAlreadyExists, AllOf(HasSubstr("file"), HasSubstr("child"))));
    EXPECT_THAT(
        PlanPackFiles(files, PackDuplicatePolicy::kFirst),
        IsOkAndHolds(ElementsAre(Field(&PackFile::source, Eq(files.front().source)))));
  }
}

TEST_F(ArchiveBackendTest, PackPlanAllowsDirectoryAncestorsAndSiblingPrefixes) {
  const PackFile parent{.source = "directory", .name = "a", .is_directory = true};
  const PackFile child{.source = "child", .name = "a/b"};
  const auto entry_orders = std::to_array<std::vector<PackFile>>({
      {parent, child},
      {child, parent},
  });
  for (const auto& files : entry_orders) {
    EXPECT_THAT(PlanPackFiles(files), IsOkAndHolds(SizeIs(2)));
  }
  EXPECT_THAT(
      PlanPackFiles({{.source = "one", .name = "a"}, {.source = "two", .name = "ab/c"}}), IsOkAndHolds(SizeIs(2)));
}

TEST_F(ArchiveBackendTest, PackPlanRejectsInvalidDestinations) {
  const std::array invalid_destinations = std::to_array<std::string>({
      std::string(),
      std::string("/absolute"),
      std::string("dir/../file"),
      std::string("bad\0name", 8),
  });
  for (const auto& name : invalid_destinations) {
    EXPECT_THAT(PlanPackFiles({{.source = "source", .name = name}}), StatusIs(absl::StatusCode::kInvalidArgument));
  }
  EXPECT_THAT(PlanPackFiles({}), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(
      PlanPackFiles({{.source = "root", .name = "./"}}), IsOkAndHolds(ElementsAre(Field(&PackFile::name, Eq(".")))));
}

TEST_F(ArchiveBackendTest, DuplicatePackPlanNeverCallsTheWriter) {
  bool called = false;
  RegisterContainerPacker(
      [&](std::string_view, const std::vector<PackFile>&, const PackOptions&) {
        called = true;
        return absl::OkStatus();
      },
      {}, {});
  EXPECT_THAT(
      PackContainer("out.tar", {{.source = "left", .name = "a"}, {.source = "right", .name = "./a"}}),
      StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(called, IsFalse());
}

constexpr std::array kTarGzOnly = std::to_array<std::string_view>({"tar.gz"});

TEST_F(ArchiveBackendTest, WithNoRemoverAContainerCannotBeRewritten) {
  EXPECT_THAT(ContainerRemovalAvailable(), IsFalse());
  EXPECT_THAT(
      RemoveContainerMembers("some.tar", {"gone.txt"}),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("without archive support")));
}

TEST_F(ArchiveBackendTest, RegistrarObjectsInstallEachOptionalArchiveCapability) {
  const ContainerRegistrar reader{
      "test-a",
      [](std::string_view, std::optional<std::string_view>, MemberPathOptions) {
        return std::make_unique<StubFileSystem>();
      },
      {{.name = "test", .suffixes = {".test"}}}};
  const ContainerRemoverRegistrar remover{
      [](std::string_view, const std::vector<std::string>&, const vfs::MutationPolicy&) { return absl::OkStatus(); }};
  const ContainerPackerRegistrar packer{
      [](std::string_view, const std::vector<PackFile>&, const PackOptions&) { return absl::OkStatus(); },
      {"test"},
      {}};

  EXPECT_THAT(ContainerSupportAvailable(), IsTrue());
  EXPECT_THAT(ContainerRemovalAvailable(), IsTrue());
  EXPECT_THAT(ContainerPackingAvailable(), IsTrue());
}

TEST_F(ArchiveBackendTest, ARegisteredRemoverReceivesTheContainerAndMembersUnchanged) {
  std::string removed_from;
  std::vector<std::string> removed_members;
  RegisterContainerMemberRemover(
      [&removed_from, &removed_members](
          std::string_view container, const std::vector<std::string>& members, const vfs::MutationPolicy&) {
        removed_from = std::string(container);
        removed_members = members;
        return absl::OkStatus();
      });

  EXPECT_THAT(ContainerRemovalAvailable(), IsTrue());
  EXPECT_THAT(RemoveContainerMembers("some.tar", {"b.txt", "a.txt"}), IsOk());
  EXPECT_THAT(removed_from, "some.tar");
  EXPECT_THAT(removed_members, ElementsAre("b.txt", "a.txt"));
}

TEST_F(ArchiveBackendTest, ARemoversErrorReachesTheCallerUnchanged) {
  RegisterContainerMemberRemover([](std::string_view, const std::vector<std::string>&, const vfs::MutationPolicy&) {
    return absl::NotFoundError("member is absent");
  });
  EXPECT_THAT(
      RemoveContainerMembers("some.tar", {"missing.txt"}),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("member is absent")));
}

TEST_F(ArchiveBackendTest, WithNoBackendThereIsNoSupportAndOpeningSaysWhy) {
  // The lean build: the `--archive` surface exists and is documented, but nothing can look inside a
  // container. Unimplemented rather than InvalidArgument, because nothing is wrong with the path.
  EXPECT_THAT(ContainerSupportAvailable(), IsFalse());
  EXPECT_THAT(
      OpenContainer("some.tar"), StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("without archive support")));
  EXPECT_THAT(
      OpenContainerBytes("inner.tar", "bytes"),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("without archive support")));
}

TEST_F(ArchiveBackendTest, TheNameGateAcceptsFormatsAndPackagesAlike) {
  // The gate in front of the reader under `--archive=all`: a name is enough to be OFFERED, never
  // enough to be an archive. The gate is DERIVED from the registered read formats - there is no
  // second extension list - so this test registers a representative vocabulary first; the real
  // reader's declaration is pinned by @xff_archive's own register test. Both a bare format suffix
  // and a package that is one underneath count, and the comparison folds case because filesystems
  // shout.
  RegisterContainerReadFormats({
      {.name = "7z", .suffixes = {".7z"}},
      {.name = "cpio", .suffixes = {".rpm"}},
      {.name = "phar", .suffixes = {".phar"}},
      {.name = "tar", .suffixes = {".tar", ".tar.gz", ".tgz", ".txz", ".tbz2", ".tzst", ".tlz"}},
      {.name = "zip", .suffixes = {".zip", ".jar", ".whl"}},
  });
  constexpr std::array kOffered = std::to_array<std::string_view>({
      "a.tar",
      "a.tar.gz",
      "a.tgz",
      "a.zip",
      "A.ZIP",
      "x.jar",
      "x.whl",
      "x.phar",
      "x.rpm",
      "x.7z",
      // The single-word tar shortcuts: `.tzst` was missing, so a `.tzst` met mid-walk under
      // `--archive=all` was never even offered to the reader.
      "x.txz",
      "x.tbz2",
      "x.tzst",
      "x.tlz",
  });
  for (const std::string_view name : kOffered) {
    EXPECT_THAT(LooksLikeContainerName(name), IsTrue()) << name;
  }
  RegisterContainerReadFormats({});  // reset: with nothing registered, nothing dives
  EXPECT_THAT(LooksLikeContainerName("a.tar"), IsFalse());
}

TEST_F(ArchiveBackendTest, TheNameGateRejectsEverydayFiles) {
  // What the gate is FOR: walking a source tree must not open and format-bid every file in it. A name
  // with no suffix, or one that is not a container suffix, is not offered - `--archive-any` is the way
  // to reach an archive whose name says nothing. Registered formats are PRESENT here, so rejection
  // is a decision, not an empty gate.
  RegisterContainerReadFormats({
      {.name = "tar", .suffixes = {".tar", ".tar.gz"}},
      {.name = "zip", .suffixes = {".zip"}},
  });
  constexpr std::array kNotOffered = std::to_array<std::string_view>({
      "walk.cc",
      "walk.h",
      "BUILD.bazel",
      "Makefile",
      "notes",
      "blob",
      "backup.dat",
      "a.tar.",
      ".gz",
  });
  for (const std::string_view name : kNotOffered) {
    EXPECT_THAT(LooksLikeContainerName(name), IsFalse()) << name;
  }
  RegisterContainerReadFormats({});  // reset for the other tests in this process
}

TEST_F(ArchiveBackendTest, ARegisteredOpenerIsUsedAndSeesTheMemberPathOptions) {
  // The options must reach the backend, or a container opened during a run would render member paths
  // with the default separator instead of the one the user asked for.
  std::string opened;
  std::string separator;
  RegisterContainerOpener(
      [&opened, &separator](std::string_view container, std::optional<std::string_view>, MemberPathOptions options) {
        opened = std::string(container);
        separator = std::string(options.separator);
        return std::make_unique<StubFileSystem>();
      });
  EXPECT_THAT(ContainerSupportAvailable(), IsTrue());
  MBO_ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<vfs::FileSystem> opened_fs, OpenContainer("a.tar", MemberPathOptions{.separator = "#"}));
  EXPECT_THAT(opened_fs.get(), NotNull());
  EXPECT_THAT(opened, "a.tar");
  EXPECT_THAT(separator, "#");
}

TEST_F(ArchiveBackendTest, TheBackendsErrorReachesTheCallerUnchanged) {
  // "Not an archive" and "corrupt archive" must stay distinguishable through the seam: the walk treats
  // the first as an ordinary file and reports only the second, so a seam that flattened them would
  // turn every non-archive into an error.
  RegisterContainerOpener([](std::string_view container, std::optional<std::string_view>, MemberPathOptions) {
    if (container == "broken.tar") {
      return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::DataLossError("corrupt"));
    }
    return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::InvalidArgumentError("not an archive"));
  });
  EXPECT_THAT(OpenContainer("broken.tar"), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(OpenContainer("notes.txt"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveBackendTest, OpenContainerBytesHandsTheContentToTheBackend) {
  // The nested-container path: there is no file to open, so the caller passes the bytes it already
  // read out of the parent and the label the members render under.
  std::string label;
  std::string content;
  RegisterContainerOpener(
      [&label, &content](std::string_view container, std::optional<std::string_view> bytes, MemberPathOptions) {
        label = std::string(container);
        content = bytes.has_value() ? std::string(*bytes) : std::string("<no bytes>");
        return std::make_unique<StubFileSystem>();
      });
  EXPECT_THAT(OpenContainerBytes("outer.tar!inner.tar", "TARBYTES"), IsOk());
  EXPECT_THAT(label, "outer.tar!inner.tar");
  EXPECT_THAT(content, "TARBYTES");
  // And the path form still says "no bytes", so a backend can tell the two apart.
  EXPECT_THAT(OpenContainer("a.tar"), IsOk());
  EXPECT_THAT(content, "<no bytes>");
}

TEST_F(ArchiveBackendTest, RegisteringAgainReplacesTheOpener) {
  // Last registration wins, which is what lets a test install a stub over whatever the binary linked.
  RegisterContainerOpener([](std::string_view, std::optional<std::string_view>, MemberPathOptions) {
    return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::DataLossError("first"));
  });
  RegisterContainerOpener([](std::string_view, std::optional<std::string_view>, MemberPathOptions) {
    return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::AbortedError("second"));
  });
  EXPECT_THAT(OpenContainer("a.tar"), StatusIs(absl::StatusCode::kAborted));
}

TEST_F(ArchiveBackendTest, IndependentReadersComposeAndFallThroughOnlyOnNotMyFormat) {
  std::vector<std::string> calls;
  RegisterContainerReader(
      "test-b",
      [&calls](std::string_view container, std::optional<std::string_view>, MemberPathOptions) {
        calls.emplace_back("b");
        return container.ends_with(".b")
                   ? absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(std::make_unique<StubFileSystem>())
                   : absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::InvalidArgumentError("not b"));
      },
      {{.name = "b", .suffixes = {".b"}}});
  RegisterContainerReader(
      "test-a",
      [&calls](std::string_view container, std::optional<std::string_view>, MemberPathOptions) {
        calls.emplace_back("a");
        if (container == "broken.a") {
          return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::DataLossError("broken a"));
        }
        return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::InvalidArgumentError("not a"));
      },
      {{.name = "a", .suffixes = {".a"}}});

  EXPECT_THAT(ContainerSupportAvailable(), IsTrue());
  EXPECT_THAT(
      ContainerReadFormats(),
      ElementsAre(Field("name", &ReadFormatInfo::name, "a"), Field("name", &ReadFormatInfo::name, "b")));
  EXPECT_THAT(LooksLikeContainerName("bundle.a"), IsTrue());
  EXPECT_THAT(LooksLikeContainerName("bundle.b"), IsTrue());
  EXPECT_THAT(OpenContainer("bundle.b"), IsOk());
  EXPECT_THAT(calls, ElementsAre("a", "b"));

  calls.clear();
  EXPECT_THAT(OpenContainerBytes("bundle.b", "contents"), IsOk());
  EXPECT_THAT(calls, ElementsAre("a", "b"));

  calls.clear();
  EXPECT_THAT(OpenContainer("plain"), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not b")));
  EXPECT_THAT(calls, ElementsAre("a", "b"));

  calls.clear();
  EXPECT_THAT(
      OpenContainerBytes("plain", "contents"), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not b")));
  EXPECT_THAT(calls, ElementsAre("a", "b"));

  calls.clear();
  EXPECT_THAT(OpenContainer("broken.a"), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("broken a")));
  EXPECT_THAT(calls, ElementsAre("a"));

  RegisterContainerReader(
      "test-a",
      [&calls](std::string_view, std::optional<std::string_view>, MemberPathOptions) {
        calls.emplace_back("new-a");
        return std::make_unique<StubFileSystem>();
      },
      {{.name = "new-a", .suffixes = {".new-a"}}});
  EXPECT_THAT(
      ContainerReadFormats(),
      ElementsAre(Field("name", &ReadFormatInfo::name, "new-a"), Field("name", &ReadFormatInfo::name, "b")));
  calls.clear();
  EXPECT_THAT(OpenContainer("anything"), IsOk());
  EXPECT_THAT(calls, ElementsAre("new-a"));
}

TEST_F(ArchiveBackendTest, WithNoBackendThereAreNoReadFormatsAndRegistrationInstallsThem) {
  // A lean build's --help=archive simply has no formats table; a registered vocabulary is returned
  // as given. This test target links no backend, so it exercises both sides directly.
  EXPECT_THAT(ContainerReadFormats(), IsEmpty());
  RegisterContainerReadFormats({{.name = "tar", .suffixes = {".tar"}, .detail = "tape archives"}});
  EXPECT_THAT(
      ContainerReadFormats(), ElementsAre(AllOf(
                                  Field("name", &ReadFormatInfo::name, "tar"),
                                  Field("suffixes", &ReadFormatInfo::suffixes, ElementsAre(".tar")))));
  RegisterContainerReadFormats({});  // reset for the other tests in this process
  EXPECT_THAT(ContainerReadFormats(), IsEmpty());
}

TEST_F(ArchiveBackendTest, WithNoPackerNothingCanBeCreatedAndNoFormatIsOffered) {
  EXPECT_THAT(ContainerPackingAvailable(), IsFalse());
  EXPECT_THAT(ContainerPackFormats(), IsEmpty());
  EXPECT_THAT(ContainerPackVocabulary(), IsEmpty());
  EXPECT_THAT(ContainerPackFormatFor("out.tar.gz"), IsEmpty());
  EXPECT_THAT(
      PackContainer("out.tar", {}), StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("without archive support")));
}

TEST_F(ArchiveBackendTest, ARegisteredPackerReceivesThePathAndTheFilesUnchanged) {
  // Names and order are the caller's decision (the walk knows the roots and honours --sort), so the
  // seam must hand them through untouched.
  std::string packed;
  std::vector<PackFile> seen;
  std::vector<PackOption> forwarded;
  RegisterContainerPacker(
      [&packed, &seen, &forwarded](
          std::string_view path, const std::vector<PackFile>& files, const PackOptions& options) {
        packed = std::string(path);
        seen = files;
        forwarded = options.options;
        return absl::OkStatus();
      },
      {"tar", "tar.gz"},
      {PackOptionInfo{.name = "level", .value_syntax = "N", .formats = kTarGzOnly, .detail = "how hard"}});
  EXPECT_THAT(ContainerPackingAvailable(), IsTrue());
  EXPECT_THAT(
      PackContainer(
          "out.tar", {PackFile{.source = "/tmp/b", .name = "b"}, PackFile{.source = "/tmp/a", .name = "a"}},
          PackOptions{.options = {{.name = "level", .value = "9"}}}),
      IsOk());
  // The options travel verbatim: the seam does not interpret a vocabulary it does not own.
  EXPECT_THAT(
      forwarded,
      ElementsAre(AllOf(Field("name", &PackOption::name, "level"), Field("value", &PackOption::value, "9"))));
  // And so does the vocabulary, which is what the CLI checks names against before walking.
  EXPECT_THAT(ContainerPackVocabulary(), ElementsAre(Field("name", &PackOptionInfo::name, "level")));
  EXPECT_THAT(packed, "out.tar");
  EXPECT_THAT(seen, ElementsAre(Field("name", &PackFile::name, "b"), Field("name", &PackFile::name, "a")));
}

TEST_F(ArchiveBackendTest, TheFormatProbeTakesTheLongestRegisteredNameAndFoldsCase) {
  // What the CLI checks an output name against before spending a walk: `out.tar.gz` is a `tar.gz`,
  // never a `gz` or a `tar`, and an unregistered suffix names no format at all.
  RegisterContainerPacker(
      [](std::string_view, const std::vector<PackFile>&, const PackOptions&) { return absl::OkStatus(); },
      {"tar", "gz", "tar.gz", "zip"}, {});
  EXPECT_THAT(ContainerPackFormatFor("out.tar.gz"), "tar.gz");
  EXPECT_THAT(ContainerPackFormatFor("OUT.TAR.GZ"), "tar.gz");
  EXPECT_THAT(ContainerPackFormatFor("out.zip"), "zip");
  EXPECT_THAT(ContainerPackFormatFor("out.rar"), IsEmpty());
  EXPECT_THAT(ContainerPackFormatFor("zip"), IsEmpty());  // the dot is required: a bare `zip` is a name
}

TEST_F(ArchiveBackendTest, ContextualNameProbesCanBeReplacedAndRemoved) {
  RegisterContainerNameProbe("test-context", [](std::string_view path) { return path == "payload"; });
  EXPECT_THAT(ContextualContainerName("payload"), IsTrue());
  EXPECT_THAT(ContextualContainerName("other"), IsFalse());
  RegisterContainerNameProbe("test-context", [](std::string_view path) { return path == "other"; });
  EXPECT_THAT(ContextualContainerName("payload"), IsFalse());
  EXPECT_THAT(ContextualContainerName("other"), IsTrue());
  RegisterContainerNameProbe("test-context", {});
  RegisterContainerNameProbe("absent-context", {});
  EXPECT_THAT(ContextualContainerName("other"), IsFalse());
}

TEST_F(ArchiveBackendTest, StreamSourceHonorsOverrideOpener) {
  RegisterContainerOpener(
      [](std::string_view, std::optional<std::string_view> bytes,
         MemberPathOptions) -> absl::StatusOr<std::unique_ptr<vfs::FileSystem>> {
        if (bytes != "payload") {
          return absl::DataLossError("wrong source bytes");
        }
        return std::make_unique<StubFileSystem>();
      });
  EXPECT_THAT(OpenContainerSource("sample", vfs::MemoryReadSource("payload")), IsOk());
}

}  // namespace
}  // namespace xff::archive
