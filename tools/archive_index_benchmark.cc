// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Measure public archive construction and destruction, including parsing and indexing.
// Fixture generation, source ownership, and complete listing/stat preflights are untimed.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_fs.h"
#include "xff/archive/memory_archive_test.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/read_source.h"

namespace xff::archive {
namespace {

enum class Shape { kFlat, kWide, kDeep, kChain };

constexpr std::size_t kFilesPerBranch = 64;

struct ShapeCase {
  std::string_view label;
  Shape shape;
  std::size_t prefix_length;
};

struct ConstructionFixture {
  vfs::SharedReadSource source;
  std::size_t files;
  std::size_t directories;
};

constexpr std::size_t ExpectedDirectories(Shape shape, std::size_t files) {
  const std::size_t branches = (files + kFilesPerBranch - 1) / kFilesPerBranch;
  switch (shape) {
    case Shape::kFlat: return 0;
    case Shape::kWide: return branches;
    case Shape::kDeep: return 1 + (2 * branches);
    case Shape::kChain: return files;
  }
  std::unreachable();
}

absl::StatusOr<ConstructionFixture> MakeFixture(Shape shape, std::size_t count, std::size_t prefix_length) {
  std::vector<std::string> paths;
  paths.reserve(count);
  const std::string prefix(prefix_length, 'd');
  std::string chain = prefix;
  for (std::size_t index = 0; index < count; ++index) {
    switch (shape) {
      case Shape::kFlat: paths.push_back(absl::StrCat("file-", index)); break;
      case Shape::kWide: paths.push_back(absl::StrCat(prefix, index / kFilesPerBranch, "/file-", index)); break;
      case Shape::kDeep:
        paths.push_back(absl::StrCat(prefix, "/branch-", index / kFilesPerBranch, "/inner/file-", index));
        break;
      case Shape::kChain:
        paths.push_back(absl::StrCat(chain, "/file-", index));
        chain += "/d";
        break;
    }
  }
  std::vector<MemoryArchiveMember> members;
  members.reserve(paths.size());
  for (const auto& path : paths) {
    members.push_back(
        MemoryArchiveMember{
            .path = path,
            .content = "x",
        });
  }
  MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryTar(members));
  auto source = vfs::MemoryReadSource(std::move(bytes));
  MBO_ASSIGN_OR_RETURN(const auto fs, ArchiveFileSystem::OpenSource("fixture.tar", source));
  std::vector<std::string> pending{"fixture.tar"};
  std::vector<std::string> actual_files;
  actual_files.reserve(count);
  std::size_t actual_directories = 0;
  constexpr std::string_view kMemberPrefix = "fixture.tar!";
  while (!pending.empty()) {
    const std::string directory = std::move(pending.back());
    pending.pop_back();
    MBO_ASSIGN_OR_RETURN(const auto entries, fs.ReadDir(directory));
    for (const auto& entry : entries) {
      if (entry.type == vfs::FileType::kDirectory) {
        pending.push_back(entry.path);
        ++actual_directories;
      } else if (entry.type == vfs::FileType::kRegular && entry.path.starts_with(kMemberPrefix)) {
        actual_files.emplace_back(std::string_view(entry.path).substr(kMemberPrefix.size()));
        MBO_ASSIGN_OR_RETURN(const auto metadata, fs.Stat(entry.path, false));
        if (metadata.size != 1 || metadata.type != vfs::FileType::kRegular
            || metadata.source != vfs::Source::kArchiveMember || metadata.mode != 0640) {
          return absl::DataLossError("archive construction fixture metadata mismatch");
        }
      } else {
        return absl::DataLossError("archive construction fixture entry mismatch");
      }
    }
  }
  const std::size_t expected_directories = ExpectedDirectories(shape, count);
  std::ranges::sort(paths);
  std::ranges::sort(actual_files);
  if (actual_files != paths || actual_directories != expected_directories) {
    return absl::DataLossError("archive construction fixture tree mismatch");
  }
  return ConstructionFixture{
      .source = std::move(source),
      .files = count,
      .directories = expected_directories,
  };
}

void ConstructArchive(benchmark::State& state, Shape shape, std::size_t count, std::size_t prefix_length) {
  const auto fixture = MakeFixture(shape, count, prefix_length);
  if (!fixture.ok()) {
    state.SkipWithError(fixture.status().ToString());
    return;
  }
  for (const auto unused : state) {
    static_cast<void>(unused);
    auto result = ArchiveFileSystem::OpenSource("fixture.tar", fixture->source);
    if (!result.ok()) {
      state.SkipWithError(result.status().ToString());
      return;
    }
    benchmark::DoNotOptimize(result);
    benchmark::ClobberMemory();
    // The complete filesystem, node index and member vector are destroyed in this iteration.
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(fixture->files));
  state.counters["files"] = static_cast<double>(fixture->files);
  state.counters["directories"] = static_cast<double>(fixture->directories);
  state.counters["parent_prefix_bytes"] = static_cast<double>(prefix_length);
}

bool RegisterBenchmarks() {
  constexpr auto kShapes = std::to_array<ShapeCase>({
      {
          .label = "flat",
          .shape = Shape::kFlat,
          .prefix_length = 0,
      },
      {
          .label = "wide-short",
          .shape = Shape::kWide,
          .prefix_length = 3,
      },
      {
          .label = "wide-long",
          .shape = Shape::kWide,
          .prefix_length = 96,
      },
      {
          .label = "deep-short",
          .shape = Shape::kDeep,
          .prefix_length = 3,
      },
      {
          .label = "deep-long",
          .shape = Shape::kDeep,
          .prefix_length = 96,
      },
      {
          .label = "chain",
          .shape = Shape::kChain,
          .prefix_length = 3,
      },
  });
  constexpr auto kCounts = std::to_array<std::size_t>({10, 100, 1'000, 10'000, 100'000});
  constexpr std::size_t kMaximumChainFiles = 1'000;
  for (const auto& shape : kShapes) {
    for (const auto count : kCounts) {
      if (shape.shape == Shape::kChain && count > kMaximumChainFiles) {
        continue;
      }
      const std::string label = absl::StrCat("archive-construction/", shape.label, "/", count);
      benchmark::RegisterBenchmark(label, ConstructArchive, shape.shape, count, shape.prefix_length)
          ->UseRealTime()
          ->Unit(benchmark::kMicrosecond)
          ->MinWarmUpTime(0.01)
          ->MinTime(0.02);
    }
  }
  return true;
}

[[maybe_unused]] const bool kRegistered = RegisterBenchmarks();

}  // namespace
}  // namespace xff::archive

BENCHMARK_MAIN();
