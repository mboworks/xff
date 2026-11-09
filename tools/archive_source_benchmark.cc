// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Compare archive input callbacks with caller-owned versus legacy owned storage.
// Fixture creation and source ownership are outside the timed iteration loop.

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_reader.h"
#include "xff/vfs/read_source.h"

namespace xff::archive {
namespace {

class MemoryCursor final : public vfs::ReadStream {
 public:
  MemoryCursor(std::shared_ptr<const std::string> bytes, bool caller_owned)
      : bytes_(std::move(bytes)), caller_owned_(caller_owned) {}

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    auto result = bytes_->substr(offset_, std::min(max_bytes, bytes_->size() - offset_));
    offset_ += result.size();
    return result;
  }

  bool SupportsReadInto() const noexcept override { return caller_owned_; }

  absl::StatusOr<std::size_t> ReadInto(std::span<char> buffer) override {
    const std::size_t count = std::min(buffer.size(), bytes_->size() - offset_);
    std::ranges::copy(std::string_view(*bytes_).substr(offset_, count), buffer.begin());
    offset_ += count;
    return count;
  }

 private:
  std::shared_ptr<const std::string> bytes_;
  std::size_t offset_ = 0;
  bool caller_owned_;
};

class BenchmarkSource final : public vfs::ReadSource {
 public:
  BenchmarkSource(std::string bytes, bool caller_owned)
      : bytes_(std::make_shared<const std::string>(std::move(bytes))), caller_owned_(caller_owned) {}

  absl::StatusOr<std::unique_ptr<vfs::ReadStream>> Open() const override {
    return std::make_unique<MemoryCursor>(bytes_, caller_owned_);
  }

