// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/status.h"
#include "xff/engine/walk.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {
using ::mbo::testing::IsOk;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::SizeIs;

struct StatTrace {
  std::mutex mutex;
  std::condition_variable condition;
  std::set<std::thread::id> threads;
  std::set<vfs::MetadataFields> fields;
  std::size_t children = 0;
  bool await_peer = false;
};

struct WideFs : vfs::FileSystem {
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

struct DirectoryFs : WideFs {
  DirectoryFs() { count = 4; }

  std::size_t directories = 513;
  std::set<std::string> unreadable;
  mutable std::map<std::string, std::size_t> reads;

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    {
      const std::scoped_lock lock(trace->mutex);
      ++reads[std::string(path)];
    }
    if (unreadable.contains(std::string(path))) {
      return absl::PermissionDeniedError(path);
    }
    if (path != "root") {
      return WideFs::ReadDir(path);
    }
    std::vector<vfs::Entry> entries;
    entries.reserve(directories);
    for (std::size_t index = 0; index < directories; ++index) {
      const auto name = "d" + std::to_string(index);
      entries.push_back({.path = "root/" + name, .name = name, .type = vfs::FileType::kDirectory});
    }
    return entries;
  }

  absl::StatusOr<vfs::Metadata> StatFields(std::string_view path, bool follow, vfs::MetadataFields fields)
      const override {
    if (path != "root" && path.find('/', 5) == std::string_view::npos) {
      return vfs::Metadata{.type = vfs::FileType::kDirectory, .ino = 2};
    }
    return WideFs::StatFields(path, follow, fields);
  }
};

struct WalkLog {
  std::vector<std::pair<std::string, std::uint64_t>> visits;
  std::vector<std::pair<std::string, absl::StatusCode>> errors;
  std::set<std::thread::id> visitors;
};

WalkLog Record(const WideFs& fs, const WalkOptions& options, std::size_t stop_after = 0) {
  WalkLog log;
  const auto roots = std::to_array<std::string>({"root"});
  EXPECT_THAT(
      Walk(
          fs, roots, options,
          [&](const Visit& visit) {
            log.visits.emplace_back(visit.path, visit.metadata.size);
            log.visitors.insert(std::this_thread::get_id());
            return stop_after != 0 && log.visits.size() >= stop_after ? WalkAction::kStop : WalkAction::kContinue;
          },
          [&](std::string_view path, const absl::Status& status) { log.errors.emplace_back(path, status.code()); }),
      IsOk());
  return log;
}

struct WalkParallelTest : ::testing::Test {};

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

