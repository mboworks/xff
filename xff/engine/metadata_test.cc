// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

using ::mbo::testing::IsOk;
using ::testing::Eq;
using ::testing::IsEmpty;

class MetadataFs final : public vfs::FileSystem {
 public:
  std::vector<vfs::Entry> entries;
  mutable std::atomic<std::size_t> stats = 0;
  bool fail_stats = false;
  mutable std::atomic<std::size_t> birth_requests = 0;

  absl::StatusOr<vfs::Metadata> StatFields(std::string_view path, bool follow, vfs::MetadataFields fields)
      const override {
    if (fields == vfs::MetadataFields::kBirthTime) {
      ++birth_requests;
    }
    auto result = Stat(path, follow);
    if (result.ok() && fields == vfs::MetadataFields::kBirthTime) {
      result->btime = absl::UnixEpoch();
    }
    return result;
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override { return entries; }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    ++stats;
    if (fail_stats && path != "root") {
      return absl::PermissionDeniedError("stat denied");
    }
    return vfs::Metadata{
        .type = path == "root" ? vfs::FileType::kDirectory : vfs::FileType::kRegular,
        .size = 7,
        .ino = 1,
    };
  }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "metadata tests must never mutate files";
    return absl::PermissionDeniedError("read only");
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::NotFoundError("no link"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return "needle\n"; }
};

struct MetadataTest : ::testing::Test {
  MetadataFs fs;

  void Populate(std::size_t count, vfs::FileType type = vfs::FileType::kRegular) {
    for (std::size_t index = 0; index < count; ++index) {
      const std::string name = absl::StrCat("file", index, ".txt");
      fs.entries.push_back({.path = absl::StrCat("root/", name), .name = name, .type = type});
    }
  }

  void Check(const std::vector<std::string>& expression, std::size_t workers, std::size_t expected_stats) {
    std::vector<std::string> args{"--exact", "--color=never", absl::StrCat("--jobs=", workers), "root"};
    args.insert(args.end(), expression.begin(), expression.end());
    MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse(args));
    fs.stats = 0;
    std::vector<absl::Status> errors;
    const auto result = RunFind(
        command, fs, [](std::string_view) {}, [&](std::string_view, absl::Status status) { errors.push_back(status); });
    EXPECT_THAT(result.errors, Eq(0));
    EXPECT_THAT(errors, IsEmpty());
    EXPECT_THAT(fs.stats.load(), Eq(expected_stats));
  }
};

TEST_F(MetadataTest, BasicConsumersDoNotRequestBirthTime) {
  Populate(10);
  Check({"-size", "7c"}, 1, 11);
  Check({"--summary=ext"}, 1, 11);
  Check({"--template={mtime:epoch}"}, 1, 11);
  EXPECT_THAT(fs.birth_requests.load(), Eq(0));
}

TEST_F(MetadataTest, ReferenceMetadataRequestsOnlyTheComparedFields) {
  Populate(10);
  Check({"-newer", "reference"}, 1, 22);
  Check({"-samefile", "reference"}, 1, 22);
  EXPECT_THAT(fs.birth_requests.load(), Eq(0));
  Check({"-newermB", "reference"}, 1, 22);
  EXPECT_THAT(fs.birth_requests.load(), Eq(11));
}

TEST_F(MetadataTest, BirthTimeConsumersRequestExtendedFields) {
  Populate(10);
  Check({"-newerBt", "@1"}, 1, 11);
  EXPECT_THAT(fs.birth_requests.load(), Eq(11));
  fs.birth_requests = 0;
  Check({"--template={btime:epoch}"}, 1, 11);
  EXPECT_THAT(fs.birth_requests.load(), Eq(11));
  fs.birth_requests = 0;
  Check({"--summary={btime:epoch}"}, 1, 11);
  EXPECT_THAT(fs.birth_requests.load(), Eq(11));
  fs.birth_requests = 0;
  Check({"-name", "absent", "-newerBt", "@1"}, 1, 1);
  EXPECT_THAT(fs.birth_requests.load(), Eq(1));
  fs.birth_requests = 0;
  Check({"-type", "f", "-grep:{btime:epoch}", "needle"}, 1, 11);
  EXPECT_THAT(fs.birth_requests.load(), Eq(11));
}