 private:
  std::shared_ptr<const std::string> bytes_;
  bool caller_owned_;
};

absl::StatusOr<std::string> MakeTar(std::size_t members, std::size_t payload, std::string_view entry_content = {}) {
  // XFF_ABI_POINTER: libarchive's C writer handle is released before this function returns.
  struct ::archive* writer = ::archive_write_new();
  ::archive_write_set_format_pax_restricted(writer);
  const std::string generated = entry_content.empty() ? std::string(payload, 'x') : std::string();
  const std::string_view content = entry_content.empty() ? std::string_view(generated) : entry_content;
  std::string bytes((members * (content.size() + 1'024)) + 16'384, '\0');
  std::size_t used = 0;
  ::archive_write_open_memory(writer, bytes.data(), bytes.size(), &used);
  bool valid = true;
  for (std::size_t index = 0; index < members; ++index) {
    // XFF_ABI_POINTER: libarchive's C entry is released after each fixture record.
    struct ::archive_entry* entry = ::archive_entry_new();
    const std::string name = absl::StrCat("file-", index);
    ::archive_entry_set_pathname(entry, name.c_str());
    ::archive_entry_set_size(entry, static_cast<la_int64_t>(content.size()));
    ::archive_entry_set_filetype(entry, AE_IFREG);
    ::archive_entry_set_perm(entry, 0644);
    valid &= ::archive_write_header(writer, entry) == ARCHIVE_OK;
    valid &= ::archive_write_data(writer, content.data(), content.size()) == static_cast<la_ssize_t>(content.size());
    ::archive_entry_free(entry);
  }
  valid &= ::archive_write_close(writer) == ARCHIVE_OK;
  ::archive_write_free(writer);
  if (!valid) {
    return absl::InternalError("cannot create archive benchmark fixture");
  }
  bytes.resize(used);
  return bytes;
}

enum class SourceKind { kLegacy, kCallerOwned, kMemory, kNestedMemory };

absl::StatusOr<vfs::SharedReadSource> MakeSource(SourceKind kind, std::string bytes) {
  switch (kind) {
    case SourceKind::kLegacy: return std::make_shared<BenchmarkSource>(std::move(bytes), false);
    case SourceKind::kCallerOwned: return std::make_shared<BenchmarkSource>(std::move(bytes), true);
    case SourceKind::kMemory: return vfs::MemoryReadSource(std::move(bytes));
    case SourceKind::kNestedMemory: {
      MBO_ASSIGN_OR_RETURN(auto outer, MakeTar(1, bytes.size(), bytes));
      return MemberReadSource(vfs::MemoryReadSource(std::move(outer)), "file-0");
    }
  }
  return absl::InvalidArgumentError("unknown archive benchmark source");
}

void Measure(benchmark::State& state, SourceKind kind, bool read_last) {
  const auto members = static_cast<std::size_t>(state.range(0));
  const auto payload = static_cast<std::size_t>(state.range(1));
  auto bytes = MakeTar(members, payload);
  if (!bytes.ok()) {
    state.SkipWithError(bytes.status().ToString());
    return;
  }
  // Keep legacy controls explicitly incapable; exercise actual memory/member sources separately.
  auto built_source = MakeSource(kind, std::move(*bytes));
  if (!built_source.ok()) {
    state.SkipWithError(built_source.status().ToString());
    return;
  }
  const auto source = std::move(*built_source);
  const auto member = MemberReadSource(source, absl::StrCat("file-", members - 1));
  const std::string expected(payload, 'x');
  const auto listing = ListMembersOfSource(source);
  if (!listing.ok() || listing->size() != members) {
    state.SkipWithError("initial archive listing differs from fixture");
    return;
  }
  for (std::size_t index = 0; index < members; ++index) {
    const auto& entry = listing->at(index);
    if (entry.path != absl::StrCat("file-", index) || std::cmp_not_equal(entry.size, payload)) {
      const auto error = absl::StrCat(
          "archive listing differs at ", index, ": path=", entry.path, " size=", entry.size,
          " expected-size=", payload);
      state.SkipWithError(error);
      return;
    }
  }
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores): benchmark loop controls the measured iterations.
  for (const auto unused : state) {
    if (read_last) {
      auto result = vfs::ReadSourceBytes(*member, payload);
      if (!result.ok() || *result != expected) {
        state.SkipWithError("archive member content differs from fixture");
        return;
      }
      benchmark::DoNotOptimize(result);
    } else {
      auto result = ListMembersOfSource(source);
      if (!result.ok() || result->size() != members || std::cmp_not_equal(result->back().size, payload)) {
        state.SkipWithError("archive listing differs from fixture");
        return;
      }
      benchmark::DoNotOptimize(result);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(members));
}

struct SourceCase {
  std::string_view name;
  SourceKind kind;
};

void RegisterLarge(std::span<const SourceCase> sources) {
  for (const auto& source : sources) {
    for (const bool read_last : {false, true}) {
      const std::string name = absl::StrCat("archive-large/tar/", source.name, "/", read_last ? "last" : "list");
      // XFF_ABI_POINTER: Google Benchmark returns a borrowed mutable registration object.
      auto* registration = benchmark::RegisterBenchmark(name, Measure, source.kind, read_last);
      for (const auto count : {10, 100}) {
        for (const auto payload : {4'096, 65'536, 1'048'576}) {
          registration->Args({count, payload});
        }
      }
      registration->Unit(benchmark::kMicrosecond)->UseRealTime()->MinTime(0.02)->MinWarmUpTime(0.01)->Repetitions(9);
    }
  }
}

// NOLINTBEGIN(*-magic-numbers)
struct FileSpec {
  std::string path;
  std::string content;
};

// Produces data-descriptor ZIP entries in memory, independently of the reader implementation.
absl::StatusOr<std::string> MakeSourceZip(const std::vector<FileSpec>& files, bool stored, bool force_zip64 = false) {
  using Writer = std::unique_ptr<struct ::archive, decltype(&::archive_write_free)>;
  using Entry = std::unique_ptr<struct ::archive_entry, decltype(&::archive_entry_free)>;
  // The memory-writer callbacks borrow both objects until writer destruction, including failures.
  std::string bytes;
  std::size_t written = 0;
  const Writer writer(::archive_write_new(), &::archive_write_free);
  if (!writer || ::archive_write_set_format_zip(writer.get()) != ARCHIVE_OK
      || ::archive_write_set_options(writer.get(), stored ? "zip:compression=store" : "zip:compression=deflate")
             != ARCHIVE_OK) {
    return absl::InternalError("cannot configure in-memory ZIP fixture");
  }
  if (force_zip64 && ::archive_write_set_format_option(writer.get(), "zip", "zip64", "1") != ARCHIVE_OK) {
    return absl::InternalError("cannot force Zip64 fixture headers");
  }
  std::size_t capacity = 64UZ * 1'024;
  for (const auto& file : files) {
    capacity += file.content.size() + file.path.size() + 512;
  }
  bytes.resize(capacity, '\0');
  if (::archive_write_open_memory(writer.get(), bytes.data(), bytes.size(), &written) != ARCHIVE_OK) {
    return absl::InternalError("cannot open in-memory ZIP fixture");
  }
  for (const auto& file : files) {
    const Entry entry(::archive_entry_new(), &::archive_entry_free);
    if (!entry) {
      return absl::InternalError("cannot allocate ZIP fixture entry");
    }
    ::archive_entry_set_pathname(entry.get(), file.path.c_str());
    ::archive_entry_set_size(entry.get(), static_cast<std::int64_t>(file.content.size()));
    ::archive_entry_set_filetype(entry.get(), AE_IFREG);
    ::archive_entry_set_perm(entry.get(), 0644);
    if (::archive_write_header(writer.get(), entry.get()) != ARCHIVE_OK
        || ::archive_write_data(writer.get(), file.content.data(), file.content.size())
               != static_cast<la_ssize_t>(file.content.size())) {
      return absl::InternalError("cannot write ZIP fixture entry");
    }
  }
  if (::archive_write_close(writer.get()) != ARCHIVE_OK) {
    return absl::InternalError("cannot finish ZIP fixture");
  }
  bytes.resize(written);
  return bytes;
}

enum class ZipTask { kNames, kFirst, kLast, kCycle };

std::string ZipContent(std::size_t index, std::size_t payload) {
  std::string result(payload, static_cast<char>('a' + (index % 26)));
  const std::string prefix = absl::StrCat("entry ", index, "\n");
  result.replace(0, std::min(payload, prefix.size()), prefix, 0, std::min(payload, prefix.size()));
  return result;
}

void MeasureZip(benchmark::State& state, SourceKind kind, ZipTask task, bool stored) {
  const auto members = static_cast<std::size_t>(state.range(0));
  const auto payload = static_cast<std::size_t>(state.range(1));
  std::vector<FileSpec> files;
  files.reserve(members);
  for (std::size_t index = 0; index < members; ++index) {
    files.push_back({.path = absl::StrCat("file-", index), .content = ZipContent(index, payload)});
  }
  auto bytes = MakeSourceZip(files, stored);
  if (!bytes.ok()) {
    state.SkipWithError(bytes.status().ToString());
    return;
  }
  state.counters["source_bytes"] = static_cast<double>(bytes->size());
  auto built_source = MakeSource(kind, std::move(*bytes));
  if (!built_source.ok()) {
    state.SkipWithError(built_source.status().ToString());
    return;
  }
  const auto source = std::move(*built_source);
  const auto listing = ListMembersOfSource(source);
  if (!listing.ok() || listing->size() != members) {
    state.SkipWithError("ZIP listing count differs from independent fixture");
    return;
  }
  std::size_t size_differences = 0;
  for (std::size_t index = 0; index < members; ++index) {
    if (listing->at(index).path != files.at(index).path) {
      state.SkipWithError("ZIP names/order differ from independent fixture");
      return;
    }
    size_differences += std::cmp_not_equal(listing->at(index).size, files.at(index).content.size()) ? 1 : 0;
  }
  // The parent's streaming descriptor-size bug is reported, never silently presented as correct.
  // The names-only timing compares counts/names; exact metadata has separate committed tests.
  state.counters["descriptor_size_differences"] = static_cast<double>(size_differences);
  std::vector<std::size_t> indices;
  switch (task) {
    case ZipTask::kNames: break;
    case ZipTask::kFirst: indices.push_back(0); break;
    case ZipTask::kLast: indices.push_back(members - 1); break;
    case ZipTask::kCycle: indices = {0, members / 2, members - 1}; break;
  }
  std::vector<vfs::SharedReadSource> targets;
  targets.reserve(indices.size());
  for (const auto index : indices) {
    targets.push_back(MemberReadSource(source, files.at(index).path));
  }
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores): benchmark loop controls measured iterations.
  for (const auto unused : state) {
    if (task == ZipTask::kNames) {
      auto result = ListMembersOfSource(source);
      if (!result.ok() || result->size() != members || result->back().path != files.back().path) {
        state.SkipWithError("ZIP names-only result differs from fixture");
        return;
      }
      benchmark::DoNotOptimize(result);
    } else {
      for (std::size_t at = 0; at < targets.size(); ++at) {
        auto result = vfs::ReadSourceBytes(*targets.at(at), payload);
        if (!result.ok() || *result != files.at(indices.at(at)).content) {
          state.SkipWithError("ZIP target bytes differ from independent fixture");
          return;
        }
        benchmark::DoNotOptimize(result);
      }
    }
  }
  state.SetItemsProcessed(
      state.iterations() * static_cast<std::int64_t>(task == ZipTask::kNames ? members : indices.size()));
}

void RegisterZip() {
  static constexpr auto kSources = std::to_array<SourceCase>({
      {.name = "sequential", .kind = SourceKind::kCallerOwned},
      {.name = "memory", .kind = SourceKind::kMemory},
      {.name = "nested", .kind = SourceKind::kNestedMemory},
  });

  struct TaskCase {
    std::string_view name;
    ZipTask task;
  };

  static constexpr auto kTasks = std::to_array<TaskCase>({
      {.name = "names-only", .task = ZipTask::kNames},
      {.name = "first", .task = ZipTask::kFirst},
      {.name = "last", .task = ZipTask::kLast},
      {.name = "cycle", .task = ZipTask::kCycle},
  });

  struct Fixture {
    std::int64_t count;
    std::int64_t payload;
  };

  static constexpr auto kFixtures = std::to_array<Fixture>({
      {.count = 10, .payload = 128},
      {.count = 1'000, .payload = 128},
      {.count = 10'000, .payload = 128},
      {.count = 1'000, .payload = 4'096},
      {.count = 100, .payload = 65'536},
      {.count = 10, .payload = 1'048'576},
  });
  for (const auto& source : kSources) {
    for (const bool stored : {false, true}) {
      for (const auto& task : kTasks) {
        const std::string name = absl::StrCat("zip/", stored ? "stored/" : "deflate/", source.name, "/", task.name);
        // XFF_ABI_POINTER: Google Benchmark returns its borrowed registration object.
        auto* registration = benchmark::RegisterBenchmark(name, MeasureZip, source.kind, task.task, stored);
        for (const auto& fixture : kFixtures) {
          registration->Args({fixture.count, fixture.payload});
        }
        // Stored source exceeds default conservative admission; retain a source-size fallback control.
        if (stored && source.kind != SourceKind::kNestedMemory && task.task != ZipTask::kCycle) {
          registration->Args({40, 1'048'576});
        }
        registration->Unit(benchmark::kMicrosecond)->UseRealTime()->MinTime(0.02)->MinWarmUpTime(0.01)->Repetitions(9);
      }
    }
  }
}

// NOLINTEND(*-magic-numbers)

// Control the early-success and missing-name paths separately from last-member replay.
// Fresh means a new member wrapper, not cold operating-system caches: every iteration
// opens a new archive cursor, while the immutable memory fixture stays resident.
void MeasureMemberOpenControl(benchmark::State& state, bool missing, bool fresh) {
  constexpr std::size_t kPayload = 128;
  auto bytes = MakeTar(static_cast<std::size_t>(state.range(0)), kPayload);
  if (!bytes.ok()) {
    state.SkipWithError(bytes.status().ToString());
    return;
  }
  const auto source = vfs::MemoryReadSource(std::move(*bytes));
  const std::string_view name = missing ? "absent" : "file-0";
  const std::string request = state.range(1) == 0 ? std::string(name) : absl::StrCat("././", name, "///");
  const auto member = MemberReadSource(source, request);
  const std::string expected(kPayload, 'x');
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores): benchmark loop controls measured iterations.
  for (const auto unused : state) {
    auto result = fresh ? vfs::ReadSourceBytes(*MemberReadSource(source, request), kPayload)
                        : vfs::ReadSourceBytes(*member, kPayload);
    if (missing ? result.status().code() != absl::StatusCode::kNotFound : (!result.ok() || *result != expected)) {
      state.SkipWithError("first/missing member control differs from its fixture");
      return;
    }
    benchmark::DoNotOptimize(result);
  }
  state.SetItemsProcessed(state.iterations());
}

void RegisterMemberOpenControls() {
  struct ControlCase {
    std::string_view name;
    bool missing;
    bool fresh;
  };

  constexpr auto kCases = std::to_array<ControlCase>({
      {
          .name = "first/reopen",
          .missing = false,
          .fresh = false,
      },
      {
          .name = "first/fresh-wrapper",
          .missing = false,
          .fresh = true,
      },
      {
          .name = "missing/reopen",
          .missing = true,
          .fresh = false,
      },
      {
          .name = "missing/fresh-wrapper",
          .missing = true,
          .fresh = true,
      },
  });
  constexpr auto kCounts = std::to_array<std::int64_t>({10, 10'000});
  for (const auto& control : kCases) {
    const std::string name = absl::StrCat("archive-open-control/", control.name);
    // XFF_ABI_POINTER: Google Benchmark returns a borrowed mutable registration object.
    auto* registration = benchmark::RegisterBenchmark(name, MeasureMemberOpenControl, control.missing, control.fresh);
    for (const auto count : kCounts) {
      registration->Args({count, 0});
      registration->Args({count, 1});
    }
    registration->Unit(benchmark::kMicrosecond)->UseRealTime()->MinTime(0.02)->MinWarmUpTime(0.01)->Repetitions(9);
  }
}

void RegisterAll() {
  constexpr auto kSources = std::to_array<SourceCase>({
      {.name = "legacy", .kind = SourceKind::kLegacy},
      {.name = "caller-owned", .kind = SourceKind::kCallerOwned},
      {.name = "memory", .kind = SourceKind::kMemory},
      {.name = "nested-memory", .kind = SourceKind::kNestedMemory},
  });
  constexpr auto kCounts = std::to_array<std::int64_t>({10, 1'000, 10'000});
  constexpr auto kPayloads = std::to_array<std::int64_t>({128, 4'096});
  for (const auto& source : kSources) {
    for (const bool read_last : {false, true}) {
      const std::string name = absl::StrCat("archive/", source.name, "/", read_last ? "last" : "list");
      // XFF_ABI_POINTER: google-benchmark returns a borrowed mutable registration object.
      auto* registration = benchmark::RegisterBenchmark(name, Measure, source.kind, read_last);
      for (const auto count : kCounts) {
        for (const auto payload : kPayloads) {
          registration->Args({count, payload});
        }
      }
      registration->Unit(benchmark::kMicrosecond)->UseRealTime()->MinTime(0.02)->MinWarmUpTime(0.01)->Repetitions(9);
    }
  }
  RegisterLarge(kSources);
  RegisterZip();
  RegisterMemberOpenControls();
}

}  // namespace
}  // namespace xff::archive

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  xff::archive::RegisterAll();
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
