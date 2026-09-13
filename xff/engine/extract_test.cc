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

#include "xff/engine/extract.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

namespace stdfs = ::std::filesystem;
using ::mbo::testing::StatusIs;
using ::testing::EndsWith;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::Not;
using ::testing::SizeIs;

// A filesystem that answers content by path and nothing else, standing in for a mounted container:
// what the extractor needs is exactly ReadContent, and a member is not a path the real filesystem
// has.
class MemberFs : public vfs::FileSystem {
 public:
  void Add(std::string path, std::string content) { content_.emplace_back(std::move(path), std::move(content)); }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    for (const auto& [member, content] : content_) {
      if (member == path) {
        return content;
      }
    }
    return absl::NotFoundError(absl::StrCat("no such member: ", path));
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::UnimplementedError("unused");
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return absl::UnimplementedError("unused");
  }

  absl::Status Remove(std::string_view) const override { return absl::UnimplementedError("unused"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

 private:
  std::vector<std::pair<std::string, std::string>> content_;
};

struct ExtractTest : ::testing::Test {
  static std::string Read(const std::string& path) {
    const std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
  }

  MemberFs fs_;
};

TEST_F(ExtractTest, TheFirstWritableCandidateWithRoomWins) {
  // The preference exists so a member can be written to a memory-backed directory (tmpfs) instead of a
  // disk. The candidates are injected here rather than probed, so the test describes a layout instead
  // of depending on the machine it runs on: a nonexistent candidate is skipped, a real one is taken.
  const std::string real(::testing::TempDir());
  EXPECT_THAT(
      ChooseExtractDirectory(/*member_size=*/16, std::vector<std::string>{"/nonexistent-xff-candidate", real}), real);
}

TEST_F(ExtractTest, TheLastCandidateIsTheFallbackEvenWhenItLooksTooSmall) {
  // The final candidate is the ordinary temporary directory: by then there is nowhere else to go, so it
  // is used whatever it reports free and a write that fails reports the real error.
  const std::string real(::testing::TempDir());
  const std::uint64_t huge = std::uint64_t{1} << 62U;
  EXPECT_THAT(ChooseExtractDirectory(huge, std::vector<std::string>{real}), real);
}

TEST_F(ExtractTest, AMemberTooLargeForACandidateFallsThroughToTheNext) {
  // A tmpfs is RAM shared with the whole machine, so a member that would take most of it must land on
  // disk instead - the check is what keeps the preference from being a way to fill memory.
  const std::string real(::testing::TempDir());
  const std::uint64_t huge = std::uint64_t{1} << 62U;
  EXPECT_THAT(
      ChooseExtractDirectory(huge, std::vector<std::string>{real, "/nonexistent-xff-fallback"}),
      "/nonexistent-xff-fallback");
}

TEST_F(ExtractTest, TheDefaultCandidatesEndAtTheOrdinaryTemporaryDirectory) {
  // Whatever the platform offers, the list must end somewhere that exists: /dev/shm is Linux-only and
  // XDG_RUNTIME_DIR is often unset, so the fallback is the one candidate always present.
  const std::vector<std::string> candidates = DefaultExtractDirectories();
  ASSERT_THAT(candidates, Not(IsEmpty()));
  EXPECT_THAT(candidates.back(), Not(IsEmpty()));
  EXPECT_THAT(stdfs::is_directory(candidates.back()), IsTrue());
}

TEST_F(ExtractTest, AnExtractedMemberIsARealFileWithTheMembersNameAndBytes) {
  // The whole point: after this, a child process opens an ordinary path and needs to know nothing
  // about archives. The NAME carries over too, so a tool keying on the extension still works.
  fs_.Add("a.tar!dir/two.txt", "two\n");
  ExtractedMembers extracted;
  MBO_ASSERT_OK_AND_ASSIGN(const std::string path, extracted.Extract(fs_, "a.tar!dir/two.txt"));
  EXPECT_THAT(path, EndsWith("/two.txt"));
  EXPECT_THAT(Read(path), "two\n");
  EXPECT_THAT(extracted.Held(), SizeIs(1));
}

TEST_F(ExtractTest, TwoMembersWithTheSameNameGetTheirOwnDirectories) {
  // A member path is unique but a member NAME is not, and the name is what the temporary file must
  // keep - so the uniqueness has to live in the directory instead.
  fs_.Add("a.tar!README", "from a\n");
  fs_.Add("b.tar!README", "from b\n");
  ExtractedMembers extracted;
  MBO_ASSERT_OK_AND_ASSIGN(const std::string first, extracted.Extract(fs_, "a.tar!README"));
  MBO_ASSERT_OK_AND_ASSIGN(const std::string second, extracted.Extract(fs_, "b.tar!README"));
  EXPECT_THAT(first, Ne(second));
  EXPECT_THAT(Read(first), "from a\n");
  EXPECT_THAT(Read(second), "from b\n");
}

TEST_F(ExtractTest, ReleaseRemovesTheFileAndItsDirectory) {
  fs_.Add("a.tar!one.txt", "one\n");
  ExtractedMembers extracted;
  MBO_ASSERT_OK_AND_ASSIGN(const std::string path, extracted.Extract(fs_, "a.tar!one.txt"));
  const stdfs::path dir = stdfs::path(path).parent_path();
  extracted.Release(path);
  EXPECT_THAT(stdfs::exists(path), IsFalse());
  EXPECT_THAT(stdfs::exists(dir), IsFalse());
  EXPECT_THAT(extracted.Held(), IsEmpty());
  extracted.Release(path);  // releasing twice is not an error: the caller may release unconditionally
  EXPECT_THAT(extracted.Held(), IsEmpty());
}

TEST_F(ExtractTest, WhatIsStillHeldIsRemovedWhenTheRunEnds) {
  // A `-exec ... +` batch runs after the walk, so its members stay extracted until then; nothing may
  // survive the run itself.
  fs_.Add("a.tar!one.txt", "one\n");
  std::string path;
  {
    ExtractedMembers extracted;
    MBO_ASSERT_OK_AND_ASSIGN(const std::string extracted_path, extracted.Extract(fs_, "a.tar!one.txt"));
    path = extracted_path;
    EXPECT_THAT(stdfs::exists(path), IsTrue());
  }
  EXPECT_THAT(stdfs::exists(path), IsFalse());
  EXPECT_THAT(stdfs::exists(stdfs::path(path).parent_path()), IsFalse());
}

TEST_F(ExtractTest, BlockedWritingAndDryRunNeverMaterializeMembers) {
  fs_.Add("a.tar!one.txt", "content");
  ExtractedMembers blocked({.block_writing = true});
  EXPECT_THAT(blocked.Extract(fs_, "a.tar!one.txt"), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(blocked.Held(), IsEmpty());
  ExtractedMembers preview({.dry_run = true});
  EXPECT_THAT(preview.Extract(fs_, "a.tar!one.txt"), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(preview.Held(), IsEmpty());
}

TEST_F(ExtractTest, AMemberThatCannotBeReadIsAnErrorNotAnEmptyFile) {
  // Extraction is the step that can fail (a corrupt container, a truncated member); handing the child
  // an empty file would look like success and be wrong.
  ExtractedMembers extracted;
  EXPECT_THAT(extracted.Extract(fs_, "a.tar!missing"), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(extracted.Held(), IsEmpty());
}

}  // namespace
}  // namespace xff::engine