TEST_F(MetadataTest, KnownTypesAvoidPerFileStatsAtEveryScaleAndWorkerCount) {
  for (const std::size_t count : {10, 100, 1'000, 10'000}) {
    fs.entries.clear();
    Populate(count);
    for (const std::size_t workers : {1, 4}) {
      SCOPED_TRACE(absl::StrCat(count, " files, ", workers, " workers"));
      Check({"-type", "f", "-print0"}, workers, 1);
      Check({"--safe", "-name", "*.txt", "-type", "f"}, workers, 1);
      Check({"-content", "needle"}, workers, 1);
      Check({"-size", "7c"}, workers, count + 1);
      Check({"--summary=ext"}, workers, count + 1);
      Check({"--template={size}"}, workers, count + 1);
    }
  }
}

TEST_F(MetadataTest, ShortCircuitingDefersAndCachesFullMetadata) {
  Populate(1'000);
  for (const std::size_t workers : {1, 4}) {
    Check({"-name", "missing", "-size", "7c"}, workers, 1);
    Check({"-ignore_readdir_race", "-name", "missing", "-size", "7c"}, workers, 1);
    Check({"-name", "file0.txt", "-size", "7c", "-size", "7c"}, workers, 2);
    Check({"-false", "-o", "-size", "7c"}, workers, 1'001);
    Check({"-true", "-o", "-size", "7c"}, workers, 1);
  }
}

TEST_F(MetadataTest, LazyStatFailureCannotReachAnOrAction) {
  Populate(10);
  fs.fail_stats = true;
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"--exact", "--color=never", "root", "-type", "f", "-a", "(", "-size", "7c", "-o", "-print", ")"}));
  std::string output;
  std::vector<absl::Status> errors;
  const auto result = RunFind(
      command, fs, [&](std::string_view value) { output += value; },
      [&](std::string_view, absl::Status status) { errors.push_back(status); });
  EXPECT_THAT(result.errors, Eq(10));
  EXPECT_THAT(errors.size(), Eq(10));
  EXPECT_THAT(output, IsEmpty());
  EXPECT_THAT(fs.stats.load(), Eq(11));
}

TEST_F(MetadataTest, ParallelContentMatchingPreservesOutputAcrossBatchesAndBooleanOperators) {
  Populate(1'000);
  const std::vector<std::vector<std::string>> expressions{
      {"-type", "f", "-content", "needle", "-print0"},     {"-name", "file1*", "-content", "needle"},
      {"-content", "absent", "-o", "-content", "needle"},  {"!", "-content", "needle"},
      {"-type", "f", "-content", "needle", "-first", "3"}, {"-content", "needle", "-quit"},
  };
  for (const auto& expression : expressions) {
    std::vector<std::string> actual;
    for (const std::size_t workers : {1, 4}) {
      std::vector<std::string> args{"--exact", "--color=never", absl::StrCat("--jobs=", workers), "root"};
      args.insert(args.end(), expression.begin(), expression.end());
      MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse(args));
      std::string output;
      const auto result = RunFind(
          command, fs, [&](std::string_view value) { output += value; },
          [](std::string_view, absl::Status status) { EXPECT_THAT(status, IsOk()); });
      EXPECT_THAT(result.errors, Eq(0));
      actual.push_back(std::move(output));
    }
    EXPECT_THAT(actual.at(0), Eq(actual.at(1)));
  }
}

TEST_F(MetadataTest, UnknownDirectoryEntryTypesStillFetchMetadata) {
  Populate(100, vfs::FileType::kUnknown);
  for (const std::size_t workers : {1, 4}) {
    Check({"-type", "f"}, workers, 101);
  }
}

}  // namespace
}  // namespace xff::engine
