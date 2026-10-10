// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/read_source.h"

namespace xff::engine {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::SizeIs;

// Each side has 64 independent sibling directories, enough to exercise read-ahead,
// followed by 64 large files that qualify for the comparison and content workers.
// No host filesystem or timing-based admission assumption enters these controls.
class CommandFs final : public vfs::FileSystem {
 public:
  explicit CommandFs(std::size_t participants, std::size_t content_participants = 0)
      : participants_(participants),
        content_participants_(content_participants == 0 ? participants : content_participants) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    if (!IsDirectory(path)) {
      return absl::NotFoundError(path);
    }
    if (path == "left" || path == "right") {
      const absl::MutexLock lock(mutex_);
      root_readers_.insert(std::this_thread::get_id());
      std::vector<vfs::Entry> entries;
      entries.reserve(64);
      for (std::size_t index = 0; index < 64; ++index) {
        const auto name = std::to_string(index);
        entries.push_back({.path = std::string(path) + "/" + name, .name = name, .type = vfs::FileType::kDirectory});
      }
      return entries;
    }
    Record(path.starts_with("left/") ? 0 : 1, participants_ > 1 ? participants_ - 1 : 1);
    return std::vector<vfs::Entry>{
        {
            .path = std::string(path) + "/file",
            .name = "file",
            .type = vfs::FileType::kRegular,
        },
    };
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    const bool file = path.ends_with("/file");
    if (!IsDirectory(file ? path.substr(0, path.size() - 5) : path)) {
      return absl::NotFoundError(path);
    }
    // Only ancestor identities matter for loop detection; left/right and all sibling
    // directories have different identities from their own root.
    return vfs::Metadata{
        .type = file ? vfs::FileType::kRegular : vfs::FileType::kDirectory,
        .size = file ? bytes_.size() : 0,
        .ino = path == "left"    ? 1U
               : path == "right" ? 2U
                                 : 3U,
        .dev = 1,
    };
  }

  absl::StatusOr<vfs::SharedReadSource> ContentSource(std::string_view path) const override {
    if (!path.ends_with("/file") || !IsDirectory(path.substr(0, path.size() - 5))) {
      return absl::NotFoundError(path);
    }
    Record(2, content_participants_);
    return vfs::MemoryReadSource(bytes_);
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    MBO_ASSIGN_OR_RETURN(const auto source, ContentSource(path));
    return vfs::ReadSourceBytes(*source, bytes_.size());
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only fixture"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  void CheckBudget() const {
    const absl::MutexLock lock(mutex_);
    EXPECT_THAT(root_readers_, ElementsAre(std::this_thread::get_id()));
    std::set<std::thread::id> all = root_readers_;
    for (const auto& threads : phase_readers_) {
      all.insert(threads.begin(), threads.end());
    }
    EXPECT_THAT(all, SizeIs(Le(participants_)));
    EXPECT_THAT(all, Contains(std::this_thread::get_id()));
    EXPECT_THAT(synchronized_, IsTrue());
    EXPECT_THAT(phase_readers_.at(2), SizeIs(Ge(content_participants_)));
    EXPECT_THAT(phase_readers_.at(2), Contains(coordinator_));
  }

  void CheckWalkReuse() const {
    const absl::MutexLock lock(mutex_);
    EXPECT_THAT(phase_readers_.at(0), Eq(phase_readers_.at(1)));
    EXPECT_THAT(phase_readers_.at(0), SizeIs(participants_ > 1 ? participants_ - 1 : 1));
  }

 private:
  static bool IsDirectory(const std::string_view path) {
    if (path == "left" || path == "right") {
      return true;
    }
    const auto name = path.starts_with("left/")    ? path.substr(5)
                      : path.starts_with("right/") ? path.substr(6)
                                                   : std::string_view{};
    return !name.empty() && std::ranges::all_of(name, [](char digit) { return digit >= '0' && digit <= '9'; });
  }

