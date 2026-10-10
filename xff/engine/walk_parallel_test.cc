// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/engine/run_executor.h"
#include "xff/engine/walk.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {
using ::mbo::testing::IsOk;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::SizeIs;

struct StatTrace {
  std::mutex mutex;
  std::condition_variable condition;
  std::set<std::thread::id> threads;
  std::set<vfs::MetadataFields> fields;
  std::size_t children = 0;
  bool await_peer = false;
};

struct WideFs final : vfs::FileSystem {
  explicit WideFs(std::shared_ptr<StatTrace> trace = std::make_shared<StatTrace>()) : trace(std::move(trace)) {}

  std::shared_ptr<StatTrace> trace;
  std::size_t count = 1'024;
  vfs::FileType root_type = vfs::FileType::kDirectory;
  vfs::Source source = vfs::Source::kLocalFs;
  std::map<std::string, absl::StatusCode> failures;

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    std::vector<vfs::Entry> entries;
    entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto name = std::to_string(index);
      entries.push_back(
          {.path = std::string(path) + "/" + name, .name = name, .type = vfs::FileType::kRegular, .source = source});
    }
    return entries;
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool follow) const override {
    return StatFields(path, follow, vfs::MetadataFields::kBirthTime);
  }

  absl::StatusOr<vfs::Metadata> StatFields(std::string_view path, bool, vfs::MetadataFields fields) const override {
    if (path == "root") {
      return vfs::Metadata{.type = root_type, .ino = 1};
    }
    {
      std::unique_lock lock(trace->mutex);
      ++trace->children;
      trace->fields.insert(fields);
      if (trace->threads.insert(std::this_thread::get_id()).second && trace->await_peer) {
        trace->condition.notify_all();
        trace->condition.wait_for(lock, std::chrono::seconds(2), [&] { return trace->threads.size() > 1; });
      }
    }
    if (const auto found = failures.find(std::string(path)); found != failures.end()) {
      return absl::Status(found->second, path);
    }
    return vfs::Metadata{.type = vfs::FileType::kRegular, .source = source, .size = path.size(), .ino = 2};
  }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override {
    return absl::UnimplementedError("metadata only");
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }
};

struct WalkLog {
  std::vector<std::pair<std::string, std::uint64_t>> visits;
  std::vector<std::pair<std::string, absl::StatusCode>> errors;
  std::set<std::thread::id> visitors;
};

WalkLog Record(const WideFs& fs, const WalkOptions& options) {
  WalkLog log;
  const auto roots = std::to_array<std::string>({"root"});
  EXPECT_THAT(
      Walk(
          fs, roots, options,
          [&](const Visit& visit) {
            log.visits.emplace_back(visit.path, visit.metadata.size);
            log.visitors.insert(std::this_thread::get_id());
            return WalkAction::kContinue;
          },
          [&](std::string_view path, const absl::Status& status) { log.errors.emplace_back(path, status.code()); }),
      IsOk());
  return log;
}

struct WalkParallelTest : ::testing::Test {};

TEST_F(WalkParallelTest, EagerStatBudgetIncludesTheCallingCoordinator) {
  const WideFs serial;
  const auto expected = Record(serial, {.workers = 1});
  for (const std::size_t participants : {1, 2, 3, 10}) {
    SCOPED_TRACE(participants);
    const WideFs fs;
    fs.trace->await_peer = participants > 1;
    const auto actual = Record(fs, {.workers = participants});
    EXPECT_THAT(actual.visits, Eq(expected.visits));
    EXPECT_THAT(actual.errors, IsEmpty());
    EXPECT_THAT(fs.trace->threads, SizeIs(Le(participants)));
    EXPECT_THAT(fs.trace->threads, Contains(std::this_thread::get_id()));
  }
}

