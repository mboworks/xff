// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Archive member lookup only: fixtures, indexing, and complete-value preflights are untimed.

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "benchmark/benchmark.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_fs.h"
#include "xff/archive/memory_archive_test.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/read_source.h"

namespace xff::archive {
namespace {

enum class Operation { kStat, kAccess, kReadLink, kReadDir, kReadDirMetadata, kCachedReadContent, kContentSource };

constexpr std::string_view kPayload = "retained member content\n";

struct MemberFixture {
  ArchiveFileSystem filesystem;
  std::string file;
  std::string link;
  std::string directory;
  std::string target;
};

absl::StatusOr<MemberFixture> MakeFixture(std::size_t prefix_length) {
  const std::string directory(prefix_length, 'd');
  const std::string file = directory + "/file.txt";
  const std::string link = directory + "/link";
  const auto members = std::to_array<MemoryArchiveMember>({
      {
          .path = file,
          .content = kPayload,
      },
      {
          .path = link,
          .symlink_target = file,
      },
  });
  MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryTar(members));
  MBO_ASSIGN_OR_RETURN(auto fs, ArchiveFileSystem::OpenSource("fixture.tar", vfs::MemoryReadSource(std::move(bytes))));
  MBO_ASSIGN_OR_RETURN(const auto metadata, fs.Stat("fixture.tar!" + file, false));
  MBO_ASSIGN_OR_RETURN(const auto target, fs.ReadLink("fixture.tar!" + link));
  MBO_ASSIGN_OR_RETURN(const auto content, fs.ReadContent("fixture.tar!" + file));
  MBO_ASSIGN_OR_RETURN(const auto listing, fs.ReadDir("fixture.tar!" + directory));
  MBO_ASSIGN_OR_RETURN(
      const auto observed, fs.ReadDirMetadata("fixture.tar!" + directory, vfs::MetadataFields::kBasic));
  MBO_ASSIGN_OR_RETURN(const auto source, fs.ContentSource("fixture.tar!" + file));
  MBO_ASSIGN_OR_RETURN(const auto source_content, vfs::ReadSourceBytes(*source, 1'024));
  if (metadata.type != vfs::FileType::kRegular || metadata.size != kPayload.size() || content != kPayload
      || target != file || source_content != kPayload || !fs.Access("fixture.tar!" + file, vfs::AccessMode::kRead)
      || listing.size() != 2 || observed.entries.size() != 2 || observed.observations.size() != 2) {
    return absl::DataLossError("archive member fixture preflight mismatch");
  }
  if (listing.front().name != "file.txt" || listing.back().name != "link"
      || observed.entries.front().path != "fixture.tar!" + file || observed.entries.back().path != "fixture.tar!" + link
      || observed.observations.front().value.size != kPayload.size()
      || observed.observations.back().value.type != vfs::FileType::kSymlink) {
    return absl::DataLossError("archive member directory preflight mismatch");
  }
  MBO_RETURN_IF_ERROR(observed.observations.front().status);
  MBO_RETURN_IF_ERROR(observed.observations.back().status);
  return MemberFixture{
      .filesystem = std::move(fs),
      .file = "fixture.tar!" + file,
      .link = "fixture.tar!" + link,
      .directory = "fixture.tar!" + directory,
      .target = file,
  };
}

template<Operation kOperation>
auto Lookup(const MemberFixture& fixture) {
  if constexpr (kOperation == Operation::kStat) {
    return fixture.filesystem.Stat(fixture.file, false);
  } else if constexpr (kOperation == Operation::kAccess) {
    return fixture.filesystem.Access(fixture.file, vfs::AccessMode::kRead);
  } else if constexpr (kOperation == Operation::kReadLink) {
    return fixture.filesystem.ReadLink(fixture.link);
  } else if constexpr (kOperation == Operation::kReadDir) {
    return fixture.filesystem.ReadDir(fixture.directory);
  } else if constexpr (kOperation == Operation::kReadDirMetadata) {
    return fixture.filesystem.ReadDirMetadata(fixture.directory, vfs::MetadataFields::kBasic);
  } else if constexpr (kOperation == Operation::kCachedReadContent) {
    return fixture.filesystem.ReadContent(fixture.file);
  } else {
    return fixture.filesystem.ContentSource(fixture.file);
  }
}

template<Operation kOperation>
void ReadMember(benchmark::State& state, std::size_t prefix_length) {
  const auto fixture = MakeFixture(prefix_length);
  if (!fixture.ok()) {
    state.SkipWithError(fixture.status().ToString());
    return;
  }
  for (const auto unused : state) {
    static_cast<void>(unused);
    auto result = Lookup<kOperation>(*fixture);
    if constexpr (kOperation == Operation::kAccess) {
      if (!result) {
        state.SkipWithError("member read access unexpectedly refused");
        return;
      }
    } else {
      if (!result.ok()) {
        state.SkipWithError(result.status().ToString());
        return;
      }
    }
    benchmark::DoNotOptimize(result);
    benchmark::ClobberMemory();
  }
  state.counters["member_prefix_bytes"] = static_cast<double>(prefix_length);
}

template<Operation kOperation>
void RegisterOperation(std::string_view name) {
  constexpr auto kSizes = std::to_array<std::size_t>({3, 96});
  for (const auto size : kSizes) {
    const std::string label = "archive-member/" + std::string(name) + "/" + std::to_string(size);
    benchmark::RegisterBenchmark(label, ReadMember<kOperation>, size)
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond)
        ->MinWarmUpTime(0.01)
        ->MinTime(0.02);
  }
}

bool RegisterBenchmarks() {
  RegisterOperation<Operation::kStat>("stat");
  RegisterOperation<Operation::kAccess>("access");
  RegisterOperation<Operation::kReadLink>("readlink");
  RegisterOperation<Operation::kReadDir>("readdir");
  RegisterOperation<Operation::kReadDirMetadata>("metadata");
  RegisterOperation<Operation::kCachedReadContent>("cached-content");
  RegisterOperation<Operation::kContentSource>("content-source");
  return true;
}

[[maybe_unused]] const bool kRegistered = RegisterBenchmarks();

}  // namespace
}  // namespace xff::archive

BENCHMARK_MAIN();