  void Record(std::size_t phase, std::size_t expected) const ABSL_LOCKS_EXCLUDED(mutex_) {
    const absl::MutexLock lock(mutex_);
    if (!phase_readers_.at(phase).insert(std::this_thread::get_id()).second) {
      return;
    }
    // First calls rendezvous so one fast worker cannot drain the whole fixture before
    // its peers run. Timeout bounds a broken scheduler instead of hanging a test.
    const auto ready = [&] ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
      return phase_readers_.at(phase).size() >= expected
             && (phase != 2 || phase_readers_.at(phase).contains(coordinator_));
    };
    synchronized_ = mutex_.AwaitWithTimeout(absl::Condition(&ready), absl::Seconds(5)) && synchronized_;
  }

  const std::size_t participants_;
  const std::size_t content_participants_;
  const std::thread::id coordinator_ = std::this_thread::get_id();
  const std::string bytes_ = std::string(262'144, 'a') + "needle\n";
  // The mutex guards reader identities and rendezvous outcomes. Filesystem data is immutable.
  mutable absl::Mutex mutex_;
  mutable std::set<std::thread::id> root_readers_ ABSL_GUARDED_BY(mutex_);
  mutable std::array<std::set<std::thread::id>, 3> phase_readers_ ABSL_GUARDED_BY(mutex_);
  mutable bool synchronized_ ABSL_GUARDED_BY(mutex_) = true;
};

struct CommandWorkersTest : ::testing::Test {
  static absl::StatusOr<std::string> Run(const std::vector<std::string>& arguments, const CommandFs& fs) {
    MBO_ASSIGN_OR_RETURN(auto command, parser::Parse(arguments));
    parser::BindMatchers(command, parser::GrammarFromGlobals(command.globals), parser::CaseMode::kSensitive);
    std::string output;
    std::vector<absl::Status> errors;
    const auto result = RunFind(
        command, fs, [&](std::string_view text) { output.append(text); },
        [&](std::string_view, absl::Status status) { errors.push_back(std::move(status)); });
    EXPECT_THAT(errors, IsEmpty());
    EXPECT_THAT(result.errors, Eq(0));
    return output;
  }
};

TEST_F(CommandWorkersTest, SingleParticipantComparisonHasNoSecondCoordinator) {
  const CommandFs fs(1);
  EXPECT_THAT(Run({"--jobs=1", "--compare", "--compare-select=identical", "left", "right"}, fs), IsOk());
  fs.CheckBudget();
  fs.CheckWalkReuse();
}

TEST_F(CommandWorkersTest, ComparisonReusesBoundedWorkersAcrossBothWalksAndPairs) {
  const CommandFs serial(1);
  ASSERT_OK_AND_ASSIGN(
      const auto expected, Run({"--jobs=1", "--compare", "--compare-select=identical", "left", "right"}, serial));
  for (const std::size_t participants : {2, 3, 10}) {
    SCOPED_TRACE(participants);
    const CommandFs fs(participants);
    ASSERT_OK_AND_ASSIGN(
        const auto output,
        Run({"--jobs=" + std::to_string(participants), "--compare", "--compare-select=identical", "left", "right"},
            fs));
    EXPECT_THAT(output, EqualsText(expected));
    fs.CheckBudget();
    fs.CheckWalkReuse();
  }
}

TEST_F(CommandWorkersTest, ContentMatchingIncludesTheCallingCoordinatorInTheAllowance) {
  const CommandFs serial(1);
  ASSERT_OK_AND_ASSIGN(const auto expected, Run({"--jobs=1", "--sort=tree", "left", "-content", "needle"}, serial));
  for (const std::size_t participants : {2, 3, 10}) {
    SCOPED_TRACE(participants);
    // Matching sees directories as well as files. Force caller/worker overlap without
    // assuming that every admitted logical worker reaches a file in this small tail.
    const CommandFs fs(participants, std::min(participants, std::size_t{2}));
    ASSERT_OK_AND_ASSIGN(
        const auto output,
        Run({"--jobs=" + std::to_string(participants), "--sort=tree", "left", "-content", "needle"}, fs));
    EXPECT_THAT(output, EqualsText(expected));
    fs.CheckBudget();
  }
}

}  // namespace
}  // namespace xff::engine