TEST_F(WalkParallelTest, EagerNameOnlyStatsRetainParallelismFieldsPathsAndErrors) {
  struct NamedWideFs final : WideFs {
    bool SupportsNameOnlyListings() const override { return true; }

    absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
      {
        const std::scoped_lock lock(trace->mutex);
        ++complete_reads;
      }
      return WideFs::ReadDir(path);
    }

    absl::StatusOr<std::vector<vfs::Entry>> ReadDirNames(std::string_view path) const override {
      {
        const std::scoped_lock lock(trace->mutex);
        ++name_reads;
      }
      MBO_ASSIGN_OR_RETURN(auto entries, WideFs::ReadDir(path));
      for (auto& entry : entries) {
        entry.path.clear();
      }
      return entries;
    }

    mutable std::size_t name_reads = 0;
    mutable std::size_t complete_reads = 0;
  };

  constexpr auto kFields = std::to_array({vfs::MetadataFields::kBasic, vfs::MetadataFields::kBirthTime});
  for (const auto fields : kFields) {
    for (const bool failures : {false, true}) {
      WideFs serial;
      NamedWideFs parallel;
      if (failures) {
        serial.failures = {
            {"root/100", absl::StatusCode::kNotFound},
            {"root/800", absl::StatusCode::kPermissionDenied},
        };
        parallel.failures = serial.failures;
      }
      parallel.trace->await_peer = true;
      const auto expected = Record(serial, {.workers = 1, .metadata_fields = fields});
      const auto actual = Record(parallel, {.workers = 3, .metadata_fields = fields});
      EXPECT_THAT(actual.visits, Eq(expected.visits));
      EXPECT_THAT(actual.errors, Eq(expected.errors));
      EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
      EXPECT_THAT(parallel.trace->children, Eq(1'024));
      EXPECT_THAT(parallel.trace->threads.size(), Gt(1));
      EXPECT_THAT(parallel.trace->fields, ElementsAre(fields));
      EXPECT_THAT(parallel.name_reads, Eq(1));
      EXPECT_THAT(parallel.complete_reads, Eq(0));
    }
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

struct ObservedWideFs final : WideFs {
  bool name_only = true;
  bool serial_only = false;
  mutable std::vector<std::size_t> requested_stat_workers;
  bool partial = false;
  bool unavailable = false;
  bool malformed = false;
  mutable std::atomic<std::size_t> observation_reads = 0;

  bool SupportsNameOnlyListings() const override { return name_only; }

  bool SupportsDirectoryMetadata(std::size_t stat_workers) const override {
    requested_stat_workers.push_back(stat_workers);
    return !serial_only || stat_workers <= 1;
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDirNames(std::string_view path) const override {
    MBO_ASSIGN_OR_RETURN(auto entries, WideFs::ReadDir(path));
    for (auto& entry : entries) {
      entry.path.clear();
    }
    return entries;
  }

  absl::StatusOr<vfs::DirectoryMetadata> ReadDirMetadata(std::string_view path, vfs::MetadataFields) const override {
    ++observation_reads;
    MBO_ASSIGN_OR_RETURN(auto entries, name_only ? ReadDirNames(path) : WideFs::ReadDir(path));
    if (unavailable) {
      return vfs::DirectoryMetadata{.entries = std::move(entries)};
    }
    std::vector<vfs::MetadataObservation> observations;
    observations.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
      const std::string full_path = std::string(path) + "/" + entries.at(index).name;
      if ((partial && index % 2 != 0) || failures.contains(full_path)) {
        observations.push_back(vfs::MetadataObservation{.status = absl::UnavailableError("")});
      } else {
        observations.push_back(
            vfs::MetadataObservation{
                .value =
                    {
                        .type = vfs::FileType::kRegular,
                        .source = source,
                        .size = full_path.size(),
                        .ino = 2,
                    },
            });
      }
    }
    if (malformed) {
      observations.pop_back();
    }
    return vfs::DirectoryMetadata{.entries = std::move(entries), .observations = std::move(observations)};
  }
};

struct ObservedDirectoryFs final : DirectoryFs {
  bool name_only = true;
  bool serial_only = false;
  mutable std::vector<std::size_t> requested_stat_workers;
  mutable std::atomic<std::size_t> observation_reads = 0;

  bool SupportsNameOnlyListings() const override { return name_only; }

  bool SupportsDirectoryMetadata(std::size_t stat_workers) const override {
    requested_stat_workers.push_back(stat_workers);
    return !serial_only || stat_workers <= 1;
  }

  absl::StatusOr<vfs::DirectoryMetadata> ReadDirMetadata(std::string_view path, vfs::MetadataFields fields)
      const override {
    ++observation_reads;
    MBO_ASSIGN_OR_RETURN(auto entries, DirectoryFs::ReadDir(path));
    std::vector<vfs::MetadataObservation> observations;
    observations.reserve(entries.size());
    for (auto& entry : entries) {
      if (entry.type == vfs::FileType::kDirectory) {
        observations.push_back(vfs::MetadataObservation{.status = absl::UnavailableError("")});
      } else {
        MBO_ASSIGN_OR_RETURN(const auto metadata, DirectoryFs::StatFields(entry.path, false, fields));
        observations.push_back(vfs::MetadataObservation{.value = metadata});
      }
      if (name_only) {
        entry.path.clear();
      }
    }
    return vfs::DirectoryMetadata{.entries = std::move(entries), .observations = std::move(observations)};
  }
};

TEST_F(WalkParallelTest, OwnedDirectoryObservationsSurviveSortingPrefetchAndReadErrors) {
  constexpr auto kOrders = std::to_array({
      SortOrder::kNone,
      SortOrder::kDir,
      SortOrder::kTree,
      SortOrder::kGlobal,
      SortOrder::kSubtree,
  });
  constexpr auto kWorkers = std::to_array<std::size_t>({1, 3});
  for (const auto order : kOrders) {
    for (const auto workers : kWorkers) {
      SCOPED_TRACE(static_cast<int>(order));
      SCOPED_TRACE(workers);
      DirectoryFs reference;
      ObservedDirectoryFs observed;
      reference.unreadable = observed.unreadable = {"root/d100"};
      const auto expected = Record(reference, {.sort = order, .workers = 1});
      const auto actual = Record(observed, {.sort = order, .workers = workers});
      EXPECT_THAT(actual.visits, Eq(expected.visits));
      EXPECT_THAT(actual.errors, Eq(expected.errors));
      EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
    }
  }
}

TEST_F(WalkParallelTest, OwnedFullPathObservationsSurviveSortedPrefetchAndDirectoryErrors) {
  constexpr auto kOrders = std::to_array({
      SortOrder::kNone,
      SortOrder::kDir,
      SortOrder::kTree,
      SortOrder::kGlobal,
      SortOrder::kSubtree,
  });
  for (const auto order : kOrders) {
    DirectoryFs reference;
    ObservedDirectoryFs observed;
    observed.name_only = false;
    reference.unreadable = observed.unreadable = {"root/d100"};
    const auto expected = Record(reference, {.sort = order, .workers = 1});
    const auto actual = Record(observed, {.sort = order, .workers = 3});
    EXPECT_THAT(actual.visits, Eq(expected.visits));
    EXPECT_THAT(actual.errors, Eq(expected.errors));
    EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
    EXPECT_THAT(observed.observation_reads.load(), Gt(0));
  }
}

TEST_F(WalkParallelTest, AbandonedPrefetchedObservationOwnersDrainWithoutPublishingUnvisitedErrors) {
  DirectoryFs reference;
  ObservedDirectoryFs observed;
  reference.unreadable = observed.unreadable = {"root/d100"};
  const std::size_t stop_after = 1 + observed.directories + observed.count + 1;
  const auto expected = Record(reference, {.sort = SortOrder::kDir, .workers = 1}, stop_after);
  const auto actual = Record(observed, {.sort = SortOrder::kDir, .workers = 3}, stop_after);
  EXPECT_THAT(actual.visits, Eq(expected.visits));
  EXPECT_THAT(actual.errors, IsEmpty());
}

TEST_F(WalkParallelTest, SerialObservationPreferenceRetainsCoordinatorStatWorkers) {
  const WideFs reference;
  ObservedWideFs observed;
  observed.serial_only = true;
  observed.trace->await_peer = true;
  const auto expected = Record(reference, {.workers = 1});
  const auto actual = Record(observed, {.workers = 3});
  EXPECT_THAT(actual.visits, Eq(expected.visits));
  EXPECT_THAT(actual.errors, IsEmpty());
  EXPECT_THAT(observed.requested_stat_workers, ElementsAre(1, 3));
  EXPECT_THAT(observed.observation_reads.load(), Eq(0));
  EXPECT_THAT(observed.trace->children, Eq(1'024));
  EXPECT_THAT(observed.trace->threads.size(), Gt(1));
}

TEST_F(WalkParallelTest, SerialObservationPreferenceStillFeedsDirectoryReadWorkers) {
  const DirectoryFs reference;
  ObservedDirectoryFs observed;
  observed.serial_only = true;
  const auto expected = Record(reference, {.sort = SortOrder::kDir, .workers = 1});
  const auto actual = Record(observed, {.sort = SortOrder::kDir, .workers = 3});
  EXPECT_THAT(actual.visits, Eq(expected.visits));
  EXPECT_THAT(actual.errors, IsEmpty());
  EXPECT_THAT(observed.requested_stat_workers, ElementsAre(1, 3));
  EXPECT_THAT(observed.observation_reads.load(), Gt(0));
}

TEST_F(WalkParallelTest, CompleteDirectoryObservationsAvoidPerChildStatAndKeepSerialVisits) {
  const WideFs reference;
  const ObservedWideFs observed;
  const auto expected = Record(reference, {.workers = 1});
  const auto actual = Record(observed, {.workers = 3});
  EXPECT_THAT(actual.visits, Eq(expected.visits));
  EXPECT_THAT(actual.errors, IsEmpty());
  EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
  EXPECT_THAT(observed.observation_reads.load(), Eq(1));
  EXPECT_THAT(observed.trace->children, Eq(0));
  EXPECT_THAT(observed.trace->threads, IsEmpty());
}

TEST_F(WalkParallelTest, CompleteFullPathObservationsRetainFieldsAndCoordinatorVisits) {
  constexpr auto kWorkers = std::to_array<std::size_t>({1, 3});
  constexpr auto kFields = std::to_array({vfs::MetadataFields::kBasic, vfs::MetadataFields::kBirthTime});
  for (const auto workers : kWorkers) {
    for (const auto fields : kFields) {
      const WideFs reference;
      ObservedWideFs observed;
      observed.name_only = false;
      const auto expected = Record(reference, {.workers = 1, .metadata_fields = fields});
      const auto actual = Record(observed, {.workers = workers, .metadata_fields = fields});
      EXPECT_THAT(actual.visits, Eq(expected.visits));
      EXPECT_THAT(actual.errors, IsEmpty());
      EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
      EXPECT_THAT(observed.observation_reads.load(), Eq(1));
      EXPECT_THAT(observed.trace->children, Eq(0));
      EXPECT_THAT(observed.trace->threads, IsEmpty());
    }
  }
}

TEST_F(WalkParallelTest, IncompleteFullPathObservationsKeepFallbackErrorsAndFields) {
  constexpr auto kWorkers = std::to_array<std::size_t>({1, 3});
  for (const auto workers : kWorkers) {
    WideFs reference;
    ObservedWideFs observed;
    observed.name_only = false;
    observed.partial = true;
    observed.trace->await_peer = workers > 1;
    reference.failures = {{"root/100", absl::StatusCode::kNotFound}, {"root/801", absl::StatusCode::kPermissionDenied}};
    observed.failures = reference.failures;
    const auto expected = Record(reference, {.workers = 1, .metadata_fields = vfs::MetadataFields::kBirthTime});
    const auto actual = Record(observed, {.workers = workers, .metadata_fields = vfs::MetadataFields::kBirthTime});
    EXPECT_THAT(actual.visits, Eq(expected.visits));
    EXPECT_THAT(actual.errors, Eq(expected.errors));
    EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
    EXPECT_THAT(observed.observation_reads.load(), Eq(1));
    EXPECT_THAT(observed.trace->children, Eq(513));
    EXPECT_THAT(observed.trace->fields, ElementsAre(vfs::MetadataFields::kBirthTime));
  }
}

TEST_F(WalkParallelTest, FullPathObservationsRespectLazyDemandAndRejectMalformedBatches) {
  constexpr auto kDemands = std::to_array({MetadataDemand::kNever, MetadataDemand::kOnDemand});
  for (const auto demand : kDemands) {
    ObservedWideFs observed;
    observed.name_only = false;
    const auto actual = Record(observed, {.workers = 3, .metadata = demand});
    EXPECT_THAT(actual.visits, SizeIs(1'025));
    EXPECT_THAT(actual.errors, IsEmpty());
    EXPECT_THAT(observed.observation_reads.load(), Eq(0));
    EXPECT_THAT(observed.trace->children, Eq(0));
  }
  ObservedWideFs malformed;
  malformed.name_only = false;
  malformed.malformed = true;
  const auto actual = Record(malformed, {.workers = 3});
  EXPECT_THAT(actual.visits, SizeIs(1));
  EXPECT_THAT(actual.errors, SizeIs(1));
  EXPECT_THAT(malformed.trace->children, Eq(0));
}

TEST_F(WalkParallelTest, MountedFullPathObservationsAvoidNestedStatWorkersAndRetainOwnership) {
  for (const bool partial : {false, true}) {
    WideFs host;
    host.root_type = vfs::FileType::kRegular;
    const auto trace = std::make_shared<StatTrace>();
    std::shared_ptr<const vfs::FileSystem> retained;
    std::size_t visited = 0;
    const auto roots = std::to_array<std::string>({"root"});
    EXPECT_THAT(
        Walk(
            host, roots, {.workers = 3, .archive = ArchiveDive::kRoots},
            [&](const Visit& visit) {
              ++visited;
              if (visit.fs_owner) {
                retained = visit.fs_owner;
              }
              return WalkAction::kContinue;
            },
            [](std::string_view, const absl::Status& status) { EXPECT_THAT(status, IsOk()); },
            [&](std::string_view, const vfs::FileSystem&,
                int) -> absl::StatusOr<std::unique_ptr<const vfs::FileSystem>> {
              auto mounted = std::make_unique<ObservedWideFs>();
              mounted->name_only = false;
              mounted->partial = partial;
              mounted->trace = trace;
              return mounted;
            }),
        IsOk());
    EXPECT_THAT(visited, Eq(1'025));
    ASSERT_THAT(retained, NotNull());
    MBO_ASSERT_OK_AND_ASSIGN(const auto listing, retained->ReadDirMetadata("root", vfs::MetadataFields::kBasic));
    EXPECT_THAT(listing.entries, SizeIs(1'024));
    EXPECT_THAT(trace->children, Eq(partial ? 512 : 0));
    if (partial) {
      EXPECT_THAT(trace->threads, ElementsAre(std::this_thread::get_id()));
    } else {
      EXPECT_THAT(trace->threads, IsEmpty());
    }
  }
}

TEST_F(WalkParallelTest, NativeArchiveAllKeepsCompletePathsInsteadOfNameOnlyObservations) {
  ObservedWideFs fs;
  fs.count = 8;
  const auto roots = std::to_array<std::string>({"root"});
  std::vector<std::string> paths;
  EXPECT_THAT(
      Walk(
          fs, roots, {.workers = 3, .archive = ArchiveDive::kAll},
          [&](const Visit& visit) {
            paths.emplace_back(visit.path);
            return WalkAction::kContinue;
          },
          [](std::string_view, const absl::Status& status) { EXPECT_THAT(status, IsOk()); },
          [](std::string_view, const vfs::FileSystem&, int) -> absl::StatusOr<std::unique_ptr<const vfs::FileSystem>> {
            return absl::InvalidArgumentError("not a container");
          }),
      IsOk());
  EXPECT_THAT(
      paths, ElementsAre("root", "root/0", "root/1", "root/2", "root/3", "root/4", "root/5", "root/6", "root/7"));
  EXPECT_THAT(fs.observation_reads.load(), Eq(0));
  EXPECT_THAT(fs.trace->children, Eq(8));
}

TEST_F(WalkParallelTest, MissingDirectoryObservationsRetainParallelStatsFieldsAndErrors) {
  WideFs reference;
  ObservedWideFs observed;
  observed.partial = true;
  observed.trace->await_peer = true;
  reference.failures = {{"root/100", absl::StatusCode::kNotFound}, {"root/801", absl::StatusCode::kPermissionDenied}};
  observed.failures = reference.failures;
  const auto expected = Record(reference, {.workers = 1, .metadata_fields = vfs::MetadataFields::kBasic});
  const auto actual = Record(observed, {.workers = 3, .metadata_fields = vfs::MetadataFields::kBasic});
  EXPECT_THAT(actual.visits, Eq(expected.visits));
  EXPECT_THAT(actual.errors, Eq(expected.errors));
  EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
  EXPECT_THAT(observed.observation_reads.load(), Eq(1));
  EXPECT_THAT(observed.trace->children, Eq(513));
  EXPECT_THAT(observed.trace->threads.size(), Gt(1));
  EXPECT_THAT(observed.trace->fields, ElementsAre(vfs::MetadataFields::kBasic));
}

TEST_F(WalkParallelTest, EmptyObservationSidecarPreservesOrdinaryStatsErrorsFieldsAndWorkerPolicy) {
  constexpr auto kWorkers = std::to_array<std::size_t>({1, 3});
  constexpr auto kFields = std::to_array({vfs::MetadataFields::kBasic, vfs::MetadataFields::kBirthTime});
  for (const auto workers : kWorkers) {
    for (const auto fields : kFields) {
      WideFs reference;
      ObservedWideFs observed;
      observed.unavailable = true;
      observed.trace->await_peer = workers > 1;
      reference.failures = {
          {"root/100", absl::StatusCode::kNotFound},
          {"root/801", absl::StatusCode::kPermissionDenied},
      };
      observed.failures = reference.failures;
      const auto expected = Record(reference, {.workers = 1, .metadata_fields = fields});
      const auto actual = Record(observed, {.workers = workers, .metadata_fields = fields});
      EXPECT_THAT(actual.visits, Eq(expected.visits));
      EXPECT_THAT(actual.errors, Eq(expected.errors));
      EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
      EXPECT_THAT(observed.observation_reads.load(), Eq(1));
      EXPECT_THAT(observed.trace->children, Eq(1'024));
      EXPECT_THAT(observed.trace->fields, ElementsAre(fields));
      if (workers > 1) {
        EXPECT_THAT(observed.trace->threads.size(), Gt(1));
      } else {
        EXPECT_THAT(observed.trace->threads, ElementsAre(std::this_thread::get_id()));
      }
    }
  }
}

TEST_F(WalkParallelTest, DirectoryObservationsAreNeverRequestedForLazyOrMetadataFreeTraversal) {
  constexpr auto kDemands = std::to_array({MetadataDemand::kNever, MetadataDemand::kOnDemand});
  for (const auto demand : kDemands) {
    const ObservedWideFs fs;
    const auto log = Record(fs, {.workers = 3, .metadata = demand});
    EXPECT_THAT(log.visits, SizeIs(1'025));
    EXPECT_THAT(log.errors, IsEmpty());
    EXPECT_THAT(fs.observation_reads.load(), Eq(0));
    EXPECT_THAT(fs.trace->children, Eq(0));
  }
}

TEST_F(WalkParallelTest, MisalignedDirectoryObservationsFailWithoutIndexingOutsideTheBatch) {
  ObservedWideFs fs;
  fs.malformed = true;
  const auto actual = Record(fs, {.workers = 3});
  EXPECT_THAT(actual.errors, SizeIs(1));
  EXPECT_THAT(actual.visits, SizeIs(1));
  EXPECT_THAT(fs.trace->children, Eq(0));
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
  fs.failures = {
      {"root/100", absl::StatusCode::kNotFound},
      {"root/800", absl::StatusCode::kPermissionDenied},
  };
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

TEST_F(WalkParallelTest, ReusedDirectoryAndStatRequestsKeepExactResultsAndReadCounts) {
  static constexpr auto kOrders = std::to_array({
      SortOrder::kNone,
      SortOrder::kDir,
      SortOrder::kTree,
      SortOrder::kGlobal,
      SortOrder::kSubtree,
  });
  for (const auto order : kOrders) {
    SCOPED_TRACE(static_cast<int>(order));
    const DirectoryFs serial;
    const DirectoryFs parallel;
    const auto expected = Record(serial, {.sort = order, .workers = 1});
    const auto actual = Record(parallel, {.sort = order, .workers = 3});
    EXPECT_THAT(actual.visits, Eq(expected.visits));
    EXPECT_THAT(actual.errors, IsEmpty());
    EXPECT_THAT(actual.visitors, ElementsAre(std::this_thread::get_id()));
    EXPECT_THAT(parallel.reads, SizeIs(parallel.directories + 1));
    EXPECT_THAT(parallel.trace->children, Eq(parallel.directories * parallel.count));
    for (const auto& [path, count] : parallel.reads) {
      SCOPED_TRACE(path);
      EXPECT_THAT(count, Eq(1));
    }
  }
}

TEST_F(WalkParallelTest, ReusedRequestsPreserveDirectoryErrorPublicationOrder) {
  DirectoryFs serial;
  DirectoryFs parallel;
  serial.unreadable = parallel.unreadable = {"root/d100", "root/d17", "root/d3"};
  const auto expected = Record(serial, {.sort = SortOrder::kTree, .workers = 1});
  const auto actual = Record(parallel, {.sort = SortOrder::kTree, .workers = 4});
  EXPECT_THAT(actual.visits, Eq(expected.visits));
  EXPECT_THAT(actual.errors, Eq(expected.errors));
  EXPECT_THAT(actual.errors, SizeIs(3));
  EXPECT_THAT(parallel.reads, SizeIs(parallel.directories + 1));
}

TEST_F(WalkParallelTest, AbandonedRequestsDrainWithoutPublishingUnvisitedErrors) {
  DirectoryFs serial;
  DirectoryFs parallel;
  serial.unreadable = parallel.unreadable = {"root/d100"};
  // Under directory-first order, all root directories are visited before the first leaf.
  // Stop within the second child's leaves, leaving prefetched siblings without consumers.
  const std::size_t stop_after = 1 + parallel.directories + parallel.count + 1;
  const auto expected = Record(serial, {.sort = SortOrder::kDir, .workers = 1}, stop_after);
  const auto actual = Record(parallel, {.sort = SortOrder::kDir, .workers = 3}, stop_after);
  EXPECT_THAT(actual.visits, Eq(expected.visits));
  EXPECT_THAT(actual.errors, IsEmpty());
  // Walk joins the workers before returning, including already queued abandoned reads.
  EXPECT_THAT(parallel.reads.at("root/d100"), Eq(1));
}

}  // namespace
}  // namespace xff::engine
