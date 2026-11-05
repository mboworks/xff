// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Directory lookup only: fixture creation/indexing and complete-entry preflights are untimed.

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
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

namespace xff::archive {
namespace {

enum class TreeShape { kFlat, kWide, kDeep, kChain };
enum class QuerySet { kRoot, kOne, kTen };

constexpr std::string_view kContainer = "fixture.tar";

struct DirectoryFixture {
  ArchiveFileSystem filesystem;
  std::vector<std::string> queries;
  std::size_t nodes;
  std::size_t returned;
};

struct MemberNames {
  std::vector<std::string> names;
  std::vector<std::string> leaf_dirs;
};

struct ExpectedDirectories {
  std::map<std::string, std::vector<vfs::Entry>, std::less<>> children;
  std::size_t nodes;
};

MemberNames MakeMemberNames(TreeShape shape, std::size_t members) {
  std::vector<std::string> names;
  names.reserve(members);
  std::vector<std::string> leaf_dirs;
  leaf_dirs.reserve(members);
  std::string chain;
  for (std::size_t index = 0; index < members; ++index) {
    std::string parent;
    switch (shape) {
      case TreeShape::kFlat: break;
      case TreeShape::kWide: parent = absl::StrCat("bucket-", index / 10); break;
      case TreeShape::kDeep:
        parent = absl::StrCat("level-a/level-b/level-c/level-d/level-e/level-f/level-g/level-h/bucket-", index / 10);
        break;
      case TreeShape::kChain:
        if (!chain.empty()) {
          chain += '/';
        }
        chain += 'd';
        parent = chain;
        break;
    }
    if (!parent.empty() && (leaf_dirs.empty() || leaf_dirs.back() != parent)) {
      leaf_dirs.push_back(parent);
    }
    names.push_back(parent.empty() ? absl::StrCat("file-", index) : absl::StrCat(parent, "/file-", index));
  }
  return {.names = std::move(names), .leaf_dirs = std::move(leaf_dirs)};
}

ExpectedDirectories MakeExpectedDirectories(const std::vector<std::string>& names) {
  // An independent expected node set includes every synthesized ancestor, then groups direct
  // children. Compare every returned entry field before timing, not merely a total count.
  std::map<std::string, vfs::FileType, std::less<>> nodes;
  for (const auto& name : names) {
    nodes.emplace(name, vfs::FileType::kRegular);
    std::string_view ancestor = name;
    for (;;) {
      const auto slash = ancestor.rfind('/');
      if (slash == std::string_view::npos) {
        break;
      }
      ancestor = ancestor.substr(0, slash);
      nodes.emplace(ancestor, vfs::FileType::kDirectory);
    }
  }
  std::map<std::string, std::vector<vfs::Entry>, std::less<>> expected;
  for (const auto& [path, type] : nodes) {
    const auto slash = path.rfind('/');
    const std::string parent = slash == std::string::npos ? std::string{} : path.substr(0, slash);
    expected[parent].push_back(
        vfs::Entry{
            .path = absl::StrCat(kContainer, "!", path),
            .name = slash == std::string::npos ? path : path.substr(slash + 1),
            .type = type,
            .source = vfs::Source::kArchiveMember,
            .read_only = true,
        });
  }
  return {.children = std::move(expected), .nodes = nodes.size()};
}

std::vector<std::string> SelectDirectories(QuerySet query_set, const std::vector<std::string>& leaf_dirs) {
  std::vector<std::string> selected;
  if (query_set == QuerySet::kRoot) {
    selected.emplace_back();
  } else {
    const std::size_t count = query_set == QuerySet::kOne ? 1 : std::min(leaf_dirs.size(), 10UZ);
    selected.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      selected.push_back(leaf_dirs.at((((2 * index) + 1) * leaf_dirs.size()) / (2 * count)));
    }
  }
  return selected;
}

absl::Status CheckDirectoryEntries(const std::vector<vfs::Entry>& actual, const std::vector<vfs::Entry>& expected) {
  if (actual.size() != expected.size()) {
    return absl::DataLossError("archive directory preflight count mismatch");
  }
  for (std::size_t index = 0; index < actual.size(); ++index) {
    const auto& entry = actual.at(index);
    const auto& reference = expected.at(index);
    if (entry.path != reference.path || entry.name != reference.name || entry.type != reference.type
        || entry.source != reference.source || entry.read_only != reference.read_only) {
      return absl::DataLossError("archive directory preflight entry mismatch");
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<DirectoryFixture> MakeFixture(TreeShape shape, QuerySet query_set, std::size_t members) {
  const auto [names, leaf_dirs] = MakeMemberNames(shape, members);
  std::vector<MemoryArchiveMember> records;
  records.reserve(names.size());
  for (const auto& name : names) {
    records.push_back(
        MemoryArchiveMember{
            .path = name,
        });
  }
  MBO_ASSIGN_OR_RETURN(auto bytes, MakeMemoryTar(records));
  MBO_ASSIGN_OR_RETURN(auto filesystem, ArchiveFileSystem::OpenBytes(kContainer, std::move(bytes)));
  const auto expected = MakeExpectedDirectories(names);
  const auto selected = SelectDirectories(query_set, leaf_dirs);
  DirectoryFixture fixture{
      .filesystem = std::move(filesystem),
      .queries = {},
      .nodes = expected.nodes,
      .returned = 0,
  };
  fixture.queries.reserve(selected.size());
  for (const auto& member : selected) {
    const auto query = member.empty() ? std::string(kContainer) : absl::StrCat(kContainer, "!", member);
    MBO_ASSIGN_OR_RETURN(const auto actual, fixture.filesystem.ReadDir(query));
    const auto wanted = expected.children.find(member);
    if (wanted == expected.children.end()) {
      return absl::DataLossError("archive directory preflight missing expected directory");
    }
    MBO_RETURN_IF_ERROR(CheckDirectoryEntries(actual, wanted->second));
    fixture.returned += actual.size();
    fixture.queries.push_back(query);
  }
  return fixture;
}

void ReadDirectories(benchmark::State& state, TreeShape shape, QuerySet queries, std::size_t members) {
  auto fixture = MakeFixture(shape, queries, members);
  if (!fixture.ok()) {
    state.SkipWithError(fixture.status().ToString());
    return;
  }
  for (const auto unused : state) {
    static_cast<void>(unused);
    for (const auto& query : fixture->queries) {
      auto entries = fixture->filesystem.ReadDir(query);
      if (!entries.ok()) {
        state.SkipWithError(entries.status().ToString());
        return;
      }
      benchmark::DoNotOptimize(entries->data());
      benchmark::ClobberMemory();
    }
  }
  state.counters["stored_members"] = static_cast<double>(members);
  state.counters["indexed_nodes"] = static_cast<double>(fixture->nodes);
  state.counters["directory_queries"] = static_cast<double>(fixture->queries.size());
  state.counters["returned_entries"] = static_cast<double>(fixture->returned);
}

bool RegisterBenchmarks() {
  struct ShapeSpec {
    TreeShape shape;
    std::string_view name;
  };

  constexpr auto kShapes = std::to_array<ShapeSpec>({
      {
          .shape = TreeShape::kFlat,
          .name = "flat",
      },
      {
          .shape = TreeShape::kWide,
          .name = "wide",
      },
      {
          .shape = TreeShape::kDeep,
          .name = "deep",
      },
      {
          .shape = TreeShape::kChain,
          .name = "chain",
      },
  });

  struct QuerySpec {
    QuerySet queries;
    std::string_view name;
  };

  constexpr auto kQueries = std::to_array<QuerySpec>({
      {
          .queries = QuerySet::kRoot,
          .name = "root",
      },
      {
          .queries = QuerySet::kOne,
          .name = "one",
      },
      {
          .queries = QuerySet::kTen,
          .name = "ten",
      },
  });
  constexpr auto kSizes = std::to_array<std::size_t>({10, 100, 1'000, 10'000, 100'000});
  for (const auto& shape : kShapes) {
    for (const auto members : kSizes) {
      // Full path storage in an actual chain grows quadratically; keep its explicit smaller
      // control series rather than pretending a 100k-level path tree is an ordinary fileset.
      if (shape.shape == TreeShape::kChain && members > 1'000) {
        continue;
      }
      for (const auto& query : kQueries) {
        if (shape.shape == TreeShape::kFlat && query.queries != QuerySet::kRoot) {
          continue;  // A flat archive has no nonroot directory to query.
        }
        const auto name = absl::StrCat("archive-directory/", shape.name, "/", query.name, "/", members);
        benchmark::RegisterBenchmark(name, ReadDirectories, shape.shape, query.queries, members)
            ->UseRealTime()
            ->Unit(benchmark::kMicrosecond)
            ->MinWarmUpTime(0.01)
            ->MinTime(0.02);
      }
    }
  }
  return true;
}

[[maybe_unused]] const bool kRegistered = RegisterBenchmarks();

}  // namespace
}  // namespace xff::archive

BENCHMARK_MAIN();