TEST_F(WalkParallelTest, CallerCompletesEagerMetadataAndDestroysWalkWhilePoolIsOccupied) {
  RunExecutor executor(2);
  executor.Start(2);
  absl::Notification first_started;
  absl::Notification second_started;
  absl::Notification release;
  auto first = executor.Submit([&] {
    first_started.Notify();
    return release.WaitForNotificationWithTimeout(absl::Seconds(10));
  });
  auto second = executor.Submit([&] {
    second_started.Notify();
    return release.WaitForNotificationWithTimeout(absl::Seconds(10));
  });
  const bool first_running = first_started.WaitForNotificationWithTimeout(absl::Seconds(5));
  const bool second_running = second_started.WaitForNotificationWithTimeout(absl::Seconds(5));
  const WideFs serial;
  const auto expected = Record(serial, {.workers = 1});
  WideFs fs;
  const auto roots = std::to_array<std::string>({"root"});
  const auto walk = [&] {
    WalkLog log;
    EXPECT_THAT(
        Walk(
            fs, roots, {.workers = 3}, executor,
            [&](const Visit& visit) {
              log.visits.emplace_back(visit.path, visit.metadata.size);
              log.visitors.insert(std::this_thread::get_id());
              return WalkAction::kContinue;
            },
            [&](std::string_view path, absl::Status status) { log.errors.emplace_back(path, status.code()); }),
        IsOk());
    return log;
  };
  absl::Notification finished;
  bool returned_before_release = false;
  std::thread observer([&] {
    returned_before_release = finished.WaitForNotificationWithTimeout(absl::Seconds(5));
    release.Notify();
  });
  const auto actual = walk();
  EXPECT_THAT(actual.visits, Eq(expected.visits));
  EXPECT_THAT(actual.errors, IsEmpty());
  EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
  // Reuse the executor with a small serial tail after the first walk's local
  // entries, chunk index and result storage have all been destroyed.
  constexpr std::size_t kTail = 5;
  fs.count = kTail;
  EXPECT_THAT(walk().visits, SizeIs(kTail + 1));
  finished.Notify();
  observer.join();
  EXPECT_THAT(first_running, IsTrue());
  EXPECT_THAT(second_running, IsTrue());
  EXPECT_THAT(returned_before_release, IsTrue());
  EXPECT_THAT(first.Get(), IsTrue());
  EXPECT_THAT(second.Get(), IsTrue());
  EXPECT_THAT(fs.trace->children, Eq(1'024 + kTail));
  EXPECT_THAT(fs.trace->threads, ElementsAre(std::this_thread::get_id()));
  EXPECT_THAT(executor.worker_count(), Eq(2));
  EXPECT_THAT(executor.Submit([] { return 42; }).Get(), Eq(42));
}

TEST_F(WalkParallelTest, EagerStatsOverlapAndKeepCoordinatorVisitOrderAndFields) {
  constexpr auto kFields = std::to_array({vfs::MetadataFields::kBasic, vfs::MetadataFields::kBirthTime});
  for (const auto fields : kFields) {
    const WideFs serial;
    const WideFs parallel;
    parallel.trace->await_peer = true;
    const auto expected = Record(serial, {.workers = 1, .metadata_fields = fields});
    const auto actual = Record(parallel, {.workers = 4, .metadata_fields = fields});
    EXPECT_THAT(actual.visits, Eq(expected.visits));
    EXPECT_THAT(actual.errors, IsEmpty());
    EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
    EXPECT_THAT(parallel.trace->children, Eq(1'024));
    EXPECT_THAT(parallel.trace->threads.size(), Gt(1));
    EXPECT_THAT(parallel.trace->fields, ElementsAre(fields));
  }
}

TEST_F(WalkParallelTest, LazyAndMetadataFreeListingsDoNotStartStatWorkers) {
  constexpr auto kDemands = std::to_array({MetadataDemand::kNever, MetadataDemand::kOnDemand});
  for (const auto demand : kDemands) {
    const WideFs fs;
    const auto log = Record(fs, {.workers = 4, .metadata = demand});
    EXPECT_THAT(log.visits, SizeIs(1'025));
    EXPECT_THAT(log.errors, IsEmpty());
    EXPECT_THAT(fs.trace->threads, IsEmpty());
    EXPECT_THAT(fs.trace->children, Eq(0));
  }
}

TEST_F(WalkParallelTest, SmallAndArchiveSourceListingsRemainSerial) {
  WideFs fs;
  fs.count = 100;
  EXPECT_THAT(Record(fs, {.workers = 4}).visits, SizeIs(101));
  EXPECT_THAT(fs.trace->threads, ElementsAre(std::this_thread::get_id()));
  fs.count = 1'024;
  fs.source = vfs::Source::kArchiveMember;
  EXPECT_THAT(Record(fs, {.workers = 4}).visits, SizeIs(1'025));
  EXPECT_THAT(fs.trace->threads, ElementsAre(std::this_thread::get_id()));
}

TEST_F(WalkParallelTest, StatFailuresAndRaceSuppressionKeepSerialOrder) {
  WideFs fs;
  fs.failures = {{"root/100", absl::StatusCode::kNotFound}, {"root/800", absl::StatusCode::kPermissionDenied}};
  const auto expected = Record(fs, {.workers = 1});
  const auto actual = Record(fs, {.workers = 4});
  EXPECT_THAT(actual.visits, Eq(expected.visits));
  EXPECT_THAT(actual.errors, Eq(expected.errors));
  EXPECT_THAT(actual.errors, SizeIs(2));
  EXPECT_THAT(
      Record(fs, {.ignore_readdir_race = true, .workers = 4}).errors,
      ElementsAre(std::pair(std::string("root/800"), absl::StatusCode::kPermissionDenied)));
}

TEST_F(WalkParallelTest, OwnedMountRemainsSerialWithoutArchiveSourceHints) {
  WideFs fs;
  fs.root_type = vfs::FileType::kRegular;
  const auto trace = std::make_shared<StatTrace>();
  std::size_t visited = 0;
  const auto roots = std::to_array<std::string>({"root"});
  EXPECT_THAT(
      Walk(
          fs, roots, {.workers = 4, .archive = ArchiveDive::kRoots},
          [&](const Visit&) {
            ++visited;
            return WalkAction::kContinue;
          },
          [](std::string_view, const absl::Status& status) { EXPECT_THAT(status, IsOk()); },
          [&](std::string_view, const vfs::FileSystem&, int) -> absl::StatusOr<std::unique_ptr<const vfs::FileSystem>> {
            return std::make_unique<WideFs>(trace);
          }),
      IsOk());
  EXPECT_THAT(visited, Eq(1'025));
  EXPECT_THAT(trace->threads, ElementsAre(std::this_thread::get_id()));
}

}  // namespace
}  // namespace xff::engine
